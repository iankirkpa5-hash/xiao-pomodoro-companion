#!/usr/bin/env python3
"""Request a bounded XIAO recording, verify its PCM bytes, and write a raw WAV."""
import argparse
from array import array
import hashlib
import json
import math
from pathlib import Path
import re
import sys
import time
import uuid
import wave
import zlib

from hardware_loop import LineBuffer, Observation, open_serial, source_metadata, utc_now

INFO = re.compile(r"PCM INFO (\d+) (\d+) (\d+) (\d+) (\d+) (\d+) (\d+)")
STATE = re.compile(r"PCM STATE (\d+) (RECORDING|READY|FAILED) (\d+) (\d+) (\d+) (\d+) (\d+) (\d+) (\d+) ([0-9a-f]{8})")
DATA = re.compile(r"PCM DATA (\d+) (\d+) ([0-9a-f]+) ([0-9a-f]{8})")
ERROR = re.compile(r"PCM ERROR (\d+) ([A-Z_]+)")


def parse_info(line):
    match = INFO.fullmatch(line)
    if not match:
        return None
    values = list(map(int, match.groups()))
    if values[:6] != [1, 16000, 1, 16, 5000, 64]:
        raise ValueError("unsupported PCM protocol/format")
    return dict(zip(("protocol", "sample_rate", "channels", "bits", "max_ms", "chunk_bytes", "free_psram"), values))


def parse_state(line, session, target):
    match = STATE.fullmatch(line)
    if not match or int(match[1]) != session:
        return None
    count, total, rate, first, last, gap, errors = map(int, match.groups()[2:9])
    if total != target or rate != 16000 or not 0 <= count <= total:
        raise ValueError("inconsistent recording metadata")
    if any(x >= 2**32 for x in (first, last, gap, errors)):
        raise ValueError("invalid uint32 metadata")
    if match[2] == "READY" and (count != total or errors):
        raise ValueError("READY recording is incomplete or has read errors")
    return dict(state=match[2], samples=count, target=total, sample_rate=rate,
                first_frame_ms=first, last_frame_ms=last, max_frame_gap_ms=gap,
                read_errors=errors, crc32=int(match[10], 16))


def parse_data(line, session, offset, length):
    match = DATA.fullmatch(line)
    if not match or int(match[1]) != session or int(match[2]) != offset:
        return None
    data = bytes.fromhex(match[3])
    if len(data) != length or zlib.crc32(data) != int(match[4], 16):
        raise ValueError("chunk length/CRC mismatch")
    return data


def verify_pcm(pcm, status):
    if status["state"] != "READY" or status["read_errors"]:
        raise ValueError("recording failed")
    if len(pcm) != status["target"] * 2 or zlib.crc32(pcm) != status["crc32"]:
        raise ValueError("whole-recording length/CRC mismatch")


def write_wav(path, pcm, status):
    verify_pcm(pcm, status)
    # Exclusive creation protects existing recordings; samples remain bit-exact.
    with path.open("xb") as stream:
        with wave.open(stream, "wb") as wav:
            wav.setnchannels(1)
            wav.setsampwidth(2)
            wav.setframerate(status["sample_rate"])
            wav.writeframes(pcm)
    with wave.open(str(path), "rb") as wav:
        if wav.getnframes() != status["target"] or wav.readframes(wav.getnframes()) != pcm:
            raise ValueError("WAV readback differs from original PCM")


def audio_statistics(pcm):
    samples = array("h")
    samples.frombytes(pcm)
    if sys.byteorder != "little":
        samples.byteswap()
    mean = sum(samples) / len(samples)
    return dict(samples=len(samples), mean=mean, minimum=min(samples), maximum=max(samples),
                centered_rms=math.sqrt(sum((x-mean)**2 for x in samples) / len(samples)),
                centered_peak=max(abs(x-mean) for x in samples),
                full_scale_samples=sum(x in (-32768, 32767) for x in samples))


class PcmClient:
    def __init__(self, link, raw, commands, session):
        self.link, self.raw, self.commands, self.session = link, raw, commands, session
        self.framer = LineBuffer()
        self.observation = Observation(160)
        self.started = time.monotonic()
        self.retries = 0
        self.rejected_packets = []

    def request(self, command, parse, timeout=1.0, attempts=3):
        for attempt in range(attempts):
            if attempt:
                self.retries += 1
            self.commands.append(dict(utc=utc_now(), command=command, attempt=attempt+1))
            data = ("\n" + command + "\n").encode("ascii")
            if self.link.write(data) != len(data):
                raise RuntimeError("partial command write")
            deadline = time.monotonic() + timeout
            while time.monotonic() < deadline:
                chunk = self.link.read(max(1, min(self.link.in_waiting, 4096)))
                if not chunk:
                    continue
                self.raw.write(chunk)
                answer = None
                for line in self.framer.feed(chunk):
                    self.observation.accept(line, time.monotonic()-self.started)
                    failure = ERROR.fullmatch(line)
                    if failure and int(failure[1]) == self.session:
                        if failure[2] == "BUSY" and command.startswith("PCM RELEASE "):
                            continue
                        raise RuntimeError("device: " + failure[2])
                    try:
                        value = parse(line)
                    except ValueError as exc:
                        if len(self.rejected_packets) < 50:
                            self.rejected_packets.append(dict(line=line, reason=str(exc)))
                        continue
                    if value is not None:
                        answer = value
                self.raw.flush()
                if answer is not None:
                    return answer
        raise RuntimeError("no valid reply to " + command)


def collect(client, seconds, report):
    info = client.request("PCM INFO", parse_info, attempts=5)
    report["device_info"] = info
    target = seconds * info["sample_rate"]
    parse = lambda line: parse_state(line, client.session, target)
    print(f"Recording {seconds}s now. Speak normally or make the test sound.", flush=True)
    status = client.request(f"PCM START {client.session} {seconds*1000}", parse)
    deadline = time.monotonic() + seconds + 5
    while status["state"] == "RECORDING":
        if time.monotonic() > deadline:
            raise RuntimeError("recording deadline exceeded")
        time.sleep(.1)
        status = client.request(f"PCM STATUS {client.session}", parse)
    report["recording"] = status
    if status["state"] != "READY":
        raise RuntimeError("device capture failed; no WAV created")
    print("Recording finished; downloading verified chunks.", flush=True)
    pcm = bytearray()
    deadline = time.monotonic() + 180
    total = target * 2
    for offset in range(0, total, info["chunk_bytes"]):
        if time.monotonic() > deadline:
            raise RuntimeError("download deadline exceeded")
        length = min(info["chunk_bytes"], total-offset)
        pcm.extend(client.request(f"PCM READ {client.session} {offset}",
                                  lambda line: parse_data(line, client.session, offset, length)))
        if len(pcm) % 32000 == 0:
            print(f"PCM received: {len(pcm)}/{total} bytes", flush=True)
    verify_pcm(pcm, status)
    if client.observation.error_count or client.framer.overlong:
        raise RuntimeError("serial diagnostics contain errors; inspect raw log")
    frame_span = (status["last_frame_ms"] - status["first_frame_ms"]) % 2**32
    report["timing_warnings"] = []
    if abs(frame_span - (seconds*1000 - 10)) > 100 or status["max_frame_gap_ms"] > 50:
        report["timing_warnings"].append("frame_processing_timing_needs_review")
    report["frame_processing_span_ms"] = frame_span
    report["pcm_statistics"] = audio_statistics(pcm)
    report["pcm_sha256"] = hashlib.sha256(pcm).hexdigest()
    return pcm, status


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--seconds", type=int, choices=range(1, 6), default=5)
    parser.add_argument("--notes", default="")
    args = parser.parse_args(argv)
    repo = Path(__file__).resolve().parents[1]
    session = uuid.uuid4().int % (2**32-1) + 1
    output = repo / "test-runs" / ("pcm-" + utc_now().split(".")[0].replace(":", "").replace("-", "") + f"-{session:08x}")
    output.mkdir(parents=True, exist_ok=False)
    report = dict(schema_version=1, started_utc=utc_now(), session=session, port=args.port,
                  seconds_requested=args.seconds, operator_notes=args.notes,
                  source=source_metadata(repo), tool_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                  verdict="NOT_RUN", commands=[], physical_acceptance="pending_listening_and_ui_checks",
                  format="16000 Hz, mono, signed 16-bit little-endian PCM; raw DC offset preserved")
    client = link = None
    raw = None
    result = 1
    try:
        link = open_serial(args.port, 115200, 10)
        link.timeout = .05
        raw = (output / "serial.log").open("xb")
        client = PcmClient(link, raw, report["commands"], session)
        pcm, status = collect(client, args.seconds, report)
        with (output / "audio.pcm").open("xb") as binary:
            binary.write(pcm)
        write_wav(output / "audio.wav", pcm, status)
        report["wav_sha256"] = hashlib.sha256((output / "audio.wav").read_bytes()).hexdigest()
        report["verdict"] = "PASS_PCM_TRANSFER" if not report["timing_warnings"] else "PCM_SAVED_TIMING_REVIEW"
        result = 0
    except KeyboardInterrupt:
        report["verdict"] = "ABORTED"
    except (RuntimeError, OSError, ValueError) as exc:
        report["verdict"] = "FAILED"
        report["error"] = str(exc)
    finally:
        if client:
            try:
                client.request(f"PCM RELEASE {session}",
                               lambda line: True if line == f"PCM RELEASED {session}" else None)
                report["buffer_released"] = True
            except (RuntimeError, OSError) as exc:
                report["buffer_released"] = False
                report["release_error"] = str(exc)
            report["serial_observation"] = client.observation.summarize(time.monotonic()-client.started, 1, 2)
            report["retries"] = client.retries
            report["rejected_packets"] = client.rejected_packets
            report["overlong_lines"] = client.framer.overlong
        if raw:
            raw.close()
        if link:
            link.close()
        report["finished_utc"] = utc_now()
        (output / "report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(dict(verdict=report["verdict"], directory=str(output), error=report.get("error"))), flush=True)
    return result


if __name__ == "__main__":
    sys.exit(main())

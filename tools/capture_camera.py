#!/usr/bin/env python3
"""Request a single on-demand XIAO JPEG frame, verify its bytes, and write frame.jpg."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import sys
import time
import uuid
import zlib

from hardware_loop import LineBuffer, Observation, open_serial, source_metadata, utc_now

INFO = re.compile(r"CAM INFO (\d+) (\d+) (\d+) (JPEG) (\d+) (\d+) (\d+)")
STATE = re.compile(r"CAM STATE (\d+) (READY) (\d+) (\d+) (\d+) (\d+) (\d+) ([0-9a-f]{8})")
DATA = re.compile(r"CAM DATA (\d+) (\d+) ([0-9a-f]+) ([0-9a-f]{8})")
ERROR = re.compile(r"CAM ERROR (\d+) ([A-Z_]+)")


def parse_info(line):
    match = INFO.fullmatch(line)
    if not match:
        return None
    protocol, width, height = map(int, match.groups()[:3])
    fmt = match.group(4)
    max_bytes, chunk_bytes, free_psram = map(int, match.groups()[4:])
    if protocol != 1 or fmt != "JPEG" or width < 1 or height < 1 or max_bytes < 1 or chunk_bytes != 64:
        raise ValueError("unsupported CAM protocol/format")
    return dict(
        protocol=protocol,
        width=width,
        height=height,
        format=fmt,
        max_frame_bytes=max_bytes,
        chunk_bytes=chunk_bytes,
        free_psram=free_psram,
    )


def parse_state(line, session, max_bytes):
    match = STATE.fullmatch(line)
    if not match or int(match[1]) != session:
        return None
    length, width, height, fmt, capture_ms = map(int, match.groups()[2:7])
    if not 4 <= length <= max_bytes or width < 1 or height < 1 or fmt != 4:
        raise ValueError("inconsistent camera metadata")
    if capture_ms >= 2**32:
        raise ValueError("invalid uint32 metadata")
    return dict(
        state=match[2],
        length=length,
        width=width,
        height=height,
        format=fmt,
        capture_ms=capture_ms,
        crc32=int(match[8], 16),
    )


def parse_data(line, session, offset, length):
    match = DATA.fullmatch(line)
    if not match or int(match[1]) != session or int(match[2]) != offset:
        return None
    data = bytes.fromhex(match[3])
    if len(data) != length or zlib.crc32(data) != int(match[4], 16):
        raise ValueError("chunk length/CRC mismatch")
    return data


def verify_jpeg(jpeg, status):
    if status["state"] != "READY":
        raise ValueError("capture not ready")
    if len(jpeg) != status["length"] or zlib.crc32(jpeg) != status["crc32"]:
        raise ValueError("whole-frame length/CRC mismatch")
    if len(jpeg) < 4 or not jpeg.startswith(b"\xff\xd8"):
        raise ValueError("missing JPEG SOI marker")
    eoi = jpeg.rfind(b"\xff\xd9")
    if eoi < 2:
        raise ValueError("missing JPEG EOI marker")
    trailing = jpeg[eoi + 2:]
    if any(byte != 0 for byte in trailing):
        raise ValueError("unexpected non-zero data after JPEG EOI marker")
    return dict(
        soi_valid=True,
        eoi_offset=eoi,
        trailing_zero_bytes=len(trailing),
        bytes=len(jpeg),
    )


def write_jpeg(path, jpeg, status):
    details = verify_jpeg(jpeg, status)
    # Exclusive creation protects existing captures; raw device bytes stay unchanged.
    with path.open("xb") as stream:
        stream.write(jpeg)
    if path.read_bytes() != jpeg:
        raise ValueError("JPEG readback differs from verified bytes")
    return details


class CameraClient:
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
            self.commands.append(dict(utc=utc_now(), command=command, attempt=attempt + 1))
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
                    self.observation.accept(line, time.monotonic() - self.started)
                    failure = ERROR.fullmatch(line)
                    if failure and int(failure[1]) == self.session:
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


def collect(client, report, size="QVGA"):
    info = client.request("CAM INFO", parse_info, attempts=5)
    report["device_info"] = info
    print(f"Requesting single {size} JPEG frame (camera initializes once and deinitializes immediately).", flush=True)
    cmd = f"CAM CAPTURE {client.session}" if size == "QVGA" else f"CAM CAPTURE {client.session} {size}"
    status = client.request(
        cmd,
        lambda line: parse_state(line, client.session, info["max_frame_bytes"]),
        timeout=7.0,
        attempts=2,
    )
    report["capture"] = status
    print(
        f"Captured {status['width']}x{status['height']} JPEG "
        f"({status['length']} bytes in {status['capture_ms']} ms); downloading verified chunks.",
        flush=True,
    )
    jpeg = bytearray()
    total = status["length"]
    deadline = time.monotonic() + 90
    for offset in range(0, total, info["chunk_bytes"]):
        if time.monotonic() > deadline:
            raise RuntimeError("download deadline exceeded")
        length = min(info["chunk_bytes"], total - offset)
        jpeg.extend(
            client.request(
                f"CAM READ {client.session} {offset}",
                lambda line, off=offset, n=length: parse_data(line, client.session, off, n),
            )
        )
    jpeg_bytes = bytes(jpeg)
    report["jpeg_verification"] = verify_jpeg(jpeg_bytes, status)
    if client.observation.error_count or client.framer.overlong:
        raise RuntimeError("serial diagnostics contain errors; inspect raw log")
    report["jpeg_sha256"] = hashlib.sha256(jpeg_bytes).hexdigest()
    return jpeg_bytes, status


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--size", choices=("QVGA", "VGA", "SVGA", "UXGA"), default="QVGA")
    parser.add_argument("--notes", default="")
    args = parser.parse_args(argv)
    repo = Path(__file__).resolve().parents[1]
    session = uuid.uuid4().int % (2**32 - 1) + 1
    output = repo / "test-runs" / (
        "cam-" + utc_now().split(".")[0].replace(":", "").replace("-", "") + f"-{session:08x}"
    )
    output.mkdir(parents=True, exist_ok=False)
    report = dict(
        schema_version=1,
        started_utc=utc_now(),
        session=session,
        port=args.port,
        size_requested=args.size,
        operator_notes=args.notes,
        source=source_metadata(repo),
        tool_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        verdict="NOT_RUN",
        commands=[],
        physical_acceptance="pending_visual_thermal_and_ui_checks",
        thermal_boundary=(
            "camera initializes only on CAM CAPTURE and deinitializes immediately after frame copy; "
            "deinit stops driver/XCLK but does not prove sensor rail power-off"
        ),
    )
    client = link = None
    raw = None
    result = 1
    try:
        link = open_serial(args.port, 115200, 10)
        link.timeout = 0.05
        raw = (output / "serial.log").open("xb")
        client = CameraClient(link, raw, report["commands"], session)
        jpeg, status = collect(client, report, size=args.size)
        write_jpeg(output / "frame.jpg", jpeg, status)
        report["verdict"] = "PASS_CAMERA_CAPTURE"
        result = 0
    except KeyboardInterrupt:
        report["verdict"] = "ABORTED"
    except (RuntimeError, OSError, ValueError) as exc:
        report["verdict"] = "FAILED"
        report["error"] = str(exc)
    finally:
        if client:
            try:
                client.request(
                    f"CAM RELEASE {session}",
                    lambda line: True if line == f"CAM RELEASED {session}" else None,
                )
                report["buffer_released"] = True
            except (RuntimeError, OSError) as exc:
                report["buffer_released"] = False
                report["release_error"] = str(exc)
            report["serial_observation"] = client.observation.summarize(
                time.monotonic() - client.started, 0, 2
            )
            report["retries"] = client.retries
            report["rejected_packets"] = client.rejected_packets
            report["overlong_lines"] = client.framer.overlong
        if raw:
            raw.close()
        if link:
            link.close()
        report["finished_utc"] = utc_now()
        (output / "report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(
        json.dumps(dict(verdict=report["verdict"], directory=str(output), error=report.get("error"))),
        flush=True,
    )
    return result


if __name__ == "__main__":
    sys.exit(main())

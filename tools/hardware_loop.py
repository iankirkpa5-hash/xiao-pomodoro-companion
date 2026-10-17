#!/usr/bin/env python3
"""Bounded PlatformIO/serial evidence capture for legacy and CSV XIAO telemetry."""
import argparse
import csv
from datetime import datetime, timezone
import hashlib
import importlib.util
import json
import math
from pathlib import Path
import re
import shutil
import statistics
import subprocess
import sys
import time
import uuid

ENV = "seeed_xiao_esp32s3"
MIC = re.compile(r"^Mic: mean=(-?\d+) rms=(\S+) peak=(-?\d+) samples=(\d+)$")
TELEMETRY = re.compile(r"^(\d+),([^,]+),(\d+),(Quiet|Normal|Loud)$")
CAPTURE_DIAG = re.compile(r"^Mic capture: frames=(\d+) samples=(\d+) elapsed_ms=(\d+) read_errors=(\d+)$")
I2C_ERROR = re.compile(r"i2cRead.*Error|requestFrom\(\).*Error", re.I)
CSV_FIELDS = ["host_utc", "host_elapsed_s", "record_format", "device_timestamp_ms",
              "mean", "rms", "peak", "samples", "level"]
FAULT = re.compile(r"read failed|init failed|Guru Meditation|Backtrace:|brownout|watchdog|assert failed|abort\(\)", re.I)
BOOT = "XIAO ESP32S3 Round Display Arduino_GFX test"


def utc_now():
    return datetime.now(timezone.utc).isoformat()


def finite_positive(value):
    number = float(value)
    if not math.isfinite(number) or number <= 0:
        raise argparse.ArgumentTypeError("must be finite and greater than zero")
    return number


def positive_int(value):
    number = int(value)
    if number < 1:
        raise argparse.ArgumentTypeError("must be at least one")
    return number


def parse_mic(line):
    """Keep missing legacy/CSV fields missing; never derive a label from amplitude."""
    if re.match(r"^[+-]?\d[^,]*,", line):
        match = TELEMETRY.fullmatch(line)
        if not match:
            raise ValueError("malformed CSV telemetry")
        timestamp, rms, peak, level = match.groups()
        row = dict(record_format="csv_v1", device_timestamp_ms=int(timestamp),
                   mean=None, rms=float(rms), peak=int(peak), samples=None, level=level)
        if (not 0 <= row["device_timestamp_ms"] < 2**32 or not math.isfinite(row["rms"])
                or not 0 <= row["peak"] <= 65535 or row["rms"] < 0
                or row["rms"] > row["peak"] + 0.051):
            raise ValueError("invalid CSV telemetry values")
        return row
    if not line.startswith("Mic:"):
        return None
    match = MIC.fullmatch(line)
    if not match:
        raise ValueError("malformed Mic record")
    mean, rms, peak, samples = match.groups()
    row = dict(record_format="legacy_mic", device_timestamp_ms=None, level=None,
               mean=int(mean), rms=float(rms), peak=int(peak), samples=int(samples))
    if (not math.isfinite(row["rms"]) or not -32768 <= row["mean"] <= 32767
            or not 0 <= row["peak"] <= 65535 or row["rms"] < 0
            or row["rms"] > row["peak"] + 0.051 or row["samples"] < 1):
        raise ValueError("invalid Mic values")
    return row


class LineBuffer:
    """Keep partial serial reads together; never treat a read timeout as a newline."""
    def __init__(self):
        self.pending = b""
        self.discarding = False
        self.overlong = 0

    def feed(self, chunk):
        lines = []
        for byte in chunk:
            if byte == 10:
                if not self.discarding:
                    lines.append(self.pending.rstrip(b"\r").decode("utf-8", errors="replace"))
                self.pending = b""
                self.discarding = False
            elif not self.discarding:
                self.pending += bytes((byte,))
                if len(self.pending) > 4096:
                    self.pending = b""
                    self.discarding = True
                    self.overlong += 1
        return lines


class Observation:
    def __init__(self, expected_samples):
        self.expected_samples = expected_samples
        self.rows = []
        self.errors = []
        self.error_count = 0
        self.boot_times = []
        self.capture_diagnostics = []
        self.i2c_errors = 0

    def error(self, kind, detail, elapsed):
        self.error_count += 1
        if len(self.errors) < 20:
            self.errors.append(dict(kind=kind, detail=detail, host_elapsed_s=round(elapsed, 6)))

    def accept(self, line, elapsed):
        if BOOT in line:
            self.boot_times.append(elapsed)
        if FAULT.search(line):
            self.error("firmware_log", line, elapsed)
            return None
        if I2C_ERROR.search(line):
            self.i2c_errors += 1
            self.error("i2c_read_error", line, elapsed)
            return None
        if line.startswith("Mic capture:"):
            match = CAPTURE_DIAG.fullmatch(line)
            if not match:
                self.error("invalid_capture_diagnostic", line, elapsed)
            else:
                diagnostic = dict(zip(("frames", "samples", "elapsed_ms", "read_errors"),
                                      map(int, match.groups())))
                diagnostic["host_elapsed_s"] = round(elapsed, 6)
                self.capture_diagnostics.append(diagnostic)
                if diagnostic["read_errors"]:
                    self.error("mic_read_errors", line, elapsed)
            return None
        try:
            row = parse_mic(line)
        except ValueError as exc:
            self.error("invalid_record", str(exc) + ": " + line, elapsed)
            return None
        if row is not None:
            row.update(host_utc=utc_now(), host_elapsed_s=round(elapsed, 6))
            self.rows.append(row)
        return row

    def summarize(self, elapsed, min_records, max_gap):
        warnings = []
        rows = self.rows
        gaps = [b["host_elapsed_s"] - a["host_elapsed_s"] for a, b in zip(rows, rows[1:])]
        tail = elapsed - rows[-1]["host_elapsed_s"] if rows else elapsed
        if len(rows) < min_records:
            warnings.append("too_few_records")
        if rows and max([tail] + gaps) > max_gap:
            warnings.append("host_receive_gap_exceeded")
        if rows and rows[0]["host_elapsed_s"] > max(3.0, max_gap):
            warnings.append("late_first_record")
        if any(r["samples"] is not None and r["samples"] != self.expected_samples for r in rows):
            warnings.append("sample_count_differs_from_expected")
        if len(rows) >= min_records and len({(r["mean"], r["rms"], r["peak"]) for r in rows}) == 1:
            warnings.append("constant_statistics_need_stimulus_check")
        if len(self.boot_times) > 1 or any(t > 3 for t in self.boot_times):
            warnings.append("possible_restart_during_capture")
        formats = sorted({r["record_format"] for r in rows})
        if len(formats) > 1:
            warnings.append("mixed_telemetry_formats")
        device_gaps = []
        for a, b in zip(rows, rows[1:]):
            if a["device_timestamp_ms"] is None or b["device_timestamp_ms"] is None:
                continue
            delta = (b["device_timestamp_ms"] - a["device_timestamp_ms"]) % 2**32
            if delta == 0:
                warnings.append("duplicate_device_timestamp")
            elif delta >= 2**31:
                warnings.append("device_timestamp_reset_or_backwards")
            else:
                device_gaps.append(delta)
        if device_gaps and max(device_gaps) > max_gap * 1000:
            warnings.append("device_report_gap_exceeded")
        warnings = sorted(set(warnings))
        return {
            "verdict": "FAILED" if self.error_count else "INCONCLUSIVE" if warnings else "PASS_SERIAL_SMOKE",
            "valid_records": len(rows), "capture_elapsed_s": round(elapsed, 3),
            "error_count": self.error_count, "errors": self.errors, "warnings": warnings,
            "boot_messages": len(self.boot_times),
            "record_formats": formats,
            "sample_count_check": "CHECKED_LEGACY_ROWS" if any(r["samples"] is not None for r in rows)
                                  else "SKIP_NOT_REPORTED_PER_CSV_ROW",
            "device_report_gap_max_ms": max(device_gaps, default=None),
            "levels_observed": sorted({r["level"] for r in rows if r["level"]}),
            "i2c_error_count": self.i2c_errors,
            "capture_diagnostics": self.capture_diagnostics,
            "host_gap_max_s": round(max(gaps), 6) if gaps else None,
            "host_gap_median_s": round(statistics.median(gaps), 6) if gaps else None,
            "tail_without_record_s": round(tail, 6),
            "rms_range": [min(r["rms"] for r in rows), max(r["rms"] for r in rows)] if rows else None,
            "peak_max": max((r["peak"] for r in rows), default=None),
        }


def find_pio(explicit=None):
    if explicit:
        found = shutil.which(explicit)
        if found:
            return [found]
        if Path(explicit).is_file():
            return [str(Path(explicit).resolve())]
        raise RuntimeError("PlatformIO executable not found: " + explicit)
    for name in ("platformio", "pio"):
        found = shutil.which(name)
        if found:
            return [found]
    for path in (Path.home()/".platformio/penv/Scripts/platformio.exe", Path.home()/".platformio/penv/bin/platformio"):
        if path.is_file():
            return [str(path)]
    if importlib.util.find_spec("platformio"):
        return [sys.executable, "-m", "platformio"]
    raise RuntimeError("PlatformIO not found; install PlatformIO Core or pass --pio with its executable path")


def command_text(command, repo):
    try:
        result = subprocess.run(command, cwd=repo, capture_output=True, text=True,
                                encoding="utf-8", errors="replace", timeout=10)
        return result.stdout.strip() if result.returncode == 0 else None
    except (OSError, subprocess.TimeoutExpired):
        return None


def source_metadata(repo):
    files = [repo/"platformio.ini", *sorted((repo/"src").rglob("*"))]
    hashes = {str(p.relative_to(repo)).replace("\\", "/"): hashlib.sha256(p.read_bytes()).hexdigest()
              for p in files if p.is_file()}
    return {"git_head": command_text(["git", "rev-parse", "HEAD"], repo),
            "git_branch": command_text(["git", "branch", "--show-current"], repo),
            "git_status": command_text(["git", "status", "--short"], repo),
            "source_sha256": hashes}


def run_stage(name, command, repo, output, timeout):
    start = time.monotonic()
    print(name + ": running; log -> " + str(output/(name+".log")), flush=True)
    stage = {"command": command, "started_utc": utc_now(), "exit_code": None}
    with (output/(name+".log")).open("wb") as log:
        try:
            result = subprocess.run(command, cwd=repo, stdout=log, stderr=subprocess.STDOUT, timeout=timeout)
            stage["exit_code"] = result.returncode
        except subprocess.TimeoutExpired:
            stage["error"] = "timeout"
        except OSError as exc:
            stage["error"] = str(exc)
    stage["elapsed_s"] = round(time.monotonic()-start, 3)
    return stage


def open_serial(port, baud, wait_seconds):
    try:
        import serial
    except ImportError as exc:
        raise RuntimeError("pySerial missing; run this Python with -m pip install pyserial==3.5") from exc
    deadline = time.monotonic() + wait_seconds
    while True:
        link = serial.Serial(port=None, baudrate=baud, timeout=0.2, write_timeout=1)
        link.dtr, link.rts = True, False
        link.port = port
        try:
            link.open()
            return link
        except (serial.SerialException, OSError) as exc:
            link.close()
            if time.monotonic() >= deadline:
                raise RuntimeError("Cannot open " + port + "; close other serial monitors and recheck the port: " + str(exc)) from exc
            time.sleep(0.25)


def capture(args, output):
    observation = Observation(args.expected_samples)
    framer = LineBuffer()
    link = open_serial(args.monitor_port or args.port, args.baud, args.connect_timeout)
    start = time.monotonic()
    interrupted = False
    elapsed = 0.0
    print("capture: " + str(args.seconds) + " seconds; label=" + args.label, flush=True)
    try:
        with (output/"serial.log").open("wb") as raw, (output/"mic.csv").open("w", newline="", encoding="utf-8") as csv_file:
            writer = csv.DictWriter(csv_file, fieldnames=CSV_FIELDS)
            writer.writeheader()
            while time.monotonic()-start < args.seconds:
                chunk = link.read(max(1, min(link.in_waiting, 4096)))
                elapsed = time.monotonic()-start
                if chunk:
                    raw.write(chunk)
                    for line in framer.feed(chunk):
                        row = observation.accept(line, elapsed)
                        if row is not None:
                            writer.writerow(row)
                    raw.flush()
                    csv_file.flush()
    except KeyboardInterrupt:
        interrupted = True
    except Exception as exc:
        observation.error("capture_error", str(exc), time.monotonic()-start)
    finally:
        link.close()
    elapsed = time.monotonic()-start
    if framer.overlong:
        observation.error("overlong_lines", str(framer.overlong), elapsed)
    report = observation.summarize(elapsed, args.min_records, args.max_gap)
    report["trailing_partial_bytes"] = len(framer.pending)
    if interrupted:
        report["verdict"] = "ABORTED"
    return report


def execute(args, repo, output):
    report = {"schema_version": 2, "started_utc": utc_now(), "action": args.action,
              "source": source_metadata(repo), "stages": {}, "verdict": "NOT_RUN",
              "tool_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
              "python_version": sys.version,
              "device_firmware_identity": "not_reported_by_legacy_or_csv_v1",
              "operator_notes": getattr(args, "notes", ""),
              "physical_acceptance": "pending_manual_screen_touch_and_sound_checks",
              "timestamp_basis": "host_receive_time_plus_device_timestamp_when_reported",
              "device_timestamp_semantics": "confirm_from_running_firmware; format_alone_is_not_proof"}
    exit_code = 1
    try:
        if args.action in ("build", "cycle"):
            pio = find_pio(args.pio)
            report["platformio_version"] = command_text(pio+["--version"], repo)
            base = pio+["run", "--project-dir", str(repo), "--environment", args.env]
            stage = run_stage("build", base, repo, output, args.build_timeout)
            report["stages"]["build"] = stage
            if stage["exit_code"] != 0:
                raise RuntimeError("Build failed; upload and capture were not started")
            firmware = repo/".pio"/"build"/args.env/"firmware.bin"
            if firmware.is_file():
                report["built_firmware_sha256"] = hashlib.sha256(firmware.read_bytes()).hexdigest()
            if args.action == "cycle":
                stage = run_stage("upload", base+["--target", "upload", "--upload-port", args.port],
                                  repo, output, args.upload_timeout)
                report["stages"]["upload"] = stage
                if stage["exit_code"] != 0:
                    raise RuntimeError("Upload failed; capture was not started")
            else:
                report["verdict"], exit_code = "BUILD_OK", 0
        if args.action in ("capture", "cycle"):
            report["capture_settings"] = {k:getattr(args,k) for k in
                ("port", "monitor_port", "baud", "seconds", "label", "expected_samples", "min_records", "max_gap")}
            report["capture"] = capture(args, output)
            report["verdict"] = report["capture"]["verdict"]
            exit_code = {"PASS_SERIAL_SMOKE":0, "INCONCLUSIVE":2, "ABORTED":130}.get(report["verdict"], 1)
    except KeyboardInterrupt:
        report["verdict"], exit_code = "ABORTED", 130
    except Exception as exc:
        report["verdict"], report["error"] = "FAILED", str(exc)
    finally:
        report["finished_utc"] = utc_now()
        (output/"report.json").write_text(json.dumps(report, ensure_ascii=False, indent=2)+"\n", encoding="utf-8")
    print(json.dumps({"verdict":report["verdict"], "report":str(output/"report.json")}, ensure_ascii=False))
    return exit_code


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=["doctor", "build", "capture", "cycle"], help="cycle builds, uploads, then captures; capture never uploads")
    parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--pio", help="PlatformIO executable path (one executable, not a shell command)")
    parser.add_argument("--env", default=ENV)
    parser.add_argument("--port", help="explicit upload/capture device, e.g. COM11; never auto-selects")
    parser.add_argument("--monitor-port", help="capture port if USB re-enumerates to a different port after upload")
    parser.add_argument("--baud", type=positive_int, default=115200)
    parser.add_argument("--seconds", type=finite_positive, default=30)
    parser.add_argument("--label", default="baseline")
    parser.add_argument("--notes", default="", help="operator conditions/observations; labels do not prove a sound action")
    parser.add_argument("--expected-samples", type=positive_int, default=160)
    parser.add_argument("--min-records", type=positive_int, default=10)
    parser.add_argument("--max-gap", type=finite_positive, default=2)
    parser.add_argument("--connect-timeout", type=finite_positive, default=10)
    parser.add_argument("--build-timeout", type=finite_positive, default=1200)
    parser.add_argument("--upload-timeout", type=finite_positive, default=180)
    args = parser.parse_args(argv)
    repo = args.repo.resolve()
    if not (repo/"platformio.ini").is_file():
        parser.error("--repo must contain platformio.ini")
    if not re.fullmatch(r"[A-Za-z0-9_-]+", args.env):
        parser.error("invalid environment name")
    if args.seconds > 600:
        parser.error("--seconds must be at most 600")
    if args.action in ("capture", "cycle") and not args.port:
        parser.error("--port is required; use doctor to list ports")
    if args.action == "doctor":
        pio_error, serial_error, pio, ports = None, None, None, []
        try:
            pio = find_pio(args.pio)
        except RuntimeError as exc:
            pio_error = str(exc)
        try:
            from serial.tools import list_ports
            ports = [dict(device=p.device, description=p.description, hwid=p.hwid) for p in list_ports.comports()]
        except ImportError:
            serial_error = "pySerial missing; run this Python with -m pip install pyserial==3.5"
        print(json.dumps(dict(python=sys.executable, repo=str(repo), platformio=pio,
            platformio_version=command_text(pio+["--version"],repo) if pio else None,
            platformio_error=pio_error, serial_error=serial_error, ports=ports), ensure_ascii=False, indent=2))
        return 2 if pio_error or serial_error else 0
    if args.action == "cycle" and not importlib.util.find_spec("serial"):
        parser.error("pySerial is required before a cycle; install pyserial==3.5 with this Python")
    output = repo/"test-runs"/(datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")+"-"+uuid.uuid4().hex[:8])
    output.mkdir(parents=True, exist_ok=False)
    return execute(args, repo, output)


if __name__ == "__main__":
    sys.exit(main())

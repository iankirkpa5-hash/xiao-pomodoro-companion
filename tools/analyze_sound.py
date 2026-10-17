#!/usr/bin/env python3
"""Read-only CSV/log comparison. Generates offline SVG plots and a JSON audit.

No serial access, firmware changes, acoustic classification, or gap filling.
Only the Python standard library is required.
"""
import argparse
import csv
import hashlib
from html import escape
import json
import math
from pathlib import Path
import re
import statistics

UINT32 = 2**32
LEVELS = ("Quiet", "Normal", "Loud")
COLORS = {"Quiet": "#327965", "Normal": "#a27612", "Loud": "#ba4539"}
TOKEN = re.compile(r"(?<![\d.,])(\d+),(\d+(?:\.\d+)?),(\d+),(Quiet|Normal|Loud)(?!\w)")
I2C = re.compile(r"i2cRead.*Error|requestFrom\(\).*Error", re.I)
DIAG = re.compile(r"^Mic capture: frames=(\d+) samples=(\d+) elapsed_ms=(\d+) read_errors=(\d+)$")


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def telemetry(timestamp, rms, peak, level):
    row = dict(device_timestamp_ms=int(timestamp), rms=float(rms), peak=int(peak), level=level)
    if (not 0 <= row["device_timestamp_ms"] < UINT32 or not math.isfinite(row["rms"])
            or not 0 <= row["peak"] <= 65535 or not 0 <= row["rms"] <= row["peak"] + .051
            or level not in LEVELS):
        raise ValueError("invalid telemetry value")
    return row


def signature(row):
    return tuple(row[k] for k in ("device_timestamp_ms", "rms", "peak", "level"))


def read_csv(path):
    rows = []
    with path.open(encoding="utf-8-sig", newline="") as stream:
        reader = csv.DictReader(stream)
        required = {"host_elapsed_s", "record_format", "device_timestamp_ms", "rms", "peak", "level"}
        if not required.issubset(reader.fieldnames or []):
            raise ValueError("expected hardware_loop CSV with host and device timestamps")
        for number, record in enumerate(reader, 2):
            try:
                if record["record_format"] != "csv_v1":
                    raise ValueError("legacy rows have no device time; do not invent it")
                row = telemetry(*(record[k] for k in ("device_timestamp_ms", "rms", "peak", "level")))
                host = float(record["host_elapsed_s"])
                if not math.isfinite(host) or host < 0 or rows and host < rows[-1]["host_elapsed_s"]:
                    raise ValueError("host elapsed time must be finite and nondecreasing")
                row.update(host_elapsed_s=host, csv_line=number)
                rows.append(row)
            except (ValueError, TypeError) as exc:
                raise ValueError(f"{path}:{number}: {exc}") from exc
    if not rows:
        raise ValueError(f"no measurements in {path}")
    return rows


def timing(rows, gap_ms=750):
    """Break curves on either clock's gap, reset, or duplicate; unwrap only forward time."""
    gaps, clock_events = [], []
    segment, elapsed = 0, 0.0
    for i, row in enumerate(rows):
        row.update(break_before=i == 0, host_delta_s=None, device_delta_ms=None)
        if i:
            previous = rows[i - 1]
            host_delta = row["host_elapsed_s"] - previous["host_elapsed_s"]
            delta = (row["device_timestamp_ms"] - previous["device_timestamp_ms"]) % UINT32
            row["host_delta_s"] = host_delta
            if delta == 0 or delta >= UINT32 // 2:
                kind = "duplicate" if delta == 0 else "reset_or_backwards"
                clock_events.append(dict(csv_line=row["csv_line"], kind=kind))
                segment += 1
                elapsed = 0.0
                row["break_before"] = True
            else:
                row["device_delta_ms"] = delta
                elapsed += delta / 1000
                if row["device_timestamp_ms"] < previous["device_timestamp_ms"]:
                    clock_events.append(dict(csv_line=row["csv_line"], kind="uint32_wrap"))
                if delta > gap_ms or host_delta * 1000 > gap_ms:
                    row["break_before"] = True
                    gaps.append(dict(from_csv_line=previous["csv_line"], to_csv_line=row["csv_line"],
                                     host_start_s=previous["host_elapsed_s"], host_end_s=row["host_elapsed_s"],
                                     host_delta_s=round(host_delta, 6), device_delta_ms=delta))
        row.update(device_segment=segment, device_elapsed_s=round(elapsed, 6))
    return gaps, clock_events


def scan_log(path):
    standalone, embedded, diagnostics, touches = [], [], [], []
    errors = 0
    # A trailing partial line is evidence, not a complete telemetry record.
    lines = path.read_bytes().decode("utf-8", errors="replace").splitlines(keepends=True)
    for number, raw in enumerate(lines, 1):
        line = raw.rstrip("\r\n")
        errors += bool(I2C.search(line))
        if "Touch:" in line:
            touches.append(dict(log_line=number, text=line))
        match = DIAG.fullmatch(line)
        if match:
            diagnostics.append(dict(zip(("frames", "samples", "elapsed_ms", "read_errors"),
                                        map(int, match.groups()))))
        for match in TOKEN.finditer(line):
            try:
                row = telemetry(*match.groups())
            except ValueError:
                continue
            row.update(log_line=number, text=line)
            if match.span() == (0, len(line)) and raw.endswith("\n"):
                standalone.append(row)
            else:
                embedded.append(row)
    return dict(standalone=standalone, embedded=embedded, diagnostics=diagnostics,
                i2c_error_count=errors, touch_messages=touches)


def percentile(values, fraction):
    ordered = sorted(values)
    position = (len(ordered) - 1) * fraction
    lo, hi = math.floor(position), math.ceil(position)
    return ordered[lo] + (ordered[hi] - ordered[lo]) * (position - lo)


def describe(values):
    return dict(min=min(values), median=statistics.median(values),
                p95=percentile(values, .95), max=max(values))


def analyze_run(label, directory, gap_ms=750):
    rows = read_csv(directory / "mic.csv")
    gaps, events = timing(rows, gap_ms)
    report = json.loads((directory / "report.json").read_text(encoding="utf-8"))
    raw = scan_log(directory / "serial.log")
    capture = report["capture"]
    report_diag = [{k: d[k] for k in ("frames", "samples", "elapsed_ms", "read_errors")}
                   for d in capture["capture_diagnostics"]]
    checks = {
        "row_count_matches_report": len(rows) == capture["valid_records"],
        "csv_matches_standalone_raw_in_order": [signature(r) for r in rows] == [signature(r) for r in raw["standalone"]],
        "i2c_count_matches_report": raw["i2c_error_count"] == capture["i2c_error_count"],
        "diagnostics_match_report": raw["diagnostics"] == report_diag,
        "rms_range_matches_report": [min(r["rms"] for r in rows), max(r["rms"] for r in rows)] == capture["rms_range"],
        "peak_max_matches_report": max(r["peak"] for r in rows) == capture["peak_max"],
    }
    failed = [name for name, ok in checks.items() if not ok]
    if failed:
        raise ValueError(f"{label}: inconsistent input evidence: {', '.join(failed)}")

    for gap in gaps:
        a, b = rows[gap["from_csv_line"] - 2], rows[gap["to_csv_line"] - 2]
        gap["embedded_log_lines"] = [r["log_line"] for r in raw["embedded"]
            if 0 < (r["device_timestamp_ms"] - a["device_timestamp_ms"]) % UINT32
            < (b["device_timestamp_ms"] - a["device_timestamp_ms"]) % UINT32]
    candidates = sorted(raw["standalone"] + raw["embedded"], key=lambda r: r["log_line"])
    candidate_deltas = [(b["device_timestamp_ms"] - a["device_timestamp_ms"]) % UINT32
                        for a, b in zip(candidates, candidates[1:])]
    diagnostics = raw["diagnostics"]
    sample_rates = [d["samples"] * 1000 / d["elapsed_ms"] for d in diagnostics if d["elapsed_ms"] > 0]
    host_deltas = [r["host_delta_s"] for r in rows if r["host_delta_s"] is not None]
    device_deltas = [r["device_delta_ms"] for r in rows if r["device_delta_ms"] is not None]
    summary = dict(
        label=label, source_directory=directory.as_posix(), started_utc=report["started_utc"],
        source_sha256={name: sha256(directory / name) for name in ("mic.csv", "serial.log", "report.json")},
        local_source_at_capture=report["source"]["git_head"],
        device_firmware_identity=report["device_firmware_identity"],
        evidence_checks=checks, capture_verdict=report["verdict"],
        rows=len(rows), rms=describe([r["rms"] for r in rows]), peak=describe([r["peak"] for r in rows]),
        level_report_counts={level: sum(r["level"] == level for r in rows) for level in LEVELS},
        host_delta_s=describe(host_deltas) if host_deltas else None,
        device_delta_ms=describe(device_deltas) if device_deltas else None,
        first_host_report_s=rows[0]["host_elapsed_s"],
        tail_without_valid_report_s=round(capture["capture_elapsed_s"] - rows[-1]["host_elapsed_s"], 6),
        capture_elapsed_s=capture["capture_elapsed_s"], gap_threshold_ms=gap_ms,
        gaps=gaps, clock_events=events,
        top_peak_rows=sorted(rows, key=lambda r: r["peak"], reverse=True)[:5],
        i2c_error_count=raw["i2c_error_count"],
        embedded_payload_candidates=raw["embedded"],
        raw_candidate_count=len(candidates),
        raw_candidate_delta_ms=describe(candidate_deltas) if candidate_deltas else None,
        touch_messages=raw["touch_messages"],
        diagnostics=diagnostics,
        diagnostic_samples_per_second=describe(sample_rates) if sample_rates else None,
    )
    return dict(summary=summary, rows=rows, report=report)


def nice_limit(value):
    if value <= 0:
        return 1
    power = 10 ** math.floor(math.log10(value))
    return math.ceil(value / power * 1.05) * power


class SVG:
    def __init__(self, width, height, title):
        self.parts = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}">',
                      f'<title>{escape(title)}</title>',
                      '<style>text{font-family:Verdana,sans-serif;fill:#203d39} .small{font-size:12px} .body{font-size:14px} .title{font-size:28px;font-weight:bold} .heading{font-size:18px;font-weight:bold}</style>',
                      f'<rect width="{width}" height="{height}" fill="#faf9f4"/>']

    def text(self, x, y, text, cls="body", anchor="start"):
        self.parts.append(f'<text x="{x:.2f}" y="{y:.2f}" class="{cls}" text-anchor="{anchor}">{escape(str(text))}</text>')

    def rect(self, x, y, w, h, fill):
        self.parts.append(f'<rect x="{x:.2f}" y="{y:.2f}" width="{w:.2f}" height="{h:.2f}" fill="{fill}"/>')

    def line(self, x1, y1, x2, y2, color="#dce3df"):
        self.parts.append(f'<path d="M{x1:.2f},{y1:.2f} L{x2:.2f},{y2:.2f}" stroke="{color}" fill="none"/>')

    def dot(self, x, y, color, title):
        self.parts.append(f'<circle cx="{x:.2f}" cy="{y:.2f}" r="3" fill="{color}"><title>{escape(title)}</title></circle>')

    def save(self, path):
        path.write_text("\n".join(self.parts + ["</svg>"]) + "\n", encoding="utf-8")


def panel(svg, run, metric, x, y, w, h, xmax, ymax, color, show_gaps=True):
    rows, summary = run["rows"], run["summary"]
    px = lambda t: x + w * t / xmax
    py = lambda v: y + h - h * v / ymax
    svg.rect(x, y, w, h, "#ffffff")
    if show_gaps:
        for gap in summary["gaps"]:
            svg.rect(px(gap["host_start_s"]), y, w * (gap["host_end_s"] - gap["host_start_s"]) / xmax, h, "#f9e5d9")
        for a, b in [(0, rows[0]["host_elapsed_s"]), (rows[-1]["host_elapsed_s"], summary["capture_elapsed_s"])]:
            svg.rect(px(a), y, w * max(0, b - a) / xmax, h, "#edf0ee")
    for i in range(5):
        value = ymax * i / 4
        svg.line(x, py(value), x + w, py(value))
        svg.text(x - 9, py(value) + 4, f"{value:g}", "small", "end")
    for t in range(0, int(xmax) + 1, 5):
        svg.line(px(t), y, px(t), y + h)
        svg.text(px(t), y + h + 20, f"{t:g}", "small", "middle")
    points = []
    for row in rows:
        if row["break_before"] and points:
            svg.parts.append(f'<polyline points="{" ".join(points)}" stroke="{color}" stroke-width="1.6" fill="none"/>')
            points = []
        points.append(f'{px(row["host_elapsed_s"]):.2f},{py(row[metric]):.2f}')
        svg.dot(px(row["host_elapsed_s"]), py(row[metric]), color,
                f'CSV line {row["csv_line"]}: host {row["host_elapsed_s"]}s, device {row["device_timestamp_ms"]}ms, {metric} {row[metric]}, {row["level"]}')
    if points:
        svg.parts.append(f'<polyline points="{" ".join(points)}" stroke="{color}" stroke-width="1.6" fill="none"/>')


def comparison_chart(runs, path, shared):
    svg = SVG(1380, 960, "T02 sound comparison")
    svg.text(40, 48, "T02 / Sound comparison", "title")
    scales = "Shared linear scales" if shared else "Independent linear scales: compare patterns, not apparent height"
    svg.text(40, 78, f"{scales}. Centered PCM units, not dB SPL. Capture: 2026-10-13.")
    svg.text(40, 103, "Dots = valid CSV rows. Orange = report gap >750 ms on either clock; gray = no valid edge report. No gap filling.", "small")
    xmax = max(30, math.ceil(max(r["summary"]["capture_elapsed_s"] for r in runs) / 5) * 5)
    for i, run in enumerate(runs):
        s = run["summary"]
        y = 174 + i * 245
        svg.text(95, y - 32, f'{s["label"].upper()}  /  {s["rows"]} rows  /  I2C errors {s["i2c_error_count"]}  /  {s["capture_verdict"]}', "heading")
        for metric, x, color in [("rms", 95, "#157e80"), ("peak", 765, "#ba4539")]:
            limit = max(r["summary"][metric]["max"] for r in runs) if shared else s[metric]["max"]
            svg.text(x, y - 10, f'{metric.upper()}   median {s[metric]["median"]:g}   max {s[metric]["max"]:g}', "small")
            panel(svg, run, metric, x, y, 570, 155, xmax, nice_limit(limit), color)
    svg.text(690, 878, "Host receive elapsed time (seconds); each capture starts independently", "body", "middle")
    svg.text(40, 917, "Condition names describe operator instructions, not confirmed sound sources. Exact distance and clap count were not recorded.", "small")
    svg.text(40, 939, "RMS/peak summarize report windows. Firmware levels use short-frame classification, debounce and a Loud hold; these are not speech labels.", "small")
    svg.save(path)


def timing_chart(runs, path):
    svg = SVG(1380, 920, "T02 report timing and firmware labels")
    svg.text(40, 48, "T02 / Report timing and firmware labels", "title")
    svg.text(40, 78, "These are reporting clocks, not PCM sample timestamps. Marked gaps do not prove audio DMA stopped.")
    svg.text(40, 102, "Firmware labels are shown only at received rows, without filling state duration. Embedded log payloads stay excluded.", "small")
    xmax = max(30, math.ceil(max(r["summary"]["capture_elapsed_s"] for r in runs) / 5) * 5)
    ymax = nice_limit(max([.5] + [r["host_delta_s"] for run in runs for r in run["rows"][1:]]
                          + [r["device_delta_ms"] / 1000 for run in runs for r in run["rows"]
                             if r["device_delta_ms"] is not None]))
    for i, run in enumerate(runs):
        y, s = 155 + i * 240, run["summary"]
        svg.text(95, y - 15, f'{s["label"].upper()}  /  {len(s["gaps"])} gaps  /  {len(s["embedded_payload_candidates"])} embedded candidates', "heading")
        x, w, h = 95, 570, 150
        px = lambda t: x + w * t / xmax
        py = lambda t: y + h - h * t / ymax
        svg.rect(x, y, w, h, "white")
        for t in (ymax * j / 4 for j in range(5)):
            svg.line(x, py(t), x+w, py(t))
            svg.text(x-10, py(t)+4, f"{t:g}s", "small", "end")
        for row in run["rows"][1:]:
            for key, scale, color, offset in [("host_delta_s", 1, "#157e80", -2), ("device_delta_ms", .001, "#ba4539", 2)]:
                if row[key] is not None:
                    v = row[key] * scale
                    svg.dot(px(row["host_elapsed_s"])+offset, py(v), color, f'{key}: {row[key]}')
        svg.text(x, y+h+42, "Teal: host delta. Rust: device delta. Horizontal offset separates overlapping dots.", "small")
        for level, yy in zip(reversed(LEVELS), (y+20, y+75, y+130)):
            svg.text(755, yy+4, level, "small", "end")
            svg.line(775, yy, 1325, yy)
            for row in run["rows"]:
                if row["level"] == level:
                    svg.dot(775+550*row["host_elapsed_s"]/xmax, yy, COLORS[level], f'host {row["host_elapsed_s"]}s; RMS {row["rms"]}; peak {row["peak"]}')
        for t in range(0, int(xmax)+1, 5):
            svg.text(px(t), y+h+20, t, "small", "middle")
            svg.text(775+550*t/xmax, y+h+20, t, "small", "middle")
    svg.text(690, 896, "Host receive elapsed time (seconds). Detailed clock events and gap boundaries are preserved in summary.json.", "body", "middle")
    svg.save(path)


def write_outputs(runs, output, gap_ms=750):
    output.mkdir(parents=True, exist_ok=True)
    # Never allow derived output to replace a capture artifact.
    for run in runs:
        source = Path(run["summary"]["source_directory"]).resolve()
        if output.resolve() == source or source.is_relative_to(output.resolve()):
            raise ValueError("output must not be a capture directory or its ancestor")
    provenance = [r["report"]["source"]["source_sha256"] for r in runs]
    summary = dict(schema_version=1, analyzer_sha256=sha256(Path(__file__)),
                   scope="offline saved evidence only; no serial or firmware changes",
                   percentile_method="linear interpolation at (n-1)*p; unweighted per-report distribution",
                   gap_threshold_ms=gap_ms,
                   identical_local_source_hashes_at_capture=all(p == provenance[0] for p in provenance),
                   running_binary_verified=False,
                   runs=[r["summary"] for r in runs])
    (output / "summary.json").write_text(json.dumps(summary, indent=2, ensure_ascii=True) + "\n", encoding="utf-8")
    comparison_chart(runs, output / "comparison-shared.svg", shared=True)
    comparison_chart(runs, output / "comparison-detail.svg", shared=False)
    timing_chart(runs, output / "timing-levels.svg")
    return summary


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--run", action="append", required=True, metavar="LABEL=DIRECTORY")
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    try:
        runs = []
        for value in args.run:
            label, directory = value.split("=", 1)
            if not label or label in [r["summary"]["label"] for r in runs]:
                raise ValueError("run labels must be nonempty and unique")
            runs.append(analyze_run(label, Path(directory)))
        if len(runs) != 3:
            raise ValueError("this comparison expects exactly three labeled captures")
        summary = write_outputs(runs, args.output)
        for run in summary["runs"]:
            print(f'{run["label"]}: {run["rows"]} rows, {len(run["gaps"])} gaps, '
                  f'{len(run["embedded_payload_candidates"])} embedded candidates, '
                  f'peak max {run["peak"]["max"]}, capture {run["capture_verdict"]}')
        print(f"Saved offline comparison: {args.output}")
    except (ValueError, OSError, KeyError) as exc:
        parser.exit(1, f"Analysis failed: {exc}\n")


if __name__ == "__main__":
    main()

"""Pure-software checks. None of these tests opens a serial port."""
import csv
import json
from pathlib import Path
import sys
import tempfile
import unittest
import xml.etree.ElementTree as ET

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import analyze_sound as sound


def sample(timestamp, host, rms=10, peak=50, level="Quiet", line=2):
    return dict(sound.telemetry(timestamp, rms, peak, level), host_elapsed_s=host, csv_line=line)


class SoundAnalysisTests(unittest.TestCase):
    def test_values_and_centered_peak_above_signed_limit(self):
        self.assertEqual(sound.telemetry(0, 1637.7, 42646, "Loud")["peak"], 42646)
        for values in [(0, "nan", 2, "Quiet"), (0, -1, 2, "Quiet"), (0, 30, 2, "Quiet"),
                       (-1, 1, 2, "Quiet"), (2**32, 1, 2, "Quiet"), (0, 1, 65536, "Quiet"),
                       (0, 1, 2, "Unknown")]:
            with self.assertRaises(ValueError):
                sound.telemetry(*values)

    def test_forward_clock_and_gap(self):
        rows = [sample(1000, .1), sample(1500, .6, line=3), sample(3000, 2.1, line=4)]
        gaps, events = sound.timing(rows)
        self.assertEqual(events, [])
        self.assertEqual(len(gaps), 1)
        self.assertEqual(gaps[0]["device_delta_ms"], 1500)
        self.assertEqual([r["break_before"] for r in rows], [True, False, True])
        self.assertEqual(rows[-1]["device_elapsed_s"], 2)

    def test_host_only_gap(self):
        rows = [sample(1000, .1), sample(1500, 1.6, line=3)]
        gaps, _ = sound.timing(rows)
        self.assertEqual(len(gaps), 1)
        self.assertEqual(gaps[0]["device_delta_ms"], 500)
        self.assertTrue(rows[-1]["break_before"])

    def test_rollover_is_not_reset(self):
        rows = [sample(2**32-200, 0), sample(300, .5, line=3)]
        gaps, events = sound.timing(rows)
        self.assertEqual(gaps, [])
        self.assertEqual(events[0]["kind"], "uint32_wrap")
        self.assertEqual(rows[1]["device_elapsed_s"], .5)
        self.assertFalse(rows[1]["break_before"])

    def test_reset_and_duplicate_remain_visible(self):
        rows = [sample(10000, 0), sample(100, .5, line=3), sample(100, 1, line=4)]
        _, events = sound.timing(rows)
        self.assertEqual([e["kind"] for e in events], ["reset_or_backwards", "duplicate"])
        self.assertEqual([r["device_segment"] for r in rows], [0, 1, 2])
        self.assertTrue(all(r["break_before"] for r in rows))

    def test_log_interleaving_is_not_silently_recovered(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "serial.log"
            path.write_bytes(b"100,10.0,50,Quiet\r\n"
                             b"[700][E] requestFrom(): i2cRead returned Error600,20.0,70,Normal\n -1\n"
                             b"Touch: x=152 y=1, running=false\n"
                             b"Mic capture: frames=500 samples=80000 elapsed_ms=5000 read_errors=0\n"
                             b"1100,30.0,80,Loud")
            result = sound.scan_log(path)
            self.assertEqual(len(result["standalone"]), 1)
            self.assertEqual(len(result["embedded"]), 2)  # Includes unterminated tail.
            self.assertEqual(result["i2c_error_count"], 1)
            self.assertEqual(len(result["touch_messages"]), 1)
            self.assertEqual(result["diagnostics"][0]["samples"], 80000)

    def test_csv_empty_missing_and_invalid(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "mic.csv"
            for text in ("", "timestamp_ms,rms,peak,level\n1,1,2,Quiet\n",
                         "host_elapsed_s,record_format,device_timestamp_ms,rms,peak,level\n0,csv_v1,1,nan,2,Quiet\n",
                         "host_elapsed_s,record_format,device_timestamp_ms,rms,peak,level\n0,legacy_mic,,1,2,Quiet\n"):
                path.write_text(text)
                with self.assertRaises(ValueError):
                    sound.read_csv(path)

    def test_csv_host_time_must_not_go_backwards(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "mic.csv"
            path.write_text("host_elapsed_s,record_format,device_timestamp_ms,rms,peak,level\n"
                            "1,csv_v1,100,1,2,Quiet\n0,csv_v1,600,1,2,Quiet\n")
            with self.assertRaises(ValueError):
                sound.read_csv(path)

    def test_percentiles_are_per_report_not_audio_rms(self):
        self.assertEqual(sound.describe([0, 10])["median"], 5)
        self.assertEqual(sound.describe([0, 10])["p95"], 9.5)

    def fixture(self, directory):
        rows = [sample(1000, .1), sample(2000, 1.1, 30, 100, "Loud", line=3)]
        with (directory / "mic.csv").open("w", newline="") as stream:
            fields = ["host_elapsed_s", "record_format", "device_timestamp_ms", "rms", "peak", "level"]
            writer = csv.DictWriter(stream, fieldnames=fields, extrasaction="ignore")
            writer.writeheader()
            writer.writerows(dict(r, record_format="csv_v1") for r in rows)
        (directory / "serial.log").write_text("1000,10.0,50,Quiet\n"
            "[1600][E] requestFrom(): i2cRead returned Error1500,20.0,70,Normal\n -1\n"
            "2000,30.0,100,Loud\n")
        report = dict(started_utc="2026-10-13T00:00:00Z", verdict="FAILED", device_firmware_identity="unknown",
                      source=dict(git_head="fixture", source_sha256={}),
                      capture=dict(valid_records=2, i2c_error_count=1, capture_diagnostics=[],
                                   rms_range=[10, 30], peak_max=100, capture_elapsed_s=2))
        (directory / "report.json").write_text(json.dumps(report))

    def test_reconcile_gap_evidence_and_render_without_touching_inputs(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "capture"
            source.mkdir()
            self.fixture(source)
            before = {p.name: sound.sha256(p) for p in source.iterdir()}
            run = sound.analyze_run("quiet < test", source)
            self.assertEqual(run["summary"]["gaps"][0]["embedded_log_lines"], [2])
            self.assertEqual(run["summary"]["raw_candidate_count"], 3)
            result = sound.write_outputs([run, run, run], root / "plots")
            self.assertEqual(result["runs"][0]["capture_verdict"], "FAILED")
            self.assertEqual(before, {p.name: sound.sha256(p) for p in source.iterdir()})
            for name in ("comparison-shared.svg", "comparison-detail.svg", "timing-levels.svg"):
                tree = ET.parse(root / "plots" / name)
                self.assertTrue(tree.getroot().tag.endswith("svg"))
            with self.assertRaises(ValueError):
                sound.write_outputs([run], source)

    def test_report_or_raw_mismatch_fails_instead_of_hiding_it(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory)
            self.fixture(source)
            path = source / "report.json"
            report = json.loads(path.read_text())
            report["capture"]["valid_records"] = 60
            path.write_text(json.dumps(report))
            with self.assertRaisesRegex(ValueError, "row_count_matches_report"):
                sound.analyze_run("fixture", source)


if __name__ == "__main__":
    unittest.main()

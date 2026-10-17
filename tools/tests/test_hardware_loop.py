import importlib.util
import csv
import json
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

MODULE = Path(__file__).resolve().parents[1]/"hardware_loop.py"
spec = importlib.util.spec_from_file_location("hardware_loop", MODULE)
hil = importlib.util.module_from_spec(spec)
spec.loader.exec_module(hil)


class HardwareLoopTests(unittest.TestCase):
    def csv_healthy(self):
        observation = hil.Observation(160)
        for i in range(10):
            observation.accept(f"{1000+i*500},{3+i}.0,{20+i},Quiet", i*0.5)
        return observation

    def test_csv_keeps_timestamp_and_label_without_fabricating_missing_fields(self):
        row = hil.parse_mic("12000,4.0,14,Loud")
        self.assertEqual(row["device_timestamp_ms"], 12000)
        self.assertEqual(row["level"], "Loud")
        self.assertIsNone(row["mean"])
        self.assertIsNone(row["samples"])
        self.assertIsNone(hil.parse_mic("timestamp_ms,rms,peak,level"))
        report = self.csv_healthy().summarize(5, 10, 2)
        self.assertEqual(report["verdict"], "PASS_SERIAL_SMOKE")
        self.assertEqual(report["sample_count_check"], "SKIP_NOT_REPORTED_PER_CSV_ROW")
        self.assertEqual(report["device_report_gap_max_ms"], 500)

    def test_invalid_csv_is_not_ignored(self):
        for line in ["1,nan,5,Quiet", "1,inf,5,Quiet", "1,-1,5,Quiet",
                     "4294967296,1,5,Quiet", "-1,1,5,Quiet", "1,6,5,Quiet",
                     "1,1,65536,Quiet", "1,1,5,Unknown", "1,1,5,Quiet,extra"]:
            with self.subTest(line=line):
                observation = self.csv_healthy()
                observation.accept(line, 4.9)
                self.assertEqual(observation.summarize(5, 10, 2)["verdict"], "FAILED")

    def test_device_clock_wrap_reset_duplicate_and_gap(self):
        for timestamps, warning in [([2**32-250, 250], None),
                                    ([10000, 100], "device_timestamp_reset_or_backwards"),
                                    ([100, 100], "duplicate_device_timestamp"),
                                    ([100, 5100], "device_report_gap_exceeded")]:
            with self.subTest(timestamps=timestamps):
                observation = hil.Observation(160)
                for i, timestamp in enumerate(timestamps):
                    observation.accept(f"{timestamp},{3+i},10,Quiet", i*0.5)
                report = observation.summarize(1, 2, 2)
                if warning:
                    self.assertIn(warning, report["warnings"])
                else:
                    self.assertEqual(report["verdict"], "PASS_SERIAL_SMOKE")
                    self.assertEqual(report["device_report_gap_max_ms"], 500)

    def test_capture_diagnostics_and_i2c_errors_are_retained(self):
        observation = self.csv_healthy()
        observation.accept("Mic capture: frames=500 samples=80000 elapsed_ms=5000 read_errors=0", 5)
        report = observation.summarize(5, 10, 2)
        self.assertEqual(report["capture_diagnostics"][0]["samples"], 80000)
        self.assertEqual(report["verdict"], "PASS_SERIAL_SMOKE")
        observation.accept("[123] requestFrom(): i2cRead returned Error -1", 5)
        report = observation.summarize(5, 10, 2)
        self.assertEqual(report["i2c_error_count"], 1)
        self.assertEqual(report["verdict"], "FAILED")
        for line in ["Mic capture: broken", "Mic: no fresh complete PCM frames",
                     "Mic capture: frames=50 samples=8000 elapsed_ms=5000 read_errors=1"]:
            with self.subTest(line=line):
                observation = self.csv_healthy()
                observation.accept(line, 5)
                self.assertEqual(observation.summarize(5, 10, 2)["verdict"], "FAILED")

    def test_mixed_formats_are_flagged(self):
        observation = self.csv_healthy()
        observation.accept("Mic: mean=1256 rms=4.0 peak=11 samples=160", 5)
        self.assertIn("mixed_telemetry_formats", observation.summarize(5.1, 10, 2)["warnings"])

    def test_capture_saves_fragmented_csv_and_closes_link(self):
        class FakeLink:
            in_waiting = 4096
            closed = False
            clock = 0
            chunks = iter([b"timestamp_ms,rms,peak,level\r\n1000,3.0,",
                           b"12,Quiet\r\n1500,4.0,14,Loud\n"])

            def read(self, size):
                self.clock += 0.5
                return next(self.chunks, b"")

            def close(self):
                self.closed = True

        link = FakeLink()
        args = SimpleNamespace(expected_samples=160, monitor_port=None, port="FAKE",
                               baud=115200, connect_timeout=1, seconds=2, label="SIMULATED",
                               min_records=2, max_gap=2)
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            with patch.object(hil, "open_serial", return_value=link), \
                 patch.object(hil.time, "monotonic", side_effect=lambda: link.clock):
                report = hil.capture(args, root)
            self.assertTrue(link.closed)
            self.assertEqual(report["verdict"], "PASS_SERIAL_SMOKE")
            with (root/"mic.csv").open(newline="", encoding="utf-8") as stream:
                rows = list(csv.DictReader(stream))
            self.assertEqual(len(rows), 2)
            self.assertEqual(rows[1]["level"], "Loud")
            self.assertEqual(rows[0]["device_timestamp_ms"], "1000")
            self.assertEqual(rows[0]["samples"], "")
            self.assertTrue((root/"serial.log").read_bytes().endswith(b"14,Loud\n"))

    def healthy(self):
        observation = hil.Observation(160)
        for i in range(10):
            observation.accept(f"Mic: mean=1200 rms={3+i}.0 peak={20+i} samples=160", i*0.5)
        return observation

    def test_fragmented_serial_and_crlf(self):
        buffer = hil.LineBuffer()
        self.assertEqual(buffer.feed(b"Mic: mean=1256 rms="), [])
        self.assertEqual(buffer.feed(b"4.0 peak=11 samples=160\r"), [])
        lines = buffer.feed(b"\nPomodoro: Focus 1500s running=false\n")
        self.assertEqual(hil.parse_mic(lines[0])["rms"], 4)
        self.assertIsNone(hil.parse_mic(lines[1]))

    def test_overlong_line_is_discarded_until_newline(self):
        buffer = hil.LineBuffer()
        buffer.feed(b"a"*5000)
        self.assertEqual(buffer.feed(b"tail\nOK\n"), ["OK"])
        self.assertEqual(buffer.overlong, 1)

    def test_impossible_values_do_not_pass(self):
        for line in ["Mic: mean=0 rms=nan peak=10 samples=160",
                     "Mic: mean=0 rms=inf peak=10 samples=160",
                     "Mic: mean=0 rms=11.0 peak=10 samples=160",
                     "Mic: mean=0 rms=1.0 peak=10 samples=0",
                     "Mic: mean=0 rms=bad peak=10 samples=160"]:
            with self.subTest(line=line), self.assertRaises(ValueError):
                hil.parse_mic(line)

    def test_healthy_stream_only_passes_serial_smoke(self):
        self.assertEqual(self.healthy().summarize(5,10,2)["verdict"], "PASS_SERIAL_SMOKE")

    def test_empty_partial_and_stalled_streams_are_inconclusive(self):
        self.assertEqual(hil.Observation(160).summarize(30,10,2)["verdict"], "INCONCLUSIVE")
        self.assertEqual(self.healthy().summarize(10,10,2)["verdict"], "INCONCLUSIVE")
        observation = self.healthy()
        observation.rows[0]["samples"] = 80
        self.assertIn("sample_count_differs_from_expected", observation.summarize(5,10,2)["warnings"])

    def test_faults_override_otherwise_healthy_stream(self):
        for line in ["Mic: read failed", "Mic init failed: I2S.begin(PDM_MONO_MODE)",
                     "Guru Meditation Error", "Brownout detector was triggered", "Mic: broken"]:
            with self.subTest(line=line):
                observation = self.healthy()
                observation.accept(line,4.9)
                self.assertEqual(observation.summarize(5,10,2)["verdict"], "FAILED")

    def test_constant_statistics_and_restarts_need_review(self):
        observation = hil.Observation(160)
        for i in range(10):
            observation.accept("Mic: mean=-30935 rms=0.0 peak=0 samples=160",i*0.5)
        self.assertIn("constant_statistics_need_stimulus_check",observation.summarize(5,10,2)["warnings"])
        observation = self.healthy()
        observation.accept(hil.BOOT,4.8)
        self.assertIn("possible_restart_during_capture",observation.summarize(5,10,2)["warnings"])

    def test_build_failure_never_uploads_or_captures(self):
        args=SimpleNamespace(action="cycle",pio=None,env=hil.ENV,build_timeout=1,port="COM11",upload_timeout=1)
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory)
            with patch.object(hil,"find_pio",return_value=["pio"]), \
                 patch.object(hil,"command_text",return_value="test"), \
                 patch.object(hil,"run_stage",return_value={"exit_code":1}) as stage, \
                 patch.object(hil,"capture") as capture:
                self.assertEqual(hil.execute(args,root,root),1)
                self.assertEqual(stage.call_count,1)
                capture.assert_not_called()
            self.assertEqual(json.loads((root/"report.json").read_text())["verdict"],"FAILED")

    def test_upload_failure_never_captures(self):
        args=SimpleNamespace(action="cycle",pio=None,env=hil.ENV,build_timeout=1,port="COM11",upload_timeout=1)
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory)
            with patch.object(hil,"find_pio",return_value=["pio"]), \
                 patch.object(hil,"command_text",return_value="test"), \
                 patch.object(hil,"run_stage",side_effect=[{"exit_code":0},{"exit_code":1}]) as stage, \
                 patch.object(hil,"capture") as capture:
                self.assertEqual(hil.execute(args,root,root),1)
                self.assertEqual(stage.call_count,2)
                self.assertIn("--upload-port",stage.call_args.args[1])
                capture.assert_not_called()


if __name__ == "__main__":
    unittest.main()

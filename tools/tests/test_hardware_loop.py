import importlib.util
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

"""POSIX pseudo-terminal checks: actual pySerial I/O, never a real board."""
import csv
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import threading
import time
import unittest

SCRIPT = Path(__file__).resolve().parents[1]/"hardware_loop.py"


@unittest.skipUnless(os.name == "posix" and importlib.util.find_spec("serial"),
                     "requires POSIX pseudo-terminals and pySerial")
class SerialCaptureTests(unittest.TestCase):
    def run_capture(self, inject_fault):
        import pty
        master, slave = pty.openpty()
        stopped = threading.Event()

        def write_stream():
            try:
                for i in range(50):
                    if stopped.wait(0.06):
                        break
                    line = f"Mic: mean=1200 rms={3+i%5}.0 peak={20+i%5} samples=160\r\n".encode()
                    os.write(master,line[:17])
                    time.sleep(0.003)
                    os.write(master,line[17:])
                    if inject_fault and i == 10:
                        os.write(master,b"Guru Meditation Error: simulated fault\r\n")
            except OSError:
                pass

        worker = threading.Thread(target=write_stream)
        try:
            with tempfile.TemporaryDirectory(prefix="xiao simulated ") as directory:
                repo = Path(directory)
                (repo/"platformio.ini").write_text("[env:seeed_xiao_esp32s3]\n")
                worker.start()
                result = subprocess.run([sys.executable,str(SCRIPT),"capture","--repo",str(repo),
                    "--port",os.ttyname(slave),"--seconds","1.8","--label","SIMULATED_PTY",
                    "--max-gap","0.5"],capture_output=True,text=True,timeout=8)
                stopped.set()
                worker.join(timeout=2)
                reports = list((repo/"test-runs").glob("*/report.json"))
                self.assertEqual(len(reports),1,result.stdout+result.stderr)
                report = json.loads(reports[0].read_text())
                with reports[0].with_name("mic.csv").open() as data:
                    rows = list(csv.DictReader(data))
                self.assertGreaterEqual(len(rows),10)
                self.assertEqual(len(rows),report["capture"]["valid_records"])
                self.assertEqual(report["device_firmware_identity"],"not_reported_by_legacy_or_csv_v1")
                self.assertTrue(reports[0].with_name("serial.log").read_bytes())
                self.assertNotIn("upload",report["stages"])
                if inject_fault:
                    self.assertEqual(result.returncode,1)
                    self.assertEqual(report["verdict"],"FAILED")
                else:
                    self.assertEqual(result.returncode,0,result.stdout+result.stderr)
                    self.assertEqual(report["verdict"],"PASS_SERIAL_SMOKE")
        finally:
            stopped.set()
            if worker.is_alive():
                worker.join(timeout=2)
            os.close(master)
            os.close(slave)

    def test_fragmented_stream_through_pyserial(self):
        self.run_capture(False)

    def test_panic_is_retained_and_fails_capture(self):
        self.run_capture(True)


if __name__ == "__main__":
    unittest.main()

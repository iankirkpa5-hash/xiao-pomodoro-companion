"""Offline camera protocol and JPEG integrity tests; no serial device is opened."""
import io
from pathlib import Path
import sys
import tempfile
import unittest
import zlib

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import capture_camera as cam


class FakeLink:
    def __init__(self, respond, fragment=19):
        self.respond, self.fragment = respond, fragment
        self.pending = bytearray()
        self.writes = []

    @property
    def in_waiting(self):
        return len(self.pending)

    def write(self, data):
        self.writes.append(data.decode().strip())
        self.pending.extend(self.respond(self.writes[-1], len(self.writes)))
        return len(data)

    def read(self, count):
        count = min(count, self.fragment, len(self.pending))
        data = bytes(self.pending[:count])
        del self.pending[:count]
        return data


def sample_jpeg(payload_len=140, trailing_zeros=0):
    payload = bytes((i * 29 + 7) & 0xFF for i in range(payload_len))
    return b"\xff\xd8" + payload + b"\xff\xd9" + (b"\x00" * trailing_zeros)


class CameraTests(unittest.TestCase):
    def status(self, jpeg):
        return dict(
            state="READY",
            length=len(jpeg),
            width=320,
            height=240,
            format=4,
            capture_ms=260,
            crc32=zlib.crc32(jpeg),
        )

    def test_negotiation_and_state(self):
        info = cam.parse_info("CAM INFO 1 320 240 JPEG 131072 64 8000000")
        self.assertEqual((info["width"], info["height"], info["chunk_bytes"]), (320, 240, 64))
        with self.assertRaises(ValueError):
            cam.parse_info("CAM INFO 2 320 240 JPEG 131072 64 8000000")
        with self.assertRaises(ValueError):
            cam.parse_info("CAM INFO 1 320 240 JPEG 131072 128 8000000")
        self.assertIsNone(cam.parse_info("CAM INFO 1 320 240 RGB565 131072 64 8000000"))

        valid = "CAM STATE 77 READY 2464 320 240 4 260 cbf43926"
        parsed = cam.parse_state(valid, 77, 131072)
        self.assertEqual((parsed["length"], parsed["crc32"]), (2464, 0xCBF43926))
        self.assertIsNone(cam.parse_state(valid, 78, 131072))
        for invalid in (
            valid.replace("2464", "200000"),
            valid.replace(" 4 260", " 0 260"),
            valid.replace("260 ", "4294967296 "),
            valid.replace("320 240", "0 240"),
        ):
            with self.assertRaises(ValueError):
                cam.parse_state(invalid, 77, 131072)

    def test_packet_crc_and_identity(self):
        data = b"\xff\xd8\xff\xe0\x00\x10JFIF"
        line = f"CAM DATA 77 64 {data.hex()} {zlib.crc32(data):08x}"
        self.assertEqual(cam.parse_data(line, 77, 64, len(data)), data)
        self.assertIsNone(cam.parse_data(line, 76, 64, len(data)))
        self.assertIsNone(cam.parse_data(line, 77, 0, len(data)))
        self.assertIsNone(cam.parse_data("noise " + line, 77, 64, len(data)))
        with self.assertRaises(ValueError):
            cam.parse_data(line[:-8] + "00000000", 77, 64, len(data))
        with self.assertRaises(ValueError):
            cam.parse_data(line, 77, 64, 64)

    def test_jpeg_verification_and_write_protection(self):
        jpeg = sample_jpeg(180, trailing_zeros=2)
        state = self.status(jpeg)
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "frame.jpg"
            details = cam.write_jpeg(path, jpeg, state)
            self.assertTrue(details["soi_valid"])
            self.assertEqual(details["trailing_zero_bytes"], 2)
            self.assertEqual(path.read_bytes(), jpeg)
            with self.assertRaises(FileExistsError):
                cam.write_jpeg(path, jpeg, state)

        for bad in (
            b"\x00\x00" + jpeg[2:],  # missing SOI
            jpeg[:-4],               # missing EOI
            jpeg + b"\x01",          # non-zero trailing garbage after EOI
        ):
            with self.assertRaises(ValueError):
                cam.verify_jpeg(bad, self.status(bad))
        with self.assertRaises(ValueError):
            cam.verify_jpeg(jpeg[:-1], state)  # length/CRC mismatch

    def test_retry_corrupt_chunk_and_skip_telemetry(self):
        data = b"\xff\xd8\x12\x34"
        good = f"CAM DATA 77 0 {data.hex()} {zlib.crc32(data):08x}\n".encode()

        def response(command, count):
            self.assertEqual(command, "CAM READ 77 0")
            if count == 1:
                return b"CAM DATA 77 0 ffd81234 00000000\n"
            return b"1500,12.5,60,Quiet\nPomodoro: Focus 1499s running=true\n" + good

        client = cam.CameraClient(FakeLink(response), io.BytesIO(), [], 77)
        answer = client.request(
            "CAM READ 77 0",
            lambda line: cam.parse_data(line, 77, 0, len(data)),
            timeout=0.01,
        )
        self.assertEqual(answer, data)
        self.assertEqual(client.retries, 1)
        self.assertEqual(len(client.rejected_packets), 1)
        self.assertEqual(len(client.observation.rows), 1)

    def test_errors_timeout_and_partial_write(self):
        client = cam.CameraClient(FakeLink(lambda *_: b""), io.BytesIO(), [], 77)
        with self.assertRaises(RuntimeError):
            client.request("CAM INFO", cam.parse_info, timeout=0.001, attempts=2)

        for err in ("INIT_FAILED", "NO_PSRAM", "CAPTURE_FAILED", "BUSY"):
            client = cam.CameraClient(
                FakeLink(lambda *_, e=err: f"CAM ERROR 77 {e}\n".encode()),
                io.BytesIO(),
                [],
                77,
            )
            with self.assertRaisesRegex(RuntimeError, err):
                client.request("CAM CAPTURE 77", lambda _: None)

        client.link.write = lambda data: len(data) - 1
        with self.assertRaisesRegex(RuntimeError, "partial"):
            client.request("CAM INFO", cam.parse_info)

    def test_complete_collect_preserves_exact_jpeg_bytes(self):
        jpeg = sample_jpeg(210, trailing_zeros=1)

        def response(command, _):
            if command == "CAM INFO":
                return b"CAM INFO 1 320 240 JPEG 131072 64 8000000\n"
            if command == "CAM CAPTURE 77":
                return f"CAM STATE 77 READY {len(jpeg)} 320 240 4 275 {zlib.crc32(jpeg):08x}\n".encode()
            offset = int(command.split()[-1])
            chunk = jpeg[offset:offset + 64]
            return f"CAM DATA 77 {offset} {chunk.hex()} {zlib.crc32(chunk):08x}\n".encode()

        client = cam.CameraClient(FakeLink(response, 4096), io.BytesIO(), [], 77)
        report = {}
        received, status = cam.collect(client, report)
        self.assertEqual(received, jpeg)
        self.assertEqual(status["width"], 320)
        self.assertEqual(status["height"], 240)
        self.assertEqual(report["jpeg_verification"]["trailing_zero_bytes"], 1)
        self.assertEqual(len(report["jpeg_sha256"]), 64)

    def test_serial_error_diagnostic_fails_collect(self):
        jpeg = sample_jpeg(60)

        def response(command, _):
            if command == "CAM INFO":
                return b"CAM INFO 1 320 240 JPEG 131072 64 8000000\n"
            if command == "CAM CAPTURE 77":
                return (
                    b"i2cRead() Error\n"
                    + f"CAM STATE 77 READY {len(jpeg)} 320 240 4 255 {zlib.crc32(jpeg):08x}\n".encode()
                )
            offset = int(command.split()[-1])
            chunk = jpeg[offset:offset + 64]
            return f"CAM DATA 77 {offset} {chunk.hex()} {zlib.crc32(chunk):08x}\n".encode()

        client = cam.CameraClient(FakeLink(response, 4096), io.BytesIO(), [], 77)
        with self.assertRaisesRegex(RuntimeError, "serial diagnostics contain errors"):
            cam.collect(client, {})


if __name__ == "__main__":
    unittest.main()

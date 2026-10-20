"""Offline protocol/integrity tests; no serial device is opened."""
import io
from pathlib import Path
import struct
import sys
import tempfile
import unittest
import wave
import zlib

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import record_pcm as pcm
import prepare_pcm_preview as preview


class FakeLink:
    def __init__(self, respond, fragment=17):
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


class PcmTests(unittest.TestCase):
    def status(self, data):
        return dict(state="READY", read_errors=0, target=len(data)//2,
                    sample_rate=16000, crc32=zlib.crc32(data))

    def test_negotiation_and_status(self):
        self.assertEqual(pcm.parse_info("PCM INFO 1 16000 1 16 5000 64 160000")["channels"], 1)
        with self.assertRaises(ValueError):
            pcm.parse_info("PCM INFO 1 44100 1 16 5000 64 160000")
        valid = "PCM STATE 42 READY 80000 80000 16000 10 5000 10 0 cbf43926"
        self.assertEqual(pcm.parse_state(valid, 42, 80000)["crc32"], 0xcbf43926)
        self.assertIsNone(pcm.parse_state(valid, 43, 80000))
        for invalid in (valid.replace("READY 80000", "READY 79999"),
                        valid.replace("10 0 cb", "10 1 cb"), valid.replace("5000 10", "4294967296 10")):
            with self.assertRaises(ValueError):
                pcm.parse_state(invalid, 42, 80000)

    def test_packet_crc_and_identity(self):
        data = struct.pack("<hhhh", -32768, 32767, 1234, -1234)
        line = f"PCM DATA 42 64 {data.hex()} {zlib.crc32(data):08x}"
        self.assertEqual(pcm.parse_data(line, 42, 64, 8), data)
        self.assertIsNone(pcm.parse_data(line, 41, 64, 8))
        self.assertIsNone(pcm.parse_data(line, 42, 0, 8))
        self.assertIsNone(pcm.parse_data("error " + line, 42, 64, 8))
        with self.assertRaises(ValueError):
            pcm.parse_data(line[:-8] + "00000000", 42, 64, 8)
        with self.assertRaises(ValueError):
            pcm.parse_data(line, 42, 64, 64)

    def test_whole_file_and_raw_wav_round_trip(self):
        data = struct.pack("<hhhh", -32768, 32767, 1250, 1250) * 20000
        state = self.status(data)
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp)/"audio.wav"
            pcm.write_wav(path, data, state)
            with wave.open(str(path), "rb") as wav:
                self.assertEqual((wav.getnchannels(), wav.getsampwidth(), wav.getframerate(), wav.getnframes()),
                                 (1, 2, 16000, 80000))
                self.assertEqual(wav.readframes(80000), data)
            with self.assertRaises(FileExistsError):
                pcm.write_wav(path, data, state)
            for bad in (data[:-2], b"XX" + data[2:]):
                with self.assertRaises(ValueError):
                    pcm.write_wav(Path(tmp)/"invalid.wav", bad, state)
                self.assertFalse((Path(tmp)/"invalid.wav").exists())

    def test_retry_corrupt_packet_and_skip_noise(self):
        data = b"\x00\x80\xff\x7f"
        good = f"PCM DATA 42 0 {data.hex()} {zlib.crc32(data):08x}\n".encode()
        def response(command, count):
            self.assertEqual(command, "PCM READ 42 0")
            if count == 1:
                return b"PCM DATA 42 0 0080ff7f 00000000\n"
            return b"1000,10.0,50,Quiet\nPomodoro: Focus 42s running=true\n" + good
        client = pcm.PcmClient(FakeLink(response), io.BytesIO(), [], 42)
        answer = client.request("PCM READ 42 0", lambda line: pcm.parse_data(line, 42, 0, 4), timeout=.01)
        self.assertEqual(answer, data)
        self.assertEqual(client.retries, 1)
        self.assertEqual(len(client.rejected_packets), 1)
        self.assertEqual(len(client.observation.rows), 1)

    def test_timeout_device_error_and_partial_command_write(self):
        client = pcm.PcmClient(FakeLink(lambda *_: b""), io.BytesIO(), [], 42)
        with self.assertRaises(RuntimeError):
            client.request("PCM INFO", pcm.parse_info, timeout=.001, attempts=2)
        client = pcm.PcmClient(FakeLink(lambda *_: b"PCM ERROR 42 NO_PSRAM\n"), io.BytesIO(), [], 42)
        with self.assertRaisesRegex(RuntimeError, "NO_PSRAM"):
            client.request("PCM START 42 5000", lambda _: None)
        client.link.write = lambda data: len(data)-1
        with self.assertRaisesRegex(RuntimeError, "partial"):
            client.request("PCM INFO", pcm.parse_info)

    def test_complete_collect_preserves_bytes_and_records_timing(self):
        data = struct.pack("<h", 1250) * 16000
        def response(command, _):
            if command == "PCM INFO":
                return b"PCM INFO 1 16000 1 16 5000 64 8000000\n"
            if command == "PCM START 42 1000":
                return f"PCM STATE 42 READY 16000 16000 16000 10 1000 10 0 {zlib.crc32(data):08x}\n".encode()
            offset = int(command.split()[-1])
            chunk = data[offset:offset+64]
            return f"PCM DATA 42 {offset} {chunk.hex()} {zlib.crc32(chunk):08x}\n".encode()
        client = pcm.PcmClient(FakeLink(response, 4096), io.BytesIO(), [], 42)
        report = {}
        received, status = pcm.collect(client, 1, report)
        self.assertEqual(received, data)
        self.assertEqual(report["timing_warnings"], [])
        self.assertEqual(report["pcm_statistics"]["mean"], 1250)
        self.assertEqual(report["pcm_statistics"]["centered_rms"], 0)
        self.assertEqual(status["samples"], 16000)

    def test_preview_constant_dc_is_silent_and_source_is_unchanged(self):
        data = struct.pack("<h", 1250) * 16000
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp)/"audio.wav"
            pcm.write_wav(path, data, self.status(data))
            original = path.read_bytes()
            output = preview.prepare(path)
            self.assertEqual(path.read_bytes(), original)
            for name in ("centered.wav", "highpass-80hz.wav"):
                with wave.open(str(output/name), "rb") as wav:
                    self.assertEqual(wav.getnframes(), 16000)
                    self.assertEqual(wav.readframes(16000), bytes(32000))
            with self.assertRaises(FileExistsError):
                preview.prepare(path)

    def test_preview_headroom_fade_and_highpass_length(self):
        data = struct.pack("<hh", -32768, 32767) * 8000
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp)/"audio.wav"
            pcm.write_wav(path, data, self.status(data))
            output = preview.prepare(path)
            for name in ("centered.wav", "highpass-80hz.wav"):
                with wave.open(str(output/name), "rb") as wav:
                    values = struct.unpack("<16000h", wav.readframes(16000))
                self.assertEqual((values[0], values[-1]), (0, 0))
                self.assertLessEqual(max(map(abs, values)), 16383)
            self.assertEqual(preview.highpass([100.0]*10, 16000, 80), [0.0]*10)

    def test_preview_rejects_empty_and_wrong_format(self):
        with tempfile.TemporaryDirectory() as tmp:
            for rate, data in ((16000, b""), (8000, b"\x00\x00")):
                path = Path(tmp)/f"{rate}.wav"
                with wave.open(str(path), "wb") as wav:
                    wav.setnchannels(1)
                    wav.setsampwidth(2)
                    wav.setframerate(rate)
                    wav.writeframes(data)
                with self.assertRaises(ValueError):
                    preview.prepare(path)


if __name__ == "__main__":
    unittest.main()

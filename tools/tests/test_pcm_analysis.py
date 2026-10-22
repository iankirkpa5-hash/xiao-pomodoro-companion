"""Known-signal checks, including odd/even one-sided FFT endpoint scaling."""
import hashlib
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import unittest
import wave

try:
    import numpy as np
except ImportError:
    raise unittest.SkipTest("install tools/requirements-audio.txt for offline FFT tests")

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import analyze_pcm as analysis


def wav_file(path, samples, rate=16000, channels=1, width=2):
    with wave.open(str(path), "wb") as stream:
        stream.setparams((channels, width, rate, 0, "NONE", "not compressed"))
        stream.writeframes(np.asarray(samples, dtype="<i2").tobytes())


class SpectrumTests(unittest.TestCase):
    def test_two_tones_amplitudes_and_dc_invariance(self):
        t = np.arange(16000) / 16000
        x = 1200 + 1000*np.sin(2*np.pi*440*t) + 250*np.sin(2*np.pi*1000*t)
        frequency, amplitude, _ = analysis.spectrum(x, 16000)
        self.assertAlmostEqual(frequency[np.argmax(amplitude)], 440)
        self.assertAlmostEqual(amplitude[440], 1000, places=8)
        self.assertAlmostEqual(amplitude[1000], 250, places=8)
        np.testing.assert_allclose(analysis.spectrum(x + 9000, 16000)[1], amplitude, atol=1e-9)

    def test_full_scale_sine_and_rms_references(self):
        x = 32768*np.sin(2*np.pi*100*np.arange(16000)/16000)
        self.assertAlmostEqual(analysis.amplitude_dbfs(analysis.spectrum(x, 16000)[1][100]), 0, places=9)
        self.assertAlmostEqual(analysis.scalar_dbfs(np.sqrt(np.mean(x*x))), -3.0102999566, places=8)

    def test_nyquist_even_and_last_bin_odd(self):
        _, amp, _ = analysis.spectrum(200*(-1.0)**np.arange(160), 16000, "rectangular")
        self.assertAlmostEqual(amp[-1], 200)
        n = 161
        x = 200*np.cos(2*np.pi*80*np.arange(n)/n)
        f, amp, _ = analysis.spectrum(x, 16000, "rectangular")
        self.assertAlmostEqual(amp[-1], 200)
        self.assertLess(f[-1], 8000)

    def test_psd_integral_matches_windowed_parseval(self):
        rng = np.random.default_rng(30)
        for n in (640, 641):
            x = rng.normal(size=n) + 1200
            for name in ("hann", "rectangular"):
                _, _, psd = analysis.spectrum(x, 16000, name)
                # np.hanning(N+1)[:-1] independently constructs the periodic window.
                w = np.hanning(n + 1)[:-1] if name == "hann" else np.ones(n)
                expected = np.sum(((x-x.mean())*w)**2) / np.sum(w*w)
                self.assertAlmostEqual(np.sum(psd)*16000/n, expected, places=11)

    def test_zero_input_and_bad_arguments(self):
        _, amp, psd = analysis.spectrum(np.full(8000, 1234), 16000)
        self.assertFalse(amp.any())
        self.assertFalse(psd.any())
        self.assertTrue(np.isfinite(analysis.amplitude_dbfs(amp)).all())
        report = analysis.summarize(np.full(8000, 1234), 16000, "test")
        self.assertIsNone(report["centered_rms_dbfs"])
        self.assertEqual(report["separated_spectral_peaks"], [])
        json.dumps(report, allow_nan=False)
        for x, rate in (([1, 2], 16000), ([1, 2, 3, float("nan")], 16000), (np.ones(4), 0)):
            with self.assertRaises(ValueError):
                analysis.spectrum(x, rate)

    def test_window_spacing_and_shared_center(self):
        x = np.zeros(80000)
        x[32000:32160] = 100
        windows = analysis.excerpts(x, 16000)
        self.assertEqual([len(w) for _, w in windows], [640, 4000])
        self.assertEqual(windows[0][0]+320, windows[1][0]+2000)
        self.assertEqual([16000/len(w) for _, w in windows], [25, 4])
        for start, w in windows:
            self.assertTrue(start >= 0 and start + len(w) <= len(x))
        with self.assertRaises(ValueError):
            analysis.excerpts(np.ones(159), 16000)


class WavTests(unittest.TestCase):
    def test_wav_validation_and_hash(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)/"audio.wav"
            x = [-32768, -1, 0, 32767]*2000
            wav_file(path, x)
            samples, rate, digest = analysis.read_wav(path)
            np.testing.assert_array_equal(samples, x)
            self.assertEqual(rate, 16000)
            self.assertEqual(digest, hashlib.sha256(path.read_bytes()).hexdigest())
            path.write_bytes(path.read_bytes()[:-2])
            with self.assertRaises(ValueError):
                analysis.read_wav(path)
            for channels, width in ((2, 2), (1, 1)):
                wav_file(path, x, channels=channels, width=width)
                with self.assertRaises(ValueError):
                    analysis.read_wav(path)

    @unittest.skipUnless(importlib.util.find_spec("matplotlib"), "install Matplotlib for artifact tests")
    def test_analysis_preserves_source_and_refuses_overwrite(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)/"audio.wav"
            wav_file(path, (1200 + 400*np.sin(2*np.pi*440*np.arange(8000)/16000)).astype(int))
            before = path.read_bytes()
            digest = hashlib.sha256(before).hexdigest()
            with self.assertRaises(ValueError):
                analysis.analyze(path, expected_sha256="bad")
            self.assertFalse((path.parent/"waveform-fft").exists())
            output, report = analysis.analyze(path, expected_sha256=digest)
            for name in ("waveform-fft.png", "waveform-fft.svg", "window-comparison.png", "window-comparison.svg", "analysis.json"):
                self.assertGreater((output/name).stat().st_size, 100)
            self.assertTrue(report["source_unchanged"])
            self.assertEqual(path.read_bytes(), before)
            with self.assertRaises(FileExistsError):
                analysis.analyze(path)


if __name__ == "__main__":
    unittest.main()

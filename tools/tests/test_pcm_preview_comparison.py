"""Offline matched-gain evidence and power-ratio tests; no serial connection."""
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
    raise unittest.SkipTest("install tools/requirements-audio.txt for preview comparison tests")

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import compare_pcm_previews as comparison
import prepare_pcm_preview as preview


def save_wav(path, samples, rate=16000):
    with wave.open(str(path), "wb") as stream:
        stream.setparams((1, 2, rate, 0, "NONE", "not compressed"))
        stream.writeframes(np.asarray(samples, dtype="<i2").tobytes())


class PairTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.source = Path(self.temp.name) / "audio.wav"
        t = np.arange(16000)/16000
        save_wav(self.source, 1200 + 500*np.sin(2*np.pi*20*t) + 300*np.sin(2*np.pi*1000*t))
        self.folder = preview.prepare(self.source, gain=8)
        self.manifest = self.folder / "comparison.json"
        self.metadata = json.loads(self.manifest.read_text())

    def update_manifest(self):
        self.manifest.write_text(json.dumps(self.metadata))

    def update_output_hash(self, name):
        for item in self.metadata["outputs"]:
            if item["file"] == name:
                item["sha256"] = comparison.digest(self.folder / name)
        self.update_manifest()

    def test_valid_pair_provenance(self):
        signals, rate, gain, hashes = comparison.load_pair(self.source, self.folder)
        self.assertEqual((rate, gain), (16000, 8))
        self.assertEqual([len(s) for s in signals], [16000, 16000])
        self.assertEqual(len(hashes), 4)
        self.assertTrue(all(comparison.digest(p) == h for p, h in hashes.items()))

    def test_source_and_preview_hash_mismatch(self):
        self.metadata["source_sha256"] = "wrong"
        self.update_manifest()
        with self.assertRaisesRegex(ValueError, "source SHA"):
            comparison.load_pair(self.source, self.folder)
        self.metadata["source_sha256"] = comparison.digest(self.source)
        self.metadata["outputs"][0]["sha256"] = "wrong"
        self.update_manifest()
        with self.assertRaisesRegex(ValueError, "preview SHA"):
            comparison.load_pair(self.source, self.folder)

    def test_manifest_cannot_hide_mixed_gain(self):
        other = preview.prepare(self.source, gain=16)
        name = "highpass-80hz.wav"
        (self.folder/name).write_bytes((other/name).read_bytes())
        self.update_output_hash(name)
        with self.assertRaisesRegex(ValueError, "shared-gain recipe"):
            comparison.load_pair(self.source, self.folder)

    def test_different_length_and_rate_rejected_even_with_valid_hash(self):
        name = "centered.wav"
        for samples, rate in ((np.zeros(15999), 16000), (np.zeros(16000), 8000)):
            save_wav(self.folder/name, samples, rate)
            self.update_output_hash(name)
            with self.assertRaisesRegex(ValueError, "format/count"):
                comparison.load_pair(self.source, self.folder)

    def test_invalid_gain_and_recipe(self):
        for gain in (0, float("nan"), float("inf"), 33, True):
            self.metadata["shared_gain"] = gain
            self.update_manifest()
            with self.assertRaisesRegex(ValueError, "invalid shared gain"):
                comparison.load_pair(self.source, self.folder)
        self.metadata["shared_gain"] = 8
        self.metadata["fade_ms"] = 0
        self.update_manifest()
        with self.assertRaisesRegex(ValueError, "recipe"):
            comparison.load_pair(self.source, self.folder)

    @unittest.skipUnless(importlib.util.find_spec("matplotlib"), "install Matplotlib for plot tests")
    def test_complete_report_plot_and_preservation(self):
        paths = (self.source, self.manifest, *(self.folder/n for n in comparison.NAMES))
        before = [p.read_bytes() for p in paths]
        output = Path(self.temp.name)/"comparison"
        report = comparison.compare(self.source, self.folder, output)
        self.assertTrue(report["inputs_unchanged"])
        self.assertTrue(report["all_preview_samples_reconstructed"])
        self.assertEqual(report["listening_acceptance"], "pending_user_comparison")
        self.assertLess(report["changes_b_vs_a"]["bands"][0]["b_over_a"], .1)
        self.assertEqual([p.read_bytes() for p in paths], before)
        for name in ("matched-gain-comparison.png", "matched-gain-comparison.svg", "comparison-analysis.json"):
            self.assertGreater((output/name).stat().st_size, 100)
        with self.assertRaises(FileExistsError):
            comparison.compare(self.source, self.folder, output)


class PowerTests(unittest.TestCase):
    def test_gain_changes_absolute_power_not_fraction(self):
        t = np.arange(16000)/16000
        x = 100*np.sin(2*np.pi*20*t) + 50*np.sin(2*np.pi*1000*t)
        a, b = comparison.measure(x, 16000), comparison.measure(x*2, 16000)
        changes = comparison.compare_metrics(a, b)
        self.assertAlmostEqual(changes["centered_rms"]["b_over_a"], 2)
        self.assertAlmostEqual(changes["total_weighted_power"]["b_over_a"], 4)
        self.assertAlmostEqual(changes["total_weighted_power"]["change_db"], 6.020599913, places=8)
        for before, after in zip(a["bands"], b["bands"]):
            self.assertAlmostEqual(before["weighted_power_fraction"], after["weighted_power_fraction"])
        self.assertAlmostEqual(sum(band["weighted_power_counts_squared"] for band in a["bands"]),
                               a["total_weighted_power_counts_squared"])
        scaled = comparison.measure(x*8, 16000, 8)
        unity = comparison.measure(x, 16000, 1)
        self.assertAlmostEqual(scaled["total_source_referred_power_counts_squared"],
                               unity["total_weighted_power_counts_squared"])
        for band_scaled, band_unity in zip(scaled["bands"], unity["bands"]):
            self.assertAlmostEqual(band_scaled["source_referred_power_counts_squared"],
                                   band_unity["weighted_power_counts_squared"])
        with self.assertRaises(ValueError):
            comparison.measure(x, 16000, 0)

    def test_filter_steady_state_against_transfer_function(self):
        rate = 16000
        alpha = np.exp(-2*np.pi*80/rate)
        for hz in (20, 80, 1000):
            t = np.arange(rate*2)/rate
            x = np.sin(2*np.pi*hz*t)
            y = np.asarray(preview.highpass(x, rate, 80))
            ratio = np.sqrt(np.mean(y[rate:]**2) / np.mean(x[rate:]**2))
            z_inverse = np.exp(-2j*np.pi*hz/rate)
            expected = abs(alpha*(1-z_inverse)/(1-alpha*z_inverse))
            self.assertAlmostEqual(ratio, expected, places=10)

    def test_silence_is_explicit_not_nan(self):
        metrics = comparison.measure(np.zeros(8000), 16000)
        self.assertEqual(metrics["centered_rms_counts"], 0)
        self.assertIsNone(metrics["bands"][0]["weighted_power_fraction"])
        changes = comparison.compare_metrics(metrics, metrics)
        self.assertIsNone(changes["centered_rms"]["b_over_a"])
        json.dumps(changes, allow_nan=False)


if __name__ == "__main__":
    unittest.main()

#!/usr/bin/env python3
"""Read-only matched-gain comparison of existing PCM listening previews."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import math
from pathlib import Path

import numpy as np

import analyze_pcm as fft
import prepare_pcm_preview as preview

NAMES = ("centered.wav", "highpass-80hz.wav")
BANDS = ((0, 80), (80, 300), (300, 3000), (3000, 8000))


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def load_pair(source, directory):
    """Verify actual bytes, not just manifest assertions about a shared gain."""
    source, directory = Path(source).resolve(), Path(directory).resolve()
    manifest_path = directory / "comparison.json"
    manifest_bytes = manifest_path.read_bytes()
    manifest = json.loads(manifest_bytes)
    raw, rate, raw_hash = fft.read_wav(source)
    if raw_hash != manifest["source_sha256"]:
        raise ValueError("source SHA-256 mismatch")
    if rate != 16000 or manifest["sample_rate"] != rate or manifest["samples"] != len(raw):
        raise ValueError("source format/count mismatch; expected 16 kHz")
    gain = manifest["shared_gain"]
    if isinstance(gain, bool) or not isinstance(gain, (float, int)) or not math.isfinite(gain) or not 0 < gain <= 32:
        raise ValueError("invalid shared gain")
    if manifest["fade_ms"] != 10 or manifest["highpass_hz"] != 80:
        raise ValueError("expected 10 ms fade and 80 Hz high-pass recipe")
    entries = manifest["outputs"]
    if len(entries) != 2 or {item["file"] for item in entries} != set(NAMES):
        raise ValueError("expected exactly the centered and high-pass pair")
    hashes = {item["file"]: item["sha256"] for item in entries}
    centered = (raw - raw.mean()).tolist()
    expected = (centered, preview.highpass(centered, rate, 80))
    signals = []
    inputs = {str(source): raw_hash, str(manifest_path): hashlib.sha256(manifest_bytes).hexdigest()}
    for name, signal in zip(NAMES, expected):
        path = directory / name
        samples, file_rate, file_hash = fft.read_wav(path)
        if file_hash != hashes[name]:
            raise ValueError(f"preview SHA-256 mismatch: {name}")
        if file_rate != rate or len(samples) != len(raw):
            raise ValueError(f"preview format/count mismatch: {name}")
        rebuilt = np.frombuffer(preview.render(signal, rate, gain), dtype="<i2")
        if not np.array_equal(samples, rebuilt):
            raise ValueError(f"preview does not match shared-gain recipe: {name}")
        inputs[str(path)] = file_hash
        signals.append(samples)
    return signals, rate, gain, inputs


def power_dbfs(power):
    return 10*math.log10(power/(fft.FULL_SCALE**2)) if power > 0 else None


def measure(samples, rate, gain=1.0):
    if not math.isfinite(gain) or gain <= 0:
        raise ValueError("gain must be positive and finite")
    scale = gain*gain
    centered = samples - samples.mean()
    rms = float(np.sqrt(np.mean(centered**2)))
    peak = float(np.max(np.abs(centered)))
    frequency, _, psd = fft.spectrum(samples, rate)
    total = float(np.sum(psd) * rate / len(samples))
    bands = []
    for lo, hi in BANDS:
        mask = (frequency >= lo) & (frequency <= hi if hi == 8000 else frequency < hi)
        power = float(np.sum(psd[mask]) * rate / len(samples))
        source_power = power/scale
        bands.append(dict(low_hz=lo, high_hz=hi,
                          weighted_power_counts_squared=power,
                          weighted_power_dbfs=power_dbfs(power),
                          source_referred_power_counts_squared=source_power,
                          source_referred_power_dbfs=power_dbfs(source_power),
                          weighted_power_fraction=power/total if total else None))
    return dict(mean_counts=float(samples.mean()), centered_rms_counts=rms, centered_peak_counts=peak,
                centered_rms_dbfs=fft.scalar_dbfs(rms), centered_peak_dbfs=fft.scalar_dbfs(peak),
                file_peak_counts=float(np.max(np.abs(samples))),
                rail_samples=int(np.count_nonzero((samples == -32768) | (samples == 32767))),
                total_weighted_power_counts_squared=total,
                total_weighted_power_dbfs=power_dbfs(total),
                total_source_referred_power_counts_squared=total/scale,
                total_source_referred_power_dbfs=power_dbfs(total/scale),
                bands=bands)


def change(before, after, power=False):
    # Null explicitly represents undefined ratios, rather than writing NaN/Infinity.
    ratio = after/before if before > 0 else None
    db = (10 if power else 20)*math.log10(ratio) if ratio is not None and ratio > 0 else None
    return dict(b_over_a=ratio, change_db=db)


def compare_metrics(a, b):
    return dict(
        centered_rms=change(a["centered_rms_counts"], b["centered_rms_counts"]),
        centered_peak=change(a["centered_peak_counts"], b["centered_peak_counts"]),
        total_weighted_power=change(a["total_weighted_power_counts_squared"], b["total_weighted_power_counts_squared"], True),
        bands=[dict(low_hz=x["low_hz"], high_hz=x["high_hz"],
                    **change(x["weighted_power_counts_squared"], y["weighted_power_counts_squared"], True))
               for x, y in zip(a["bands"], b["bands"])],
    )


def plot_pair(signals, rate, gain, measurements, output):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    colors = ("#176d77", "#b55a25")
    labels = ("A / centered", "B / 80 Hz high-pass")
    plt.rcParams.update({"font.family": "DejaVu Sans", "font.size": 10,
                         "axes.spines.top": False, "axes.spines.right": False,
                         "axes.grid": True, "grid.alpha": .16,
                         "figure.facecolor": "#faf9f5", "axes.facecolor": "#faf9f5"})
    fig = plt.figure(figsize=(12, 10), layout="constrained")
    grid = fig.add_gridspec(3, 2, height_ratios=(1, 1.25, 1))
    waves = (fig.add_subplot(grid[0, 0]), fig.add_subplot(grid[0, 1]))
    spectra = fig.add_subplot(grid[1, :])
    power_ax, share_ax = fig.add_subplot(grid[2, 0]), fig.add_subplot(grid[2, 1])
    fig.suptitle(f"XIAO / matched-gain A-B comparison\nSame {gain:g}x gain, timing and endpoint fades; no per-file normalization", fontsize=16)
    bound = max(float(np.max(np.abs(s))) for s in signals) * 1.08 or 1
    time = np.arange(len(signals[0])) / rate
    for i, (samples, metrics) in enumerate(zip(signals, measurements)):
        waves[i].plot(time, samples, color=colors[i], linewidth=.55)
        waves[i].set(xlim=(0, len(samples)/rate), ylim=(-bound, bound), xlabel="Time (s)", ylabel="Stored PCM counts",
                     title=f"{labels[i]}\nCentered RMS {metrics['centered_rms_counts']:.1f} / peak {metrics['centered_peak_counts']:.1f}")
        frequency, _, psd = fft.spectrum(samples, rate)
        db = 10*np.log10(np.maximum(psd / fft.FULL_SCALE**2, 1e-16))
        spectra.semilogx(frequency[1:], db[1:], color=colors[i], alpha=.85, linewidth=.7, label=labels[i])
        power = np.array([x["source_referred_power_counts_squared"] for x in metrics["bands"]])
        share = [100*x["weighted_power_fraction"] if x["weighted_power_fraction"] is not None else 0 for x in metrics["bands"]]
        x = np.arange(4) + (i-.5)*.36
        power_ax.bar(x, power, width=.36, color=colors[i], label=labels[i])
        bars = share_ax.bar(x, share, width=.36, color=colors[i])
        share_ax.bar_label(bars, fmt="%.1f%%", padding=3, fontsize=9)
    spectra.axvline(80, color="#5c6266", linestyle="--", linewidth=1, label="80 Hz reference")
    spectra.set(xlim=(rate/len(signals[0]), rate/2), ylim=(-150, 0), xlabel="Frequency (Hz), logarithmic axis",
                ylabel="PSD (dBFS / Hz)", title="Whole-record periodic Hann PSD; each file mean removed")
    spectra.legend(loc="upper right")
    band_labels = ("0-<80", "80-<300", "300-<3000", "3000-8000")
    for ax in (power_ax, share_ax):
        ax.set_xticks(np.arange(4), band_labels)
        ax.set_xlabel("Frequency band (Hz)")
    scale = "log" if any(m["total_source_referred_power_counts_squared"] > 0 for m in measurements) else "linear"
    power_ax.set(yscale=scale, ylabel="Power (source counts squared)",
                 title="Source-referred power (÷ shared gain²), not acoustic watts")
    share_ax.set(ylim=(0, 105), ylabel="Share of each file's weighted power (%)", title="Share alone does not measure attenuation")
    for ext in ("png", "svg"):
        fig.savefig(output / f"matched-gain-comparison.{ext}", dpi=150, facecolor=fig.get_facecolor())
    plt.close(fig)
    return matplotlib.__version__


def compare(source, directory, output):
    signals, rate, gain, inputs = load_pair(source, directory)
    metrics = [measure(s, rate, gain) for s in signals]
    report = dict(created_utc=datetime.now(timezone.utc).isoformat(), source=str(Path(source).resolve()),
                  previews=str(Path(directory).resolve()), input_sha256=inputs,
                  matched_gain_verified=True, all_preview_samples_reconstructed=True, inputs_unchanged=False,
                  sample_rate_hz=rate, samples=len(signals[0]), duration_s=len(signals[0])/rate,
                  shared_gain=gain, fade_ms=10, highpass_hz=80, per_file_normalization=False,
                  spectral_method="whole-record periodic Hann, per-file mean removal, one-sided PSD",
                  absolute_power_reference="preview counts squared; source_referred fields divide by shared_gain squared; window-weighted, not acoustic watts",
                  numpy_version=np.__version__, measurements=dict(zip(NAMES, metrics)),
                  changes_b_vs_a=compare_metrics(*metrics), listening_acceptance="pending_user_comparison",
                  tool_sha256={Path(m.__file__).name: digest(m.__file__) for m in (fft, preview)})
    report["tool_sha256"][Path(__file__).name] = digest(__file__)
    output = Path(output).resolve()
    output.mkdir(parents=True, exist_ok=False)
    report["matplotlib_version"] = plot_pair(signals, rate, gain, metrics, output)
    if any(digest(path) != expected for path, expected in inputs.items()):
        raise RuntimeError("an input changed during comparison")
    report["inputs_unchanged"] = True
    (output / "comparison-analysis.json").write_text(json.dumps(report, indent=2, allow_nan=False) + "\n", encoding="utf-8")
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path, help="unchanged raw audio.wav")
    parser.add_argument("previews", type=Path, help="directory with both previews and comparison.json")
    parser.add_argument("--output", type=Path, required=True, help="new output directory; never overwritten")
    args = parser.parse_args()
    print(json.dumps(compare(args.source, args.previews, args.output), indent=2, allow_nan=False))


if __name__ == "__main__":
    main()

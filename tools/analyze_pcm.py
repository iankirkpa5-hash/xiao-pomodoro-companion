#!/usr/bin/env python3
"""Offline mono PCM16 waveform/FFT study. Never writes audio or opens serial."""
import argparse
from datetime import datetime, timezone
import hashlib
import io
import json
from pathlib import Path
import wave

import numpy as np

FULL_SCALE = 32768.0


def read_wav(path):
    data = Path(path).read_bytes()
    with wave.open(io.BytesIO(data), "rb") as wav:
        if (wav.getnchannels(), wav.getsampwidth(), wav.getcomptype()) != (1, 2, "NONE"):
            raise ValueError("expected uncompressed mono signed PCM16 WAV")
        rate, count = wav.getframerate(), wav.getnframes()
        pcm = wav.readframes(count)
    if rate <= 0 or count < 4 or len(pcm) != count * 2:
        raise ValueError("empty, too short, or truncated WAV")
    return np.frombuffer(pcm, dtype="<i2").astype(np.float64), rate, hashlib.sha256(data).hexdigest()


def spectrum(samples, rate, window="hann"):
    """Return one-sided peak-amplitude spectrum and power spectral density.

    Hann amplitude uses coherent gain; PSD uses window energy. They have
    different normalizations and must not be substituted for each other.
    """
    x = np.asarray(samples, dtype=np.float64)
    if x.ndim != 1 or len(x) < 4 or not np.isfinite(x).all() or not np.isfinite(rate) or rate <= 0:
        raise ValueError("expected >=4 finite samples and positive sample rate")
    if window not in ("hann", "rectangular"):
        raise ValueError("unsupported window")
    n = len(x)
    # Periodic Hann, suitable for DFT analysis (not the symmetric FIR window).
    weights = .5 - .5 * np.cos(2 * np.pi * np.arange(n) / n) if window == "hann" else np.ones(n)
    transform = np.fft.rfft((x - x.mean()) * weights)
    factors = np.full(len(transform), 2.0)
    factors[0] = 1
    if n % 2 == 0:
        factors[-1] = 1  # Neither DC nor the even-length Nyquist bin has a partner.
    amplitude = np.abs(transform) * factors / weights.sum()
    psd = np.abs(transform)**2 * factors / (rate * np.sum(weights**2))
    return np.fft.rfftfreq(n, 1 / rate), amplitude, psd


def amplitude_dbfs(amplitude):
    return 20 * np.log10(np.maximum(np.asarray(amplitude) / FULL_SCALE, 1e-7))


def scalar_dbfs(value):
    return float(20 * np.log10(value / FULL_SCALE)) if value > 0 else None


def excerpts(samples, rate, lengths=(.04, .25)):
    """Compare nested windows around the highest-RMS 10 ms block, not random words."""
    block = max(1, round(.01 * rate))
    usable = len(samples) // block * block
    if usable == 0 or len(samples) < round(max(lengths) * rate):
        raise ValueError("recording must contain at least 250 ms")
    centered = samples - samples.mean()
    energy = np.mean(centered[:usable].reshape(-1, block)**2, axis=1)
    center = (int(np.argmax(energy)) + .5) * block
    margin = round(max(lengths) * rate) / 2
    center = int(np.clip(center, margin, len(samples) - margin))
    result = []
    for seconds in lengths:
        size = round(seconds * rate)
        start = center - size // 2
        result.append((start, samples[start:start + size]))
    return result


def summarize(samples, rate, source_hash):
    centered = samples - samples.mean()
    frequency, amplitude, psd = spectrum(samples, rate)
    spacing = rate / len(samples)
    total = float(psd.sum() * spacing)
    bands = []
    for lo, hi in ((0, 80), (80, 300), (300, 3000), (3000, rate / 2)):
        if hi <= lo:
            continue
        mask = (frequency >= lo) & ((frequency <= hi) if hi == rate / 2 else (frequency < hi))
        power = float(psd[mask].sum() * spacing)
        bands.append(dict(low_hz=lo, high_hz=hi, weighted_power_fraction=power / total if total else 0.0))
    maxima = np.flatnonzero((amplitude[1:-1] > amplitude[:-2]) & (amplitude[1:-1] >= amplitude[2:])) + 1
    selected = []
    # Separated candidates, not a pitch estimate or a sound-source classifier.
    for index in maxima[np.argsort(amplitude[maxima])[::-1]]:
        if all(abs(frequency[index] - frequency[j]) >= 10 for j in selected):
            selected.append(int(index))
        if len(selected) == 8:
            break
    rms, peak = float(np.sqrt(np.mean(centered**2))), float(np.max(np.abs(centered)))
    return dict(
        source_sha256=source_hash, sample_rate_hz=rate, samples=len(samples),
        duration_s=len(samples) / rate, nyquist_hz=rate / 2, fft_bin_spacing_hz=spacing,
        raw_mean_counts=float(samples.mean()), raw_min_counts=float(samples.min()), raw_max_counts=float(samples.max()),
        centered_rms_counts=rms, centered_peak_counts=peak,
        centered_rms_dbfs=scalar_dbfs(rms), centered_peak_dbfs=scalar_dbfs(peak),
        full_scale_samples=int(np.count_nonzero((samples == -32768) | (samples == 32767))),
        window="periodic Hann", zero_padding=False,
        fft_peak_amplitude_reference="32768 counts; bin-centered sinusoid peak, not SPL",
        psd_integral_weighted_mean_square=total, bands=bands,
        separated_spectral_peaks=[dict(hz=float(frequency[j]), amplitude_counts=float(amplitude[j]),
                                       amplitude_dbfs=float(amplitude_dbfs(amplitude[j]))) for j in selected],
        excerpts=[dict(start_sample=start, samples=len(x), start_s=start / rate,
                       duration_s=len(x) / rate, fft_bin_spacing_hz=rate / len(x),
                       hann_main_lobe_null_to_null_hz=4 * rate / len(x))
                  for start, x in excerpts(samples, rate)],
    )


def make_plots(samples, rate, report, output):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    report["matplotlib_version"] = matplotlib.__version__

    plt.rcParams.update({"font.family": "DejaVu Sans", "font.size": 10,
                         "axes.spines.top": False, "axes.spines.right": False,
                         "axes.grid": True, "grid.alpha": .18, "figure.facecolor": "#faf9f5",
                         "axes.facecolor": "#faf9f5", "savefig.facecolor": "#faf9f5"})
    teal, orange = "#176d77", "#b55a25"
    centered = samples - samples.mean()
    frequency, amplitude, _ = spectrum(samples, rate)
    windows = excerpts(samples, rate)
    fig, axes = plt.subplots(3, 1, figsize=(12, 10), layout="constrained")
    fig.suptitle("XIAO / saved speech WAV\nRaw capture, DC removed; no gain or high-pass filter", fontsize=17)
    axes[0].plot(np.arange(len(samples)) / rate, centered, color=teal, linewidth=.55)
    axes[0].set(title=f"{len(samples):,} samples / {rate:,} Hz / {len(samples)/rate:.2f} s | "
                      f"removed mean {samples.mean():.2f} | RMS {report['centered_rms_counts']:.2f}",
                xlabel="Time (s)", ylabel="Centered PCM counts", xlim=(0, len(samples)/rate))
    start, short = windows[0]
    axes[0].axvspan(start/rate, (start+len(short))/rate, color=orange, alpha=.25)
    axes[1].plot((start + np.arange(len(short))) / rate, short - samples.mean(), color=orange, linewidth=.9)
    axes[1].set(title="40 ms detail near the highest-energy 10 ms block (not a phoneme label)",
                xlabel="Time in source (s)", ylabel="Centered PCM counts")
    axes[2].semilogx(frequency[1:], amplitude_dbfs(amplitude[1:]), color=teal, linewidth=.6)
    axes[2].set(title=f"Whole-record Hann FFT | bin spacing {rate/len(samples):g} Hz | not a pitch estimate",
                xlabel="Frequency (Hz), logarithmic axis", ylabel="Peak-amplitude spectrum (dBFS)",
                xlim=(rate/len(samples), rate/2), ylim=(-140, 0))
    axes[2].axvline(80, color=orange, linestyle="--", linewidth=1, label="80 Hz reference (not a filter)")
    axes[2].legend(loc="upper right")
    for ext in ("png", "svg"):
        fig.savefig(output / f"waveform-fft.{ext}", dpi=150)
    plt.close(fig)

    fig, axes = plt.subplots(2, 2, figsize=(12, 8), layout="constrained")
    fig.suptitle("Window length is a trade-off\nSame speech region above / synthetic 440 + 470 Hz below", fontsize=16)
    for col, (start, x) in enumerate(windows):
        n = len(x)
        freq, amp, _ = spectrum(x, rate)
        axes[0, col].plot(freq, amplitude_dbfs(amp), color=teal, marker=".", markersize=2, linewidth=.8)
        axes[0, col].set(title=f"Speech: {n/rate*1000:.0f} ms | {n} samples | df = {rate/n:g} Hz",
                         xlabel="Frequency (Hz)", ylabel="Peak-amplitude spectrum (dBFS)",
                         xlim=(0, 1500), ylim=(-110, -20))
        t = np.arange(n) / rate
        synthetic = FULL_SCALE * .3 * (np.sin(2*np.pi*440*t) + np.sin(2*np.pi*470*t))
        freq, amp, _ = spectrum(synthetic, rate)
        axes[1, col].plot(freq, amp / FULL_SCALE, color=orange, marker=".", linewidth=1)
        for hz in (440, 470):
            axes[1, col].axvline(hz, color="#59646e", linestyle="--", linewidth=.8)
        axes[1, col].set(title=f"Synthetic only | Hann main lobe ~{4*rate/n:g} Hz null-to-null",
                         xlabel="Frequency (Hz)", ylabel="Peak amplitude / full scale", xlim=(350, 560), ylim=(0, .65))
    for ext in ("png", "svg"):
        fig.savefig(output / f"window-comparison.{ext}", dpi=150)
    plt.close(fig)


def analyze(source, output=None, expected_sha256=None):
    source = Path(source).resolve()
    samples, rate, source_hash = read_wav(source)
    if expected_sha256 and source_hash != expected_sha256.lower():
        raise ValueError("source SHA-256 does not match expected capture")
    if rate != 16000:
        raise ValueError("this XIAO experiment expects a 16000 Hz recording")
    report = summarize(samples, rate, source_hash)
    report.update(source=str(source), created_utc=datetime.now(timezone.utc).isoformat(),
                  numpy_version=np.__version__, source_unchanged=False,
                  analysis_tool_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest())
    output = Path(output).resolve() if output else source.parent / "waveform-fft"
    output.mkdir(parents=True, exist_ok=False)
    make_plots(samples, rate, report, output)
    if hashlib.sha256(source.read_bytes()).hexdigest() != source_hash:
        raise RuntimeError("source changed during analysis")
    report["source_unchanged"] = True
    (output / "analysis.json").write_text(json.dumps(report, indent=2, allow_nan=False) + "\n", encoding="utf-8")
    return output, report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("wav", type=Path)
    parser.add_argument("--output", type=Path, help="new output directory; existing directories are never overwritten")
    parser.add_argument("--expected-sha256", help="optional capture-report WAV hash")
    args = parser.parse_args()
    output, report = analyze(args.wav, args.output, args.expected_sha256)
    print(json.dumps(dict(output=str(output), **report), indent=2))


if __name__ == "__main__":
    main()

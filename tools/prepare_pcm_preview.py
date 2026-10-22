#!/usr/bin/env python3
"""Create labeled listening comparisons without changing original PCM evidence."""
import argparse
from array import array
import hashlib
import json
import math
from pathlib import Path
import sys
import wave


DEFAULT_GAIN = 16.0
MAX_GAIN = 32.0
HEADROOM_PEAK = 16383  # -6 dBFS for signed 16-bit samples


def validate_gain(gain):
    if not math.isfinite(gain) or not 1.0 <= gain <= MAX_GAIN:
        raise ValueError(f"gain must be between 1 and {MAX_GAIN:g}")
    return gain


def highpass(samples, rate, cutoff):
    alpha = math.exp(-2 * math.pi * cutoff / rate)
    previous_input, previous_output = samples[0], 0.0
    result = []
    for value in samples:
        filtered = alpha * (previous_output + value - previous_input)
        result.append(filtered)
        previous_input, previous_output = value, filtered
    return result


def render(samples, rate, gain):
    # A 10 ms endpoint fade avoids adding a click at the file boundaries.
    fade = min(round(rate * .01), len(samples) // 2)
    output = array("h")
    for i, value in enumerate(samples):
        envelope = min(1.0, i / max(1, fade), (len(samples)-1-i) / max(1, fade))
        output.append(round(value * gain * envelope))
    if sys.byteorder != "little":
        output.byteswap()
    return output.tobytes()


def prepare(source, gain=DEFAULT_GAIN):
    gain = validate_gain(gain)
    before = hashlib.sha256(source.read_bytes()).hexdigest()
    with wave.open(str(source), "rb") as wav:
        if (wav.getnchannels(), wav.getsampwidth(), wav.getframerate()) != (1, 2, 16000):
            raise ValueError("expected 16 kHz mono signed 16-bit PCM WAV")
        data = array("h", wav.readframes(wav.getnframes()))
    if not data:
        raise ValueError("empty WAV")
    if sys.byteorder != "little":
        data.byteswap()
    rate = 16000
    mean = sum(data) / len(data)
    centered = [x-mean for x in data]
    filtered = highpass(centered, rate, 80)
    # The same fixed gain in both comparisons avoids automatic gain pumping.
    maximum = max(max(map(abs, centered)), max(map(abs, filtered)))
    headroom_cap = HEADROOM_PEAK / maximum if maximum else 1.0
    shared_gain = min(gain, headroom_cap)
    suffix = "-listening-comparison" if gain == DEFAULT_GAIN else f"-listening-comparison-gain{gain:g}"
    output = source.parent / (source.stem + suffix)
    output.mkdir(exist_ok=False)
    peak_dbfs = round(20 * math.log10(maximum * shared_gain / 32767), 2) if maximum else None
    report = dict(source=str(source.resolve()), source_sha256=before,
                  sample_rate=rate, samples=len(data), raw_mean=mean,
                  requested_gain=gain, headroom_cap=headroom_cap,
                  shared_gain=shared_gain, estimated_peak_dbfs=peak_dbfs,
                  fade_ms=10, highpass_hz=80,
                  purpose="Listening comparison only; not a firmware or audio-quality fix",
                  outputs=[])
    for name, samples in (("centered.wav", centered), ("highpass-80hz.wav", filtered)):
        path = output / name
        with path.open("xb") as stream:
            with wave.open(stream, "wb") as wav:
                wav.setnchannels(1)
                wav.setsampwidth(2)
                wav.setframerate(rate)
                wav.writeframes(render(samples, rate, shared_gain))
        report["outputs"].append(dict(file=name, sha256=hashlib.sha256(path.read_bytes()).hexdigest()))
    if hashlib.sha256(source.read_bytes()).hexdigest() != before:
        raise RuntimeError("source changed during preview generation")
    (output / "comparison.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    return output


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("wav", type=Path)
    parser.add_argument("--gain", type=float, default=DEFAULT_GAIN,
                        help="fixed preview gain from 1 to 32 (default 8); still capped for -6 dBFS peak headroom")
    args = parser.parse_args()
    try:
        output = prepare(args.wav, args.gain)
    except ValueError as error:
        parser.error(str(error))
    print(output)


if __name__ == "__main__":
    main()

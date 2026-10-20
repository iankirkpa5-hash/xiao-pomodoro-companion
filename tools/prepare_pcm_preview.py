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


def prepare(source):
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
    gain = min(8.0, 16383 / maximum) if maximum else 1.0
    output = source.parent / (source.stem + "-listening-comparison")
    output.mkdir(exist_ok=False)
    report = dict(source=str(source.resolve()), source_sha256=before,
                  sample_rate=rate, samples=len(data), raw_mean=mean,
                  shared_gain=gain, fade_ms=10, highpass_hz=80,
                  purpose="Listening comparison only; not a firmware or audio-quality fix",
                  outputs=[])
    for name, samples in (("centered.wav", centered), ("highpass-80hz.wav", filtered)):
        path = output / name
        with path.open("xb") as stream:
            with wave.open(stream, "wb") as wav:
                wav.setnchannels(1)
                wav.setsampwidth(2)
                wav.setframerate(rate)
                wav.writeframes(render(samples, rate, gain))
        report["outputs"].append(dict(file=name, sha256=hashlib.sha256(path.read_bytes()).hexdigest()))
    if hashlib.sha256(source.read_bytes()).hexdigest() != before:
        raise RuntimeError("source changed during preview generation")
    (output / "comparison.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    return output


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("wav", type=Path)
    args = parser.parse_args()
    print(prepare(args.wav))


if __name__ == "__main__":
    main()

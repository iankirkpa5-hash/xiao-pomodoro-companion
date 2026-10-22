# Host Preview Gain Follow-Up

Date: 2026-10-22 (Asia/Shanghai).
Branch: `codex/pcm-playback-gain`, based on `f7313cf`. This is a host-side
tooling change; no firmware was rebuilt or uploaded.

Scope: make the local listening preview louder without changing the recorded
samples, the device's sound-level thresholds, the microphone path, or the
Pomodoro UI. The raw WAV and the capture path are unchanged.

## Change

`prepare_pcm_preview.py` gained a bounded `--gain` option:

- accepted range: 1-32, default 16 (was 8);
- the same gain is used for `centered.wav` and `highpass-80hz.wav`;
- the requested gain is still capped by the existing -6 dBFS peak-headroom rule;
- a non-default gain writes to `audio-listening-comparison-gain<N>/`;
- `comparison.json` records `requested_gain`, `headroom_cap`, `shared_gain`, and
  `estimated_peak_dbfs`.

No automatic gain control, silence gate, time stretching, or sample insertion
was added.

## Listening Check

Source: the 2026-10-20 side-position retest
(`pcm-20261020T145940-4fa8c8ee/audio.wav`), centered/high-pass peak 845.38.

| Requested | Actual | Centered peak | RMS | Result |
| ---: | ---: | ---: | ---: | --- |
| 8x | 8.00x | 6763 (-13.7 dBFS) | 1057 | previous default |
| 16x | 16.00x | 13526 (-7.7 dBFS) | 2114 | selected default |
| 20x | 19.38x (headroom cap) | 16383 (-6.0 dBFS) | 2560 | louder option |

The operator listened to the three `centered.wav` previews and reported that all
three were clear; the only perceived difference was loudness. A 24x request
would have produced the same 19.38x result because of the headroom cap, so it
was not added as a separate comparison.

## Decision And Boundaries

The default preview gain is raised from 8x to 16x (+6 dB) to address the low
playback volume while retaining about 1.7 dB more headroom than the maximum safe
setting. `--gain` remains available for a louder or quieter preview.

This is a host-side listening aid. It does not change the recorded PCM, the
device's Quiet/Normal/Loud thresholds, the microphone path, or any firmware
behavior. It is not a claim about the device speaker, room acoustics, or
lossless capture. The original WAV remains unchanged; previews stay local and
ignored by Git.

## Automated Coverage

- Python suite: 39 tests, 37 passed, 2 POSIX-only tests skipped on Windows.
- New tests cover gain bounds (including NaN and infinity), shared gain, default
  compatibility, gain-suffixed output naming, and headroom capping.

## Related Records

- [October 20 PCM/WAV experiment](2026-10-20-pcm-wav.md)
- [PCM recorder usage](../pcm-recording.md)

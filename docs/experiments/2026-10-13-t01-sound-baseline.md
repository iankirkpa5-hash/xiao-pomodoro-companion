# T01 Sound Baseline and Capture Tool Check

Date: 2026-10-13 (Asia/Shanghai). Task: T01, real-board evidence collection.

## Scope and Provenance

- Local branch: `codex/microphone-serial-data`; HEAD: `a26959ec0a7ea7dfb7e424f6a0e4e7a43e52ce56`.
- Local source includes uncommitted September continuous-microphone and web-monitor work. It is not identical to remote main (`fc32445`) or the published v1.6 tag.
- Capture tooling was imported from draft PR #1 at `e7c73dd2618e09a0d84d8595dafb545e76144f63`, then adapted locally for legacy Mic records and timestamped CSV. The PR was not merged or changed remotely.
- All `src/` files and `platformio.ini` still matched the preflight source hashes after the captures. No firmware build, upload, threshold change, touch fix, or NVS write was performed this round.
- Python 3.11.7 in the existing PlatformIO environment; PlatformIO Core 6.2.0; pySerial available. Port: COM11, USB VID:PID `303A:1001`, device serial `E0:72:A1:FA:17:9C`.
- The board emitted `timestamp_ms,rms,peak,level` and `Mic capture:` throughput diagnostics. This matches the experimental format but does not prove the exact running binary; no unique firmware identifier is reported.

## Method and Operator Observations

First read an uncontrolled 30-second preflight without assigning a sound class.
Then prompt quiet, normal speech, and claps separately for approximately 30 seconds
each. Capture only, at 115200 baud; one serial reader; no control commands.
The serial handle was closed after each run.

The requested protocol was no speech/tapping/touch during quiet; normal speech
at 20-30 cm; and one clap about every three seconds at 20-30 cm without touching
the board or table. These are instructions, not measured conditions.

The user subsequently confirmed following the prompts and said the distance was
very close. Record that actual description rather than asserting 20-30 cm.
Exact distance, clap count/timestamps, and whether there was an incidental sound
or movement in the quiet interval remain unconfirmed.

Before the series, the user reported that screen, touch, and countdown were
normal. This is a human observation, independent of the serial error checks.
Settings, Bright, Save, Reset, Stats and NVS were not individually retested here.

## Evidence Locations

Each directory contains `serial.log`, `mic.csv`, and `report.json`:

| Condition | Local directory under `test-runs/` |
| --- | --- |
| Uncontrolled preflight | `20261013T131359Z-0ec0e814` |
| Quiet | `20261013T132241Z-4f4c50f9` |
| Speech | `20261013T132639Z-03888e7a` |
| Clap | `20261013T132928Z-6698a2dd` |

Directory times are UTC; the corresponding local start times are 21:13:59,
21:22:41, 21:26:39 and 21:29:28. Raw artifacts remain local and ignored by Git;
they are not available merely by cloning the repository. Keep these directories
for the next comparison. Reports retain source and capture-tool hashes; the
later operator confirmation above supplements, but does not overwrite, their
original notes.

## Observed Results

These are record ranges, not calibrated acoustic measurements or per-clap scores.

| Condition | Valid rows | RMS range | Maximum peak | Levels observed | I2C errors | Largest device report gap |
| --- | ---: | ---: | ---: | --- | ---: | ---: |
| Uncontrolled | 50 | 8.1-105.3 | 470 | Quiet, Normal | 221 | 1000 ms |
| Quiet | 54 | 7.8-327.0 | 6260 | Quiet, Normal, Loud | 221 | 1500 ms |
| Speech | 58 | 6.4-217.4 | 955 | Quiet, Normal | 222 | 1000 ms |
| Clap | 44 | 7.2-2386.7 | 42646 | Quiet, Loud | 224 | 2000 ms |

All four reports are **FAILED** because I2C read errors were observed. Data
capture itself succeeded: it produced valid records and preserved the raw errors.
No capture is being relabeled PASS merely because the UI appeared normal.

All six throughput diagnostics in each run reported zero microphone read errors.
Across the four runs, the observed diagnostics were about
80,000-80,160 samples per five seconds; the raw reports preserve every diagnostic.
Processing throughput is not proof of lossless DMA acquisition. A CSV row has
no sample-count field, so the per-record sample-count check is explicitly SKIP.

The current source aggregates interval RMS and maximum peak while retaining a
debounced/held level. A level can outlast its triggering frame. Centering signed
16-bit PCM can produce absolute deviations above 32767, so peak 42646 is not
automatically an invalid number; these statistics alone cannot diagnose clipping
or prove that every peak came from a clap.

## Conclusion and Limits

- Real USB capture and dual-format tooling have evidence; controlled acoustic acceptance remains incomplete.
- The clap-labeled run contains a substantially higher peak than the speech-labeled run. This addresses the earlier observation that peaks appeared not to move, but is not a clap-recognition success rate.
- The quiet-labeled run also contains a high peak and Loud. Investigate timing and conditions before changing thresholds or declaring false positives.
- Repeated `requestFrom(): i2cRead returned Error -1` remains unresolved. It is separate from the zero microphone-read-error counter. Do not suppress it or assume its cause from this log alone.
- Device report intervals reached two seconds. This measures report timing, not necessarily an audio-sampling gap; continuous capture diagnostics must be considered separately.
- No reboot banner was captured; absence of a banner is not proof of long-term stability. No complete daily-use or PR acceptance is claimed.

## Next Single Step

T02: plot these saved datasets first, retaining both host receive time and device
report time. Mark report gaps and keep the operator conditions visible. Inspect
the quiet outlier and clap peaks without treating Quiet/Normal/Loud as sound-source
labels. Do not upload firmware or tune thresholds as part of that comparison.

After the evidence review, isolate one issue for a subsequent change, including
the recurring I2C errors if warranted. T03 build/upload/capture acceptance and
the PR merge remain pending.

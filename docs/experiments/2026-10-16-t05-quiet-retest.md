# Quiet Retest: Clean Baseline After the Touch-IRQ Fix

Date: 2026-10-16 (Asia/Shanghai).
Scope: re-capture the quiet group with the same post-IRQ-gate firmware. The
first evening quiet run was not accepted; this record documents the replacement.

## Why the First Quiet Run Was Rejected

Run `20261016T143500Z-d7b09d53` (label `t02-retest-quiet`) was not accepted as
a clean quiet baseline:

- Two `Loud` rows at host elapsed 16.5 s (rms 257.0, peak 4415) and 17.0 s
  (rms 111.9, peak 1331).
- One additional quiet-labeled spike at 19.5 s (rms 85.3, peak 671).
- Quiet baseline: rms median 11.3, mean 21.9, p95 40.4, maximum 85.3.
- The operator later reported a sudden sound during that run, source
  unspecified. The raw artifacts remain preserved and unmodified.

## Quiet Retest

Capture only, no firmware change, COM11, 30 seconds, operator-confirmed quiet,
no board or table contact. Command:

```text
python tools/hardware_loop.py capture --port COM11 --seconds 30 --label t02-retest-quiet-2
```

Result: `PASS_SERIAL_SMOKE`.

| Check | Rejected run | Retest |
| --- | ---: | ---: |
| Valid CSV rows | 60 | 60 |
| Levels observed | Loud, Quiet | Quiet |
| RMS median / mean | 11.3 / 21.9 | 3.7 / 3.7 |
| RMS p95 / maximum | 40.4 / 85.3 | 4.7 / 5.6 |
| Peak p95 / maximum | 671 / 4415 | 22 / 29 |
| I2C errors | 0 | 0 |
| Microphone read errors | 0 | 0 |
| Maximum device report gap | 500 ms | 500 ms |

All six five-second capture diagnostics reported 500 frames and 80,000 samples
(about 16 kHz) with zero failed reads. Every row is Quiet; no Loud or Normal
rows remain. The retest quiet floor (median 3.7) is also below the quiet
baseline observed between claps in the later clap repeat run (median 5.6).

This is a clean quiet baseline for the current firmware only. It is not full
acceptance: screen, touch, and sound-response checks and the T03 manual/PR
acceptance remain open. The speech and clap reruns are documented separately
and still await their operator confirmations.

## Saved Artifacts

Git-ignored directory `test-runs/20261016T154530Z-5fa21be1`:

| File | SHA-256 |
| --- | --- |
| report.json | 9935657373DACE86DC57672B5DE4BD4A6B3ED003443DDD7DA1AA2315BF59C230 |
| mic.csv | 7B6A4E8A7CF3F306244C7B8F12982E48F85271CBAA53DCC191C5CB5257D7AD6B |
| serial.log | 82FC3AB6930020D0CCA094B8E1F4EC86B3F0E7F195AB62D1CC0B49DC10FB253D |

The rejected run remains at `test-runs/20261016T143500Z-d7b09d53` with its own
original hashes; neither raw log was edited.

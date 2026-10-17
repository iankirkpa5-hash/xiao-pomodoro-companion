# T02 Clap Retest

Date: 2026-10-17 (Asia/Shanghai).

## Result

The requested 30-second clap retest produced 60 complete CSV reports, with
500 ms device timestamp intervals, zero I2C errors, zero mixed payload
candidates, and zero microphone read errors. The capture verdict is
`PASS_SERIAL_SMOKE`.

There are nine separated reports with peak above 2600; all nine have the
firmware label `Loud`. Maximum peak is 31548 and maximum interval RMS is 1823.8.
This confirms substantial transient amplitude changes in this run. The operator
subsequently reported approximately 15 claps, starting close and moving farther
away, with quieter claps later. Exact count, distances and action timestamps
were not supplied, so no clap detection rate is assigned.

## Conditions and Provenance

The user requested another clap-group retest. After checking COM11, a start
cue requested one clap approximately every three seconds for 30 seconds,
approximately 20 cm from the microphone, without board or desk contact.
These are requested conditions, not measured distance or confirmed actions.
The operator was told when recording ended. Earlier runs were retained.

The operator's follow-up supersedes the requested fixed-distance conditions:
approximately 15 claps were performed, initially close and later farther away,
with reduced sound intensity. Treat this as a variable-distance, variable-level
trial. It is not a confirmed fixed-20-cm test or an exact ten-clap sequence.
The original capture report is preserved; this follow-up records information
received after capture without rewriting its original notes.

Local evidence directory: `test-runs/20261017T131745Z-c037b2e0/`.
It contains `serial.log`, `mic.csv`, and `report.json`, and is Git-ignored.
Cloning the repository does not include these raw artifacts.

Capture started at 21:17:45 local time (13:17:45 UTC). Local HEAD was
`a26959ec0a7ea7dfb7e424f6a0e4e7a43e52ce56` with uncommitted work. The captured
local source hash map matches the October 16 build/upload/capture source map
from the [touch IRQ fix](2026-10-16-t04-touch-irq-serial.md).
The device does not report a unique build hash; matching local sources alone
does not attest the running binary. This retest performed capture only.

## Final Fixed-Count Run (21:50, 13/13)

After the variable-distance run, the operator requested one more 30-second
capture with a fixed clap count. Local evidence directory:
`test-runs/20261017T135042Z-7dc958f6/` (label
`t02-clap-retest-20261017-r2`), capture only, COM11, same firmware.

Result: `PASS_SERIAL_SMOKE`. 60 valid rows, 0 I2C errors, 0 mixed payload
candidates, 0 microphone read errors, six diagnostics of 80,000 samples per
5,000 ms (about 16 kHz), 500 ms device report spacing.

The operator confirmed 13 claps with varied loudness. Thirteen loud events
were retained, one per clap, all labeled `Loud`:

| Host elapsed (s) | Interval RMS | Retained peak | Firmware label |
| ---: | ---: | ---: | --- |
| 5.969 | 348.3 | 14021 | Loud |
| 7.969 | 1096.2 | 26393 | Loud |
| 9.969 | 680.5 | 20338 | Loud |
| 11.969 | 240.5 | 6692 | Loud |
| 13.469 | 1081.7 | 25730 | Loud |
| 15.469 | 1610.2 | 41752 | Loud |
| 16.969 | 727.8 | 22701 | Loud |
| 19.969 | 310.8 | 9695 | Loud |
| 21.469 | 2283.5 | 27940 | Loud |
| 23.469 | 1310.5 | 33344 | Loud |
| 24.969 | 352.3 | 8483 | Loud |
| 26.969 | 954.6 | 20052 | Loud |
| 28.469 | 1022.5 | 29369 | Loud |

Match: 13 claps reported, 13 retained loud events, operator-confirmed. Every
event cleared the transient Loud path (peak >= 4800); the weakest, 6692 at
11.969 s, has 1.4x margin. Event spacing was 1.5-3 s. A 526 mid-level event at
19.469 s stayed `Normal` and a 529 startup blip at 0.969 s stayed `Quiet`, so
neither was misread as a clap.

Raw artifact SHA-256 values:

```text
report.json 2690BD99812ED2671ACA8737AEE53963228A833BF2B2D4066F1D8BD7EC7C033C
mic.csv     E3E487A353F48C9DC3DDD882C0000BF6792976DEB9CD5788DDCED4F64CD8DEAF
serial.log  15B7D68384EE2B6C25239296931E147FC86CB6C33FB2F897ADC39A59639BA3E6
```

The variable-distance run above remains a separate record; it is not rewritten.

## Measurements

| Metric | Observation |
| --- | --- |
| Capture duration | 30.031 s |
| Valid CSV records | 60 |
| Device report intervals | All 500 ms |
| Host receive intervals | Median 0.5 s, maximum 1.0 s |
| I2C errors / mixed payload candidates | 0 / 0 |
| Microphone read errors / boot messages / touch messages | 0 / 0 / 0 |
| Throughput diagnostics | Six reports, each 80000 samples / 5000 ms |
| RMS minimum / median / maximum | 4.1 / 6.3 / 1823.8 |
| Peak minimum / median / maximum | 16 / 29.5 / 31548 |
| Quiet / Normal / Loud reports | 48 / 0 / 12 |

Units are centered PCM counts, not calibrated sound-pressure decibels.
The offline analyzer reconciled the CSV rows with standalone raw log lines,
capture counts, diagnostics, and reported extrema; all checks passed.
The capture ended with one trailing partial byte, reported by the tool; it is
not used as a measurement.

Five host intervals exceed the analyzer's 750 ms display threshold. Their
device intervals are still 500 ms, and all 60 expected reports are present.
These are receive-time delays/batching, not evidence of missing device reports.
Throughput diagnostics do not prove lossless PCM acquisition.

## High-Peak Reports

The cutoff of 2600 below is an exploratory way to list prominent reports,
not a new classifier threshold or a clap counter. Time is host receive elapsed
time, not the precise time of the acoustic impulse.

| Host elapsed (s) | Interval RMS | Retained peak | Firmware label |
| ---: | ---: | ---: | --- |
| 0.531 | 674.6 | 18811 | Loud |
| 3.031 | 613.7 | 20207 | Loud |
| 5.531 | 277.3 | 7885 | Loud |
| 7.531 | 658.9 | 23565 | Loud |
| 9.531 | 1823.8 | 31548 | Loud |
| 11.531 | 1199.2 | 29736 | Loud |
| 21.531 | 862.3 | 24410 | Loud |
| 23.531 | 1054.6 | 27982 | Loud |
| 28.297 | 1132.3 | 22375 | Loud |

There are 12 Loud reports but nine listed high-peak reports. The firmware
classifies shorter frames and holds Loud briefly, so multiple received labels
can follow one impulse. Neither number can be substituted for actual claps.
The sparse high-peak period between 11.5 and 21.5 seconds remains an observation;
without an action diary it cannot be called missed claps.

The other 51 reports have peaks of only 16-132 and a maximum RMS of 13.4.
There are no additional moderate peaks hidden just below the exploratory 2600
cutoff. Therefore reducing the reporting cutoff alone would not recover six
clearly separated weaker events from this CSV. Increased distance and quieter
claps are plausible contributors, but cannot by themselves establish what
happened to every reported clap. Complete serial reports do not prove that
every acoustic event was captured, and 9 / approximately 15 is not a measured
detection rate.

## Acceptance Still Needed

- The fixed-count run above closes the clap group at 13/13. A fixed-distance,
  similar-intensity variant was not repeated, because the operator closed the
  group with this fixed-count run instead.
- Screen-label sync was observed on October 17: Loud during claps and Normal
  during speech were confirmed on screen. The speech Quiet/Normal flicker was
  addressed by the [quiet debounce change](2026-10-17-t06-quiet-debounce.md).
- The speech group completion is operator-confirmed for the 10-16 run
  (42 Quiet / 18 Normal, no false Loud).
- The quiet group is closed by the separate clean
  [quiet retest](2026-10-16-t05-quiet-retest.md).
- Reset, Stats, and power-cycle NVS manual checks passed on October 17
  ([manual check record](2026-10-17-t07-manual-reset-stats-nvs.md)); PR #1
  acceptance remains open.

No firmware thresholds were adjusted and no release acceptance, commit, tag,
or push was performed for these retests.

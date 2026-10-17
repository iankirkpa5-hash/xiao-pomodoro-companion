# Quiet Debounce Change: Stable Normal During Speech

Date: 2026-10-17 (Asia/Shanghai).
Scope: one evidence-led T04 firmware change plus a real build/upload/capture
verification. No release, commit, tag, or push was performed for this record.

## Evidence Before the Change

Two screen-sync captures on the T04 firmware (still 5-frame quiet debounce):

- `20261017T140358Z-708ef391` (far/soft speech): energy hovered at the Normal
  boundary; only 4 Normal rows appeared near the end; the screen flickered
  Quiet and Normal.
- `20261017T140954Z-da2a7a06` (closer speech): 16 Normal + 2 Loud rows, but
  5 Quiet rows still carried peaks of 1400-2000. The label dropped to Quiet
  during word gaps of about 200 ms and flipped back. The operator saw the same
  flicker on screen.

The flicker was not a false detection: the 5-frame quiet debounce (about 200 ms
at the 40 ms update cadence) was shorter than natural word gaps.

## Change

`src/microphone.cpp`: `MIC_QUIET_DEBOUNCE_FRAMES` 5 -> 15 (about 600 ms).
Loud and Normal thresholds, the 600 ms Loud hold, and the Normal debounce are
unchanged. `tools/microphone-tests/test.cpp` was updated to the new fallback
timing; all eight regression groups pass in WSL with ASan/UBSan.

## Build and Upload

PlatformIO build passed (RAM 19,536 / 327,680 bytes; flash 351,429 / 3,342,336
bytes) and upload to COM11 was verified. No other source file changed.

## Verification

Capture `20261017T141505Z-5ee908b1` (label `t02-speech-debounce-verify`),
30 seconds of speech at about 20 cm:

- `PASS_SERIAL_SMOKE`: 60 valid rows, 0 I2C errors, 0 microphone read errors.
- 19 Normal rows spanning about t=10-21.5 s; 41 Quiet rows, all genuinely
  quiet (max peak 516, max RMS 39.5; before the change Quiet rows carried
  peaks up to 2000).
- Six transitions in the 500 ms CSV sampling; no Quiet row carries
  speech-level energy.
- Operator confirmed the screen holds Normal through word gaps and falls back
  to Quiet only after real pauses.

## Artifacts

Evidence: `test-runs/20261017T140358Z-708ef391` and
`test-runs/20261017T140954Z-da2a7a06`.

Verification `test-runs/20261017T141505Z-5ee908b1` SHA-256 values:

```text
report.json 230DBDEF86159A03EDAE834485A5799FD87F373A56B9EF849CCD74A1D56C5305
mic.csv     401ED4C43DD0AA376DBB886CBBE64D2E976FBEBBF9920A476017AEE652531373
serial.log  376898D265D0B280CA9326FF9E72E36DE8294A3FC630E238D2B77A0A4830CAA4
```

## Limits and Remaining

The change affects only the Normal-to-Quiet fallback timing. The 13/13 clap
and clean quiet records were captured on the previous firmware; their
detection thresholds are unchanged, so a quick post-change clap sanity check
is optional. Reset/Stats/NVS manual checks and PR #1 acceptance were completed
later the same day ([manual checks](2026-10-17-t07-manual-reset-stats-nvs.md);
PR merged as `ec5cfe8`).

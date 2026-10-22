# Manual Reset / Stats / NVS / Full Focus Checks (C)

Date: 2026-10-22 (Asia/Shanghai).
Firmware: `main` at `53b5055`, built and uploaded to COM11 on 2026-10-22.
Firmware SHA-256: `43cc8cdc6aa46b14eb56bcdba1e8d83d0649dd66945d9976ee66b91cc98a0fe2`
(build-specific; this is not an on-device build ID).

Scope: close the remaining manual C checks after the PCM/WAV and preview-gain
work. No source changes were made for this record; the checks are capture plus
operator observation. The host preview-gain change in PR #4 does not affect the
firmware.

## Setup

- XIAO ESP32S3 Sense with the Sense expansion board installed, Round Display,
  camera module removed, USB-C connected.
- COM11 at 115200 baud; only one serial reader at a time.
- Starting settings reported by the operator: Focus 50, Bright 60, Done 2.
- Clearing Stats was approved. The original Done 2 cannot be restored.

## Reset

- The operator started a Focus session, entered Settings, and selected Reset.
- Serial: `Settings reset` followed by `Pomodoro: Focus 1500s running=false`
  (25:00 at that point, because the saved Focus was 25).
- The operator did not separately type the 25:00 screen observation; the
  sequence moved on to changing Focus. A later Reset after the post-reboot save
  (Focus 45) produced `Settings reset` ->
  `Pomodoro: Focus 2700s running=false`.
- Result: serial PASS for both Reset actions. The screen confirmation is
  partial/indirect, not a separate 25:00 report.

## Stats

- Serial: `Settings stats cleared`.
- After the power cycle the operator saw Done 0.
- The increment path was then exercised by the full Focus completion:
  `Completed focus sessions=1`; the operator confirmed Done 1 afterwards.
- Result: PASS for clear and increment.

## NVS Persistence

- The operator saved Focus 45 and Bright 60.
- USB-C was unplugged for about five seconds, then reconnected.
- After reboot the operator saw Focus 45, Bright 60, Done 0.
- Post-reboot capture `20261022T122137Z-9a7ab526`: `Settings saved: focus=45
  min brightness=60%`, timer steady at `Focus 2700s running=false`,
  `PASS_SERIAL_SMOKE`, zero I2C errors, zero read errors.
- Later the settings were restored: `Settings saved: focus=50 min
  brightness=60%`, then `Settings reset` ->
  `Pomodoro: Focus 3000s running=false` (50:00, not running).
- Result: PASS. The final daily setting is Focus 50 / Bright 60; Done remains 1.

## Full Focus Completion

- The operator set Focus 25, Bright 60, started the session, and left it alone.
- A one-off long logger recorded the whole session. The existing
  `hardware_loop.py capture` cap is 600 seconds, so the 25-minute run used a
  local raw logger that flushed each received chunk; it is not repo tooling.
- At device time 1,685,973 ms the log shows, in order:
  - `Session complete`
  - `Completed focus sessions=1`
  - `Switched to Break`
- `Pomodoro: Break 300s running=false` then appeared once per second
  (393 lines in this log) and the Break state persisted.
- About 60 seconds after completion, the idle screen replaced the Break screen.
  At device time 2,079,163 ms the operator tapped the idle screen:
  `Idle wake to focus` + `Idle tap`, followed by a new
  `Pomodoro: Focus 1500s running=true`.
- The operator saw the idle screen, not the Break 5:00 screen, and confirmed
  Done 1 afterwards.
- Result: completion, Done increment, and Break transition PASS in serial.
  The Break 5:00 screen itself was not visually captured; this is a UX
  observation, not a failed completion.

## Evidence

| Directory | What it captured | Verdict | serial.log SHA-256 |
| --- | --- | --- | --- |
| `20261022T121030Z-14310c67` | Reset, Focus 45, Save | `PASS_SERIAL_SMOKE` | `35c6ef280c1ecd9e14959004c3e517ec0d339f7520f8391dd42433985abfdb2c` |
| `20261022T121539Z-798c2ce6` | Stats clear, Save | `PASS_SERIAL_SMOKE` | `daeb34223057c443643b66faafb073e19fe0ebfa0381f5367bdb65c93497516e` |
| `20261022T122137Z-9a7ab526` | Post-reboot Save/Reset | `PASS_SERIAL_SMOKE` | `6fdd33958a2be9467ac55bb9585f75a44133e4460aa4cbf52e8942f4dcaf4896` |
| `20261022T125841Z-d24a4877` | Restore Focus 50, Reset | `PASS_SERIAL_SMOKE` | `447ac76b18b2dc14cbcb900dc705814fb464c4b152cc953fd328d9875287ca5c` |
| `long-phase3-20261022T202435` | Full Focus completion, raw log only | no report | `8d243ae533f8bd8ed9b6c354d4f6e27fcf3f34110ea164d506d74946bfd6e35d` |

All four bounded captures reported zero I2C errors and zero read errors. The
long log contains no I2C error lines and no nonzero `read_errors` values.
`Session complete`, `Completed focus sessions=1`, and `Switched to Break` each
appear exactly once.

## Result And Remaining

- Reset, Stats clear, Stats increment, NVS persistence, and full Focus
  completion are recorded as PASS within the limits above.
- The Break 5:00 screen is replaced by the idle screen after 60 seconds. Tapping
  idle starts a new Focus rather than returning to the paused Break. This is a
  candidate follow-up: either keep the Break screen until acknowledged or make
  idle tap restore Break.
- Settings are restored to Focus 50 / Bright 60, 50:00 paused. Done is 1 because
  the completion incremented it; the original Done 2 was cleared and cannot be
  restored.
- No release tag was created; the displayed version remains `v1.6`.

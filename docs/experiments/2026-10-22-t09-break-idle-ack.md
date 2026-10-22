# Break Idle Acknowledgment Follow-Up

Date: 2026-10-22 (Asia/Shanghai).
Branch: `codex/break-idle-ack`, based on `fee4a27`.
Firmware: built and uploaded to COM11 on 2026-10-22.
Firmware SHA-256: `0108d2e634f163afcf95666a5375fceb4a33d1ea691927f992651801a8cd3029`
(build-specific; not an on-device build ID).

Scope: implement option A from the C finding: after a Focus session completes,
keep the Break 5:00 screen visible until the operator acts instead of replacing
it with the idle screen after 60 seconds. Timer, stats, sound thresholds, and
UI layout are otherwise unchanged.

## Change

- Added `break_awaiting_ack`, set when `switchSession()` enters Break and
  cleared when the operator touches the screen.
- `updateIdleScreen()` keeps resetting the idle timer while the flag is set, so
  the Break 5:00 / PAUSE screen remains.
- Serial markers: `Break awaiting ack` when the flag is set, and
  `Break acknowledged` on the first touch that clears it.
- Any short or long press acknowledges the state; a short tap then starts the
  Break countdown, while a long press opens Settings as before.

## Verification

- The operator set Focus 25 / Bright 60 and started a complete session.
- Log (`long-break-ack-20261022T210926`):
  - line 5219: `Session complete`
  - line 5220: `Completed focus sessions=2`
  - line 5221: `Switched to Break`
  - line 5222: `Break awaiting ack`
- No `Idle screen` occurred after completion. The only `Idle screen` line is
  line 165, from before the test.
- `Pomodoro: Break 300s running=false` persisted for about 333 seconds
  (1,629,233 ms to 1,962,773 ms) until the operator touched the screen. The
  operator confirmed the Break 5:00 screen stayed visible beyond 90 seconds.
- line 6291: `Break acknowledged`, followed by
  `Pomodoro: Break 300s running=true`; the short tap started Break.
- Done was 2 after completion (it was 1 before this run).
- Settings were restored with `Settings saved: focus=50 min brightness=60%`;
  `Settings reset` then produced `Pomodoro: Focus 3000s running=false`.
  The final 10-second capture `20261022T134812Z-d4c2f762` passed with zero I2C
  errors and zero read errors; the operator then left Focus 50 running.

## Evidence

| Directory | What it captured | Result |
| --- | --- | --- |
| `long-break-ack-20261022T210926` | Full Focus 25, completion, no idle for 333 s, acknowledgment | raw log, 181,433 bytes, SHA-256 `64e358046bd9a899a3587c3229aacf12454ff8b2bfa1abbe1044714c83ddcaca` |
| `20261022T134450Z-70aa0797` | Restore Save/Reset after the test | raw log; capture manually stopped after restore |
| `20261022T134812Z-d4c2f762` | Final state check | `PASS_SERIAL_SMOKE`, I2C 0, read errors 0 |

## Boundaries

- One full 25-minute Focus run verified; this is not a multi-hour idle or
  LCD burn-in test.
- The Break screen now remains until the first touch by design. If the device
  is left alone after Focus completion, it will stay on Break 5:00 / PAUSE.
- No release tag was created; the displayed version remains `v1.6`.

## Related Records

- [C manual checks](2026-10-22-t08-manual-reset-stats-nvs-focus.md)
- [Roadmap B07](../roadmap.md)

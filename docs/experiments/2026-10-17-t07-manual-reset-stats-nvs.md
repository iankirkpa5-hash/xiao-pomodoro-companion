# Manual Reset / Stats / NVS Persistence Checks

Date: 2026-10-17 (Asia/Shanghai).
Scope: close the remaining T03 manual items on the post-debounce firmware.
No source changes were made for this record; all checks were capture-only plus
operator observation.

## Reset

- Operator started a session, entered Settings, and tapped Reset (item 2).
- Serial: `Settings reset`, followed by `Pomodoro: Focus 2700s running=false`
  (full 45 minutes, not running).
- Screen: back on the home screen with the timer at 45:00 and not running.
  Operator confirmed.

## Stats

- Serial: `Settings stats cleared`.
- Screen: Done went to 0 and stayed 0 across later Settings visits and the
  power cycle. Operator confirmed.
- The increment path (Done +1 when a focus session completes) was not retested;
  it requires a full focus session and remains covered by daily use.

## NVS Persistence

- Serial: `Settings saved: focus=45 min brightness=30%` — a fresh write of
  focus 45 min and brightness 30%.
- Power cycle: USB unplugged for about five seconds, then reconnected.
  After reboot the Settings screen still shows Focus 45, Bright 30, Done 0.
  Operator confirmed.
- Post-reboot capture `20261017T143016Z-f67e53aa`: `PASS_SERIAL_SMOKE`, zero
  I2C errors, zero read errors, timer steady at 2700 s.

## Evidence Directories

| Directory | What it captured |
| --- | --- |
| `20261017T142305Z-ffec29ae` | First 90 s operation capture (session start, Settings navigation) |
| `20261017T142511Z-80769fd3` | 60 s continuation: `Settings stats cleared`, `Settings reset` |
| `20261017T143016Z-f67e53aa` | Post-reboot steady state plus `Settings saved` line |

Serial log SHA-256 values:

```text
ffec29ae C20923783D77EBAA57EA3B968A8196906F9A8007199E99469FE9A2D71E206891
80769fd3 7F9F5E9AAF932DDFDEBC31AD3601CCEA87D3B24C1C6ED90E462714322EEA87A8
f67e53aa ACD7E70FB58E62DE1F32132039BE8668A51E4514B542F7C102C0A095E22A495D
```

## Remaining

PR #1 review and merge. All sound-group, screen-sync, and manual items on the
local branch are now recorded.

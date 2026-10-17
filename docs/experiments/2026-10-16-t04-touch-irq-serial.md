# Touch IRQ Gate and Serial Retest

Date: 2026-10-16 (Asia/Shanghai).
Scope: one evidence-led T04 fix, with a real T03 build/upload/capture cycle.

## Result

Adding the active-low touch IRQ check removed the repeated I2C errors and mixed
microphone lines in the two post-upload observations. The user also confirmed
Start/Pause, long-press Settings, item switching, Bright, and Save worked.

This is a bounded fix and serial smoke-test result, not a new stable release or
completion of all sound tests. The original T01/T02 FAILED reports are unchanged.
No commit, tag, PR merge, or push was performed.

## Evidence and Minimal Change

The [T02 comparison](2026-10-14-t02-sound-comparison.md) found microphone payloads
inside I2C error lines. Before editing, another 30-second capture reproduced
222 I2C errors, 19 embedded payload candidates and a three-second valid-report gap.

The old `readTouch()` called `Wire.requestFrom(0x2e, 5)` on every loop, despite
already configuring GPIO44 as `INPUT_PULLUP`. It never checked that pin.
Seeed's [official Round Display driver](https://github.com/Seeed-Studio/Seeed_Arduino_RoundDisplay/blob/main/src/lv_xiao_round_screen.h)
checks the active-low D7 touch interrupt before requesting coordinates. The
[hardware documentation](https://wiki.seeedstudio.com/seeedstudio_round_display_usage/)
also assigns D7 to touch interrupt and D4/D5 to I2C.

Only `src/input.cpp` changed in this firmware step:

```cpp
if (digitalRead(TOUCH_INT) != LOW) {
  return false;
}
```

The existing main-loop event evaluation still runs when no contact is present,
so the release timer is not skipped. No new delays, pins, touch thresholds,
gesture rules, microphone thresholds, UI, Settings, NVS logic, `driver.h`, or
`platformio.ini` changes were introduced. Existing uncommitted microphone work
was preserved.

In the installed Arduino-ESP32 2.0.17 core, `Wire.cpp` reports failed reads via
`log_e`. `esp32-hal-uart.c::log_printfv` sends those messages through `ets_printf`;
`HWCDC.cpp::cdc0_write_char` places individual bytes in the USB ring buffer.
Application `Serial.write` uses its separate write-lock path. This explains how
error text and application output can interleave; an application-only mutex
would not make the core diagnostic byte stream atomic.

This patch avoids the unnecessary idle transactions that provoked the observed
errors. **It does not replace the USB logging implementation or guarantee
framing under every future error.** Errors are not muted, and malformed lines
are still rejected by the existing host tooling. If errors return during actual
touch, investigate that separately rather than accepting mixed data as valid.

## Real-Board Comparison

USB device: COM11, VID:PID `303A:1001`. Only one serial reader at a time.
No controlled acoustic stimulus was requested in these observations.

| Observation | Duration | Valid CSV rows | I2C errors | Mixed payload candidates | Maximum device report gap | Result |
| --- | ---: | ---: | ---: | ---: | ---: | --- |
| Before IRQ gate | 30 s | 41 | 222 | 19 | 3000 ms | FAILED |
| Build/upload/capture after gate | 30 s | 57 | 0 | 0 | 500 ms | PASS_SERIAL_SMOKE |
| Follow-up after manual confirmation | 60 s | 120 | 0 | 0 | 500 ms | PASS_SERIAL_SMOKE |

The first post-upload capture includes one startup banner and startup delay,
hence 57 rather than 60 rows. It is not steady-state missing data. Maximum host
receive gaps were 2.985 s before, 0.515 s after upload, and 0.610 s in follow-up.
The latter's device intervals all remained 500 ms, so host scheduling jitter
must not be labeled a PCM sampling gap.

Microphone diagnostics reported zero read errors throughout. Follow-up processing
was 80,000 samples per 5,000 ms, approximately 16 kHz. No new Touch message or
restart was found in the follow-up, and timer logs showed `running=true` with
decreasing remaining time. These are observations, not proof of indefinite
stability or lossless DMA capture.

The first post-upload sample had a high peak (21319). It occurred at startup
without a controlled sound action; it is not counted as a clap or a clean quiet
sample. A future acoustic retest should allow startup to settle and annotate
actions. T02's quiet outlier is not explained away by this transport fix.

## Operator Confirmation

After upload, the user replied that all requested operations were normal:

- Short press starts/pauses.
- Long press enters Settings.
- Left side changes the selected item.
- Bright adjusts the screen, and Save returns normally.

This reply arrived **before** the 60-second follow-up began. Do not claim those
manual actions were synchronized with the follow-up log; no Touch events were
recorded in that capture. Stats was not requested to be cleared. Reset, Stats,
power-cycle NVS persistence, and the quiet/speech/clap series were not separately
retested this round. Manual Save may persist the user's chosen settings; the
upload did not erase NVS or add storage changes.

## Build and Software Checks

- Standalone PlatformIO build passed; then `cycle` built, uploaded and captured
  successfully, retaining all three stages' results.
- Arduino framework 2.0.17; GFX Library 1.4.7; RAM 19,536 / 327,680 bytes;
  flash 351,425 / 3,342,336 bytes.
- Seven new [input regression test groups](../../tools/input-tests/README.md)
  passed with AddressSanitizer and UndefinedBehaviorSanitizer in WSL. They
  compile the actual `input.cpp`, not a duplicate implementation.
- Eight existing microphone regression groups passed with the same sanitizers.
- Python capture/analysis suite: 26 passed, two POSIX tests skipped on Windows.
- Existing web telemetry parser suite: six passed.

The first clock-wrap test run used an unrealistic zero last-gesture timestamp
while jumping directly to rollover, hitting the existing 300 ms debounce. Its
fixture was corrected to place the previous accepted gesture before rollover;
the firmware's debounce was not changed. These tests do not cover all gesture
edge cases or certify electrical behavior.

The cycle's built firmware SHA-256 is:

```text
deaa519b63afc48ae370614702e8e6f8a3722d460016e0ce0a6040d5f85b3054
```

The upload log reports successful verification. Current firmware sources match
the cycle's recorded source hashes, and only `src/input.cpp` differs between
the pre-change and post-change source maps. The device still does not report
its own unique build hash, so this is host build/upload provenance, not remote
attestation of a running binary.

## Saved Artifacts

Directories under local, Git-ignored `test-runs/`:

| Run | Directory | Files |
| --- | --- | --- |
| Before | `20261016T140009Z-3cfff0c0` | serial.log, mic.csv, report.json |
| Standalone build | `20261016T140659Z-e0813bb0` | build.log, report.json |
| Build/upload/capture | `20261016T141129Z-27706f1a` | build.log, upload.log, serial.log, mic.csv, report.json |
| Follow-up | `20261016T141826Z-5e6605b7` | serial.log, mic.csv, report.json |

Raw-log SHA-256 values, retained here so the exact inputs can be identified:

```text
before:   3da1e4aaa3b53c8878761a28d70d0772868b2044730abcf9696151bb8a3beb81
cycle:    2d46f91fc4fb805a8cce892e5fa7ea7fecaeba6a472326e48dc6bd09af52d1e5
followup: eb685d02b447f55170bc926f03c7c0bb0c90745d6de7ecb2f1b0d2c72a14401d
```

The existing offline analyzer reconciled each CSV against standalone raw lines
and its report, while counting embedded candidates separately. No source log
was changed or stripped of errors. Raw artifacts are not included merely by
cloning the repository.

## Next Step

An evening retest series followed the same day: quiet (first run rejected, then
a clean re-run), speech, and clap captures. The quiet re-run is recorded in the
[T05 quiet record](2026-10-16-t05-quiet-retest.md).

Repeat the quiet / speech / clap captures with the same firmware after startup
settles, with actions and distance recorded. Include a serial capture during
actual Settings use if mixed lines or unexpected touch recur. Keep normal
speech, transient energy and held display labels conceptually separate.

Once that evidence is clear, take the planned short PCM-to-WAV experiment as a
separate step. Do not tune thresholds, introduce AI, or mark PR #1 accepted
solely because these serial smoke tests passed.

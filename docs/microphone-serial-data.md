# Microphone Serial Data: First Step

This experiment adds timestamped feature rows to the v1.6 microphone indicator.
A local [web monitor](../tools/microphone-monitor/README.md) now provides curves,
serial diagnostics, and CSV download to assist device validation. The sound
response and hardware checks below still require observation.

## Browser Test

Run `node tools/microphone-monitor/serve.cjs` from the repository root and open
<http://127.0.0.1:8765> in desktop Edge or Chrome. Click **連接 XIAO**, then choose
the XIAO serial port. The browser reads the same four-column firmware output;
it does not use the computer microphone or change the device's classification.
Close other serial monitors first. Use **中斷連線** to release the port afterward.

## Output

After successful microphone initialization, the serial port prints this header:

```text
timestamp_ms,rms,peak,level
```

Example format only, not a device measurement:

```text
12500,6.2,18,Quiet
```

| Field | Meaning |
| --- | --- |
| `timestamp_ms` | Device uptime when the last complete 10 ms frame in this report was processed |
| `rms` | RMS over all samples in this reporting interval, with DC removed separately from each 10 ms frame; PCM units, one decimal place |
| `peak` | Maximum absolute centered sample across all complete frames since the previous report; PCM units |
| `level` | The same debounced `Quiet`, `Normal`, or `Loud` label used by the normal screen |

Rows are emitted at most once every 500 ms. A dedicated I2S RX task continuously
reads DMA data into complete 160-sample frames at 16 kHz. Partial reads are
retained until a frame is complete. Reads have a bounded wait in that task,
never in the touch/timer/display loop. The task neither prints nor draws.
The driver uses the ESP-IDF I2S API already included in Arduino-ESP32, with no
new external dependency or PlatformIO configuration change. See the official
[ESP-IDF I2S reference](https://docs.espressif.com/projects/esp-idf/en/v4.4.7/esp32s3/api-reference/peripherals/i2s.html).

Each frame contributes its centered sum of squares and maximum absolute value
to a reporting accumulator. Printing takes and clears that accumulator, so a
short peak is not replaced by later quiet frames. Interval RMS is calculated
from total squared energy divided by total sample count, not by averaging RMS
values. RMS and peak describe the same interval, not necessarily the same frame.

Classification thresholds are unchanged. The main-loop label update cadence and
the 600 ms Loud hold remain. Since October 17 the quiet debounce is 15 frames
(about 600 ms), so Normal survives ordinary word gaps instead of flickering
back to Quiet; see the [debounce record](experiments/2026-10-17-t06-quiet-debounce.md). A loud frame seen between
UI updates is retained with its capture timestamp. Printing uses the same cached
label as the screen; it does not reclassify aggregated reporting values.

Timestamps should increase within a normal test run. They restart on reboot and
wrap with the 32-bit `millis()` counter after roughly 49.7 days. They are device
uptime, not wall-clock time. Timestamp gaps can be slightly below 500 ms because
frame completion and print schedules are independent. Loop work can make gaps
longer: the accumulator then covers a longer interval. This is a stream of
audio features, not continuous PCM audio or precise clap timestamps.

The label includes debounce and a Loud hold interval, so a low-energy report may
still carry `Loud` briefly after a loud event. Multiple sounds in one reporting
interval share one peak value. Raw samples lost upstream cannot be recovered by
the accumulator, and classification is not proof of clap recognition.

Every approximately five seconds, `Mic capture:` reports processed frames,
samples, elapsed milliseconds, and cumulative failed bulk reads. The expected
sample rate is near 16,000 samples/second. A zero read-error counter does not by
itself prove there were no DMA overruns or other hardware errors.

Startup, Pomodoro, Settings, and driver diagnostics still share the serial port.
Only a row with four fields, a numeric timestamp, numeric RMS / peak, and one of
the three labels is microphone data. `Mic: no fresh complete PCM frames` is a
diagnostic, not a zero-volume measurement. Plotting tools skip non-data lines.

## Device Check

From the project root, build, upload to the connected XIAO, and open the monitor:

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -t upload
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" device monitor
```

Keep the normal Pomodoro screen visible while comparing labels. Settings, Idle,
and transition screens do not show the microphone label even though sampling
continues.

- [x] All 79 parsed data rows in the initial capture contained the four fields.
- [x] Timestamps increased throughout the initial capture; interval variation is recorded below.
- [x] The user reports normal screen rendering and basic touch operation after upload.
- [ ] Quiet, speech, and clap observations show changing RMS / peak values.
- [ ] Each reported label agrees with the normal screen at that moment.
- [ ] Start / Pause, Settings, Bright, Save, Reset, and Stats still work.
- [x] I2C read errors recurred; their relation to physical actions remains to be checked.

## Initial Device Capture (2026-09-25, Before Peak Retention)

Build and upload to COM11 succeeded. A bounded 45-second serial capture produced:

| Measurement | Observed result |
| --- | --- |
| Valid microphone rows | 79 |
| First / last timestamp | 73764 / 118273 ms |
| Timestamp ordering | Strictly increasing |
| Minimum / maximum sample timestamp gap | 480 / 2010 ms |
| Centered RMS range | 2.6 to 492.6 |
| Centered peak range | 8 to 1338 |
| Reported labels | Quiet: 75, Normal: 1, Loud: 3 |
| Malformed numeric microphone rows | 0 |
| I2C read errors | 332 |

Example measured rows:

```text
73764,7.4,19,Quiet
87264,492.6,1338,Normal
89270,37.5,93,Loud
```

The final example is compatible with the existing Loud hold behavior; the
500 ms reports need not contain the high-energy frame that triggered it.

The recurring diagnostic was:

```text
requestFrom(): i2cRead returned Error -1
```

The input module requests touch data at I2C address `0x2e` on each input update.
It has not been changed by this experiment. The errors were also reported before
this change, but their root cause and contribution to the two-second data gap
have not been established. The capture validates the row format and timestamp
ordering, not a uniform output cadence or overall device stability.

The user subsequently confirmed normal screen rendering and touch operation,
but explicitly has not tested the microphone yet. This does not confirm label
agreement with the serial output or each Settings action. Controlled quiet,
speech, and clap tests remain pending. The three labels seen in the capture
are not evidence that these specific sound tests passed. No release tag or
classifier tuning is part of this step.

## Peak Retention Regression Check (2026-09-26)

The user-provided CSV contained 697 rows across two uptime segments. Median
within-segment spacing was 520 ms and the longest gap was 1,528 ms. Of 57 Loud
rows, 54 had a peak below 1,200. The trace had no physical-action markers, so
individual rows cannot be labeled as claps or speech. This motivated retaining
interval peaks instead of printing only the last frame.

[Host C++ regression tests](../tools/microphone-tests/README.md) compile the actual
microphone implementation with I2S/Arduino stubs. They passed with address and
undefined-behavior sanitizers, including short-peak retention followed by quiet
frames, interval RMS, partial reads, DC removal, stale input, timestamp wrap,
and unchanged threshold boundaries. The six web parser/telemetry tests also
passed. These checks do not replace hardware validation.

An intermediate Arduino `I2S.onReceive` implementation was also built and
uploaded, but measured only about 8,200 processed samples/second despite a
16,000 Hz configuration. It was not accepted as a complete capture fix. The
current implementation instead uses a dedicated ESP-IDF RX task. The original
Arduino wrapper and the new task must not both own I2S0.

A 30-second serial check after the dedicated-RX upload observed:

| Measurement | Observed result |
| --- | --- |
| Valid rows | 56 |
| First / last timestamp | 76392 / 105892 ms, strictly increasing |
| Reporting gaps | 500 to 1000 ms |
| Interval RMS / peak range | 3.8 to 4.3 / 13 to 19 |
| Labels | 56 Quiet, no controlled sound actions marked |
| Capture throughput | 80,000 to 80,160 samples per approximately 5 seconds |
| Failed bulk reads | 0 in all six diagnostics |
| I2C read errors | 222, still unresolved |

Example throughput diagnostic:

```text
Mic capture: frames=500 samples=80000 elapsed_ms=5002 read_errors=0
```

This demonstrates near-16-kHz processing during the check, not guaranteed
lossless capture or successful clap recognition. Serial output still has
occasional longer gaps. The device's sound response and touch/Settings behavior
need the user checks below. The monitor was closed after capture to release COM11.

After upload, repeat this controlled test without unplugging or resetting:

1. Quiet for 10 seconds.
2. Normal speech for 10 seconds.
3. Five claps, each separated by three seconds.
4. Confirm screen, Start/Pause, Settings, Bright, Save, Reset, and Stats.

Record the actions and capture `timestamp_ms,rms,peak,level` alongside
`Mic capture:` diagnostics. Compare clap timing to peak changes, not only the
Loud label. Physical sound response and the interaction regression remain
pending user confirmation for this firmware.

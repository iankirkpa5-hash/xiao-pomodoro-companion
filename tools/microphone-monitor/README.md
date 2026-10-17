# XIAO Microphone Web Monitor

A local, read-only Web Serial monitor for the microphone experiment. It reads
the XIAO USB serial port, not the computer microphone. No libraries, cloud
services, package installation, or new firmware change are required beyond the
existing `timestamp_ms,rms,peak,level` firmware output.

## Run

From the repository root with Node.js installed:

```powershell
node tools/microphone-monitor/serve.cjs
```

Open <http://127.0.0.1:8765> in desktop Edge or Chrome. Click **連接 XIAO** and
select the XIAO USB serial port (currently COM11). Close PlatformIO or other
serial monitors first; only one application can own this port at a time.
The browser asks for permission to access the selected serial device.

The server binds only to `127.0.0.1` and serves only this monitor's static files.
It does not access the serial port or receive measurement data. Browser support
and secure-context checks are shown on the page. See the official
[Web Serial guide](https://developer.chrome.com/docs/capabilities/serial).

## Observe

- Keep the normal Pomodoro screen visible to compare the device's Mic label.
- Observe quiet, normal speech, and spaced claps; check both values and labels.
- RMS and peak use separate automatic chart scales and PCM units, not dB SPL.
- With the peak-retention firmware, RMS summarizes all complete 10 ms frames
  since the previous report, and peak is their maximum. Older firmware reports
  only the last frame; the four-column format alone cannot distinguish them.
- A very large peak expands the automatic scale for up to 60 seconds. This can
  make smaller changes less visible; the numeric readout and CSV retain them.
- The browser preserves the firmware's label, including debounce and Loud hold.
- No fresh sample for two seconds hides the current numbers and shows a stale
  data notice. Chart segments break for device-time gaps over 1.2 seconds.
- I2C diagnostics and malformed microphone rows are counted separately. A
  visible curve does not imply the hardware passed its tests.
- `Mic capture:` diagnostics show processed samples per elapsed interval and
  failed reads. They remain in the log, not the four-column CSV.
- Click **中斷連線** before uploading firmware or using another serial monitor.

The chart holds up to 60 seconds of device sample time. A device timestamp reset
starts a new chart segment. Normal 32-bit `millis()` rollover remains continuous.
Repeated identical timestamps are counted and excluded from fresh measurements.
Reconnects start a new chart segment and retain previously collected CSV rows.

## Save Data

For saved CLI captures (including host receive time), use the separate
[T02 offline comparison](../../docs/experiments/2026-10-14-t02-sound-comparison.md).
It reads files without opening a serial port and preserves gaps. The October 13
logs contain microphone payloads interleaved into I2C error lines; neither live
nor offline curves silently convert those mixed lines into valid measurements.

**下載 CSV** downloads only valid microphone rows with the original four columns.
Data stays in browser memory until downloaded; refreshing or closing the page
discards it. At most 10,000 recent rows are retained for download. The page shows
how many older rows were discarded when this limit is reached. Device restarts
can produce timestamp resets in exported data; timestamps are not wall time.
Only the last 120 serial log lines are displayed. I2C errors are not silently
converted into sound measurements or included as numeric CSV rows.

## Checks

Parser and telemetry tests require only Node.js:

```powershell
node --test tools/microphone-monitor/telemetry.test.mjs
```

The page has also been exercised in a separate headless Edge instance with
simulated serial input: chunk boundaries, malformed lines, I2C diagnostics,
stale data, CSV download, disconnect/reconnect, unplug, chooser cancellation,
and desktop/mobile layout. These software checks do not replace testing the
real XIAO through the browser and comparing its screen.

# Short PCM Recording Experiment

This experiment records up to five seconds from the existing PDM receive task,
downloads the original samples over USB serial, and creates a local WAV. It does
not add AI, speech recognition, network upload, an SD card, or microphone controls
to Settings. It was merged into `main` through PR #2 (`6379372`); the
displayed version stays `v1.6`.

## Record And Listen

Keep the Sense expansion board installed, the camera module removed, and J1/J2
unchanged. Close other serial monitors and disconnect the web monitor before
running this tool. The PC is the only recorder controller; recording does not
start automatically at boot.

From the repository root, with the experimental firmware installed:

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" tools/record_pcm.py --port COM11 --seconds 5 --notes "Normal speech, microphone beside mouth at about 20-30 cm"
```

The tool prints when recording starts and finishes. Downloading takes additional
time after the sound capture. Speak normally with the microphone to the side of
the mouth: direct breath can introduce large low-frequency disturbances. Record
the actual conditions, not just the requested conditions.

Each run creates a new ignored `test-runs/pcm-.../` directory containing:

| File | Meaning |
| --- | --- |
| `audio.pcm` | Bit-exact signed 16-bit little-endian mono samples, 16,000 Hz |
| `audio.wav` | Same samples with a WAV header; no gain or DC removal |
| `serial.log` | Original received bytes, including diagnostics and PCM replies |
| `report.json` | Source hashes, commands, sample count, CRC, timing, retries, and results |

Five seconds contains 80,000 samples / 160,000 PCM bytes. A valid transfer does
not by itself establish good sound quality. Listen for recognizable speech,
normal speed, missing syllables, crackling, and clipping; separately check timer,
touch, Settings, and Bright operation.

The raw WAV can be quiet and DC-biased. Optional listening comparisons preserve
the original file and never change the device's sound-level thresholds:

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" tools/prepare_pcm_preview.py test-runs/pcm-EXAMPLE/audio.wav
```

Replace `pcm-EXAMPLE` with the directory printed by the recorder. The new
`audio-listening-comparison/` directory contains `centered.wav` (whole-record DC
removal) and `highpass-80hz.wav` (additional one-pole high-pass filtering). Both
use the same fixed gain, 16x by default, reduced when necessary for -6 dBFS peak
headroom, plus 10 ms endpoint fades. For quiet recordings, a bounded override is
available:

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" tools/prepare_pcm_preview.py test-runs/pcm-EXAMPLE/audio.wav --gain 20
```

`--gain` accepts 1-32 and is shared by both outputs. The request is still capped
by the same -6 dBFS headroom rule, so the actual gain in `comparison.json` can be
lower than requested (`requested_gain`, `headroom_cap`, `shared_gain`, and
`estimated_peak_dbfs` record the result). A non-default gain writes to
`audio-listening-comparison-gain20/`, and existing outputs are never overwritten.
There is no automatic gain control, silence gate, time stretching, or deletion
of interior samples. Processing parameters and source/output hashes are saved in
`comparison.json`. Louder previews also raise background noise; these are
listening aids, not raw measurements or a claim that a recording defect was
repaired.

A 2026-10-23 matched-gain A/B check on the existing 16x pair measured a
23.73 dB reduction in 0-<80 Hz weighted power (84.15% to 2.88%) and a 1.73 dB
reduction in 80-300 Hz. The operator reported clear speech, no thinning or
artifacts, and preferred the high-pass preview for daily listening. See the
[low-cut comparison](experiments/2026-10-23-lowcut-comparison.md). Merged
through PR #7 (`0b8f0fe`); this is listening evidence only; it does not change
capture, thresholds, or firmware.

Recordings remain local and are ignored by Git. They may contain private speech;
do not publish them with source changes unless deliberately selected.

## Capture And Transfer Boundaries

- The existing I2S task feeds each unmodified 160-sample frame into the recorder.
  There is no second I2S reader and no reinitialization of the microphone.
- Allocate a single bounded PSRAM buffer only after a valid START command.
  Allocation failure returns `NO_PSRAM`; there is no internal-RAM fallback.
- Copying and CRC work occur outside short critical sections. A writer-in-flight
  guard prevents releasing the buffer while the audio task is copying into it.
- Stop at the exact target count. READY data is immutable until RELEASE or
  expiry. Any known microphone read failure fails the recording.
- Only the main loop handles commands and replies. Input work and packet size
  are bounded; output waits for serial TX space instead of streaming a whole
  recording in one blocking operation. Other diagnostics remain enabled.
- Transfer 64-byte hex chunks with session ID, offset, and CRC32. The host retries
  corrupted/timed-out replies, verifies the entire recording CRC, and checks the
  WAV readback before reporting a successful transfer. It never substitutes zero
  samples for missing data.
- START retries with the same ID/duration do not restart a recording. RELEASE is
  idempotent. Host cleanup requests RELEASE; if the host disappears, 60 seconds
  without session activity expires the buffer. Capture itself times out after
  the requested duration plus two seconds.

PSRAM allocation uses the capability-based allocator described in the
[ESP-IDF memory allocation documentation](https://docs.espressif.com/projects/esp-idf/en/v4.4.7/esp32s3/api-reference/system/mem_alloc.html).
The buffer stores copies of received samples; it is not a replacement DMA buffer.

## Serial Protocol V1

Numbers are decimal except CRCs and PCM hex. Lines end with LF (CRLF accepted).
The session ID is a nonzero uint32 generated by the host. Protocol replies begin
with `PCM`; existing telemetry continues independently.

```text
PCM INFO
PCM INFO 1 16000 1 16 5000 64 <free_psram_bytes>

PCM START <id> <duration_ms>
PCM STATUS <id>
PCM STATE <id> <RECORDING|READY|FAILED> <count> <target> <rate> <first_ms> <last_ms> <max_gap_ms> <read_errors> <crc32>

PCM READ <id> <offset_bytes>
PCM DATA <id> <offset_bytes> <pcm_hex> <chunk_crc32>

PCM RELEASE <id>
PCM RELEASED <id>

PCM ERROR <id_or_0> <reason>
```

Firmware accepts 100..5000 ms in 10 ms increments; the CLI exposes 1..5 seconds.
READ offsets must be multiples of 64 and inside the READY buffer. CRC32 uses the
standard reflected IEEE polynomial `0xedb88320`, with initial/final XOR
`0xffffffff`, matching Python `zlib.crc32`.

## Evidence Limits

Frame timestamps are processing times, not hardware sample timestamps. For 500
frames, a span near 4990 ms and no read errors support timely processing, but
cannot prove there was no DMA overrun. The existing driver does not install an
event queue to count `I2S_EVENT_RX_Q_OVF`; see the
[ESP-IDF I2S data-loss notes](https://docs.espressif.com/projects/esp-idf/en/v4.4.7/esp32s3/api-reference/peripherals/i2s.html#how-to-prevent-data-lost).
CRC proves transfer integrity only, not acoustic quality or a lossless physical
capture. There is no on-device unique firmware build ID; use the build/upload
record and source hashes, not `APP_VERSION` alone, to identify this experiment.

See the [October 20 implementation and listening record](experiments/2026-10-20-pcm-wav.md).

## Offline Tests

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" -m unittest discover -s tools/tests -v
```

Recorder tests compile the actual source against host stubs (Linux/WSL):

```sh
g++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined \
  -I tools/microphone-tests/stubs tools/pcm-tests/test.cpp -o /tmp/xiao-pcm-tests
/tmp/xiao-pcm-tests
```

Also run the existing [microphone regression tests](../tools/microphone-tests/README.md).
Host tests do not reproduce ESP32 scheduling, PSRAM timing, or real sound.

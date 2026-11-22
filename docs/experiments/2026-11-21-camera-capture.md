# On-Demand Camera Capture, Live Preview, And High-Res Still Study

Date: 2026-11-21.
Branch: `feature/camera-capture`, based on `main` (`5231dec`).
Status: **Headless real-device verification completed (Round Display detached for repair); Round Display / touch / daily Pomodoro regression pending display return.**

## Hardware And Conditions

- Board: Seeed Studio XIAO ESP32S3 Sense + Sense expansion board + removable OV2640 camera module + added heatsink on the camera/module assembly.
- Round Display: **not connected** (out for repair during this session).
- Power / link: USB-C to Windows host (`COM11`), 115200 baud (ESP32-S3 native USB CDC).
- Ambient conditions: cool indoor room temperature; temperature acceptability below applies to this setup and ambient condition, not an always-on or warm-room certification.
- Firmware configuration: `platformio.ini`, Pomodoro timer/UI logic, touch IRQ gating, and microphone thresholds remain unchanged.

## What Was Built

- **Firmware (`src/camera_capture.h/.cpp`, `src/serial_commands.h/.cpp`, `src/pcm_capture.h/.cpp`, `src/main.cpp`)**:
  - Camera stays completely off at boot (`setup()` never initializes it).
  - Shared non-blocking USB serial command router serving both `PCM` and `CAM` protocols without interfering with each other; `CAM CAPTURE` and `CAM STREAM` return `BUSY` while a `PCM` recording holds a buffer.
  - Single-frame `CAM CAPTURE <id> [QVGA|VGA|SVGA|UXGA]`: uses verified `v1.7-camera-test` pinout and `LEDC_CHANNEL_1` / `LEDC_TIMER_1`, discards 2 warmup frames for OV2640 AEC/AWB settling, copies raw JPEG (up to 256 KiB in PSRAM), and **immediately calls `esp_camera_deinit()`** on both success and every error path.
  - Fast Base64 `CAM DUMP <id>` (120 raw bytes / 160 Base64 chars per line, fitting under ESP32-S3 HWCDC's 256-byte TX ring buffer) alongside chunked hex `CAM READ <id> <offset>` fallback, verified with IEEE CRC32 and JPEG SOI/EOI checks.
  - Bounded live preview `CAM STREAM START <id> [interval_ms] [FAST]` / `PING` / `STOP`: default `10 MHz` single buffer (`fb_count=1`, `CAMERA_GRAB_WHEN_EMPTY`); opt-in `FAST` mode (`20 MHz`, `fb_count=2`, `CAMERA_GRAB_LATEST`). Enforces a 180 s `MAX_TIME` limit and 10 s `WATCHDOG` auto-`deinit`, and reports ESP32-S3 internal die temperature (`temperatureRead()`).
  - Hardware OV2640 tuning `CAM TUNE GET / SET / RESET` (`BRIGHTNESS`, `CONTRAST`, `SATURATION`, `AE`, `WB`, `HMIRROR`, `VFLIP`), passive when idle and applied live while streaming as well as on still captures.
- **Host Tools**:
  - CLI evidence capture: `tools/capture_camera.py` (with `--size QVGA|VGA|SVGA|UXGA`) saving `frame.jpg`, `serial.log`, and `report.json` under ignored `test-runs/`.
  - Local Web Serial viewer: `tools/camera-monitor/` (`127.0.0.1:8766`, zero external dependencies, strict CSP + `Permissions-Policy: serial=(self), camera=(), microphone=()`), supporting live preview, speed selection, high-res still shutter, live OV2640 tuning sliders, display-only rotate/mirror, 6-shot history strip, JPEG download, and bounded timed interval capture (with camera `deinit` between shots).

## Real-Device Observations (Headless, With Heatsink)

1. **Boot without Round Display**: confirmed headless operation without display attached (`TOUCH_INT` `INPUT_PULLUP` kept touch idle; SPI writes did not block boot).
2. **Single-frame & high-res capture**:
   - Verified end-to-end JPEG capture and viewing through `tools/camera-monitor`.
   - Operator confirmed high-resolution still capture (`SVGA` / `UXGA`) is noticeably sharper than `320×240` live preview ("效果很棒，拍的時候比預覽還要清晰").
3. **Live preview frame-rate comparison (QVGA 320×240 JPEG, USB serial Base64 + per-frame IEEE CRC32)**:

| Preview Mode | XCLK | Frame Buffers | Requested Interval | Measured FPS | Notes |
| --- | ---: | --- | ---: | ---: | --- |
| Power-saving | 10 MHz | 1 (`GRAB_WHEN_EMPTY`) | 150 ms | **6.4 FPS** | Dominated by 150 ms wait (~6 ms USB TX) |
| Standard (default) | 10 MHz | 1 (`GRAB_WHEN_EMPTY`) | 0 ms | **6.9 FPS** | Limited by 10 MHz sensor period + single-buffer VSYNC wait |
| Fast benchmark (`FAST`) | 20 MHz | 2 (`GRAB_LATEST`) | 0 ms | **27.8 FPS** | ~4× speedup: 2× from 20 MHz XCLK + ~2× from double buffering |

4. **Thermal observation**:
   - Operator reported temperature was acceptable with the added heatsink in the current cool ambient room temperature.
   - Hardware boundary unchanged: `PWDN` and `RESET` remain `-1`, so `esp_camera_deinit()` stops the driver and XCLK without proving sensor rail power-off. Warm-ambient thermal testing and long-duration testing remain separate.

## Offline And Automated Verification

- Firmware build: `pio run` (`seeed_xiao_esp32s3`) — PASS (RAM 7.5% / 24,720 B, Flash 12.3% / 410,497 B).
- C++ host regression tests (`-Wall -Wextra -Werror -fsanitize=address,undefined`):
  - `tools/camera-tests/test.cpp`: 8/8 groups PASS
  - `tools/pcm-tests/test.cpp`: 5/5 groups PASS
  - `tools/microphone-tests/test.cpp`: 8/8 groups PASS
  - `tools/input-tests/test.cpp`: 7/7 groups PASS
- Node.js tests:
  - `tools/camera-monitor/protocol.test.mjs`: 8/8 PASS
  - `tools/microphone-monitor/telemetry.test.mjs`: 6/6 PASS
- Python unit tests: `python3 -m unittest discover -s tools/tests -v` — 64/64 PASS (including 8 camera tests).

## Remaining Before Merging To Daily-Use `main`

- Re-test with the repaired Round Display attached: display rendering, touch short/long press, Settings / Bright / Reset / Stats / NVS, and concurrent microphone / PCM capture regression.
- Re-check camera warmth under normal/warmer room temperatures if continuous `FAST` preview is used frequently.

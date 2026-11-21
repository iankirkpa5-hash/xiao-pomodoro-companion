# XIAO Camera Web Monitor

A local, zero-dependency Web Serial viewer for the `feature/camera-capture`
firmware. It talks directly to the XIAO ESP32S3 Sense over USB serial (`CAM`
Protocol V1 + `CAM STREAM` live preview) and never opens the computer's built-in
webcam or microphone.

No Round Display is required: you can test the XIAO ESP32S3 Sense with only the
Sense expansion board, camera module, and USB-C cable attached.

## Run

From the repository root with Node.js installed:

```powershell
node tools/camera-monitor/serve.cjs
```

Open <http://127.0.0.1:8766> in desktop Chrome or Edge.

1. Close PlatformIO or other serial monitors first (only one app can own COM11).
2. Click **連接 XIAO** and pick the XIAO USB serial port.
3. Click **開始即時預覽** to stream live 320×240 JPEG frames over USB while
   framing. Use **旋轉** / **鏡像** to orient the preview (display-only; saved
   files keep exact raw device JPEG bytes).
4. Click **拍一張** while previewing to instantly save the current verified live
   frame into the history strip (or click **拍一張** while preview is stopped
   for a one-shot `CAM CAPTURE` with immediate `deinit`).
5. Click **停止預覽** when done so the board immediately runs
   `esp_camera_deinit()`, click **下載 JPEG** to save locally, and click
   **中斷連線** before uploading firmware.

## Safety Boundary

- The server binds only to `127.0.0.1` and serves only static files.
- Live preview has two firmware-enforced auto-deinit safeguards: a **3-minute
  maximum session limit** (`MAX_TIME`) and a **10-second host-ping watchdog**
  (`WATCHDOG`) if the tab closes or disconnects unexpectedly.
- Displayed temperature is the ESP32-S3 internal chip temperature sensor, not a
  direct surface measurement of the OV2640 sensor or heatsink.
- On XIAO ESP32S3 Sense, `PWDN` and `RESET` are `-1`. Calling
  `esp_camera_deinit()` stops the driver and XCLK, but does not prove the sensor
  supply rail is switched off. Check module warmth by touch and unplug USB
  immediately if it reaches Grade 3 heat.

## Offline Checks

```sh
node --test tools/camera-monitor/protocol.test.mjs
```

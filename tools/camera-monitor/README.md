# XIAO Camera Web Monitor

A local, zero-dependency Web Serial viewer for the `feature/camera-capture`
firmware. It talks directly to the XIAO ESP32S3 Sense over USB serial (`CAM`
Protocol V1) and never opens the computer's built-in webcam or microphone.

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
3. Click **拍一張** to request one QVGA JPEG frame. The board initializes the
   camera, captures one frame, immediately runs `esp_camera_deinit()`, transfers
   verified 64-byte CRC32 chunks, and displays the image in the browser.
4. Use **旋轉** / **鏡像** to adjust preview orientation (display-only; downloaded
   files keep the exact raw device JPEG bytes).
5. Click **下載 JPEG** to save the verified image locally, and click **中斷連線**
   before uploading firmware or using another serial tool.

## Safety Boundary

- The server binds only to `127.0.0.1` and serves only static files.
- Captured photos remain in browser memory until downloaded (at most 6 recent
  shots retained).
- On XIAO ESP32S3 Sense, `PWDN` and `RESET` are `-1`. Calling
  `esp_camera_deinit()` stops the driver and XCLK, but does not prove the sensor
  supply rail is switched off. Check module warmth by touch and unplug USB
  immediately if it reaches Grade 3 heat.

## Offline Checks

```sh
node --test tools/camera-monitor/protocol.test.mjs
```

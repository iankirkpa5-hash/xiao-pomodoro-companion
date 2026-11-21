# Camera Capture Regression Tests

These host tests compile the actual `src/camera_capture.cpp`, `src/pcm_capture.cpp`,
and `src/serial_commands.cpp` with Arduino, PSRAM, and `esp_camera` stubs.

They exercise passive `CAM INFO` without camera initialization, XIAO ESP32S3 Sense
pinout and `LEDC_CHANNEL_1` configuration, single-frame JPEG capture with immediate
`esp_camera_deinit()`, chunked hex/CRC32 transfer, idempotent `CAPTURE` / `RELEASE`
retries, guaranteed deinit across all error paths, coexistence and mutual exclusion
with `PCM` commands on the shared USB serial port, buffer expiry across `uint32_t`
rollover, malformed/overlong command rejection, and TX backpressure.

From the repository root on Linux or WSL with g++ installed:

```sh
g++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined \
  -I tools/camera-tests/stubs -I tools/microphone-tests/stubs \
  tools/camera-tests/test.cpp -o /tmp/xiao-camera-tests
/tmp/xiao-camera-tests
```

Host tests do not emulate OV2640 SCCB timing, real JPEG encoding, PSRAM bandwidth,
or camera thermal behavior. Real-device checks must verify the saved JPEG visually
and confirm thermal, display, touch, and microphone behavior separately.

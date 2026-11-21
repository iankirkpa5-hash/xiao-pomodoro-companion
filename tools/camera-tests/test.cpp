#include <cassert>
#include <iostream>
#include <string>
#include <vector>
#include "../../src/pcm_capture.cpp"
#include "../../src/camera_capture.cpp"
#include "../../src/serial_commands.cpp"

std::vector<uint8_t> makeJpeg(size_t length) {
  assert(length >= 4);
  std::vector<uint8_t> data(length);
  data[0] = 0xff;
  data[1] = 0xd8;
  for (size_t i = 2; i + 2 < length; ++i) {
    data[i] = static_cast<uint8_t>((i * 37 + 13) & 0xff);
  }
  data[length - 2] = 0xff;
  data[length - 1] = 0xd9;
  return data;
}

std::string sendRouted(const std::string &text, bool mic_ready = true) {
  Serial.output.clear();
  Serial.input += text + "\n";
  for (int i = 0; i < 100 && (Serial.available() || pcm_capture_has_pending_reply() ||
                              camera_capture_has_pending_reply());
       ++i) {
    serial_commands_service(mic_ready);
  }
  assert(Serial.input.empty());
  return Serial.output;
}

void resetCameraTest() {
  camReleaseCapture();
  releaseRecording();
  fake_camera = FakeCamera{};
  fake_no_psram = false;
  Serial.input.clear();
  Serial.output.clear();
  Serial.tx_space = 256;
  cam_reply_length = cam_reply_offset = 0;
  reply_length = reply_offset = 0;
  sc_line_length = 0;
  sc_discard = false;
  sc_prefix_is_cam = false;
}

int main() {
  resetCameraTest();

  // 1. Passive CAM INFO never initializes the camera and reports verified capabilities.
  const std::string info = sendRouted("CAM INFO");
  assert(info.find("CAM INFO 1 320 240 JPEG 262144 64 ") == 0);
  assert(fake_camera.init_calls == 0 && !fake_camera.initialized);
  assert(fake_allocations == 0);
  std::cout << "PASS passive CAM INFO without camera init\n";

  // 2. Capture once, verify pinout/LEDC1, warmup + immediate deinit, CRC, chunks, idempotent retries & release.
  fake_camera.frame_bytes = makeJpeg(200);
  const uint32_t expected_crc =
      ~camCrcUpdate(0xffffffffU, fake_camera.frame_bytes.data(), fake_camera.frame_bytes.size());
  char expected_crc_hex[16];
  snprintf(expected_crc_hex, sizeof(expected_crc_hex), "%08lx",
           static_cast<unsigned long>(expected_crc));

  const std::string state = sendRouted("CAM CAPTURE 77");
  assert(state.find("CAM STATE 77 READY 200 320 240 4 250 ") == 0);
  assert(state.find(expected_crc_hex) != std::string::npos);
  assert(fake_camera.init_calls == 1 && fake_camera.deinit_calls == 1);
  assert(fake_camera.fb_get_calls == 1 + CAM_WARMUP_FRAMES &&
         fake_camera.fb_return_calls == 1 + CAM_WARMUP_FRAMES);
  assert(!fake_camera.initialized && !fake_camera.outstanding_fb);
  assert(fake_allocations == 1);

  // Verify XIAO ESP32S3 Sense pinout and LEDC_CHANNEL_1 (separate from LCD_BL channel 0).
  const auto &cfg = fake_camera.last_config;
  assert(cfg.ledc_channel == LEDC_CHANNEL_1 && cfg.ledc_timer == LEDC_TIMER_1);
  assert(cfg.pin_xclk == 10 && cfg.pin_sccb_sda == 40 && cfg.pin_sccb_scl == 39);
  assert(cfg.pin_vsync == 38 && cfg.pin_href == 47 && cfg.pin_pclk == 13);
  assert(cfg.pin_pwdn == -1 && cfg.pin_reset == -1);
  assert(cfg.pin_d0 == 15 && cfg.pin_d1 == 17 && cfg.pin_d2 == 18 && cfg.pin_d3 == 16);
  assert(cfg.pin_d4 == 14 && cfg.pin_d5 == 12 && cfg.pin_d6 == 11 && cfg.pin_d7 == 48);
  assert(cfg.xclk_freq_hz == 10000000 && cfg.pixel_format == PIXFORMAT_JPEG);
  assert(cfg.frame_size == FRAMESIZE_QVGA && cfg.fb_location == CAMERA_FB_IN_PSRAM);

  // Idempotent CAPTURE retry must return same state without re-initializing camera.
  assert(sendRouted("CAM CAPTURE 77") == state);
  assert(fake_camera.init_calls == 1 && fake_camera.deinit_calls == 1);
  // Different session ID while occupied returns BUSY.
  assert(sendRouted("CAM CAPTURE 78") == "CAM ERROR 78 BUSY\n");
  // STATUS & READ checks including last partial chunk (200 - 192 = 8 bytes).
  assert(sendRouted("CAM STATUS 77") == state);
  assert(sendRouted("CAM STATUS 78") == "CAM ERROR 78 NO_CAPTURE\n");
  assert(sendRouted("CAM READ 77 0").find("CAM DATA 77 0 ffd8") == 0);
  const std::string last_chunk = sendRouted("CAM READ 77 192");
  assert(last_chunk.find("CAM DATA 77 192 ") == 0);
  assert(last_chunk.find("ffd9 ") != std::string::npos);
  assert(sendRouted("CAM READ 77 1") == "CAM ERROR 77 BAD_OFFSET\n");
  assert(sendRouted("CAM READ 77 200") == "CAM ERROR 77 BAD_OFFSET\n");
  assert(sendRouted("CAM READ 78 0") == "CAM ERROR 78 NO_CAPTURE\n");
  // Wrong session RELEASE rejected while active; matching RELEASE frees PSRAM and is idempotent.
  assert(sendRouted("CAM RELEASE 78") == "CAM ERROR 78 NO_CAPTURE\n");
  assert(sendRouted("CAM RELEASE 77") == "CAM RELEASED 77\n");
  assert(fake_allocations == 0);
  assert(sendRouted("CAM RELEASE 77") == "CAM RELEASED 77\n");
  std::cout << "PASS capture, pinout, immediate deinit, CRC, chunks, retry and release\n";

  // 3. Guaranteed deinit + fb_return + zero leaks on every error path.
  resetCameraTest();
  fake_no_psram = true;
  assert(sendRouted("CAM CAPTURE 10") == "CAM ERROR 10 NO_PSRAM\n");
  assert(fake_camera.init_calls == 0 && fake_allocations == 0);

  resetCameraTest();
  fake_camera.fail_init = true;
  assert(sendRouted("CAM CAPTURE 11") == "CAM ERROR 11 INIT_FAILED\n");
  assert(fake_camera.init_calls == 1 && fake_camera.deinit_calls == 0 && !fake_camera.initialized);

  resetCameraTest();
  fake_camera.fail_fb_get = true;
  assert(sendRouted("CAM CAPTURE 12") == "CAM ERROR 12 CAPTURE_FAILED\n");
  assert(fake_camera.init_calls == 1 && fake_camera.deinit_calls == 1 && !fake_camera.initialized);

  resetCameraTest();
  fake_camera.frame_bytes = {0x00, 0x01, 0x02, 0x03};  // Not JPEG SOI
  assert(sendRouted("CAM CAPTURE 13") == "CAM ERROR 13 BAD_FRAME\n");
  assert(fake_camera.init_calls == 1 && fake_camera.deinit_calls == 1 &&
         fake_camera.fb_return_calls == 1 + CAM_WARMUP_FRAMES);

  resetCameraTest();
  fake_camera.frame_bytes = makeJpeg(16);
  fake_camera.format = PIXFORMAT_RGB565;
  assert(sendRouted("CAM CAPTURE 14") == "CAM ERROR 14 BAD_FRAME\n");
  assert(fake_camera.deinit_calls == 1 && fake_camera.fb_return_calls == 1 + CAM_WARMUP_FRAMES);

  resetCameraTest();
  fake_camera.frame_bytes = makeJpeg(CAM_MAX_FRAME_BYTES + 1);
  assert(sendRouted("CAM CAPTURE 15") == "CAM ERROR 15 FRAME_TOO_LARGE\n");
  assert(fake_camera.deinit_calls == 1 && fake_camera.fb_return_calls == 1 + CAM_WARMUP_FRAMES &&
         fake_allocations == 0);

  resetCameraTest();
  fake_camera.frame_bytes = makeJpeg(128);
  fake_camera.on_fb_get = []() {
    if (fake_camera.fb_get_calls > CAM_WARMUP_FRAMES) fake_no_psram = true;
  };
  assert(sendRouted("CAM CAPTURE 16") == "CAM ERROR 16 NO_PSRAM\n");
  assert(fake_camera.deinit_calls == 1 && fake_camera.fb_return_calls == 1 + CAM_WARMUP_FRAMES &&
         fake_allocations == 0);
  std::cout << "PASS guaranteed deinit and zero leaks across all error paths\n";

  // 4. Coexistence with PCM on the same Serial stream + mutual exclusion during PCM recording.
  resetCameraTest();
  fake_camera.frame_bytes = makeJpeg(100);
  assert(sendRouted("PCM INFO").find("PCM INFO 1 16000 1 16 5000 64 ") == 0);
  assert(sendRouted("PCM START 50 100").find("PCM STATE 50 RECORDING") == 0);
  assert(sendRouted("CAM INFO").find("CAM INFO 1 320 240 JPEG 262144 64 ") == 0);
  assert(sendRouted("CAM CAPTURE 51") == "CAM ERROR 51 BUSY\n");
  assert(fake_camera.init_calls == 0);
  assert(sendRouted("PCM RELEASE 50") == "PCM RELEASED 50\n");
  assert(sendRouted("CAM CAPTURE 51").find("CAM STATE 51 READY 100 ") == 0);
  assert(sendRouted("CAM RELEASE 51") == "CAM RELEASED 51\n");
  assert(fake_allocations == 0);
  std::cout << "PASS serial coexistence and PCM/CAM mutual exclusion\n";

  // 5. Buffer expiry + uint32 millis() rollover + malformed/overlong/backpressure handling.
  resetCameraTest();
  fake_ms = UINT32_MAX - 100;
  fake_camera.frame_bytes = makeJpeg(96);
  assert(sendRouted("CAM CAPTURE 90").find("CAM STATE 90 READY 96 320 240 4 250 ") == 0);
  assert(fake_allocations == 1);
  fake_ms += CAM_RETENTION_MS + 1;
  serial_commands_service(true);
  assert(!camera_capture_is_active() && fake_allocations == 0);

  for (const auto &bad : std::vector<std::string>{
           "CAM", "CAM INFO extra", "CAM CAPTURE 0", "CAM CAPTURE -1",
           "CAM CAPTURE 4294967296", "CAM CAPTURE 1 4K", "CAM UNKNOWN 1",
           "CAM CAPTURE 1\t2", "CAM CAPTURE " + std::string(100, '1')}) {
    assert(sendRouted(bad).find("CAM ERROR") == 0);
    assert(!camera_capture_is_active());
  }

  // Partial command timeout routes CAM prefix error cleanly.
  Serial.input = "CAM CAPTURE ";
  serial_commands_service(true);
  fake_ms += 1001;
  serial_commands_service(true);
  assert(sendRouted("99") == "CAM ERROR 0 BAD_COMMAND\n");

  // TX backpressure holds reply until space is available.
  Serial.output.clear();
  Serial.tx_space = 0;
  Serial.input = "CAM INFO\n";
  serial_commands_service(true);
  assert(camera_capture_has_pending_reply());
  serial_commands_service(true);
  assert(camera_capture_has_pending_reply());
  Serial.tx_space = 256;
  serial_commands_service(true);
  assert(!camera_capture_has_pending_reply());
  assert(Serial.output.find("CAM INFO 1 320 240 JPEG 262144 64 ") == 0);
  std::cout << "PASS expiry, rollover, malformed commands and TX backpressure\n";

  // 6. Live preview streaming: START, Base64 FRAME/FDATA/FEND, PING/PONG, mid-frame STOP, WATCHDOG, MAX_TIME.
  resetCameraTest();
  fake_ms = 1000;
  fake_temp_c = 41.2f;
  fake_camera.frame_bytes = makeJpeg(250);
  assert(sendRouted("CAM STREAM START 200 2001") == "CAM ERROR 200 BAD_INTERVAL\n");
  assert(sendRouted("CAM STREAM START 200 0 TURBO") == "CAM ERROR 200 BAD_COMMAND\n");
  assert(!fake_camera.initialized);
  assert(sendRouted("CAM STREAM START 199 0 FAST") == "CAM STREAMING 199 0 180000 412\n");
  assert(fake_camera.last_config.xclk_freq_hz == 20000000 &&
         fake_camera.last_config.fb_count == 2 &&
         fake_camera.last_config.grab_mode == CAMERA_GRAB_LATEST);
  assert(sendRouted("CAM STREAM STOP 199") == "CAM STOPPED 199 HOST\n");
  fake_camera.init_calls = fake_camera.deinit_calls = 0;
  const std::string started = sendRouted("CAM STREAM START 200 150");
  assert(fake_camera.last_config.xclk_freq_hz == 10000000 &&
         fake_camera.last_config.fb_count == 1 &&
         fake_camera.last_config.grab_mode == CAMERA_GRAB_WHEN_EMPTY);
  assert(started == "CAM STREAMING 200 150 180000 412\n");
  assert(fake_camera.initialized && fake_camera.init_calls == 1 && fake_camera.deinit_calls == 0);
  // Single capture rejected while streaming.
  assert(sendRouted("CAM CAPTURE 201") == "CAM ERROR 201 BUSY\n");
  // Advance past settle delay and pump one full frame (250 bytes -> 3 FDATA chunks + FEND).
  fake_ms += CAM_SETTLE_DELAY_MS;
  Serial.output.clear();
  for (int i = 0; i < 10 && Serial.output.find("CAM FEND 200 1\n") == std::string::npos; ++i) {
    serial_commands_service(true);
  }
  assert(Serial.output.find("CAM FRAME 200 1 250 320 240 ") != std::string::npos);
  assert(Serial.output.find("CAM FDATA 200 1 ") != std::string::npos);
  assert(Serial.output.find("CAM FEND 200 1\n") != std::string::npos);
  assert(fake_allocations == 0 && !fake_camera.outstanding_fb);
  // PING refreshes watchdog and reports remaining time + temperature.
  fake_ms += 2000;
  const std::string pong = sendRouted("CAM STREAM PING 200");
  assert(pong.find("CAM PONG 200 ") == 0 && pong.find(" 412\n") != std::string::npos);
  // Start next frame and interrupt mid-frame with STOP -> immediate deinit & zero leak.
  fake_ms += 200;
  Serial.output.clear();
  serial_commands_service(true);  // Emits CAM FRAME 200 2, holds cam_jpeg in PSRAM.
  assert(fake_allocations == 1);
  assert(sendRouted("CAM STREAM STOP 200") == "CAM STOPPED 200 HOST\n");
  assert(!fake_camera.initialized && fake_camera.deinit_calls == 1 && fake_allocations == 0);
  assert(sendRouted("CAM STREAM STOP 200") == "CAM STOPPED 200 IDLE\n");

  // Watchdog auto-deinit after 10s without PING.
  resetCameraTest();
  fake_ms = 5000;
  fake_camera.frame_bytes = makeJpeg(100);
  assert(sendRouted("CAM STREAM START 210 200").find("CAM STREAMING 210 200") == 0);
  fake_ms += CAM_STREAM_WATCHDOG_MS;
  Serial.output.clear();
  serial_commands_service(true);
  serial_commands_service(true);
  assert(Serial.output == "CAM STOPPED 210 WATCHDOG\n");
  assert(!fake_camera.initialized && fake_camera.deinit_calls == 1 && fake_allocations == 0);

  // Max-time auto-deinit after 3 minutes even when PINGing regularly.
  resetCameraTest();
  fake_ms = 10000;
  assert(sendRouted("CAM STREAM START 220 200").find("CAM STREAMING 220 200") == 0);
  for (uint32_t elapsed = 5000; elapsed < CAM_STREAM_MAX_MS; elapsed += 5000) {
    fake_ms = 10000 + elapsed;
    assert(sendRouted("CAM STREAM PING 220").find("CAM PONG 220 ") == 0);
  }
  fake_ms = 10000 + CAM_STREAM_MAX_MS;
  Serial.output.clear();
  serial_commands_service(true);
  serial_commands_service(true);
  assert(Serial.output == "CAM STOPPED 220 MAX_TIME\n");
  assert(!fake_camera.initialized && fake_camera.deinit_calls == 1 && fake_allocations == 0);
  std::cout << "PASS live stream, Base64 chunks, PING, mid-frame STOP, watchdog and max-time deinit\n";

  // 7. High-resolution single capture (SVGA / UXGA) + fast Base64 CAM DUMP.
  resetCameraTest();
  fake_camera.width = 800;
  fake_camera.height = 600;
  fake_camera.frame_bytes = makeJpeg(300);
  assert(sendRouted("CAM CAPTURE 300 BADRES") == "CAM ERROR 300 BAD_SIZE\n");
  assert(fake_camera.init_calls == 0);
  assert(sendRouted("CAM CAPTURE 300 SVGA").find("CAM STATE 300 READY 300 800 600 4 250 ") == 0);
  assert(fake_camera.last_config.frame_size == FRAMESIZE_SVGA && fake_camera.last_config.jpeg_quality == 10);
  assert(!fake_camera.initialized && fake_camera.deinit_calls == 1);
  Serial.output.clear();
  Serial.input = "CAM DUMP 300\n";
  for (int i = 0; i < 20 && Serial.output.find("CAM FEND 300 0\n") == std::string::npos; ++i) {
    serial_commands_service(true);
  }
  assert(Serial.output.find("CAM FRAME 300 0 300 800 600 ") == 0);
  assert(Serial.output.find("CAM FDATA 300 0 ") != std::string::npos);
  assert(Serial.output.find("CAM FEND 300 0\n") != std::string::npos);
  assert(sendRouted("CAM RELEASE 300") == "CAM RELEASED 300\n");
  assert(fake_allocations == 0);

  resetCameraTest();
  fake_camera.width = 1600;
  fake_camera.height = 1200;
  fake_camera.frame_bytes = makeJpeg(400);
  assert(sendRouted("CAM CAPTURE 301 UXGA").find("CAM STATE 301 READY 400 1600 1200 4 250 ") == 0);
  assert(fake_camera.last_config.frame_size == FRAMESIZE_UXGA && fake_camera.last_config.jpeg_quality == 12);
  assert(sendRouted("CAM RELEASE 301") == "CAM RELEASED 301\n");
  assert(fake_allocations == 0);
  std::cout << "PASS high-resolution capture (SVGA/UXGA) and fast Base64 CAM DUMP\n";

  // 8. OV2640 hardware image tuning (passive when idle, applied on capture and live while streaming).
  resetCameraTest();
  cam_tuning = CameraTuning{};
  assert(sendRouted("CAM TUNE GET") == "CAM TUNING 0 0 0 0 0 0 0\n");
  assert(fake_camera.init_calls == 0);
  assert(sendRouted("CAM TUNE SET BRIGHTNESS 2") == "CAM TUNING 2 0 0 0 0 0 0\n");
  assert(sendRouted("CAM TUNE SET AE -1") == "CAM TUNING 2 0 0 -1 0 0 0\n");
  assert(sendRouted("CAM TUNE SET CONTRAST 1") == "CAM TUNING 2 1 0 -1 0 0 0\n");
  assert(sendRouted("CAM TUNE SET SATURATION -2") == "CAM TUNING 2 1 -2 -1 0 0 0\n");
  assert(sendRouted("CAM TUNE SET WB 3") == "CAM TUNING 2 1 -2 -1 3 0 0\n");
  assert(sendRouted("CAM TUNE SET HMIRROR 1") == "CAM TUNING 2 1 -2 -1 3 1 0\n");
  assert(sendRouted("CAM TUNE SET VFLIP 1") == "CAM TUNING 2 1 -2 -1 3 1 1\n");
  assert(sendRouted("CAM TUNE SET BRIGHTNESS 3") == "CAM ERROR 0 BAD_VALUE\n");
  assert(sendRouted("CAM TUNE SET WB 5") == "CAM ERROR 0 BAD_VALUE\n");
  assert(sendRouted("CAM TUNE SET UNKNOWN 1") == "CAM ERROR 0 BAD_VALUE\n");
  assert(fake_camera.init_calls == 0 && fake_camera.tune_apply_calls == 0);

  // Capture applies stored tuning to sensor after init.
  fake_camera.frame_bytes = makeJpeg(160);
  assert(sendRouted("CAM CAPTURE 310 SVGA").find("CAM STATE 310 READY ") == 0);
  assert(fake_camera.brightness == 2 && fake_camera.contrast == 1 &&
         fake_camera.saturation == -2 && fake_camera.ae_level == -1 &&
         fake_camera.wb_mode == 3 && fake_camera.hmirror == 1 && fake_camera.vflip == 1);
  assert(sendRouted("CAM RELEASE 310") == "CAM RELEASED 310\n");

  // Live streaming applies CAM TUNE SET/RESET immediately without restarting stream.
  assert(sendRouted("CAM STREAM START 311 150").find("CAM STREAMING 311 150") == 0);
  const int before_live = fake_camera.tune_apply_calls;
  assert(sendRouted("CAM TUNE SET BRIGHTNESS -2") == "CAM TUNING -2 1 -2 -1 3 1 1\n");
  assert(fake_camera.brightness == -2 && fake_camera.tune_apply_calls > before_live);
  assert(sendRouted("CAM TUNE RESET") == "CAM TUNING 0 0 0 0 0 0 0\n");
  assert(fake_camera.brightness == 0 && fake_camera.contrast == 0 && fake_camera.wb_mode == 0);
  assert(sendRouted("CAM STREAM STOP 311") == "CAM STOPPED 311 HOST\n");
  assert(!fake_camera.initialized && fake_allocations == 0);
  std::cout << "PASS OV2640 CAM TUNE passive storage, capture application, and live stream updates\n";
}

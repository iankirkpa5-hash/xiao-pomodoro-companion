#include "camera_test.h"

#include <Arduino.h>
#include "esp_camera.h"

namespace {
enum class CameraTestMode {
  CaptureOnceAndDeinit,
  IdleHoldInitialized,
  PeriodicCapture,
};

constexpr CameraTestMode CAMERA_TEST_MODE = CameraTestMode::CaptureOnceAndDeinit;
constexpr uint32_t CAMERA_TEST_DURATION_MS = 5UL * 60UL * 1000UL;
constexpr uint32_t CAMERA_TEST_CAPTURE_INTERVAL_MS = 2000;

constexpr int CAMERA_PIN_PWDN = -1;
constexpr int CAMERA_PIN_RESET = -1;
constexpr int CAMERA_PIN_XCLK = 10;
constexpr int CAMERA_PIN_SIOD = 40;
constexpr int CAMERA_PIN_SIOC = 39;

constexpr int CAMERA_PIN_D9 = 48;
constexpr int CAMERA_PIN_D8 = 11;
constexpr int CAMERA_PIN_D7 = 12;
constexpr int CAMERA_PIN_D6 = 14;
constexpr int CAMERA_PIN_D5 = 16;
constexpr int CAMERA_PIN_D4 = 18;
constexpr int CAMERA_PIN_D3 = 17;
constexpr int CAMERA_PIN_D2 = 15;
constexpr int CAMERA_PIN_VSYNC = 38;
constexpr int CAMERA_PIN_HREF = 47;
constexpr int CAMERA_PIN_PCLK = 13;

bool camera_ready = false;
bool camera_test_active = false;
unsigned long camera_test_started_ms = 0;
unsigned long last_capture_ms = 0;
uint32_t captured_frames = 0;

camera_config_t cameraConfig() {
  camera_config_t config = {};
  config.ledc_channel = LEDC_CHANNEL_1;
  config.ledc_timer = LEDC_TIMER_1;
  config.pin_d0 = CAMERA_PIN_D2;
  config.pin_d1 = CAMERA_PIN_D3;
  config.pin_d2 = CAMERA_PIN_D4;
  config.pin_d3 = CAMERA_PIN_D5;
  config.pin_d4 = CAMERA_PIN_D6;
  config.pin_d5 = CAMERA_PIN_D7;
  config.pin_d6 = CAMERA_PIN_D8;
  config.pin_d7 = CAMERA_PIN_D9;
  config.pin_xclk = CAMERA_PIN_XCLK;
  config.pin_pclk = CAMERA_PIN_PCLK;
  config.pin_vsync = CAMERA_PIN_VSYNC;
  config.pin_href = CAMERA_PIN_HREF;
  config.pin_sccb_sda = CAMERA_PIN_SIOD;
  config.pin_sccb_scl = CAMERA_PIN_SIOC;
  config.pin_pwdn = CAMERA_PIN_PWDN;
  config.pin_reset = CAMERA_PIN_RESET;
  config.xclk_freq_hz = 10000000;
  config.pixel_format = PIXFORMAT_JPEG;
  config.frame_size = FRAMESIZE_QVGA;
  config.jpeg_quality = 12;
  config.fb_count = 1;
  config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
  config.fb_location = CAMERA_FB_IN_PSRAM;
  return config;
}

bool initCamera() {
  if (camera_ready) {
    return true;
  }

  camera_config_t config = cameraConfig();
  const esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera test: init failed err=0x%x\n", static_cast<unsigned>(err));
    return false;
  }

  camera_ready = true;
  delay(250);
  return true;
}

void deinitCamera() {
  if (!camera_ready) {
    return;
  }

  esp_camera_deinit();
  camera_ready = false;
  Serial.println("Camera test: deinit");
}

bool captureFrame(const char *prefix) {
  if (!camera_ready) {
    return false;
  }

  camera_fb_t *fb = esp_camera_fb_get();
  if (fb == nullptr) {
    Serial.printf("%s capture failed\n", prefix);
    return false;
  }

  captured_frames++;
  Serial.printf(
      "%s frame=%lu width=%u height=%u len=%u format=%u\n",
      prefix,
      static_cast<unsigned long>(captured_frames),
      static_cast<unsigned>(fb->width),
      static_cast<unsigned>(fb->height),
      static_cast<unsigned>(fb->len),
      static_cast<unsigned>(fb->format));

  esp_camera_fb_return(fb);
  return true;
}
}  // namespace

bool camera_test_capture_once() {
  Serial.println("Camera test: init");
  if (!initCamera()) {
    return false;
  }

  const bool ok = captureFrame("Camera test:");

  if (CAMERA_TEST_MODE == CameraTestMode::CaptureOnceAndDeinit) {
    deinitCamera();
  }

  return ok;
}

void camera_test_begin_configured_mode() {
  if (CAMERA_TEST_MODE == CameraTestMode::IdleHoldInitialized) {
    camera_test_begin_idle_hold();
    return;
  }

  if (CAMERA_TEST_MODE == CameraTestMode::PeriodicCapture) {
    camera_test_begin_periodic_capture();
    return;
  }

  camera_test_capture_once();
}

void camera_test_begin_idle_hold() {
  Serial.println("Camera test: idle hold init");
  if (!initCamera()) {
    return;
  }

  camera_test_active = true;
  camera_test_started_ms = millis();
  last_capture_ms = camera_test_started_ms;
  Serial.println("Camera test: idle hold started for 5 minutes");
}

void camera_test_begin_periodic_capture() {
  Serial.println("Camera test: periodic init");
  if (!initCamera()) {
    return;
  }

  camera_test_active = true;
  camera_test_started_ms = millis();
  last_capture_ms = 0;
  captured_frames = 0;
  Serial.println("Camera test: periodic capture every 2s for 5 minutes");
}

void camera_test_update(unsigned long now) {
  if (!camera_test_active) {
    return;
  }

  if (now - camera_test_started_ms >= CAMERA_TEST_DURATION_MS) {
    camera_test_active = false;
    Serial.printf("Camera test: finished frames=%lu\n", static_cast<unsigned long>(captured_frames));
    deinitCamera();
    return;
  }

  if (CAMERA_TEST_MODE != CameraTestMode::PeriodicCapture) {
    return;
  }

  if (now - last_capture_ms < CAMERA_TEST_CAPTURE_INTERVAL_MS) {
    return;
  }

  last_capture_ms = now;
  captureFrame("Camera test periodic:");
}

#pragma once

#include <Arduino.h>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <vector>

using esp_err_t = int;
constexpr esp_err_t ESP_OK = 0;
constexpr esp_err_t ESP_FAIL = -1;

enum ledc_channel_t { LEDC_CHANNEL_0 = 0, LEDC_CHANNEL_1 = 1 };
enum ledc_timer_t { LEDC_TIMER_0 = 0, LEDC_TIMER_1 = 1 };
enum pixformat_t { PIXFORMAT_RGB565 = 0, PIXFORMAT_JPEG = 4 };
enum framesize_t {
  FRAMESIZE_QVGA = 5,
  FRAMESIZE_VGA = 8,
  FRAMESIZE_SVGA = 9,
  FRAMESIZE_UXGA = 13,
};
enum camera_grab_mode_t { CAMERA_GRAB_WHEN_EMPTY = 0, CAMERA_GRAB_LATEST = 1 };
enum camera_fb_location_t { CAMERA_FB_IN_PSRAM = 0, CAMERA_FB_IN_DRAM = 1 };

struct camera_config_t {
  int pin_pwdn;
  int pin_reset;
  int pin_xclk;
  int pin_sccb_sda;
  int pin_sccb_scl;
  int pin_d7;
  int pin_d6;
  int pin_d5;
  int pin_d4;
  int pin_d3;
  int pin_d2;
  int pin_d1;
  int pin_d0;
  int pin_vsync;
  int pin_href;
  int pin_pclk;
  int xclk_freq_hz;
  ledc_timer_t ledc_timer;
  ledc_channel_t ledc_channel;
  pixformat_t pixel_format;
  framesize_t frame_size;
  int jpeg_quality;
  size_t fb_count;
  camera_fb_location_t fb_location;
  camera_grab_mode_t grab_mode;
};

struct camera_fb_t {
  uint8_t *buf;
  size_t len;
  size_t width;
  size_t height;
  pixformat_t format;
};

inline void delay(unsigned long ms) { fake_ms += static_cast<uint32_t>(ms); }
inline float fake_temp_c = 38.5f;
inline float temperatureRead() { return fake_temp_c; }

struct sensor_t {
  int (*set_contrast)(sensor_t *sensor, int level);
  int (*set_brightness)(sensor_t *sensor, int level);
  int (*set_saturation)(sensor_t *sensor, int level);
  int (*set_ae_level)(sensor_t *sensor, int level);
  int (*set_wb_mode)(sensor_t *sensor, int mode);
  int (*set_hmirror)(sensor_t *sensor, int enable);
  int (*set_vflip)(sensor_t *sensor, int enable);
};

struct FakeCamera {
  bool initialized = false;
  bool outstanding_fb = false;
  bool fail_init = false;
  bool fail_fb_get = false;
  void (*on_fb_get)() = nullptr;
  int init_calls = 0;
  int deinit_calls = 0;
  int fb_get_calls = 0;
  int fb_return_calls = 0;
  int brightness = 0;
  int contrast = 0;
  int saturation = 0;
  int ae_level = 0;
  int wb_mode = 0;
  int hmirror = 0;
  int vflip = 0;
  int tune_apply_calls = 0;
  camera_config_t last_config{};
  std::vector<uint8_t> frame_bytes;
  size_t width = 320;
  size_t height = 240;
  pixformat_t format = PIXFORMAT_JPEG;
  camera_fb_t fb{};
};

inline FakeCamera fake_camera;

inline sensor_t fake_sensor{
    [](sensor_t *, int v) { fake_camera.contrast = v; fake_camera.tune_apply_calls++; return 0; },
    [](sensor_t *, int v) { fake_camera.brightness = v; fake_camera.tune_apply_calls++; return 0; },
    [](sensor_t *, int v) { fake_camera.saturation = v; fake_camera.tune_apply_calls++; return 0; },
    [](sensor_t *, int v) { fake_camera.ae_level = v; fake_camera.tune_apply_calls++; return 0; },
    [](sensor_t *, int v) { fake_camera.wb_mode = v; fake_camera.tune_apply_calls++; return 0; },
    [](sensor_t *, int v) { fake_camera.hmirror = v; fake_camera.tune_apply_calls++; return 0; },
    [](sensor_t *, int v) { fake_camera.vflip = v; fake_camera.tune_apply_calls++; return 0; },
};

inline sensor_t *esp_camera_sensor_get() {
  return fake_camera.initialized ? &fake_sensor : nullptr;
}

inline esp_err_t esp_camera_init(const camera_config_t *config) {
  assert(config != nullptr);
  assert(!fake_camera.initialized);
  fake_camera.init_calls++;
  fake_camera.last_config = *config;
  if (fake_camera.fail_init) return ESP_FAIL;
  fake_camera.initialized = true;
  return ESP_OK;
}

inline esp_err_t esp_camera_deinit() {
  assert(fake_camera.initialized);
  assert(!fake_camera.outstanding_fb);
  fake_camera.deinit_calls++;
  fake_camera.initialized = false;
  return ESP_OK;
}

inline camera_fb_t *esp_camera_fb_get() {
  assert(fake_camera.initialized);
  assert(!fake_camera.outstanding_fb);
  fake_camera.fb_get_calls++;
  if (fake_camera.on_fb_get) fake_camera.on_fb_get();
  if (fake_camera.fail_fb_get) return nullptr;
  fake_camera.fb.buf = fake_camera.frame_bytes.empty() ? nullptr : fake_camera.frame_bytes.data();
  fake_camera.fb.len = fake_camera.frame_bytes.size();
  fake_camera.fb.width = fake_camera.width;
  fake_camera.fb.height = fake_camera.height;
  fake_camera.fb.format = fake_camera.format;
  fake_camera.outstanding_fb = true;
  return &fake_camera.fb;
}

inline void esp_camera_fb_return(camera_fb_t *fb) {
  assert(fb == &fake_camera.fb);
  assert(fake_camera.outstanding_fb);
  fake_camera.fb_return_calls++;
  fake_camera.outstanding_fb = false;
}

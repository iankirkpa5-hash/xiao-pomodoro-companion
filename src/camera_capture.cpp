#include "camera_capture.h"

#include <Arduino.h>
#include <esp_camera.h>
#include <esp_heap_caps.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace {
constexpr uint32_t CAM_PROTOCOL_VERSION = 1;
constexpr uint32_t CAM_DEFAULT_WIDTH = 320;
constexpr uint32_t CAM_DEFAULT_HEIGHT = 240;
constexpr uint32_t CAM_MAX_FRAME_BYTES = 131072;  // 128 KiB bounded PSRAM cap.
constexpr size_t CAM_CHUNK_BYTES = 64;
constexpr uint32_t CAM_RETENTION_MS = 60000;
constexpr uint32_t CAM_SETTLE_DELAY_MS = 250;

// Seeed Studio XIAO ESP32S3 Sense camera pin mapping (verified in v1.7-camera-test).
// Note: PWDN and RESET are not wired to ESP32S3 GPIOs; esp_camera_deinit() stops
// the driver and XCLK, which does not prove sensor rail power-off.
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

enum class CameraState { Idle, Ready };

struct CameraFrame {
  CameraState state = CameraState::Idle;
  uint32_t id = 0;
  uint32_t length = 0;
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t format = 0;
  uint32_t capture_ms = 0;
  uint32_t crc = 0;
};

CameraFrame cam_frame;
uint8_t *cam_jpeg = nullptr;
uint32_t cam_last_activity_ms = 0;
char cam_command[64];
size_t cam_command_length = 0;
bool cam_discard_command = false;
uint32_t cam_last_rx_ms = 0;
char cam_reply[224];
size_t cam_reply_length = 0;
size_t cam_reply_offset = 0;

uint32_t camCrcUpdate(uint32_t crc, const uint8_t *data, size_t count) {
  for (size_t i = 0; i < count; ++i) {
    crc ^= data[i];
    for (int bit = 0; bit < 8; ++bit) {
      crc = (crc >> 1) ^ (0xedb88320U & (0U - (crc & 1U)));
    }
  }
  return crc;
}

void camRespond(const char *format, ...) {
  va_list args;
  va_start(args, format);
  const int length = vsnprintf(cam_reply, sizeof(cam_reply), format, args);
  va_end(args);
  cam_reply_length = length > 0 && static_cast<size_t>(length) < sizeof(cam_reply)
                         ? static_cast<size_t>(length) : 0;
  cam_reply_offset = 0;
}

bool camNumber(const char *text, uint32_t &value) {
  if (!text || !*text) return false;
  uint32_t result = 0;
  for (const char *p = text; *p; ++p) {
    if (*p < '0' || *p > '9') return false;
    const uint32_t digit = static_cast<uint32_t>(*p - '0');
    if (result > (UINT32_MAX - digit) / 10) return false;
    result = result * 10 + digit;
  }
  value = result;
  return true;
}

camera_config_t camConfig() {
  camera_config_t config{};
  // Use LEDC channel/timer 1 so LCD backlight PWM on channel 0 is untouched.
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

void camReleaseCapture() {
  uint8_t *old = cam_jpeg;
  cam_jpeg = nullptr;
  cam_frame = CameraFrame{};
  heap_caps_free(old);
}

void camStatusReply(const CameraFrame &f) {
  camRespond("CAM STATE %lu READY %lu %lu %lu %lu %lu %08lx\n",
             static_cast<unsigned long>(f.id),
             static_cast<unsigned long>(f.length),
             static_cast<unsigned long>(f.width),
             static_cast<unsigned long>(f.height),
             static_cast<unsigned long>(f.format),
             static_cast<unsigned long>(f.capture_ms),
             static_cast<unsigned long>(f.crc));
}

const char *camCaptureSingleFrame(uint32_t id) {
  if (heap_caps_get_free_size(MALLOC_CAP_SPIRAM) == 0) {
    return "NO_PSRAM";
  }

  const uint32_t started_ms = millis();
  camera_config_t config = camConfig();
  if (esp_camera_init(&config) != ESP_OK) {
    return "INIT_FAILED";
  }

  delay(CAM_SETTLE_DELAY_MS);
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    esp_camera_deinit();
    return "CAPTURE_FAILED";
  }

  if (!fb->buf || fb->len < 4 || fb->width == 0 || fb->height == 0 ||
      fb->format != PIXFORMAT_JPEG || fb->buf[0] != 0xff || fb->buf[1] != 0xd8) {
    esp_camera_fb_return(fb);
    esp_camera_deinit();
    return "BAD_FRAME";
  }

  if (fb->len > CAM_MAX_FRAME_BYTES) {
    esp_camera_fb_return(fb);
    esp_camera_deinit();
    return "FRAME_TOO_LARGE";
  }

  uint8_t *allocated = static_cast<uint8_t *>(
      heap_caps_malloc(fb->len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!allocated) {
    esp_camera_fb_return(fb);
    esp_camera_deinit();
    return "NO_PSRAM";
  }

  memcpy(allocated, fb->buf, fb->len);
  const uint32_t length = static_cast<uint32_t>(fb->len);
  const uint32_t width = static_cast<uint32_t>(fb->width);
  const uint32_t height = static_cast<uint32_t>(fb->height);
  const uint32_t format = static_cast<uint32_t>(fb->format);

  esp_camera_fb_return(fb);
  esp_camera_deinit();

  const uint32_t elapsed_ms = static_cast<uint32_t>(millis()) - started_ms;
  const uint32_t crc = ~camCrcUpdate(0xffffffffU, allocated, length);

  cam_jpeg = allocated;
  cam_frame.state = CameraState::Ready;
  cam_frame.id = id;
  cam_frame.length = length;
  cam_frame.width = width;
  cam_frame.height = height;
  cam_frame.format = format;
  cam_frame.capture_ms = elapsed_ms;
  cam_frame.crc = crc;
  cam_last_activity_ms = millis();
  return nullptr;
}

void camHandleCommand(bool pcm_busy) {
  char *save = nullptr;
  const char *prefix = strtok_r(cam_command, " ", &save);
  if (!prefix || strcmp(prefix, "CAM") != 0) return;

  const char *verb = strtok_r(nullptr, " ", &save);
  const char *first = strtok_r(nullptr, " ", &save);
  const char *second = strtok_r(nullptr, " ", &save);
  const char *extra = strtok_r(nullptr, " ", &save);

  if (verb && strcmp(verb, "INFO") == 0 && !first) {
    camRespond("CAM INFO %lu %lu %lu JPEG %lu %lu %lu\n",
               static_cast<unsigned long>(CAM_PROTOCOL_VERSION),
               static_cast<unsigned long>(CAM_DEFAULT_WIDTH),
               static_cast<unsigned long>(CAM_DEFAULT_HEIGHT),
               static_cast<unsigned long>(CAM_MAX_FRAME_BYTES),
               static_cast<unsigned long>(CAM_CHUNK_BYTES),
               static_cast<unsigned long>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
    return;
  }

  uint32_t id = 0;
  uint32_t value = 0;
  if (!verb || !camNumber(first, id) || !id || extra) {
    camRespond("CAM ERROR 0 BAD_COMMAND\n");
    return;
  }

  if (strcmp(verb, "CAPTURE") == 0) {
    if (second) {
      camRespond("CAM ERROR %lu BAD_COMMAND\n", static_cast<unsigned long>(id));
      return;
    }
    if (cam_frame.state != CameraState::Idle) {
      if (cam_frame.id == id) {
        // Retrying a lost CAPTURE reply must not re-initialize the camera.
        cam_last_activity_ms = millis();
        camStatusReply(cam_frame);
      } else {
        camRespond("CAM ERROR %lu BUSY\n", static_cast<unsigned long>(id));
      }
      return;
    }
    if (pcm_busy) {
      camRespond("CAM ERROR %lu BUSY\n", static_cast<unsigned long>(id));
      return;
    }
    const char *error = camCaptureSingleFrame(id);
    if (error) {
      camRespond("CAM ERROR %lu %s\n", static_cast<unsigned long>(id), error);
      return;
    }
    camStatusReply(cam_frame);
    return;
  }

  if (strcmp(verb, "RELEASE") == 0) {
    if (second) {
      camRespond("CAM ERROR %lu BAD_COMMAND\n", static_cast<unsigned long>(id));
      return;
    }
    if (cam_frame.state == CameraState::Idle) {
      // Idempotent so a host can safely retry a lost acknowledgement.
      camRespond("CAM RELEASED %lu\n", static_cast<unsigned long>(id));
      return;
    }
    if (cam_frame.id != id) {
      camRespond("CAM ERROR %lu NO_CAPTURE\n", static_cast<unsigned long>(id));
      return;
    }
    camReleaseCapture();
    camRespond("CAM RELEASED %lu\n", static_cast<unsigned long>(id));
    return;
  }

  if (cam_frame.state != CameraState::Ready || cam_frame.id != id) {
    if ((strcmp(verb, "STATUS") == 0 && !second) || strcmp(verb, "READ") == 0) {
      camRespond("CAM ERROR %lu NO_CAPTURE\n", static_cast<unsigned long>(id));
    } else {
      camRespond("CAM ERROR %lu BAD_COMMAND\n", static_cast<unsigned long>(id));
    }
    return;
  }

  cam_last_activity_ms = millis();
  if (strcmp(verb, "STATUS") == 0 && !second) {
    camStatusReply(cam_frame);
  } else if (strcmp(verb, "READ") == 0) {
    if (!camNumber(second, value) || value >= cam_frame.length || value % CAM_CHUNK_BYTES != 0) {
      camRespond("CAM ERROR %lu BAD_OFFSET\n", static_cast<unsigned long>(id));
      return;
    }
    const size_t count = cam_frame.length - value < CAM_CHUNK_BYTES
                             ? static_cast<size_t>(cam_frame.length - value)
                             : CAM_CHUNK_BYTES;
    const uint8_t *data = cam_jpeg + value;
    char hex[CAM_CHUNK_BYTES * 2 + 1];
    constexpr char digits[] = "0123456789abcdef";
    for (size_t i = 0; i < count; ++i) {
      hex[2 * i] = digits[data[i] >> 4];
      hex[2 * i + 1] = digits[data[i] & 15];
    }
    hex[2 * count] = 0;
    camRespond("CAM DATA %lu %lu %s %08lx\n",
               static_cast<unsigned long>(id),
               static_cast<unsigned long>(value),
               hex,
               static_cast<unsigned long>(~camCrcUpdate(0xffffffffU, data, count)));
  } else {
    camRespond("CAM ERROR %lu BAD_COMMAND\n", static_cast<unsigned long>(id));
  }
}
}  // namespace

void camera_capture_poll() {
  const uint32_t now = millis();
  if (cam_frame.state != CameraState::Idle && now - cam_last_activity_ms > CAM_RETENTION_MS) {
    camReleaseCapture();
  }

  if (cam_reply_length) {
    const size_t remaining = cam_reply_length - cam_reply_offset;
    if (Serial.availableForWrite() >= static_cast<int>(remaining)) {
      cam_reply_offset += Serial.write(
          reinterpret_cast<const uint8_t *>(cam_reply) + cam_reply_offset, remaining);
      if (cam_reply_offset == cam_reply_length) cam_reply_length = cam_reply_offset = 0;
    }
  }
}

bool camera_capture_has_pending_reply() {
  return cam_reply_length != 0;
}

bool camera_capture_is_active() {
  return cam_frame.state != CameraState::Idle;
}

bool camera_capture_handle_command(const char *line, bool pcm_busy) {
  if (!line || strncmp(line, "CAM", 3) != 0 || (line[3] != '\0' && line[3] != ' ')) {
    return false;
  }
  const size_t len = strlen(line);
  if (len >= sizeof(cam_command)) {
    camRespond("CAM ERROR 0 BAD_COMMAND\n");
    return true;
  }
  memcpy(cam_command, line, len + 1);
  camHandleCommand(pcm_busy);
  return true;
}

void camera_capture_reject_command() {
  camRespond("CAM ERROR 0 BAD_COMMAND\n");
}

void camera_capture_service(bool pcm_busy) {
  camera_capture_poll();
  if (cam_reply_length) return;

  const uint32_t now = millis();
  if (cam_command_length && now - cam_last_rx_ms > 1000) {
    cam_command_length = 0;
    cam_discard_command = true;
  }
  for (size_t budget = 0; budget < 32 && Serial.available(); ++budget) {
    const int c = Serial.read();
    cam_last_rx_ms = now;
    if (c == '\n') {
      if (cam_discard_command) camera_capture_reject_command();
      else {
        cam_command[cam_command_length] = 0;
        camHandleCommand(pcm_busy);
      }
      cam_command_length = 0;
      cam_discard_command = false;
      return;
    }
    if (c == '\r') continue;
    if (c < 32 || c > 126 || cam_command_length + 1 >= sizeof(cam_command)) {
      cam_discard_command = true;
    }
    if (!cam_discard_command) cam_command[cam_command_length++] = static_cast<char>(c);
  }
}

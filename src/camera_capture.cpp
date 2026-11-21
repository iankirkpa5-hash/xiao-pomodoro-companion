#include "camera_capture.h"

#include <Arduino.h>
#include <esp_camera.h>
#include <esp_heap_caps.h>
#include <math.h>
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
constexpr size_t CAM_STREAM_CHUNK_BYTES = 120;    // 120 raw bytes -> 160 Base64 chars (< 224 B TX).
constexpr uint32_t CAM_RETENTION_MS = 60000;
constexpr uint32_t CAM_SETTLE_DELAY_MS = 250;
constexpr uint32_t CAM_STREAM_MIN_INTERVAL_MS = 100;
constexpr uint32_t CAM_STREAM_MAX_INTERVAL_MS = 2000;
constexpr uint32_t CAM_STREAM_MAX_MS = 180000;       // 3-minute thermal safety cap.
constexpr uint32_t CAM_STREAM_WATCHDOG_MS = 10000;   // Auto-deinit if host stops pinging.

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

enum class CameraState { Idle, Ready, Streaming };

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

uint32_t cam_stream_started_ms = 0;
uint32_t cam_stream_last_ping_ms = 0;
uint32_t cam_stream_next_frame_ms = 0;
uint32_t cam_stream_interval_ms = 150;
uint32_t cam_stream_seq = 0;
uint32_t cam_stream_offset = 0;
bool cam_stream_hw_ready = false;

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

void camBase64Encode(const uint8_t *src, size_t len, char *dst) {
  static constexpr char kTable[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  size_t out = 0;
  for (size_t i = 0; i < len; i += 3) {
    const uint32_t a = src[i];
    const uint32_t b = i + 1 < len ? src[i + 1] : 0;
    const uint32_t c = i + 2 < len ? src[i + 2] : 0;
    const uint32_t triple = (a << 16) | (b << 8) | c;
    dst[out++] = kTable[(triple >> 18) & 0x3f];
    dst[out++] = kTable[(triple >> 12) & 0x3f];
    dst[out++] = (i + 1 < len) ? kTable[(triple >> 6) & 0x3f] : '=';
    dst[out++] = (i + 2 < len) ? kTable[triple & 0x3f] : '=';
  }
  dst[out] = 0;
}

int32_t camReadTempDeciC() {
  const float t = temperatureRead();
  if (!isfinite(t) || t < -60.0f || t > 160.0f) return -9990;
  return static_cast<int32_t>(lroundf(t * 10.0f));
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

bool camFlushReply() {
  if (!cam_reply_length) return false;
  const size_t remaining = cam_reply_length - cam_reply_offset;
  if (Serial.availableForWrite() >= static_cast<int>(remaining)) {
    cam_reply_offset += Serial.write(
        reinterpret_cast<const uint8_t *>(cam_reply) + cam_reply_offset, remaining);
    if (cam_reply_offset == cam_reply_length) cam_reply_length = cam_reply_offset = 0;
    return true;
  }
  return false;
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

camera_config_t camConfig(int jpeg_quality = 12) {
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
  config.jpeg_quality = jpeg_quality;
  config.fb_count = 1;
  config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
  config.fb_location = CAMERA_FB_IN_PSRAM;
  return config;
}

void camReleaseCapture() {
  uint8_t *old = cam_jpeg;
  cam_jpeg = nullptr;
  cam_frame = CameraFrame{};
  cam_stream_offset = 0;
  cam_stream_seq = 0;
  heap_caps_free(old);
  if (cam_stream_hw_ready) {
    esp_camera_deinit();
    cam_stream_hw_ready = false;
  }
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
  camera_config_t config = camConfig(12);
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

void camHandleStreamCommand(const char *action, const char *id_text,
                            const char *param_text, bool pcm_busy) {
  uint32_t id = 0;
  if (!action || !camNumber(id_text, id) || !id) {
    camRespond("CAM ERROR 0 BAD_COMMAND\n");
    return;
  }

  if (strcmp(action, "START") == 0) {
    uint32_t interval_ms = 150;
    if (param_text) {
      if (!camNumber(param_text, interval_ms) ||
          interval_ms < CAM_STREAM_MIN_INTERVAL_MS ||
          interval_ms > CAM_STREAM_MAX_INTERVAL_MS) {
        camRespond("CAM ERROR %lu BAD_INTERVAL\n", static_cast<unsigned long>(id));
        return;
      }
    }
    if (cam_frame.state == CameraState::Streaming && cam_frame.id == id) {
      cam_stream_last_ping_ms = millis();
      camRespond("CAM STREAMING %lu %lu %lu %ld\n",
                 static_cast<unsigned long>(id),
                 static_cast<unsigned long>(cam_stream_interval_ms),
                 static_cast<unsigned long>(CAM_STREAM_MAX_MS),
                 static_cast<long>(camReadTempDeciC()));
      return;
    }
    if (cam_frame.state != CameraState::Idle || pcm_busy) {
      camRespond("CAM ERROR %lu BUSY\n", static_cast<unsigned long>(id));
      return;
    }
    if (heap_caps_get_free_size(MALLOC_CAP_SPIRAM) == 0) {
      camRespond("CAM ERROR %lu NO_PSRAM\n", static_cast<unsigned long>(id));
      return;
    }
    camera_config_t config = camConfig(18);
    if (esp_camera_init(&config) != ESP_OK) {
      camRespond("CAM ERROR %lu INIT_FAILED\n", static_cast<unsigned long>(id));
      return;
    }
    const uint32_t now = millis();
    cam_stream_hw_ready = true;
    cam_frame = CameraFrame{};
    cam_frame.state = CameraState::Streaming;
    cam_frame.id = id;
    cam_stream_started_ms = now;
    cam_stream_last_ping_ms = now;
    cam_stream_next_frame_ms = now + CAM_SETTLE_DELAY_MS;
    cam_stream_interval_ms = interval_ms;
    cam_stream_seq = 0;
    cam_stream_offset = 0;
    camRespond("CAM STREAMING %lu %lu %lu %ld\n",
               static_cast<unsigned long>(id),
               static_cast<unsigned long>(cam_stream_interval_ms),
               static_cast<unsigned long>(CAM_STREAM_MAX_MS),
               static_cast<long>(camReadTempDeciC()));
    return;
  }

  if (param_text) {
    camRespond("CAM ERROR %lu BAD_COMMAND\n", static_cast<unsigned long>(id));
    return;
  }

  if (strcmp(action, "PING") == 0) {
    if (cam_frame.state != CameraState::Streaming || cam_frame.id != id) {
      camRespond("CAM ERROR %lu NO_STREAM\n", static_cast<unsigned long>(id));
      return;
    }
    const uint32_t now = millis();
    cam_stream_last_ping_ms = now;
    const uint32_t elapsed = now - cam_stream_started_ms;
    const uint32_t remaining = elapsed < CAM_STREAM_MAX_MS ? CAM_STREAM_MAX_MS - elapsed : 0;
    camRespond("CAM PONG %lu %lu %ld\n",
               static_cast<unsigned long>(id),
               static_cast<unsigned long>(remaining),
               static_cast<long>(camReadTempDeciC()));
    return;
  }

  if (strcmp(action, "STOP") == 0) {
    if (cam_frame.state == CameraState::Idle) {
      camRespond("CAM STOPPED %lu IDLE\n", static_cast<unsigned long>(id));
      return;
    }
    if (cam_frame.state != CameraState::Streaming || cam_frame.id != id) {
      camRespond("CAM ERROR %lu NO_STREAM\n", static_cast<unsigned long>(id));
      return;
    }
    camReleaseCapture();
    camRespond("CAM STOPPED %lu HOST\n", static_cast<unsigned long>(id));
    return;
  }

  camRespond("CAM ERROR %lu BAD_COMMAND\n", static_cast<unsigned long>(id));
}

void camStepStream(uint32_t now) {
  const uint32_t id = cam_frame.id;
  if (now - cam_stream_started_ms >= CAM_STREAM_MAX_MS) {
    camReleaseCapture();
    camRespond("CAM STOPPED %lu MAX_TIME\n", static_cast<unsigned long>(id));
    return;
  }
  if (now - cam_stream_last_ping_ms >= CAM_STREAM_WATCHDOG_MS) {
    camReleaseCapture();
    camRespond("CAM STOPPED %lu WATCHDOG\n", static_cast<unsigned long>(id));
    return;
  }
  // Yield to incoming serial commands (e.g. STOP / PING) before queueing more stream TX.
  if (Serial.available()) {
    return;
  }

  if (cam_jpeg) {
    if (cam_stream_offset < cam_frame.length) {
      const size_t chunk = cam_frame.length - cam_stream_offset < CAM_STREAM_CHUNK_BYTES
                               ? static_cast<size_t>(cam_frame.length - cam_stream_offset)
                               : CAM_STREAM_CHUNK_BYTES;
      char b64[((CAM_STREAM_CHUNK_BYTES + 2) / 3) * 4 + 1];
      camBase64Encode(cam_jpeg + cam_stream_offset, chunk, b64);
      cam_stream_offset += static_cast<uint32_t>(chunk);
      camRespond("CAM FDATA %lu %lu %s\n",
                 static_cast<unsigned long>(id),
                 static_cast<unsigned long>(cam_stream_seq),
                 b64);
      camFlushReply();
      return;
    }
    uint8_t *old = cam_jpeg;
    cam_jpeg = nullptr;
    heap_caps_free(old);
    cam_stream_offset = 0;
    cam_stream_next_frame_ms = now + cam_stream_interval_ms;
    camRespond("CAM FEND %lu %lu\n",
               static_cast<unsigned long>(id),
               static_cast<unsigned long>(cam_stream_seq));
    camFlushReply();
    return;
  }

  if (static_cast<int32_t>(now - cam_stream_next_frame_ms) < 0) {
    return;
  }

  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    cam_stream_next_frame_ms = now + cam_stream_interval_ms;
    return;
  }
  if (!fb->buf || fb->len < 4 || fb->len > CAM_MAX_FRAME_BYTES ||
      fb->width == 0 || fb->height == 0 || fb->format != PIXFORMAT_JPEG ||
      fb->buf[0] != 0xff || fb->buf[1] != 0xd8) {
    esp_camera_fb_return(fb);
    cam_stream_next_frame_ms = now + cam_stream_interval_ms;
    return;
  }

  uint8_t *allocated = static_cast<uint8_t *>(
      heap_caps_malloc(fb->len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!allocated) {
    esp_camera_fb_return(fb);
    cam_stream_next_frame_ms = now + cam_stream_interval_ms;
    return;
  }

  memcpy(allocated, fb->buf, fb->len);
  cam_frame.length = static_cast<uint32_t>(fb->len);
  cam_frame.width = static_cast<uint32_t>(fb->width);
  cam_frame.height = static_cast<uint32_t>(fb->height);
  cam_frame.format = static_cast<uint32_t>(fb->format);
  esp_camera_fb_return(fb);

  cam_jpeg = allocated;
  cam_stream_offset = 0;
  cam_stream_seq++;
  cam_frame.crc = ~camCrcUpdate(0xffffffffU, cam_jpeg, cam_frame.length);
  camRespond("CAM FRAME %lu %lu %lu %lu %lu %08lx %ld\n",
             static_cast<unsigned long>(id),
             static_cast<unsigned long>(cam_stream_seq),
             static_cast<unsigned long>(cam_frame.length),
             static_cast<unsigned long>(cam_frame.width),
             static_cast<unsigned long>(cam_frame.height),
             static_cast<unsigned long>(cam_frame.crc),
             static_cast<long>(camReadTempDeciC()));
  camFlushReply();
}

void camHandleCommand(bool pcm_busy) {
  char *save = nullptr;
  const char *prefix = strtok_r(cam_command, " ", &save);
  if (!prefix || strcmp(prefix, "CAM") != 0) return;

  const char *verb = strtok_r(nullptr, " ", &save);
  const char *first = strtok_r(nullptr, " ", &save);
  const char *second = strtok_r(nullptr, " ", &save);
  const char *third = strtok_r(nullptr, " ", &save);
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

  if (verb && strcmp(verb, "STREAM") == 0) {
    if (extra) {
      camRespond("CAM ERROR 0 BAD_COMMAND\n");
      return;
    }
    camHandleStreamCommand(first, second, third, pcm_busy);
    return;
  }

  uint32_t id = 0;
  uint32_t value = 0;
  if (!verb || !camNumber(first, id) || !id || third || extra) {
    camRespond("CAM ERROR 0 BAD_COMMAND\n");
    return;
  }

  if (strcmp(verb, "CAPTURE") == 0) {
    if (second) {
      camRespond("CAM ERROR %lu BAD_COMMAND\n", static_cast<unsigned long>(id));
      return;
    }
    if (cam_frame.state != CameraState::Idle) {
      if (cam_frame.state == CameraState::Ready && cam_frame.id == id) {
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
    if (cam_frame.state != CameraState::Ready || cam_frame.id != id) {
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
  if (cam_frame.state == CameraState::Ready && now - cam_last_activity_ms > CAM_RETENTION_MS) {
    camReleaseCapture();
  }

  if (cam_reply_length) {
    camFlushReply();
    return;
  }

  if (cam_frame.state == CameraState::Streaming) {
    camStepStream(now);
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

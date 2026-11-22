#include "serial_commands.h"

#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "camera_capture.h"
#include "pcm_capture.h"

namespace {
char sc_line[64];
size_t sc_line_length = 0;
bool sc_discard = false;
bool sc_prefix_is_cam = false;
uint32_t sc_last_rx_ms = 0;

bool startsWithCam(const char *buffer, size_t length) {
  return length >= 3 && strncmp(buffer, "CAM", 3) == 0 &&
         (length == 3 || buffer[3] == ' ');
}
}  // namespace

void serial_commands_service(bool microphone_ready) {
  pcm_capture_poll();
  camera_capture_poll();
  if (pcm_capture_has_pending_reply() || camera_capture_has_pending_reply()) {
    return;
  }

  const uint32_t now = millis();
  if (sc_line_length && now - sc_last_rx_ms > 1000) {
    sc_prefix_is_cam = startsWithCam(sc_line, sc_line_length);
    sc_line_length = 0;
    sc_discard = true;
  }

  for (size_t budget = 0; budget < 32 && Serial.available(); ++budget) {
    const int c = Serial.read();
    sc_last_rx_ms = now;
    if (c == '\n') {
      if (sc_discard) {
        if (sc_prefix_is_cam) camera_capture_reject_command();
        else pcm_capture_reject_command();
      } else {
        sc_line[sc_line_length] = 0;
        if (!pcm_capture_handle_command(sc_line, microphone_ready)) {
          camera_capture_handle_command(sc_line, pcm_capture_is_active());
        }
      }
      sc_line_length = 0;
      sc_discard = false;
      sc_prefix_is_cam = false;
      return;
    }
    if (c == '\r') continue;
    if (c < 32 || c > 126 || sc_line_length + 1 >= sizeof(sc_line)) {
      if (!sc_discard) {
        sc_prefix_is_cam = startsWithCam(sc_line, sc_line_length);
      }
      sc_discard = true;
    }
    if (!sc_discard) sc_line[sc_line_length++] = static_cast<char>(c);
  }
}

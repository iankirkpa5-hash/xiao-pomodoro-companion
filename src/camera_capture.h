#pragma once

#include <stddef.h>
#include <stdint.h>

// On-demand single-frame JPEG capture over USB serial.
// Never initializes the camera at boot; always deinitializes immediately after capture.
void camera_capture_poll();
bool camera_capture_has_pending_reply();
bool camera_capture_is_active();
bool camera_capture_handle_command(const char *line, bool pcm_busy);
void camera_capture_reject_command();
void camera_capture_service(bool pcm_busy);

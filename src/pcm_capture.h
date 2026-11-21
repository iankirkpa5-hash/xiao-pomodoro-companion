#pragma once

#include <stddef.h>
#include <stdint.h>

// Called only by the existing I2S reader, with complete, unmodified PCM frames.
void pcm_capture_feed(const int16_t *samples, size_t count, uint32_t now);
void pcm_capture_read_error();
// Main-loop command handling and bounded USB output; never reads I2S.
void pcm_capture_service(bool microphone_ready);
void pcm_capture_poll();
bool pcm_capture_has_pending_reply();
bool pcm_capture_is_active();
bool pcm_capture_handle_command(const char *line, bool microphone_ready);
void pcm_capture_reject_command();

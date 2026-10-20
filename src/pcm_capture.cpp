#include "pcm_capture.h"

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace {
constexpr uint32_t SAMPLE_RATE = 16000;
constexpr uint32_t MAX_MS = 5000;
constexpr uint32_t RETENTION_MS = 60000;
constexpr size_t CHUNK_BYTES = 64;
enum class State { Idle, Recording, Ready, Failed };

struct Recording {
  State state = State::Idle;
  uint32_t id = 0, target = 0, count = 0;
  uint32_t requested_ms = 0, first_ms = 0, last_ms = 0, max_gap_ms = 0;
  uint32_t errors = 0, crc = 0xffffffffU;
};
portMUX_TYPE pcm_mux = portMUX_INITIALIZER_UNLOCKED;
Recording recording;
int16_t *pcm = nullptr;
bool writer_active = false;
uint32_t last_activity_ms = 0;
char command[64];
size_t command_length = 0;
bool discard_command = false;
uint32_t last_rx_ms = 0;
char reply[224];
size_t reply_length = 0, reply_offset = 0;

uint32_t crcUpdate(uint32_t crc, const uint8_t *data, size_t count) {
  for (size_t i = 0; i < count; ++i) {
    crc ^= data[i];
    for (int bit = 0; bit < 8; ++bit) {
      crc = (crc >> 1) ^ (0xedb88320U & (0U - (crc & 1U)));
    }
  }
  return crc;
}

void respond(const char *format, ...) {
  va_list args;
  va_start(args, format);
  const int length = vsnprintf(reply, sizeof(reply), format, args);
  va_end(args);
  reply_length = length > 0 && static_cast<size_t>(length) < sizeof(reply)
                     ? static_cast<size_t>(length) : 0;
  reply_offset = 0;
}

Recording snapshot() {
  portENTER_CRITICAL(&pcm_mux);
  const Recording result = recording;
  portEXIT_CRITICAL(&pcm_mux);
  return result;
}

bool releaseRecording() {
  portENTER_CRITICAL(&pcm_mux);
  if (writer_active) {
    portEXIT_CRITICAL(&pcm_mux);
    return false;
  }
  int16_t *old = pcm;
  pcm = nullptr;
  recording = Recording{};
  portEXIT_CRITICAL(&pcm_mux);
  heap_caps_free(old);
  return true;
}

void statusReply(const Recording &r) {
  const char *state = r.state == State::Recording ? "RECORDING" :
                      r.state == State::Ready ? "READY" : "FAILED";
  respond("PCM STATE %lu %s %lu %lu %lu %lu %lu %lu %lu %08lx\n",
          static_cast<unsigned long>(r.id), state,
          static_cast<unsigned long>(r.count), static_cast<unsigned long>(r.target),
          static_cast<unsigned long>(SAMPLE_RATE), static_cast<unsigned long>(r.first_ms),
          static_cast<unsigned long>(r.last_ms), static_cast<unsigned long>(r.max_gap_ms),
          static_cast<unsigned long>(r.errors), static_cast<unsigned long>(~r.crc));
}

bool number(const char *text, uint32_t &value) {
  if (!text || !*text) return false;
  uint32_t result = 0;
  for (const char *p = text; *p; ++p) {
    if (*p < '0' || *p > '9') return false;
    const uint32_t digit = *p - '0';
    if (result > (UINT32_MAX - digit) / 10) return false;
    result = result * 10 + digit;
  }
  value = result;
  return true;
}

void handleCommand(bool microphone_ready) {
  char *save = nullptr;
  const char *prefix = strtok_r(command, " ", &save);
  if (!prefix || strcmp(prefix, "PCM")) return;
  const char *verb = strtok_r(nullptr, " ", &save);
  const char *first = strtok_r(nullptr, " ", &save);
  const char *second = strtok_r(nullptr, " ", &save);
  const char *extra = strtok_r(nullptr, " ", &save);
  if (verb && !strcmp(verb, "INFO") && !first) {
    respond("PCM INFO 1 16000 1 16 5000 64 %lu\n",
            static_cast<unsigned long>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
    return;
  }
  uint32_t id = 0, value = 0;
  if (!verb || !number(first, id) || !id || extra) {
    respond("PCM ERROR 0 BAD_COMMAND\n");
    return;
  }
  Recording r = snapshot();
  if (!strcmp(verb, "START")) {
    if (!number(second, value) || value < 100 || value > MAX_MS || value % 10) {
      respond("PCM ERROR %lu BAD_DURATION\n", static_cast<unsigned long>(id));
      return;
    }
    if (r.state != State::Idle) {
      if (r.id == id && r.target == SAMPLE_RATE * value / 1000) {
        last_activity_ms = millis();
        statusReply(r);  // Retrying a lost START reply must not restart recording.
      } else {
        respond("PCM ERROR %lu BUSY\n", static_cast<unsigned long>(id));
      }
      return;
    }
    if (!microphone_ready) {
      respond("PCM ERROR %lu NO_MIC\n", static_cast<unsigned long>(id));
      return;
    }
    const uint32_t target = SAMPLE_RATE * value / 1000;
    int16_t *allocated = static_cast<int16_t *>(
        heap_caps_malloc(target * sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!allocated) {
      respond("PCM ERROR %lu NO_PSRAM\n", static_cast<unsigned long>(id));
      return;
    }
    r = Recording{};
    r.id = id;
    r.target = target;
    r.requested_ms = millis();
    r.state = State::Recording;
    portENTER_CRITICAL(&pcm_mux);
    pcm = allocated;
    recording = r;
    portEXIT_CRITICAL(&pcm_mux);
    last_activity_ms = r.requested_ms;
    statusReply(r);
    return;
  }
  if (r.state == State::Idle || r.id != id) {
    // Release is idempotent so a host can retry a lost acknowledgement.
    if (!strcmp(verb, "RELEASE") && !second && r.state == State::Idle) {
      respond("PCM RELEASED %lu\n", static_cast<unsigned long>(id));
    } else {
      respond("PCM ERROR %lu NO_RECORDING\n", static_cast<unsigned long>(id));
    }
    return;
  }
  last_activity_ms = millis();
  if (!strcmp(verb, "STATUS") && !second) {
    statusReply(r);
  } else if (!strcmp(verb, "RELEASE") && !second) {
    if (releaseRecording()) respond("PCM RELEASED %lu\n", static_cast<unsigned long>(id));
    else respond("PCM ERROR %lu BUSY\n", static_cast<unsigned long>(id));
  } else if (!strcmp(verb, "READ")) {
    if (r.state != State::Ready) {
      respond("PCM ERROR %lu NOT_READY\n", static_cast<unsigned long>(id));
      return;
    }
    const uint32_t total = r.count * sizeof(int16_t);
    if (!number(second, value) || value >= total || value % CHUNK_BYTES) {
      respond("PCM ERROR %lu BAD_OFFSET\n", static_cast<unsigned long>(id));
      return;
    }
    // READY data is immutable. Only this main-loop handler releases its buffer.
    const size_t count = total - value < CHUNK_BYTES ? total - value : CHUNK_BYTES;
    const uint8_t *data = reinterpret_cast<const uint8_t *>(pcm) + value;
    char hex[CHUNK_BYTES * 2 + 1];
    constexpr char digits[] = "0123456789abcdef";
    for (size_t i = 0; i < count; ++i) {
      hex[2 * i] = digits[data[i] >> 4];
      hex[2 * i + 1] = digits[data[i] & 15];
    }
    hex[2 * count] = 0;
    respond("PCM DATA %lu %lu %s %08lx\n", static_cast<unsigned long>(id),
            static_cast<unsigned long>(value), hex,
            static_cast<unsigned long>(~crcUpdate(0xffffffffU, data, count)));
  } else {
    respond("PCM ERROR %lu BAD_COMMAND\n", static_cast<unsigned long>(id));
  }
}
}  // namespace

void pcm_capture_feed(const int16_t *samples, size_t count, uint32_t now) {
  portENTER_CRITICAL(&pcm_mux);
  if (recording.state != State::Recording || !count) {
    portEXIT_CRITICAL(&pcm_mux);
    return;
  }
  const uint32_t offset = recording.count;
  const size_t remaining = recording.target - offset;
  if (count > remaining) count = remaining;
  int16_t *destination = pcm + offset;
  const uint32_t previous_crc = recording.crc;
  writer_active = true;
  portEXIT_CRITICAL(&pcm_mux);

  // PSRAM copying and CRC run outside the critical section. Release waits for
  // writer_active to clear, so neither timeout nor a command frees this buffer.
  memcpy(destination, samples, count * sizeof(int16_t));
  const uint32_t crc = crcUpdate(previous_crc,
      reinterpret_cast<const uint8_t *>(samples), count * sizeof(int16_t));

  portENTER_CRITICAL(&pcm_mux);
  if (!offset) recording.first_ms = now;
  else if (now - recording.last_ms > recording.max_gap_ms) {
    recording.max_gap_ms = now - recording.last_ms;
  }
  recording.last_ms = now;
  recording.count += count;
  recording.crc = crc;
  if (recording.state == State::Recording && recording.count == recording.target) {
    recording.state = State::Ready;
  }
  writer_active = false;
  portEXIT_CRITICAL(&pcm_mux);
}

void pcm_capture_read_error() {
  portENTER_CRITICAL(&pcm_mux);
  if (recording.state == State::Recording) {
    recording.errors++;
    recording.state = State::Failed;
  }
  portEXIT_CRITICAL(&pcm_mux);
}

void pcm_capture_service(bool microphone_ready) {
  const uint32_t now = millis();
  portENTER_CRITICAL(&pcm_mux);
  if (recording.state == State::Recording &&
      now - recording.requested_ms > recording.target * 1000 / SAMPLE_RATE + 2000) {
    recording.state = State::Failed;
  }
  const bool occupied = recording.state != State::Idle;
  portEXIT_CRITICAL(&pcm_mux);
  if (occupied && now - last_activity_ms > RETENTION_MS) releaseRecording();

  if (reply_length) {
    const size_t remaining = reply_length - reply_offset;
    if (Serial.availableForWrite() >= static_cast<int>(remaining)) {
      reply_offset += Serial.write(reinterpret_cast<const uint8_t *>(reply) + reply_offset, remaining);
      if (reply_offset == reply_length) reply_length = reply_offset = 0;
    }
    return;
  }
  if (command_length && now - last_rx_ms > 1000) {
    command_length = 0;
    discard_command = true;
  }
  for (size_t budget = 0; budget < 32 && Serial.available(); ++budget) {
    const int c = Serial.read();
    last_rx_ms = now;
    if (c == '\n') {
      if (discard_command) respond("PCM ERROR 0 BAD_COMMAND\n");
      else {
        command[command_length] = 0;
        handleCommand(microphone_ready);
      }
      command_length = 0;
      discard_command = false;
      return;
    }
    if (c == '\r') continue;
    if (c < 32 || c > 126 || command_length + 1 >= sizeof(command)) {
      discard_command = true;
    }
    if (!discard_command) command[command_length++] = static_cast<char>(c);
  }
}

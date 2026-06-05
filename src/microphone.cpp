#include "microphone.h"

#include <I2S.h>
#include <math.h>

namespace {
constexpr int MIC_DATA_PIN = 41;
constexpr int MIC_CLK_PIN = 42;
constexpr uint32_t MIC_SAMPLE_RATE = 16000;
constexpr size_t MIC_SAMPLE_COUNT = 256;
constexpr uint32_t MIC_PRINT_INTERVAL_MS = 500;

bool mic_ready = false;
int16_t sample_buffer[MIC_SAMPLE_COUNT];
}  // namespace

bool microphone_begin() {
  // Seeed XIAO ESP32S3 Sense PDM microphone:
  // PDM DATA = GPIO41, PDM CLK = GPIO42.
  I2S.setAllPins(-1, MIC_CLK_PIN, MIC_DATA_PIN, -1, -1);

  if (!I2S.begin(PDM_MONO_MODE, MIC_SAMPLE_RATE, 16)) {
    Serial.println("Mic init failed: I2S.begin(PDM_MONO_MODE)");
    return false;
  }

  mic_ready = true;
  Serial.println("Mic initialized: PDM DATA=GPIO41 CLK=GPIO42");
  return true;
}

MicrophoneLevel microphone_read_level() {
  MicrophoneLevel level{false, 0.0f, 0, 0, 0};
  if (!mic_ready) {
    return level;
  }

  size_t sample_count = 0;
  const unsigned long deadline = millis() + 60;

  while (sample_count < MIC_SAMPLE_COUNT &&
         static_cast<long>(millis() - deadline) < 0) {
    const int sample = I2S.read();
    sample_buffer[sample_count++] = static_cast<int16_t>(sample);
  }

  if (sample_count == 0) {
    return level;
  }

  int64_t sum = 0;
  for (size_t i = 0; i < sample_count; ++i) {
    sum += sample_buffer[i];
  }

  const int32_t mean =
      static_cast<int32_t>(sum / static_cast<int64_t>(sample_count));
  int64_t square_sum = 0;
  int32_t peak = 0;

  for (size_t i = 0; i < sample_count; ++i) {
    const int32_t centered = static_cast<int32_t>(sample_buffer[i]) - mean;
    const int32_t magnitude = abs(centered);
    if (magnitude > peak) {
      peak = magnitude;
    }
    square_sum += static_cast<int64_t>(centered) * centered;
  }

  level.ok = true;
  level.rms =
      sqrtf(static_cast<float>(square_sum) / static_cast<float>(sample_count));
  level.peak = peak;
  level.mean = mean;
  level.samples = sample_count;
  return level;
}

void microphone_print_level(unsigned long now) {
  static unsigned long last_print_ms = 0;
  if (now - last_print_ms < MIC_PRINT_INTERVAL_MS) {
    return;
  }
  last_print_ms = now;

  const MicrophoneLevel level = microphone_read_level();
  if (!level.ok) {
    Serial.println("Mic: read failed");
    return;
  }

  Serial.printf("Mic: mean=%ld rms=%.1f peak=%ld samples=%u\n",
                static_cast<long>(level.mean), level.rms,
                static_cast<long>(level.peak),
                static_cast<unsigned>(level.samples));
}

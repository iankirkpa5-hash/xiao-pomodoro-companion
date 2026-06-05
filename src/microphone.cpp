#include "microphone.h"

#include <I2S.h>
#include <math.h>

namespace {
constexpr int MIC_DATA_PIN = 41;
constexpr int MIC_CLK_PIN = 42;
constexpr uint32_t MIC_SAMPLE_RATE = 16000;
constexpr size_t MIC_SAMPLE_COUNT = 160;  // 10 ms at 16 kHz.
constexpr uint32_t MIC_UPDATE_INTERVAL_MS = 40;
constexpr uint32_t MIC_PRINT_INTERVAL_MS = 500;
constexpr uint32_t MIC_LOUD_HOLD_MS = 600;
constexpr float MIC_LOUD_RAW_RMS = 350.0f;
constexpr int32_t MIC_LOUD_RAW_PEAK = 3200;
constexpr int32_t MIC_TRANSIENT_PEAK = 4800;
constexpr int32_t MIC_IMPULSE_PEAK = 2600;
constexpr float MIC_IMPULSE_MAX_RMS = 280.0f;
constexpr float MIC_IMPULSE_MIN_PEAK_RMS_RATIO = 12.0f;
constexpr float MIC_NORMAL_RAW_RMS = 120.0f;
constexpr int32_t MIC_NORMAL_RAW_PEAK = 1200;
constexpr uint8_t MIC_QUIET_DEBOUNCE_FRAMES = 5;
constexpr uint8_t MIC_NORMAL_DEBOUNCE_FRAMES = 2;

enum class MicrophoneLabelState {
  Quiet,
  Normal,
  Loud,
};

bool mic_ready = false;
unsigned long last_update_ms = 0;
unsigned long loud_hold_until_ms = 0;
MicrophoneLevel latest_level{false, 0.0f, 0, 0, 0};
MicrophoneLabelState label_state = MicrophoneLabelState::Quiet;
uint8_t quiet_counter = 0;
uint8_t normal_counter = 0;
int16_t sample_buffer[MIC_SAMPLE_COUNT];

void updateLabelState(unsigned long now) {
  const bool impulse_loud =
      latest_level.peak > MIC_IMPULSE_PEAK &&
      latest_level.rms < MIC_IMPULSE_MAX_RMS &&
      latest_level.peak > latest_level.rms * MIC_IMPULSE_MIN_PEAK_RMS_RATIO;
  const bool transient_loud = latest_level.peak >= MIC_TRANSIENT_PEAK;
  const bool sustained_loud =
      latest_level.rms >= MIC_LOUD_RAW_RMS && latest_level.peak >= MIC_LOUD_RAW_PEAK;
  const bool normal_frame =
      latest_level.rms >= MIC_NORMAL_RAW_RMS || latest_level.peak >= MIC_NORMAL_RAW_PEAK;

  if (impulse_loud || transient_loud || sustained_loud) {
    label_state = MicrophoneLabelState::Loud;
    loud_hold_until_ms = now + MIC_LOUD_HOLD_MS;
    quiet_counter = 0;
    normal_counter = 0;
    return;
  }

  if (label_state == MicrophoneLabelState::Loud) {
    if (static_cast<long>(now - loud_hold_until_ms) < 0) {
      return;
    }
  }

  if (normal_frame) {
    normal_counter++;
    quiet_counter = 0;
    if (normal_counter >= MIC_NORMAL_DEBOUNCE_FRAMES) {
      label_state = MicrophoneLabelState::Normal;
    }
    return;
  }

  quiet_counter++;
  normal_counter = 0;
  if (quiet_counter >= MIC_QUIET_DEBOUNCE_FRAMES) {
    label_state = MicrophoneLabelState::Quiet;
  }
}
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

bool microphone_update(unsigned long now) {
  if (!mic_ready) {
    latest_level = MicrophoneLevel{false, 0.0f, 0, 0, 0};
    return false;
  }

  if (latest_level.ok && now - last_update_ms < MIC_UPDATE_INTERVAL_MS) {
    return false;
  }

  last_update_ms = now;
  latest_level = microphone_read_level();
  if (latest_level.ok) {
    updateLabelState(now);
  }
  return latest_level.ok;
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

MicrophoneLevel microphone_level() {
  return latest_level;
}

const char *microphone_level_label() {
  if (!latest_level.ok) {
    return "Off";
  }

  switch (label_state) {
    case MicrophoneLabelState::Loud:
      return "Loud";
    case MicrophoneLabelState::Normal:
      return "Normal";
    case MicrophoneLabelState::Quiet:
    default:
      return "Quiet";
  }
}

void microphone_print_level(unsigned long now) {
  static unsigned long last_print_ms = 0;
  if (now - last_print_ms < MIC_PRINT_INTERVAL_MS) {
    return;
  }
  last_print_ms = now;

  microphone_update(now);
  const MicrophoneLevel level = microphone_level();
  if (!level.ok) {
    Serial.println("Mic: read failed");
    return;
  }

  Serial.printf("Mic: mean=%ld rms=%.1f peak=%ld samples=%u\n",
                static_cast<long>(level.mean), level.rms,
                static_cast<long>(level.peak),
                static_cast<unsigned>(level.samples));
}

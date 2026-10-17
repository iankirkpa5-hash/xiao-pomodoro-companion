#include "microphone.h"

#include <driver/i2s.h>
#include <math.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace {
constexpr int MIC_DATA_PIN = 41;
constexpr int MIC_CLK_PIN = 42;
constexpr i2s_port_t MIC_I2S_PORT = I2S_NUM_0;
constexpr uint32_t MIC_SAMPLE_RATE = 16000;
constexpr size_t MIC_SAMPLE_COUNT = 160;  // 10 ms at 16 kHz.
constexpr uint32_t MIC_UPDATE_INTERVAL_MS = 40;
constexpr uint32_t MIC_PRINT_INTERVAL_MS = 500;
constexpr uint32_t MIC_DIAGNOSTIC_INTERVAL_MS = 5000;
constexpr uint32_t MIC_STALE_MS = 250;
constexpr uint32_t MIC_LOUD_HOLD_MS = 600;
constexpr float MIC_LOUD_RAW_RMS = 350.0f;
constexpr int32_t MIC_LOUD_RAW_PEAK = 3200;
constexpr int32_t MIC_TRANSIENT_PEAK = 4800;
constexpr int32_t MIC_IMPULSE_PEAK = 2600;
constexpr float MIC_IMPULSE_MAX_RMS = 280.0f;
constexpr float MIC_IMPULSE_MIN_PEAK_RMS_RATIO = 12.0f;
constexpr float MIC_NORMAL_RAW_RMS = 120.0f;
constexpr int32_t MIC_NORMAL_RAW_PEAK = 1200;
constexpr uint8_t MIC_QUIET_DEBOUNCE_FRAMES = 15;  // ~600 ms at 40 ms updates.
constexpr uint8_t MIC_NORMAL_DEBOUNCE_FRAMES = 2;

enum class MicrophoneLabelState {
  Quiet,
  Normal,
  Loud,
};

bool mic_ready = false;
uint32_t last_update_ms = 0;
MicrophoneLevel latest_level{false, 0.0f, 0, 0, 0};
MicrophoneLabelState label_state = MicrophoneLabelState::Quiet;
uint8_t quiet_counter = 0;
uint8_t normal_counter = 0;
int16_t sample_buffer[MIC_SAMPLE_COUNT];
size_t buffered_bytes = 0;

struct ReportWindow {
  uint64_t square_sum = 0;
  uint64_t samples = 0;
  int32_t peak = 0;
  uint32_t timestamp_ms = 0;
};

// Only the I2S receive task consumes PCM. The main loop takes short snapshots.
portMUX_TYPE capture_mux = portMUX_INITIALIZER_UNLOCKED;
TaskHandle_t capture_task = nullptr;
MicrophoneLevel captured_level{false, 0.0f, 0, 0, 0};
ReportWindow report_window;
uint32_t capture_ms = 0;
uint32_t captured_frames = 0;
uint32_t consumed_frames = 0;
uint32_t read_errors = 0;
uint32_t last_loud_frame_ms = 0;
bool have_loud_frame = false;
uint32_t last_print_ms = 0;
uint32_t last_diagnostic_ms = 0;
uint32_t diagnostic_frames = 0;

bool isLoudFrame(const MicrophoneLevel &level) {
  const bool impulse_loud =
      level.peak > MIC_IMPULSE_PEAK && level.rms < MIC_IMPULSE_MAX_RMS &&
      level.peak > level.rms * MIC_IMPULSE_MIN_PEAK_RMS_RATIO;
  const bool transient_loud = level.peak >= MIC_TRANSIENT_PEAK;
  const bool sustained_loud =
      level.rms >= MIC_LOUD_RAW_RMS && level.peak >= MIC_LOUD_RAW_PEAK;
  return impulse_loud || transient_loud || sustained_loud;
}

void updateLabelState(uint32_t now, bool loud_seen, uint32_t loud_frame_ms) {
  const bool normal_frame =
      latest_level.rms >= MIC_NORMAL_RAW_RMS || latest_level.peak >= MIC_NORMAL_RAW_PEAK;

  // Preserve a transient seen between UI updates, using its capture time.
  if (loud_seen && now - loud_frame_ms < MIC_LOUD_HOLD_MS) {
    label_state = MicrophoneLabelState::Loud;
    quiet_counter = 0;
    normal_counter = 0;
    return;
  }

  if (normal_frame) {
    if (normal_counter < MIC_NORMAL_DEBOUNCE_FRAMES) normal_counter++;
    quiet_counter = 0;
    if (normal_counter >= MIC_NORMAL_DEBOUNCE_FRAMES) {
      label_state = MicrophoneLabelState::Normal;
    }
    return;
  }

  if (quiet_counter < MIC_QUIET_DEBOUNCE_FRAMES) quiet_counter++;
  normal_counter = 0;
  if (quiet_counter >= MIC_QUIET_DEBOUNCE_FRAMES) {
    label_state = MicrophoneLabelState::Quiet;
  }
}

void publishFrame() {
  int64_t sum = 0;
  for (size_t i = 0; i < MIC_SAMPLE_COUNT; ++i) sum += sample_buffer[i];
  const int32_t mean = static_cast<int32_t>(sum / static_cast<int64_t>(MIC_SAMPLE_COUNT));
  uint64_t square_sum = 0;
  int32_t peak = 0;
  for (size_t i = 0; i < MIC_SAMPLE_COUNT; ++i) {
    const int32_t centered = static_cast<int32_t>(sample_buffer[i]) - mean;
    const int32_t magnitude = abs(centered);
    if (magnitude > peak) peak = magnitude;
    square_sum += static_cast<int64_t>(centered) * centered;
  }
  const MicrophoneLevel level{
      true, sqrtf(static_cast<float>(square_sum) / MIC_SAMPLE_COUNT),
      peak, mean, MIC_SAMPLE_COUNT};
  const bool loud = isLoudFrame(level);
  const uint32_t timestamp = millis();

  portENTER_CRITICAL(&capture_mux);
  captured_level = level;
  capture_ms = timestamp;
  captured_frames++;
  report_window.square_sum += square_sum;
  report_window.samples += MIC_SAMPLE_COUNT;
  if (peak > report_window.peak) report_window.peak = peak;
  report_window.timestamp_ms = timestamp;
  if (loud) {
    have_loud_frame = true;
    last_loud_frame_ms = timestamp;
  } else if (timestamp - last_loud_frame_ms >= MIC_LOUD_HOLD_MS) {
    have_loud_frame = false;
  }
  portEXIT_CRITICAL(&capture_mux);
}

bool receiveAudio() {
  const size_t requested = sizeof(sample_buffer) - buffered_bytes;
  size_t bytes_read = 0;
  const esp_err_t result = i2s_read(
      MIC_I2S_PORT, reinterpret_cast<uint8_t *>(sample_buffer) + buffered_bytes,
      requested, &bytes_read, pdMS_TO_TICKS(20));
  if (result != ESP_OK || bytes_read == 0 || bytes_read > requested) {
    portENTER_CRITICAL(&capture_mux);
    read_errors++;
    portEXIT_CRITICAL(&capture_mux);
    return false;
  }
  buffered_bytes += bytes_read;
  if (buffered_bytes == sizeof(sample_buffer)) {
    publishFrame();
    buffered_bytes = 0;
  }
  return true;
}

void captureAudio(void *) {
  // Blocking DMA reads run here, never in the touch/timer/display loop.
  for (;;) {
    if (!receiveAudio()) vTaskDelay(pdMS_TO_TICKS(1));
  }
}
}  // namespace

bool microphone_begin() {
  if (mic_ready) return true;
  // Seeed XIAO ESP32S3 Sense PDM microphone:
  // PDM DATA = GPIO41, PDM CLK = GPIO42.
  i2s_config_t config{};
  config.mode = static_cast<i2s_mode_t>(I2S_MODE_MASTER | I2S_MODE_RX | I2S_MODE_PDM);
  config.sample_rate = MIC_SAMPLE_RATE;
  config.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
  config.channel_format = I2S_CHANNEL_FMT_ONLY_RIGHT;
  config.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  config.dma_buf_count = 8;
  config.dma_buf_len = MIC_SAMPLE_COUNT;

  if (i2s_driver_install(MIC_I2S_PORT, &config, 0, nullptr) != ESP_OK) {
    Serial.println("Mic init failed: I2S RX driver");
    return false;
  }

  i2s_pin_config_t pins{};
  pins.mck_io_num = I2S_PIN_NO_CHANGE;
  pins.bck_io_num = I2S_PIN_NO_CHANGE;
  pins.ws_io_num = MIC_CLK_PIN;
  pins.data_out_num = I2S_PIN_NO_CHANGE;
  pins.data_in_num = MIC_DATA_PIN;
  // Explicit mono clock setup matches the previously working PDM channel.
  if (i2s_set_clk(MIC_I2S_PORT, MIC_SAMPLE_RATE, I2S_BITS_PER_SAMPLE_16BIT,
                  I2S_CHANNEL_MONO) != ESP_OK ||
      i2s_set_pin(MIC_I2S_PORT, &pins) != ESP_OK) {
    Serial.println("Mic init failed: PDM clock/pins");
    i2s_driver_uninstall(MIC_I2S_PORT);
    return false;
  }
  last_print_ms = millis();
  last_diagnostic_ms = last_print_ms;
  if (xTaskCreate(captureAudio, "mic_capture", 4096, nullptr, 2, &capture_task) != pdPASS) {
    Serial.println("Mic init failed: capture task");
    i2s_driver_uninstall(MIC_I2S_PORT);
    return false;
  }
  mic_ready = true;
  Serial.println("Mic initialized: PDM DATA=GPIO41 CLK=GPIO42");
  Serial.println("Mic telemetry: interval centered RMS / max peak; continuous 10ms frames");
  Serial.println("timestamp_ms,rms,peak,level");
  return true;
}

bool microphone_update(unsigned long now) {
  if (!mic_ready) {
    latest_level = MicrophoneLevel{false, 0.0f, 0, 0, 0};
    return false;
  }

  // Touch/display work can make the loop's original timestamp stale.
  now = millis();
  if (latest_level.ok && static_cast<uint32_t>(now) - last_update_ms < MIC_UPDATE_INTERVAL_MS) {
    return false;
  }

  uint32_t frames, timestamp, loud_frame_ms;
  bool loud_seen;
  portENTER_CRITICAL(&capture_mux);
  latest_level = captured_level;
  frames = captured_frames;
  timestamp = capture_ms;
  loud_seen = have_loud_frame;
  loud_frame_ms = last_loud_frame_ms;
  portEXIT_CRITICAL(&capture_mux);
  now = millis();
  last_update_ms = now;
  if (!latest_level.ok || static_cast<uint32_t>(now) - timestamp > MIC_STALE_MS) {
    latest_level.ok = false;
    label_state = MicrophoneLabelState::Quiet;
    quiet_counter = normal_counter = 0;
    return false;
  }
  if (frames == consumed_frames) return false;
  consumed_frames = frames;
  updateLabelState(now, loud_seen, loud_frame_ms);
  return true;
}

MicrophoneLevel microphone_read_level() {
  // Snapshot only; callers must not compete with the I2S receive task.
  MicrophoneLevel level{false, 0.0f, 0, 0, 0};
  if (!mic_ready) return level;
  uint32_t timestamp;
  portENTER_CRITICAL(&capture_mux);
  level = captured_level;
  timestamp = capture_ms;
  portEXIT_CRITICAL(&capture_mux);
  if (static_cast<uint32_t>(millis()) - timestamp > MIC_STALE_MS) level.ok = false;
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
  now = millis();
  if (static_cast<uint32_t>(now) - last_print_ms < MIC_PRINT_INTERVAL_MS) {
    return;
  }
  last_print_ms = now;

  ReportWindow report;
  uint32_t frames, errors;
  portENTER_CRITICAL(&capture_mux);
  report = report_window;
  report_window = ReportWindow{};
  frames = captured_frames;
  errors = read_errors;
  portEXIT_CRITICAL(&capture_mux);

  if (!report.samples || !latest_level.ok) {
    Serial.println("Mic: no fresh complete PCM frames");
  } else {
    const float rms = sqrtf(static_cast<float>(report.square_sum) / report.samples);
    Serial.printf("%lu,%.1f,%ld,%s\n", static_cast<unsigned long>(report.timestamp_ms),
                  rms, static_cast<long>(report.peak), microphone_level_label());
  }

  const uint32_t elapsed = static_cast<uint32_t>(now) - last_diagnostic_ms;
  if (elapsed >= MIC_DIAGNOSTIC_INTERVAL_MS) {
    Serial.printf("Mic capture: frames=%lu samples=%lu elapsed_ms=%lu read_errors=%lu\n",
                  static_cast<unsigned long>(frames - diagnostic_frames),
                  static_cast<unsigned long>((frames - diagnostic_frames) * MIC_SAMPLE_COUNT),
                  static_cast<unsigned long>(elapsed), static_cast<unsigned long>(errors));
    diagnostic_frames = frames;
    last_diagnostic_ms = now;
  }
}

#include <cassert>
#include <cmath>
#include <iostream>
#include "../../src/pcm_capture.cpp"
#include "../../src/microphone.cpp"

void reset(uint32_t now = 0) {
  I2S = FakeI2S{};
  fake_task_failure = false;
  capture_task = nullptr;
  Serial.output.clear();
  fake_ms = now;
  mic_ready = false;
  last_update_ms = last_print_ms = last_diagnostic_ms = 0;
  diagnostic_frames = captured_frames = consumed_frames = read_errors = 0;
  captured_level = latest_level = MicrophoneLevel{false, 0, 0, 0, 0};
  capture_ms = last_loud_frame_ms = 0;
  have_loud_frame = false;
  report_window = ReportWindow{};
  buffered_bytes = 0;
  label_state = MicrophoneLabelState::Quiet;
  quiet_counter = normal_counter = 0;
  assert(microphone_begin());
  Serial.output.clear();
}

std::vector<uint8_t> encode(const std::vector<int16_t> &samples) {
  std::vector<uint8_t> bytes(samples.size() * sizeof(int16_t));
  memcpy(bytes.data(), samples.data(), bytes.size());
  return bytes;
}

void frame(const std::vector<int16_t> &samples, uint32_t step = 10) {
  fake_ms += step;
  I2S.feed(encode(samples));
  while (!I2S.bytes.empty()) assert(receiveAudio());
}

void tick() { microphone_update(fake_ms); }

int main() {
  const std::vector<int16_t> quiet(160, 1250);
  auto clap = quiet;
  clap[80] += 6000;

  reset();
  frame(clap);
  const auto impulse = captured_level;
  for (int i = 0; i < 49; ++i) frame(quiet);
  assert(captured_frames == 50 && report_window.samples == 8000);
  assert(captured_level.peak == 0 && report_window.peak == impulse.peak);
  const float report_rms = sqrtf(static_cast<float>(report_window.square_sum) / report_window.samples);
  assert(fabs(report_rms - impulse.rms / sqrtf(50)) < 0.01f);
  tick();
  assert(std::string(microphone_level_label()) == "Loud");
  microphone_print_level(fake_ms);
  assert(Serial.output.find("," + std::to_string(impulse.peak) + ",Loud") != std::string::npos);
  assert(report_window.samples == 0 && report_window.peak == 0);
  for (int i = 0; i < 110; ++i) { frame(quiet); tick(); }
  microphone_print_level(fake_ms);
  assert(Serial.output.find("1600,0.0,0,Quiet") != std::string::npos);
  assert(captured_frames == 160 && read_errors == 0);
  assert(!have_loud_frame);
  std::cout << "PASS continuous capture, retained peak, energy RMS, interval reset, Loud hold\n";

  reset();
  auto bytes = encode(quiet);
  I2S.max_read = 17;
  I2S.feed({bytes.begin(), bytes.begin() + 101});
  while (!I2S.bytes.empty()) assert(receiveAudio());
  assert(captured_frames == 0 && buffered_bytes == 101);
  I2S.fail_read = true;
  I2S.feed({bytes.begin() + 101, bytes.end()});
  assert(!receiveAudio());
  assert(read_errors == 1 && captured_frames == 0 && buffered_bytes == 101);
  I2S.fail_read = false;
  while (!I2S.bytes.empty()) assert(receiveAudio());
  assert(captured_frames == 1 && buffered_bytes == 0);
  assert(captured_level.mean == 1250 && captured_level.peak == 0);
  assert(!receiveAudio());
  assert(captured_frames == 1 && read_errors == 2);
  std::cout << "PASS partial/odd byte chunks, failed read, empty buffer never fabricated\n";

  reset();
  frame(std::vector<int16_t>(160, -1250));
  assert(captured_level.mean == -1250 && captured_level.rms == 0);
  std::vector<int16_t> extremes(160);
  for (size_t i = 0; i < extremes.size(); ++i) extremes[i] = i % 2 ? 32767 : -32768;
  frame(extremes);
  assert(captured_level.peak == 32768);
  assert(fabs(captured_level.rms - 32767.5f) < 0.1f);
  std::cout << "PASS negative DC and full-scale int16 without overflow\n";

  reset();
  frame(quiet);
  tick();
  fake_ms += MIC_STALE_MS + 1;
  tick();
  assert(!microphone_read_level().ok && std::string(microphone_level_label()) == "Off");
  fake_ms = 500;
  microphone_print_level(fake_ms);
  assert(Serial.output.find("no fresh complete PCM frames") != std::string::npos);
  assert(Serial.output.find(",Quiet") == std::string::npos);
  std::cout << "PASS stalled capture is not a fabricated quiet measurement\n";

  reset(UINT32_MAX - 250);
  frame(clap);
  for (int i = 0; i < 49; ++i) frame(quiet);
  tick();
  assert(microphone_read_level().ok && std::string(microphone_level_label()) == "Loud");
  microphone_print_level(fake_ms);
  assert(Serial.output.find("," + std::to_string(impulse.peak) + ",Loud") != std::string::npos);
  std::cout << "PASS 32-bit uptime wrap\n";

  reset();
  std::vector<int16_t> speech(160);
  for (size_t i = 0; i < speech.size(); ++i) speech[i] = i % 2 ? 1450 : 1050;
  for (int i = 0; i < 600; ++i) { frame(speech, 40); tick(); }
  assert(std::string(microphone_level_label()) == "Normal");
  assert(normal_counter == MIC_NORMAL_DEBOUNCE_FRAMES);
  for (int i = 0; i < 600; ++i) { frame(quiet, 40); tick(); }
  assert(std::string(microphone_level_label()) == "Quiet");
  assert(quiet_counter == MIC_QUIET_DEBOUNCE_FRAMES);
  assert(isLoudFrame(MicrophoneLevel{true, 350, 3200, 0, 160}));
  assert(!isLoudFrame(MicrophoneLevel{true, 350, 3199, 0, 160}));
  assert(isLoudFrame(MicrophoneLevel{true, 100, 2700, 0, 160}));
  std::cout << "PASS unchanged threshold boundaries and saturating debounce\n";

  reset();
  for (int i = 0; i < 500; ++i) frame(quiet);
  tick();
  microphone_print_level(fake_ms);
  assert(Serial.output.find("frames=500 samples=80000 elapsed_ms=5000 read_errors=0") != std::string::npos);
  std::cout << "PASS capture diagnostics independent of blocked main loop\n";

  reset();
  mic_ready = false;
  fake_task_failure = true;
  assert(!microphone_begin() && !I2S.installed);
  fake_task_failure = false;
  I2S.fail_begin = true;
  assert(!microphone_begin());
  std::cout << "PASS initialization failures leave no active driver\n";
}

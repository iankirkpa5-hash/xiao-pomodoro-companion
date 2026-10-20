#include <cassert>
#include <iostream>
#include <vector>
#include "../../src/pcm_capture.cpp"

std::string send(const std::string &text, bool ready = true) {
  Serial.output.clear();
  Serial.input += text + "\n";
  for (int i = 0; i < 100 && (Serial.available() || reply_length); ++i) pcm_capture_service(ready);
  assert(Serial.input.empty());
  return Serial.output;
}

void start(uint32_t id = 42, uint32_t duration = 100) {
  assert(send("PCM START " + std::to_string(id) + " " + std::to_string(duration)).find("RECORDING") != std::string::npos);
}

int main() {
  assert(send("PCM INFO").find("PCM INFO 1 16000 1 16 5000 64 ") == 0);
  assert(send("PCM START 42 5000", false) == "PCM ERROR 42 NO_MIC\n");
  fake_no_psram = true;
  assert(send("PCM START 42 5000") == "PCM ERROR 42 NO_PSRAM\n");
  fake_no_psram = false;
  assert(fake_allocations == 0);
  std::cout << "PASS capability negotiation and unavailable mic/PSRAM\n";

  std::vector<int16_t> frame(160);
  for (size_t i = 0; i < frame.size(); ++i) frame[i] = i % 2 ? 32767 : -32768;
  start();
  for (int i = 0; i < 10; ++i) { fake_ms += 10; pcm_capture_feed(frame.data(), frame.size(), fake_ms); }
  const auto r = snapshot();
  assert(r.state == State::Ready && r.count == 1600 && r.max_gap_ms == 10);
  for (size_t i = 0; i < r.count; ++i) assert(pcm[i] == frame[i % 160]);
  const auto crc = r.crc;
  pcm_capture_feed(frame.data(), frame.size(), fake_ms);
  assert(snapshot().count == 1600 && snapshot().crc == crc);
  assert(send("PCM READ 42 0").find("0080ff7f0080ff7f") != std::string::npos);
  assert(send("PCM READ 42 1") == "PCM ERROR 42 BAD_OFFSET\n");
  assert(send("PCM READ 42 3200") == "PCM ERROR 42 BAD_OFFSET\n");
  assert(send("PCM READ 43 0") == "PCM ERROR 43 NO_RECORDING\n");
  assert(send("PCM START 42 100").find("READY") != std::string::npos);
  assert(send("PCM START 43 100") == "PCM ERROR 43 BUSY\n");
  assert(send("PCM RELEASE 42") == "PCM RELEASED 42\n");
  assert(send("PCM RELEASE 42") == "PCM RELEASED 42\n");
  assert(fake_allocations == 0);
  assert(~crcUpdate(0xffffffffU, reinterpret_cast<const uint8_t *>("123456789"), 9) == 0xcbf43926U);
  std::cout << "PASS exact raw samples, immutable READY, CRC, offsets, session and retries\n";

  start();
  pcm_capture_feed(frame.data(), 160, ++fake_ms);
  pcm_capture_read_error();
  assert(snapshot().state == State::Failed && snapshot().errors == 1);
  assert(send("PCM READ 42 0") == "PCM ERROR 42 NOT_READY\n");
  send("PCM RELEASE 42");
  start();
  writer_active = true;
  assert(send("PCM RELEASE 42") == "PCM ERROR 42 BUSY\n");
  assert(fake_allocations == 1);
  writer_active = false;
  fake_ms += 2101;
  pcm_capture_service(true);
  assert(snapshot().state == State::Failed);
  fake_ms += RETENTION_MS;
  pcm_capture_service(true);
  assert(snapshot().state == State::Idle && fake_allocations == 0);
  std::cout << "PASS read failure, capture timeout, writer lifetime and buffer expiry\n";

  for (const auto &bad : std::vector<std::string>{"PCM START 42 -1", "PCM START 42 5001", "PCM START 42 0",
                          "PCM START 42 4294967296", "PCM START 0 5000", "PCM START 42 100 extra",
                          "PCM START 42 1\t00", "PCM START 42 " + std::string(100, '1')}) {
    assert(send(bad).find("PCM ERROR") == 0);
    assert(snapshot().state == State::Idle);
  }
  Serial.input = "PCM START 42 ";
  pcm_capture_service(true);
  fake_ms += 1001;
  pcm_capture_service(true);
  assert(send("100").find("BAD_COMMAND") != std::string::npos);
  assert(snapshot().state == State::Idle);
  Serial.tx_space = 0;
  Serial.input = "PCM INFO\n";
  pcm_capture_service(true);
  assert(reply_length && reply_length < 256);
  pcm_capture_service(true);
  assert(reply_length);
  Serial.tx_space = 256;
  pcm_capture_service(true);
  assert(!reply_length);
  std::cout << "PASS bounded malformed/partial command handling and TX backpressure\n";

  fake_ms = UINT32_MAX - 30;
  start(99, 5000);
  for (int i = 0; i < 500; ++i) {
    fake_ms += 10;
    pcm_capture_feed(frame.data(), frame.size(), fake_ms);
    pcm_capture_service(true);
  }
  assert(snapshot().state == State::Ready && snapshot().count == 80000);
  assert(snapshot().last_ms - snapshot().first_ms == 4990);
  assert(send("PCM READ 99 159936").find("PCM DATA 99 159936 ") == 0);
  send("PCM RELEASE 99");
  assert(fake_allocations == 0);
  std::cout << "PASS full five-second recording, last chunk and uint32 rollover\n";
}

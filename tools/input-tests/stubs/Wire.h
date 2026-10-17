#pragma once
#include <cstddef>
#include <cstdint>
#include <deque>
#include <vector>

struct FakeWire {
  int sda = -1, scl = -1;
  unsigned requests = 0;
  uint8_t address = 0, requested_length = 0;
  std::vector<uint8_t> response{1, 0, 120, 0, 120};
  std::deque<uint8_t> buffer;
  void begin(int data, int clock) { sda = data; scl = clock; }
  uint8_t requestFrom(uint8_t addr, uint8_t count) {
    ++requests;
    address = addr;
    requested_length = count;
    buffer = std::deque<uint8_t>(response.begin(), response.end());
    return static_cast<uint8_t>(response.size());
  }
  int available() const { return static_cast<int>(buffer.size()); }
  int read() {
    if (buffer.empty()) return -1;
    int value = buffer.front();
    buffer.pop_front();
    return value;
  }
  void beginTransmission(uint8_t) {}
  int endTransmission() { return 2; }
};
inline FakeWire Wire;

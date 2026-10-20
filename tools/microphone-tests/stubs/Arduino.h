#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstdarg>
#include <cstdio>
#include <string>

inline uint32_t fake_ms = 0;
inline unsigned long millis() { return fake_ms; }

struct FakeSerial {
  std::string output;
  std::string input;
  int tx_space = 256;
  int available() { return static_cast<int>(input.size()); }
  int read() { const unsigned char c = input.front(); input.erase(0, 1); return c; }
  int availableForWrite() { return tx_space; }
  size_t write(const uint8_t *data, size_t size) {
    output.append(reinterpret_cast<const char *>(data), size);
    return size;
  }
  void println(const char *text) { output += std::string(text) + "\n"; }
  void printf(const char *format, ...) {
    char text[512];
    va_list args;
    va_start(args, format);
    vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    output += text;
  }
};
inline FakeSerial Serial;

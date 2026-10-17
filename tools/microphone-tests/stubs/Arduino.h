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

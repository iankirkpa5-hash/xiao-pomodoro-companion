#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdarg>
#include <cstdio>
#include <string>

constexpr int LOW = 0;
constexpr int HIGH = 1;
constexpr int INPUT_PULLUP = 2;
inline int fake_irq = HIGH;
inline int configured_pin = -1;
inline int configured_mode = -1;
inline int read_pin = -1;
inline void pinMode(int pin, int mode) { configured_pin = pin; configured_mode = mode; }
inline int digitalRead(int pin) { read_pin = pin; return fake_irq; }

struct FakeSerial {
  std::string output;
  void println(const char *text) { output += std::string(text) + "\n"; }
  void printf(const char *format, ...) {
    char text[256];
    va_list args;
    va_start(args, format);
    vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    output += text;
  }
};
inline FakeSerial Serial;

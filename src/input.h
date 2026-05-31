#pragma once

#include <Arduino.h>

enum class InputEvent {
  None,
  ShortPress,
  LongPress,
};

struct TouchPoint {
  uint8_t x;
  uint8_t y;
};

void input_begin();
void input_scan_i2c();
InputEvent input_update(unsigned long now);
TouchPoint input_last_touch();


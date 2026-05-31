#include "input.h"

#include <Wire.h>

namespace {
constexpr int TOUCH_SDA = 5;       // XIAO D4
constexpr int TOUCH_SCL = 6;       // XIAO D5
constexpr int TOUCH_INT = 44;      // XIAO D7
constexpr uint8_t TOUCH_ADDR = 0x2e;
constexpr uint32_t LONG_PRESS_MS = 650;
constexpr uint32_t TOUCH_RELEASE_GAP_MS = 180;

unsigned long last_touch_ms = 0;
unsigned long touch_started_ms = 0;
unsigned long last_touch_seen_ms = 0;
bool touch_active = false;
bool long_press_handled = false;
TouchPoint last_touch = {120, 120};

bool readTouch(uint8_t &x, uint8_t &y) {
  uint8_t data[5] = {0};
  const uint8_t read_len = Wire.requestFrom(TOUCH_ADDR, static_cast<uint8_t>(sizeof(data)));
  if (read_len != sizeof(data)) {
    while (Wire.available()) {
      Wire.read();
    }
    return false;
  }

  for (uint8_t i = 0; i < sizeof(data); i++) {
    data[i] = Wire.read();
  }

  if (data[0] != 0x01) {
    return false;
  }

  x = data[2];
  y = data[4];
  return true;
}
}  // namespace

void input_begin() {
  pinMode(TOUCH_INT, INPUT_PULLUP);
  Wire.begin(TOUCH_SDA, TOUCH_SCL);
}

void input_scan_i2c() {
  Serial.println("Scanning I2C...");
  for (uint8_t address = 1; address < 127; address++) {
    Wire.beginTransmission(address);
    if (Wire.endTransmission() == 0) {
      Serial.printf("I2C device found: 0x%02X\n", address);
    }
  }
}

InputEvent input_update(unsigned long now) {
  uint8_t touch_x = 0;
  uint8_t touch_y = 0;
  const bool touched = readTouch(touch_x, touch_y);

  if (touched) {
    last_touch = {touch_x, touch_y};

    if (!touch_active) {
      touch_active = true;
      touch_started_ms = now;
      long_press_handled = false;
    }

    last_touch_seen_ms = now;
  }

  if (touch_active && !long_press_handled && now - touch_started_ms >= LONG_PRESS_MS) {
    long_press_handled = true;
    last_touch_ms = now;
    return InputEvent::LongPress;
  }

  const bool touch_released = touch_active && now - last_touch_seen_ms > TOUCH_RELEASE_GAP_MS;
  if (touch_released && !long_press_handled && now - last_touch_ms > 300) {
    last_touch_ms = now;
    touch_active = false;
    return InputEvent::ShortPress;
  }

  if (touch_released) {
    touch_active = false;
  }

  return InputEvent::None;
}

TouchPoint input_last_touch() {
  return last_touch;
}


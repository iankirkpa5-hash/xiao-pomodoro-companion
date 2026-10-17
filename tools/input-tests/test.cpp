#include <cassert>
#include <iostream>
#include <limits>
#include "../../src/input.cpp"

void reset() {
  Wire = FakeWire{};
  Serial.output.clear();
  fake_irq = HIGH;
  last_touch_ms = touch_started_ms = last_touch_seen_ms = 0;
  touch_active = long_press_handled = false;
  last_touch = {120, 120};
  input_begin();
}

InputEvent poll(unsigned long now, bool down) {
  fake_irq = down ? LOW : HIGH;
  return input_update(now);
}

int main() {
  reset();
  assert(configured_pin == 44 && configured_mode == INPUT_PULLUP);
  assert(Wire.sda == 5 && Wire.scl == 6);
  for (unsigned long t = 0; t < 30000; ++t) assert(poll(t, false) == InputEvent::None);
  assert(Wire.requests == 0 && read_pin == 44);
  std::cout << "PASS: inactive IRQ never requests I2C, even with stale coordinate bytes\n";

  reset();
  Wire.response = {1, 0, 42, 0, 88};
  assert(poll(1000, true) == InputEvent::None);
  assert(Wire.requests == 1 && Wire.address == 0x2e && Wire.requested_length == 5);
  assert(input_last_touch().x == 42 && input_last_touch().y == 88);
  assert(poll(1050, true) == InputEvent::None);
  assert(poll(1140, false) == InputEvent::None);
  assert(poll(1141, false) == InputEvent::ShortPress);
  assert(poll(1300, false) == InputEvent::None && Wire.requests == 2);
  std::cout << "PASS: short press still completes on release, once\n";

  reset();
  assert(poll(1000, true) == InputEvent::None);
  for (unsigned long t = 1050; t < 1650; t += 50) assert(poll(t, true) == InputEvent::None);
  assert(poll(1650, true) == InputEvent::LongPress);
  assert(poll(1700, true) == InputEvent::None);
  assert(poll(1791, false) == InputEvent::None);
  assert(poll(1900, false) == InputEvent::None);
  std::cout << "PASS: existing 650 ms hold fires once and suppresses release tap\n";

  reset();
  for (unsigned long t = 1000; t < 5000; t += 400) {
    assert(poll(t, true) == InputEvent::None);
    assert(poll(t + 50, true) == InputEvent::None);
    assert(poll(t + 141, false) == InputEvent::ShortPress);
  }
  assert(Wire.requests == 20);
  std::cout << "PASS: repeated taps remain distinct at the existing debounce interval\n";

  reset();
  for (size_t n = 0; n < 5; ++n) {
    Wire.response.assign(n, 1);
    assert(poll(1000 + n * 100, true) == InputEvent::None);
    assert(Wire.available() == 0 && !touch_active);
  }
  Wire.response = {0, 0, 33, 0, 44};
  assert(poll(2000, true) == InputEvent::None && !touch_active);
  assert(input_last_touch().x == 120 && input_last_touch().y == 120);
  std::cout << "PASS: incomplete/no-contact reads do not invent coordinates or events\n";

  reset();
  assert(poll(1000, true) == InputEvent::None);
  assert(poll(1050, false) == InputEvent::None);
  assert(poll(1080, true) == InputEvent::None);
  assert(poll(1171, false) == InputEvent::ShortPress);
  assert(Wire.requests == 2);
  std::cout << "PASS: brief release gaps retain existing 90 ms debounce\n";

  reset();
  const auto top = std::numeric_limits<unsigned long>::max();
  last_touch_ms = top - 1000;  // Last accepted gesture was before rollover.
  assert(poll(top - 50, true) == InputEvent::None);
  assert(poll(41, false) == InputEvent::ShortPress);
  assert(poll(142, false) == InputEvent::None);
  std::cout << "PASS: unsigned clock wrap preserves release elapsed time\n";
}

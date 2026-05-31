#pragma once

#include <Arduino.h>

constexpr uint32_t DEFAULT_FOCUS_MINUTES = 25;
constexpr uint32_t DEFAULT_BREAK_MINUTES = 5;
constexpr uint32_t DEFAULT_BRIGHTNESS_PERCENT = 80;

struct PomodoroConfig {
  uint32_t focus_minutes;
  uint32_t break_minutes;
  uint32_t brightness_percent;
};

void config_begin();
const PomodoroConfig &config_get();
void config_save(uint32_t focus_minutes, uint32_t break_minutes, uint32_t brightness_percent);

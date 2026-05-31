#include "config.h"

#include <Preferences.h>

namespace {
constexpr const char *CONFIG_NAMESPACE = "pomodoro";
constexpr const char *FOCUS_KEY = "focus_min";
constexpr const char *BREAK_KEY = "break_min";
constexpr const char *BRIGHTNESS_KEY = "bright_pct";

Preferences preferences;
PomodoroConfig config = {DEFAULT_FOCUS_MINUTES, DEFAULT_BREAK_MINUTES, DEFAULT_BRIGHTNESS_PERCENT};

uint32_t sanitizeMinutes(uint32_t value, uint32_t fallback) {
  return value == 0 ? fallback : value;
}

uint32_t sanitizeBrightness(uint32_t value) {
  return value == 30 || value == 60 || value == 100 ? value : DEFAULT_BRIGHTNESS_PERCENT;
}
}  // namespace

void config_begin() {
  preferences.begin(CONFIG_NAMESPACE, false);

  const bool has_focus = preferences.isKey(FOCUS_KEY);
  const bool has_break = preferences.isKey(BREAK_KEY);
  const bool has_brightness = preferences.isKey(BRIGHTNESS_KEY);

  config.focus_minutes = sanitizeMinutes(
      preferences.getUInt(FOCUS_KEY, DEFAULT_FOCUS_MINUTES),
      DEFAULT_FOCUS_MINUTES);
  config.break_minutes = sanitizeMinutes(
      preferences.getUInt(BREAK_KEY, DEFAULT_BREAK_MINUTES),
      DEFAULT_BREAK_MINUTES);
  config.brightness_percent = sanitizeBrightness(
      preferences.getUInt(BRIGHTNESS_KEY, DEFAULT_BRIGHTNESS_PERCENT));

  if (!has_focus || !has_break || !has_brightness) {
    preferences.putUInt(FOCUS_KEY, config.focus_minutes);
    preferences.putUInt(BREAK_KEY, config.break_minutes);
    preferences.putUInt(BRIGHTNESS_KEY, config.brightness_percent);
  }

  Serial.printf(
      "Config: focus=%lu min break=%lu min brightness=%lu%%\n",
      config.focus_minutes,
      config.break_minutes,
      config.brightness_percent);
}

const PomodoroConfig &config_get() {
  return config;
}

void config_save(uint32_t focus_minutes, uint32_t break_minutes, uint32_t brightness_percent) {
  config.focus_minutes = sanitizeMinutes(focus_minutes, DEFAULT_FOCUS_MINUTES);
  config.break_minutes = sanitizeMinutes(break_minutes, DEFAULT_BREAK_MINUTES);
  config.brightness_percent = sanitizeBrightness(brightness_percent);

  preferences.putUInt(FOCUS_KEY, config.focus_minutes);
  preferences.putUInt(BREAK_KEY, config.break_minutes);
  preferences.putUInt(BRIGHTNESS_KEY, config.brightness_percent);
}


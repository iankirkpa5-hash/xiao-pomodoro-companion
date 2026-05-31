#include "config.h"

#include <Preferences.h>

namespace {
constexpr const char *CONFIG_NAMESPACE = "pomodoro";
constexpr const char *FOCUS_KEY = "focus_min";
constexpr const char *BREAK_KEY = "break_min";

Preferences preferences;
PomodoroConfig config = {DEFAULT_FOCUS_MINUTES, DEFAULT_BREAK_MINUTES};

uint32_t sanitizeMinutes(uint32_t value, uint32_t fallback) {
  return value == 0 ? fallback : value;
}
}  // namespace

void config_begin() {
  preferences.begin(CONFIG_NAMESPACE, false);

  const bool has_focus = preferences.isKey(FOCUS_KEY);
  const bool has_break = preferences.isKey(BREAK_KEY);

  config.focus_minutes = sanitizeMinutes(
      preferences.getUInt(FOCUS_KEY, DEFAULT_FOCUS_MINUTES),
      DEFAULT_FOCUS_MINUTES);
  config.break_minutes = sanitizeMinutes(
      preferences.getUInt(BREAK_KEY, DEFAULT_BREAK_MINUTES),
      DEFAULT_BREAK_MINUTES);

  if (!has_focus || !has_break) {
    preferences.putUInt(FOCUS_KEY, config.focus_minutes);
    preferences.putUInt(BREAK_KEY, config.break_minutes);
  }

  Serial.printf("Config: focus=%lu min break=%lu min\n", config.focus_minutes, config.break_minutes);
}

const PomodoroConfig &config_get() {
  return config;
}

void config_save(uint32_t focus_minutes, uint32_t break_minutes) {
  config.focus_minutes = sanitizeMinutes(focus_minutes, DEFAULT_FOCUS_MINUTES);
  config.break_minutes = sanitizeMinutes(break_minutes, DEFAULT_BREAK_MINUTES);

  preferences.putUInt(FOCUS_KEY, config.focus_minutes);
  preferences.putUInt(BREAK_KEY, config.break_minutes);
}


#include <Arduino.h>
#include "app_state.h"
#include "config.h"
#include "input.h"
#include "pomodoro_timer.h"
#include "prompts.h"
#include "ui.h"

constexpr uint32_t SESSION_FEEDBACK_MS = 2000;

bool session_feedback_active = false;
unsigned long session_feedback_until_ms = 0;
bool settings_active = false;
uint8_t focus_option_index = 0;
uint8_t brightness_option_index = 0;
uint8_t settings_selected_item = 0;

constexpr uint32_t FOCUS_OPTIONS[] = {25, 45, 50};
constexpr uint8_t FOCUS_OPTION_COUNT = sizeof(FOCUS_OPTIONS) / sizeof(FOCUS_OPTIONS[0]);
constexpr uint32_t BRIGHTNESS_OPTIONS[] = {30, 60, 100};
constexpr uint8_t BRIGHTNESS_OPTION_COUNT = sizeof(BRIGHTNESS_OPTIONS) / sizeof(BRIGHTNESS_OPTIONS[0]);
constexpr uint8_t SETTINGS_ITEM_COUNT = 3;

uint8_t focusOptionIndexFor(uint32_t focus_minutes) {
  for (uint8_t i = 0; i < FOCUS_OPTION_COUNT; i++) {
    if (FOCUS_OPTIONS[i] == focus_minutes) {
      return i;
    }
  }

  return 0;
}

uint8_t brightnessOptionIndexFor(uint32_t brightness_percent) {
  for (uint8_t i = 0; i < BRIGHTNESS_OPTION_COUNT; i++) {
    if (BRIGHTNESS_OPTIONS[i] == brightness_percent) {
      return i;
    }
  }

  return 0;
}

void drawSettings() {
  ui_draw_settings(
      FOCUS_OPTIONS[focus_option_index],
      BRIGHTNESS_OPTIONS[brightness_option_index],
      settings_selected_item);
}

void resetCurrentSession() {
  setRunning(false);
  resetTimerForCurrentSession(millis());
  ui_render_reset_without_full_redraw();
  Serial.println("Session reset");
}

void enterSettings() {
  settings_active = true;
  setRunning(false);
  focus_option_index = focusOptionIndexFor(config_get().focus_minutes);
  brightness_option_index = brightnessOptionIndexFor(config_get().brightness_percent);
  settings_selected_item = 0;
  drawSettings();
  Serial.println("Settings");
}

void changeSelectedSetting() {
  if (settings_selected_item == 0) {
    focus_option_index = (focus_option_index + 1) % FOCUS_OPTION_COUNT;
    Serial.printf("Settings focus=%lu min\n", FOCUS_OPTIONS[focus_option_index]);
  } else if (settings_selected_item == 1) {
    brightness_option_index = (brightness_option_index + 1) % BRIGHTNESS_OPTION_COUNT;
    ui_apply_brightness(BRIGHTNESS_OPTIONS[brightness_option_index]);
    Serial.printf("Settings brightness=%lu%%\n", BRIGHTNESS_OPTIONS[brightness_option_index]);
  } else if (settings_selected_item == 2) {
    mode = SessionMode::Focus;
    setRunning(false);
    resetTimerForCurrentSession(millis());
    settings_active = false;
    ui_draw_static_pomodoro_home();
    Serial.println("Settings reset");
    return;
  } else {
    return;
  }

  drawSettings();
}

void switchSettingsItem() {
  settings_selected_item = (settings_selected_item + 1) % SETTINGS_ITEM_COUNT;
  drawSettings();
  Serial.printf("Settings item=%u\n", settings_selected_item);
}

void saveSettingsAndExit(unsigned long now) {
  config_save(
      FOCUS_OPTIONS[focus_option_index],
      config_get().break_minutes,
      BRIGHTNESS_OPTIONS[brightness_option_index]);
  ui_apply_brightness(config_get().brightness_percent);
  mode = SessionMode::Focus;
  setRunning(false);
  resetTimerForCurrentSession(now);
  settings_active = false;
  ui_draw_static_pomodoro_home();
  Serial.printf(
      "Settings saved: focus=%lu min brightness=%lu%%\n",
      config_get().focus_minutes,
      config_get().brightness_percent);
}

void beginSessionFeedback(unsigned long now) {
  session_feedback_active = true;
  session_feedback_until_ms = now + SESSION_FEEDBACK_MS;
  const char *message = mode == SessionMode::Break ? prompts_next() : "Deep Work";
  ui_draw_session_feedback(mode, message);
}

void finishSessionFeedbackIfReady(unsigned long now) {
  if (!session_feedback_active) {
    return;
  }

  if (static_cast<long>(now - session_feedback_until_ms) >= 0) {
    session_feedback_active = false;
    ui_draw_static_pomodoro_home();
  }
}

void switchSession(unsigned long now) {
  toggleSessionMode();
  setRunning(false);
  resetTimerForCurrentSession(now);
  beginSessionFeedback(now);
  Serial.printf("Switched to %s\n", modeLabel());
}

void updateCountdown(unsigned long now) {
  const bool changed = advanceTimer(now);

  if (changed) {
    ui_render_tick();
  }

  if (isTimerComplete()) {
    Serial.println("Session complete");
    switchSession(now);
  }
}

void handleInput(unsigned long now) {
  if (session_feedback_active) {
    return;
  }

  const InputEvent event = input_update(now);
  const TouchPoint touch = input_last_touch();
  if (settings_active) {
    if (event == InputEvent::LongPress) {
      saveSettingsAndExit(now);
      Serial.printf("Settings saved from touch: x=%u y=%u\n", touch.x, touch.y);
    }

    if (event == InputEvent::ShortPress) {
      if (touch.x < 120) {
        switchSettingsItem();
      } else {
        changeSelectedSetting();
      }
      Serial.printf("Settings tap: x=%u y=%u\n", touch.x, touch.y);
    }

    return;
  }

  if (event == InputEvent::LongPress) {
    enterSettings();
    Serial.printf("Long press settings: x=%u y=%u\n", touch.x, touch.y);
  }

  if (event == InputEvent::ShortPress) {
    toggleRunning();
    restartCountdownAt(now);
    ui_draw_status_text();

    Serial.printf("Touch: x=%u y=%u, running=%s\n", touch.x, touch.y, running ? "true" : "false");
  }
}

void printStatusEverySecond(unsigned long now) {
  static unsigned long last_print_ms = 0;

  if (now - last_print_ms >= 1000) {
    last_print_ms = now;
    Serial.printf("Pomodoro: %s %lus running=%s\n", modeLabel(), remaining_seconds, running ? "true" : "false");
  }
}

void setup() {
  Serial.begin(115200);

  // Give the USB CDC serial port a moment to enumerate on boot.
  delay(1500);

  Serial.println();
  Serial.println("XIAO ESP32S3 Round Display Arduino_GFX test");

  randomSeed(esp_random());
  config_begin();
  timer_begin(millis());
  ui_apply_brightness(config_get().brightness_percent);
  input_begin();
  input_scan_i2c();

  if (!ui_begin()) {
    Serial.println("Display init failed");
    return;
  }

  ui_draw_static_pomodoro_home();

  Serial.println("Display initialized");
}

void loop() {
  const unsigned long now = millis();

  finishSessionFeedbackIfReady(now);
  handleInput(now);
  if (!session_feedback_active && !settings_active) {
    updateCountdown(now);
  }
  if (!settings_active) {
    printStatusEverySecond(now);
  }
}

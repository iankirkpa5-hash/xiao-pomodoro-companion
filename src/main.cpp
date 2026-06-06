#include <Arduino.h>
#include "app_state.h"
#include "camera_test.h"
#include "config.h"
#include "input.h"
#include "microphone.h"
#include "pomodoro_timer.h"
#include "prompts.h"
#include "stats.h"
#include "ui.h"

constexpr uint32_t SESSION_FEEDBACK_MS = 2000;
constexpr uint32_t IDLE_SCREEN_DELAY_MS = 60000;
constexpr uint32_t POST_SETTINGS_IDLE_BLOCK_MS = 5000;

bool session_feedback_active = false;
unsigned long session_feedback_until_ms = 0;
bool settings_active = false;
bool idle_screen_active = false;
unsigned long paused_since_ms = 0;
unsigned long idle_block_until_ms = 0;
uint8_t focus_option_index = 0;
uint8_t brightness_option_index = 0;
uint8_t settings_selected_item = 0;
bool settings_snapshot_valid = false;
SessionMode settings_entry_mode = SessionMode::Focus;
bool settings_entry_running = false;
uint32_t settings_entry_session_seconds = 0;
uint32_t settings_entry_remaining_seconds = 0;

void saveSettingsAndExit(unsigned long now);

constexpr uint32_t FOCUS_OPTIONS[] = {25, 45, 50};
constexpr uint8_t FOCUS_OPTION_COUNT = sizeof(FOCUS_OPTIONS) / sizeof(FOCUS_OPTIONS[0]);
constexpr uint32_t BRIGHTNESS_OPTIONS[] = {30, 60, 100};
constexpr uint8_t BRIGHTNESS_OPTION_COUNT = sizeof(BRIGHTNESS_OPTIONS) / sizeof(BRIGHTNESS_OPTIONS[0]);
constexpr uint8_t SETTINGS_ITEM_COUNT = 5;

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
      stats_completed_focus_count(),
      settings_selected_item);
}

void resetCurrentSession() {
  const unsigned long now = millis();
  setRunning(false);
  resetTimerForCurrentSession(now);
  paused_since_ms = now;
  ui_render_reset_without_full_redraw();
  Serial.println("Session reset");
}

void enterSettings() {
  settings_active = true;
  idle_screen_active = false;
  settings_entry_mode = mode;
  settings_entry_running = running;
  settings_entry_session_seconds = sessionSeconds();
  settings_entry_remaining_seconds = remaining_seconds;
  settings_snapshot_valid = true;
  setRunning(false);
  focus_option_index = focusOptionIndexFor(config_get().focus_minutes);
  brightness_option_index = brightnessOptionIndexFor(config_get().brightness_percent);
  settings_selected_item = 0;
  drawSettings();
  Serial.println("Settings");
}

void changeSelectedSetting(unsigned long now) {
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
    resetTimerForCurrentSession(now);
    settings_snapshot_valid = false;
    settings_active = false;
    idle_screen_active = false;
    session_feedback_active = false;
    paused_since_ms = now;
    idle_block_until_ms = now + POST_SETTINGS_IDLE_BLOCK_MS;
    ui_draw_static_pomodoro_home();
    Serial.println("Settings reset");
    return;
  } else if (settings_selected_item == 3) {
    stats_clear_completed_focus();
    Serial.println("Settings stats cleared");
  } else if (settings_selected_item == 4) {
    saveSettingsAndExit(now);
    Serial.println("Settings save item");
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
  const uint32_t saved_focus_seconds = FOCUS_OPTIONS[focus_option_index] * 60UL;
  const uint32_t saved_break_seconds = config_get().break_minutes * 60UL;
  config_save(
      FOCUS_OPTIONS[focus_option_index],
      config_get().break_minutes,
      BRIGHTNESS_OPTIONS[brightness_option_index]);
  ui_apply_brightness(config_get().brightness_percent);
  if (settings_snapshot_valid) {
    mode = settings_entry_mode;
    const uint32_t elapsed_seconds =
        settings_entry_session_seconds > settings_entry_remaining_seconds
            ? settings_entry_session_seconds - settings_entry_remaining_seconds
            : 0;

    if (mode == SessionMode::Focus) {
      setCurrentSessionSeconds(saved_focus_seconds);

      if (saved_focus_seconds >= settings_entry_session_seconds ||
          settings_entry_remaining_seconds > saved_focus_seconds) {
        // Preserve elapsed Focus time when extending, or when the old
        // remaining time is still longer than the newly saved Focus length.
        remaining_seconds =
            saved_focus_seconds > elapsed_seconds ? saved_focus_seconds - elapsed_seconds : 0;
      } else {
        // If the current remaining time already fits inside the newly saved
        // shorter Focus length, keep the user's current countdown position.
        remaining_seconds = settings_entry_remaining_seconds;
      }
    } else {
      setCurrentSessionSeconds(saved_break_seconds);
      remaining_seconds = settings_entry_remaining_seconds;
    }

    setRunning(settings_entry_running && remaining_seconds > 0);
    restartCountdownAt(now);
  } else {
    setRunning(false);
    resetTimerForCurrentSession(now);
  }
  settings_snapshot_valid = false;
  settings_active = false;
  idle_screen_active = false;
  session_feedback_active = false;
  paused_since_ms = now;
  idle_block_until_ms = now + POST_SETTINGS_IDLE_BLOCK_MS;
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
    paused_since_ms = now;
    ui_draw_static_pomodoro_home();
  }
}

void switchSession(unsigned long now) {
  toggleSessionMode();
  setRunning(false);
  resetTimerForCurrentSession(now);
  paused_since_ms = now;
  beginSessionFeedback(now);
  Serial.printf("Switched to %s\n", modeLabel());
}

void updateIdleScreen(unsigned long now) {
  if (settings_active || session_feedback_active || running) {
    paused_since_ms = now;
    return;
  }

  if (static_cast<long>(now - idle_block_until_ms) < 0) {
    paused_since_ms = now;
    return;
  }

  if (idle_screen_active) {
    return;
  }

  if (now - paused_since_ms >= IDLE_SCREEN_DELAY_MS) {
    idle_screen_active = true;
    ui_draw_idle_screen();
    Serial.println("Idle screen");
  }
}

void wakeFromIdleToFocus(unsigned long now) {
  idle_screen_active = false;
  if (mode != SessionMode::Focus) {
    mode = SessionMode::Focus;
    resetTimerForCurrentSession(now);
  }
  setRunning(true);
  restartCountdownAt(now);
  ui_draw_static_pomodoro_home();
  Serial.println("Idle wake to focus");
}

void updateCountdown(unsigned long now) {
  const bool changed = advanceTimer(now);

  if (changed) {
    ui_render_tick();
  }

  if (isTimerComplete()) {
    Serial.println("Session complete");
    if (mode == SessionMode::Focus) {
      const uint32_t completed = stats_increment_completed_focus();
      Serial.printf("Completed focus sessions=%lu\n", completed);
    }
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
      Serial.printf("Settings long press ignored: x=%u y=%u\n", touch.x, touch.y);
    }

    if (event == InputEvent::ShortPress) {
      if (touch.x < 120) {
        switchSettingsItem();
      } else {
        changeSelectedSetting(now);
      }
      Serial.printf("Settings tap: x=%u y=%u\n", touch.x, touch.y);
    }

    return;
  }

  if (event == InputEvent::LongPress) {
    idle_screen_active = false;
    enterSettings();
    Serial.printf("Long press settings: x=%u y=%u\n", touch.x, touch.y);
  }

  if (event == InputEvent::ShortPress) {
    if (idle_screen_active) {
      wakeFromIdleToFocus(now);
      Serial.printf("Idle tap: x=%u y=%u\n", touch.x, touch.y);
      return;
    }

    toggleRunning();
    restartCountdownAt(now);
    ui_draw_status_text();
    if (!running) {
      paused_since_ms = now;
    }

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
  stats_begin();
  timer_begin(millis());
  camera_test_begin_configured_mode();
  ui_apply_brightness(config_get().brightness_percent);
  microphone_begin();
  input_begin();
  input_scan_i2c();

  if (!ui_begin()) {
    Serial.println("Display init failed");
    return;
  }

  ui_draw_static_pomodoro_home();
  paused_since_ms = millis();

  Serial.println("Display initialized");
}

void loop() {
  const unsigned long now = millis();

  finishSessionFeedbackIfReady(now);
  handleInput(now);
  if (!session_feedback_active && !settings_active) {
    updateCountdown(now);
  }
  updateIdleScreen(now);
  microphone_update(now);
  camera_test_update(now);
  if (!settings_active && !session_feedback_active && !idle_screen_active) {
    ui_draw_microphone_status(microphone_level_label());
  }
  if (!settings_active) {
    printStatusEverySecond(now);
  }
  microphone_print_level(now);
}

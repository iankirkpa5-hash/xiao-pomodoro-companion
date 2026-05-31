#include <Arduino.h>
#include "app_state.h"
#include "config.h"
#include "input.h"
#include "pomodoro_timer.h"
#include "ui.h"

constexpr int BACKLIGHT_BRIGHTNESS = 150;
constexpr uint32_t SESSION_FEEDBACK_MS = 2000;

bool session_feedback_active = false;
unsigned long session_feedback_until_ms = 0;

void resetCurrentSession() {
  setRunning(false);
  resetTimerForCurrentSession(millis());
  ui_render_reset_without_full_redraw();
  Serial.println("Session reset");
}

void beginSessionFeedback(unsigned long now) {
  session_feedback_active = true;
  session_feedback_until_ms = now + SESSION_FEEDBACK_MS;
  ui_draw_session_feedback(mode);
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
  if (event == InputEvent::LongPress) {
    resetCurrentSession();
    Serial.printf("Long press reset: x=%u y=%u\n", touch.x, touch.y);
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

  config_begin();
  timer_begin(millis());
  ui_set_backlight(BACKLIGHT_BRIGHTNESS);
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
  if (!session_feedback_active) {
    updateCountdown(now);
  }
  printStatusEverySecond(now);
}

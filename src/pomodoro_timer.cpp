#include "pomodoro_timer.h"

#include "config.h"

uint32_t remaining_seconds = DEFAULT_FOCUS_MINUTES * 60UL;
static unsigned long last_countdown_ms = 0;
static uint32_t current_session_seconds = DEFAULT_FOCUS_MINUTES * 60UL;

uint32_t configuredSessionSeconds() {
  const PomodoroConfig &config = config_get();
  return (mode == SessionMode::Focus ? config.focus_minutes : config.break_minutes) * 60UL;
}

void timer_begin(unsigned long now) {
  resetTimerForCurrentSession(now);
}

uint32_t sessionSeconds() {
  return current_session_seconds;
}

void setCurrentSessionSeconds(uint32_t seconds) {
  current_session_seconds = seconds;
}

void resetTimerForCurrentSession(unsigned long now) {
  current_session_seconds = configuredSessionSeconds();
  remaining_seconds = current_session_seconds;
  last_countdown_ms = now;
}

void restartCountdownAt(unsigned long now) {
  last_countdown_ms = now;
}

bool advanceTimer(unsigned long now) {
  if (!running) {
    last_countdown_ms = now;
    return false;
  }

  const unsigned long delta_ms = now - last_countdown_ms;
  if (static_cast<long>(delta_ms) < 1000) {
    return false;
  }

  const uint32_t elapsed = delta_ms / 1000;
  last_countdown_ms += elapsed * 1000;

  if (elapsed >= remaining_seconds) {
    remaining_seconds = 0;
  } else {
    remaining_seconds -= elapsed;
  }

  return true;
}

bool isTimerComplete() {
  return remaining_seconds == 0;
}


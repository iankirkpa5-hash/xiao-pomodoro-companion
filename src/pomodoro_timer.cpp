#include "pomodoro_timer.h"

uint32_t remaining_seconds = FOCUS_SECONDS;
static unsigned long last_countdown_ms = 0;

uint32_t sessionSeconds() {
  return mode == SessionMode::Focus ? FOCUS_SECONDS : BREAK_SECONDS;
}

void resetTimerForCurrentSession(unsigned long now) {
  remaining_seconds = sessionSeconds();
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

  if (now - last_countdown_ms < 1000) {
    return false;
  }

  const uint32_t elapsed = (now - last_countdown_ms) / 1000;
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


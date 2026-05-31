#include "app_state.h"

SessionMode mode = SessionMode::Focus;
bool running = false;

const char *modeLabel() {
  return mode == SessionMode::Focus ? "Focus" : "Break";
}

void toggleSessionMode() {
  mode = mode == SessionMode::Focus ? SessionMode::Break : SessionMode::Focus;
}

void setRunning(bool value) {
  running = value;
}

void toggleRunning() {
  running = !running;
}


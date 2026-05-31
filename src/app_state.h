#pragma once

enum class SessionMode {
  Focus,
  Break,
};

extern SessionMode mode;
extern bool running;

const char *modeLabel();
void toggleSessionMode();
void setRunning(bool value);
void toggleRunning();


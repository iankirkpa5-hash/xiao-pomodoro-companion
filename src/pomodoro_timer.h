#pragma once

#include <Arduino.h>
#include "app_state.h"

constexpr uint32_t FOCUS_SECONDS = 25UL * 60UL;
constexpr uint32_t BREAK_SECONDS = 5UL * 60UL;

extern uint32_t remaining_seconds;

uint32_t sessionSeconds();
void resetTimerForCurrentSession(unsigned long now);
void restartCountdownAt(unsigned long now);
bool advanceTimer(unsigned long now);
bool isTimerComplete();


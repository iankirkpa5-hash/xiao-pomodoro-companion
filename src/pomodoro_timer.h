#pragma once

#include <Arduino.h>
#include "app_state.h"

extern uint32_t remaining_seconds;

void timer_begin(unsigned long now);
uint32_t sessionSeconds();
void resetTimerForCurrentSession(unsigned long now);
void restartCountdownAt(unsigned long now);
bool advanceTimer(unsigned long now);
bool isTimerComplete();


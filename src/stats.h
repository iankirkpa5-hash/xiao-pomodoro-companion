#pragma once

#include <Arduino.h>

void stats_begin();
uint32_t stats_completed_focus_count();
uint32_t stats_increment_completed_focus();

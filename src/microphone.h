#pragma once

#include <Arduino.h>

struct MicrophoneLevel {
  bool ok;
  float rms;
  int32_t peak;
  int32_t mean;
  size_t samples;
};

bool microphone_begin();
MicrophoneLevel microphone_read_level();
void microphone_print_level(unsigned long now);

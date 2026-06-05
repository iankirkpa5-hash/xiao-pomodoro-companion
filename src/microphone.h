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
bool microphone_update(unsigned long now);
MicrophoneLevel microphone_read_level();
MicrophoneLevel microphone_level();
const char *microphone_level_label();
void microphone_print_level(unsigned long now);

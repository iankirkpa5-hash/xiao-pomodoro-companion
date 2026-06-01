#include "stats.h"

#include <Preferences.h>

namespace {
constexpr const char *STATS_NAMESPACE = "pomo_stats";
constexpr const char *COMPLETED_FOCUS_KEY = "focus_done";

Preferences preferences;
uint32_t completed_focus_count = 0;
}  // namespace

void stats_begin() {
  preferences.begin(STATS_NAMESPACE, false);

  const bool has_completed_focus = preferences.isKey(COMPLETED_FOCUS_KEY);
  completed_focus_count = preferences.getUInt(COMPLETED_FOCUS_KEY, 0);

  if (!has_completed_focus) {
    preferences.putUInt(COMPLETED_FOCUS_KEY, completed_focus_count);
  }

  Serial.printf("Stats: completed_focus=%lu\n", completed_focus_count);
}

uint32_t stats_completed_focus_count() {
  return completed_focus_count;
}

uint32_t stats_increment_completed_focus() {
  completed_focus_count++;
  preferences.putUInt(COMPLETED_FOCUS_KEY, completed_focus_count);
  return completed_focus_count;
}

void stats_clear_completed_focus() {
  completed_focus_count = 0;
  preferences.putUInt(COMPLETED_FOCUS_KEY, completed_focus_count);
}

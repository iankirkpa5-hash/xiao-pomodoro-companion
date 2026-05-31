#include "prompts.h"

#include <Arduino.h>

namespace {
const char *const PROMPTS[] = {
    "Blink slowly",
    "Relax shoulders",
    "Stretch neck",
    "Look far away",
    "Drink water",
    "Breathe deeply",
    "Roll wrists",
    "Stand up",
    "Rest your eyes",
    "Unclench jaw",
};

constexpr uint8_t PROMPT_COUNT = sizeof(PROMPTS) / sizeof(PROMPTS[0]);
uint8_t last_prompt_index = 255;
}  // namespace

const char *prompts_next() {
  uint8_t index = random(PROMPT_COUNT);
  if (PROMPT_COUNT > 1 && index == last_prompt_index) {
    index = (index + 1) % PROMPT_COUNT;
  }

  last_prompt_index = index;
  return PROMPTS[index];
}


#pragma once
using TaskHandle_t = void *;
constexpr int pdPASS = 1;
inline bool fake_task_failure = false;
inline int xTaskCreate(void (*)(void *), const char *, unsigned, void *, unsigned, TaskHandle_t *task) {
  if (fake_task_failure) return 0;
  *task = reinterpret_cast<void *>(1);
  return pdPASS;
}
inline void vTaskDelay(unsigned) {}

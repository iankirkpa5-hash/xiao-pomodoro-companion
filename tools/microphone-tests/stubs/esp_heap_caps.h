#pragma once
#include <cstdlib>
#include <cstddef>
#include <cassert>
constexpr int MALLOC_CAP_SPIRAM = 1, MALLOC_CAP_8BIT = 2;
inline bool fake_no_psram = false;
inline int fake_allocations = 0;
inline size_t heap_caps_get_free_size(int) { return fake_no_psram ? 0 : 8 * 1024 * 1024; }
inline void *heap_caps_malloc(size_t size, int caps) {
  assert(caps == (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (fake_no_psram) return nullptr;
  void *p = malloc(size);
  if (p) ++fake_allocations;
  return p;
}
inline void heap_caps_free(void *p) {
  if (p) --fake_allocations;
  free(p);
}

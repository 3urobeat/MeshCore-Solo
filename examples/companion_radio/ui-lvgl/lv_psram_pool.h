#pragma once
// LVGL's builtin allocator pool, placed in PSRAM (see lv_conf.h).
#include <esp_heap_caps.h>

static inline void* lv_psram_pool_alloc(size_t sz) {
  return heap_caps_malloc(sz, MALLOC_CAP_SPIRAM);
}

// LVGL 9 configuration for the ui-lvgl frontend. Found through
// -D LV_CONF_INCLUDE_SIMPLE + -I examples/companion_radio/ui-lvgl; anything not
// set here takes LVGL's default from lv_conf_internal.h.
#ifndef LV_CONF_H
#define LV_CONF_H

#define LV_COLOR_DEPTH 16

// Memory: LVGL's own allocator over one pool. On ESP32-S3 the pool lives in
// PSRAM so widgets / text never compete with BLE and the radio for internal RAM.
#define LV_USE_STDLIB_MALLOC    LV_STDLIB_BUILTIN
#define LV_USE_STDLIB_STRING    LV_STDLIB_CLIB
#define LV_USE_STDLIB_SPRINTF   LV_STDLIB_CLIB
#define LV_MEM_SIZE             (1024U * 1024U)
#if defined(ESP32)
  #define LV_MEM_ADR            0
  #define LV_MEM_POOL_INCLUDE   "lv_psram_pool.h"
  #define LV_MEM_POOL_ALLOC     lv_psram_pool_alloc
#endif

#define LV_USE_OS               LV_OS_NONE
#define LV_DEF_REFR_PERIOD      20     // ms; the loop also services the radio
#define LV_DPI_DEF              130    // 2.8" 320x240

#define LV_USE_LOG              0
#define LV_USE_ASSERT_NULL      1
#define LV_USE_ASSERT_MALLOC    1

// Fonts. Montserrat is ASCII + a few symbols only -- Polish / Cyrillic
// coverage needs a generated font (lv_font_conv), planned with the theme work.
#define LV_FONT_MONTSERRAT_12   1
#define LV_FONT_MONTSERRAT_14   1
#define LV_FONT_MONTSERRAT_16   1
#define LV_FONT_MONTSERRAT_20   1
#define LV_FONT_MONTSERRAT_40   1
#define LV_FONT_DEFAULT         &lv_font_montserrat_14

#define LV_USE_THEME_DEFAULT    1
#define LV_THEME_DEFAULT_DARK   1

// Not needed by the frontend (smaller build).
#define LV_USE_CHART            0
#define LV_USE_CALENDAR         0
#define LV_USE_SPAN             0
#define LV_USE_TABLE            0
#define LV_USE_SCALE            0
#define LV_USE_LOTTIE           0
#define LV_BUILD_EXAMPLES       0
#define LV_BUILD_DEMOS          0

#endif // LV_CONF_H

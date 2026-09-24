#pragma once
// LVGL <-> board glue: display flush and pointer input. One implementation per
// display stack; the rest of ui-lvgl never touches the hardware directly.
//
// Single-TU fragment: included by ui-lvgl/UITask.cpp only.

namespace lvport {

#if defined(SEEED_WIO_TRACKER_L2)
// LovyanGFX device (NV3031B QSPI panel + GT911 touch) from WioTrackerL2Display.

static lgfx::LGFX_Device* s_gfx = nullptr;
static bool s_swallow = false;   // ignore the touch that woke the display until it lifts

static void flushCb(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map) {
  int w = area->x2 - area->x1 + 1;
  int h = area->y2 - area->y1 + 1;
  s_gfx->startWrite();
  s_gfx->setAddrWindow(area->x1, area->y1, w, h);
  s_gfx->pushPixels((uint16_t*)px_map, (uint32_t)w * h, true /* LVGL RGB565 is little-endian */);
  s_gfx->endWrite();
  lv_display_flush_ready(disp);
}

static void touchCb(lv_indev_t* indev, lv_indev_data_t* data) {
  (void)indev;
  lgfx::touch_point_t tp;
  bool down = s_gfx->getTouch(&tp, 1) > 0;
  if (s_swallow) {
    if (!down) s_swallow = false;
    data->state = LV_INDEV_STATE_RELEASED;
    return;
  }
  if (down) {
    data->state = LV_INDEV_STATE_PRESSED;
    data->point.x = tp.x;
    data->point.y = tp.y;
  } else {
    data->state = LV_INDEV_STATE_RELEASED;
  }
}

static bool begin() {
  s_gfx = display.lgfxDevice();
  s_gfx->setColorDepth(16);

  lv_display_t* disp = lv_display_create(s_gfx->width(), s_gfx->height());
  lv_display_set_flush_cb(disp, flushCb);

  // Two 40-line partial buffers (25 KB each), PSRAM first.
  const uint32_t buf_sz = (uint32_t)s_gfx->width() * 40 * 2;
  uint8_t* buf1 = (uint8_t*)heap_caps_malloc(buf_sz, MALLOC_CAP_SPIRAM);
  uint8_t* buf2 = (uint8_t*)heap_caps_malloc(buf_sz, MALLOC_CAP_SPIRAM);
  if (!buf1) { buf1 = (uint8_t*)heap_caps_malloc(buf_sz, MALLOC_CAP_8BIT); buf2 = nullptr; }
  if (!buf1) return false;
  lv_display_set_buffers(disp, buf1, buf2, buf_sz, LV_DISPLAY_RENDER_MODE_PARTIAL);

  lv_indev_t* indev = lv_indev_create();
  lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(indev, touchCb);
  return true;
}

// Raw panel touch, read while LVGL is paused (display asleep).
static bool touched() {
  lgfx::touch_point_t tp;
  return s_gfx->getTouch(&tp, 1) > 0;
}

static void swallowTouch() { s_swallow = true; }

#else
  #error "ui-lvgl: no LVGL port for this board (see LvglPort.h)"
#endif

}  // namespace lvport

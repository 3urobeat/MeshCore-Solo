#pragma once
// Decoded map tiles (RGB565, 128 KB each) kept for reuse while panning / zooming
// back, least-recently-used evicted. Buffers live in PSRAM on the ESP32 and are
// allocated on first use, then kept for the session.
//
// Single-TU fragment: included by ui-lvgl/UITask.cpp only.

#if defined(ESP32)
  #include <esp_heap_caps.h>
#endif

extern "C" void lv_image_cache_drop(const void* src);   // not exported through lvgl.h here

namespace mapview {

class TileCache {
public:
  static const int SLOTS = 12;   // 6 on screen at 320x218 + a ring of neighbours

  struct Slot {
    int16_t z = -1;
    int32_t x = 0, y = 0;
    bool    present = false;     // false: looked up, there is no tile (don't retry)
    uint32_t used = 0;
    uint16_t* px = nullptr;
    lv_image_dsc_t dsc;
  };

  // The slot holding z/x/y if it was loaded (present or known missing), else nullptr.
  Slot* find(int z, int x, int y) {
    for (Slot& s : _slots)
      if (s.z == z && s.x == x && s.y == y) { s.used = ++_tick; return &s; }
    return nullptr;
  }

  // Render z/x/y through `src` into the least recently used slot.
  Slot* load(TileProvider& src, int z, int x, int y) {
    Slot* s = &_slots[0];
    for (Slot& c : _slots) if (c.used < s->used) s = &c;
    if (!s->px) {
#if defined(ESP32)
      s->px = (uint16_t*)heap_caps_malloc(TILE_PX * TILE_PX * 2, MALLOC_CAP_SPIRAM);
#else
      s->px = (uint16_t*)malloc(TILE_PX * TILE_PX * 2);
#endif
      if (!s->px) return nullptr;
    }
    lv_image_cache_drop(&s->dsc);   // same descriptor, new pixels
    s->z = z; s->x = x; s->y = y;
    s->used = ++_tick;
    s->present = src.renderTile(z, x, y, s->px);
    memset(&s->dsc, 0, sizeof(s->dsc));
    s->dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    s->dsc.header.cf = LV_COLOR_FORMAT_RGB565;
    s->dsc.header.w = TILE_PX;
    s->dsc.header.h = TILE_PX;
    s->dsc.header.stride = TILE_PX * 2;
    s->dsc.data_size = TILE_PX * TILE_PX * 2;
    s->dsc.data = (const uint8_t*)s->px;
    return s;
  }

  // Forget the "no tile here" answers (new tiles may have been written).
  void forgetMissing() { for (Slot& s : _slots) if (s.z >= 0 && !s.present) { s.z = -1; s.used = 0; } }

  // Forget everything (e.g. the card was swapped); keeps the buffers.
  void invalidate() { for (Slot& s : _slots) { s.z = -1; s.used = 0; } }

private:
  Slot _slots[SLOTS];
  uint32_t _tick = 0;
};

}  // namespace mapview

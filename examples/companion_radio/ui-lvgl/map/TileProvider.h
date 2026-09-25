#pragma once
// Map tile sources. The map screen, its tile cache and the overlays only ever
// call renderTile(): 256x256 RGB565 pixels for Web-Mercator tile z/x/y. The
// raster provider decodes PNG files from the card; a vector renderer would be
// another TileProvider drawing into the same buffer, with nothing else changing.
//
// Single-TU fragment: included by ui-lvgl/UITask.cpp only.

#include <stdio.h>
#include <sys/stat.h>
// LVGL's copy of lodepng (LV_USE_LODEPNG), compiled as C. It is patched for
// LVGL: the decode "output" is an lv_draw_buf_t* (32-bit RGBA pixels in ->data),
// not a plain pixel array, and is freed with lv_draw_buf_destroy().
#define LODEPNG_NO_COMPILE_CPP
extern "C" {
#include "src/libs/lodepng/lodepng.h"
}

namespace mapview {

static const int TILE_PX = 256;

class TileProvider {
public:
  virtual ~TileProvider() {}
  virtual bool available() = 0;                                   // anything to draw at all
  virtual bool renderTile(int z, int x, int y, uint16_t* out) = 0; // false: no tile there
  virtual const char* attribution() const = 0;
};

// Raster tiles under <root>/{z}/{x}/{y}.png -- the Meshtastic MUI layout, so
// cards prepared for either work for both (tools/maps/fetch_tiles.py writes
// it) -- or packed per column in <root>/{z}/{x}.pak ('TPK1': y_min, y_max,
// offsets[n+1], PNG blobs; the format of upstream PR #3381's tile_packer.py),
// which copies onto a FAT card far faster than millions of loose files.
class RasterTileProvider : public TileProvider {
public:
  explicit RasterTileProvider(const char* root) : _root(root) {}

  // Also reads <root>/attribution.txt (written by tools/maps/fetch_tiles.py and
  // the on-device downloader): the tile set's credit line, shown on the map.
  bool available() override {
    struct stat st;
    if (stat(_root, &st) != 0 || !S_ISDIR(st.st_mode)) return false;
    char path[64];
    snprintf(path, sizeof(path), "%s/attribution.txt", _root);
    _attr[0] = '\0';
    if (FILE* f = fopen(path, "r")) {
      size_t n = fread(_attr, 1, sizeof(_attr) - 1, f);
      fclose(f);
      while (n > 0 && (_attr[n - 1] == '\n' || _attr[n - 1] == '\r')) n--;
      _attr[n] = '\0';
    }
    return true;
  }

  bool renderTile(int z, int x, int y, uint16_t* out) override {
    uint32_t len = 0;
    uint8_t* png = readLoose(z, x, y, len);
    if (!png) png = readPacked(z, x, y, len);
    if (!png) return false;

    unsigned char* res = nullptr;
    unsigned w = 0, h = 0;
    unsigned err = lodepng_decode32(&res, &w, &h, png, len);
    lv_free(png);
    lv_draw_buf_t* db = (lv_draw_buf_t*)res;
    if (err || !db) { if (db) lv_draw_buf_destroy(db); return false; }
    if (w != TILE_PX || h != TILE_PX) { lv_draw_buf_destroy(db); return false; }

    for (int row = 0; row < TILE_PX; row++) {
      const uint8_t* p = db->data + row * db->header.stride;   // R, G, B, A
      uint16_t* o = out + row * TILE_PX;
      for (int i = 0; i < TILE_PX; i++, p += 4)
        o[i] = (uint16_t)(((p[0] & 0xF8) << 8) | ((p[1] & 0xFC) << 3) | (p[2] >> 3));
    }
    lv_draw_buf_destroy(db);
    return true;
  }

  const char* attribution() const override { return _attr[0] ? _attr : "\xC2\xA9 OpenStreetMap contributors"; }

private:
  const char* _root;
  char _attr[96] = "";

  static uint8_t* readRange(FILE* f, long off, uint32_t len) {
    if (len == 0 || len > 512 * 1024) return nullptr;
    uint8_t* buf = (uint8_t*)lv_malloc(len);
    if (!buf) return nullptr;
    if (fseek(f, off, SEEK_SET) != 0 || fread(buf, 1, len, f) != len) { lv_free(buf); return nullptr; }
    return buf;
  }

  uint8_t* readLoose(int z, int x, int y, uint32_t& len) {
    char path[64];
    snprintf(path, sizeof(path), "%s/%d/%d/%d.png", _root, z, x, y);
    FILE* f = fopen(path, "rb");
    if (!f) return nullptr;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    uint8_t* buf = sz > 0 ? readRange(f, 0, (uint32_t)sz) : nullptr;
    fclose(f);
    if (buf) len = (uint32_t)sz;
    return buf;
  }

  uint8_t* readPacked(int z, int x, int y, uint32_t& len) {
    char path[64];
    snprintf(path, sizeof(path), "%s/%d/%d.pak", _root, z, x);
    FILE* f = fopen(path, "rb");
    if (!f) return nullptr;
    uint8_t hdr[12];
    uint32_t y0, y1, off[2];
    uint8_t* buf = nullptr;
    if (fread(hdr, 1, 12, f) == 12 && memcmp(hdr, "TPK1", 4) == 0) {
      memcpy(&y0, hdr + 4, 4);
      memcpy(&y1, hdr + 8, 4);
      if (y >= (int)y0 && y <= (int)y1 &&
          fseek(f, 12 + 4 * (y - (int)y0), SEEK_SET) == 0 && fread(off, 4, 2, f) == 2 && off[1] > off[0]) {
        len = off[1] - off[0];
        buf = readRange(f, off[0], len);
      }
    }
    fclose(f);
    return buf;
  }
};

}  // namespace mapview

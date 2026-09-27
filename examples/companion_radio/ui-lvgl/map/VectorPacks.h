#pragma once
// Vector map regions: packs (/sdcard/vmap/*.vpk from tools/maps/osm_vector.py
// --pack) read in place -- one file per region, its index of tiles and points
// in PSRAM. Loose files ({dz}/{x}/{y}.vt / .vp in the same folder, the tool
// without --pack) still work; a pack is looked in first.
//
// Pack: a 64-byte header ('VPK1', count, index and data offsets, box in
// degrees x 1e6, zooms, flags, name), `count` sorted 16-byte index entries
// (kind, dz, x, y, offset, length), then the files.
//
// Single-TU fragment: included by ui-lvgl/MapScreen.h before VectorTileProvider.h.

#include <dirent.h>

namespace mapview {

static const char* const VECTOR_ROOT = "/sdcard/vmap";

class VectorPacks {
public:
  enum : uint8_t { K_TILE = 0, K_POINTS = 1 };
  enum : uint8_t { F_CONTOURS = 1 };
  static const int MAX = 16;

  struct Pack {
    char file[48];     // name in VECTOR_ROOT
    char name[29];     // the region's
    int32_t box[4];    // lon0 lat0 lon1 lat1, degrees x 1e6
    uint8_t zmin, zmax, flags;
    uint32_t count, index_off, data_off, size;
    uint8_t* index;    // count x 16 bytes (PSRAM); null: too big, searched in the file
  };

  // Where one file of the data is: read `len` bytes from `f` (already at its
  // start), then done() -- a pack's file stays open for the next read.
  struct Src { FILE* f = nullptr; uint32_t len = 0; bool owned = false; };

  // Lists the packs again (the map opening, a download finished).
  void scan() {
    closeOpen();
    for (int i = 0; i < _n; i++) free(_p[i].index);
    _n = 0;
    DIR* d = opendir(VECTOR_ROOT);
    if (!d) return;
    while (struct dirent* e = readdir(d)) {
      size_t l = strlen(e->d_name);
      if (_n >= MAX || l < 5 || l >= sizeof(_p[0].file) || strcasecmp(e->d_name + l - 4, ".vpk") != 0) continue;
      if (open(_p[_n], e->d_name)) _n++;
    }
    closedir(d);
  }

  int count() const { return _n; }
  const Pack& at(int i) const { return _p[i]; }

  bool find(uint8_t kind, int dz, int x, int y, Src& s) {
    uint64_t key = keyOf(kind, dz, x, y);
    for (int i = 0; i < _n; i++) {
      Pack& p = _p[i];
      uint32_t off, len;
      if (!lookup(p, key, off, len)) continue;
      FILE* f = handle(i);
      if (!f || fseek(f, p.data_off + off, SEEK_SET) != 0) return false;
      s.f = f; s.len = len; s.owned = false;
      return true;
    }
    char path[64];   // a loose file
    snprintf(path, sizeof(path), "%s/%d/%d/%d.%s", VECTOR_ROOT, dz, x, y, kind == K_TILE ? "vt" : "vp");
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    s.f = f; s.len = sz > 0 ? (uint32_t)sz : 0; s.owned = true;
    return true;
  }
  void done(Src& s) {
    if (s.owned && s.f) fclose(s.f);
    s.f = nullptr;
  }

  // Before a pack file is replaced or deleted.
  void closeOpen() {
    if (_open_f) fclose(_open_f);
    _open_f = nullptr;
    _open_i = -1;
  }

private:
  Pack _p[MAX];
  int _n = 0;
  FILE* _open_f = nullptr;   // the pack read last
  int _open_i = -1;
  static const uint32_t INDEX_IN_RAM_MAX = 2u * 1024 * 1024;

  static uint64_t keyOf(uint8_t kind, int dz, int x, int y) {
    return ((uint64_t)kind << 40) | ((uint64_t)(dz & 0xFF) << 32) | ((uint64_t)(x & 0xFFFF) << 16) | (uint64_t)(y & 0xFFFF);
  }
  static uint64_t keyAt(const uint8_t* e) {
    return keyOf(e[0], e[1], e[2] | (e[3] << 8), e[4] | (e[5] << 8));
  }
  static uint32_t u32(const uint8_t* b) { return b[0] | (b[1] << 8) | (b[2] << 16) | ((uint32_t)b[3] << 24); }

  FILE* handle(int i) {
    if (_open_i == i && _open_f) return _open_f;
    closeOpen();
    char path[64];
    snprintf(path, sizeof(path), "%s/%s", VECTOR_ROOT, _p[i].file);
    _open_f = fopen(path, "rb");
    _open_i = _open_f ? i : -1;
    return _open_f;
  }

  bool open(Pack& p, const char* file) {
    char path[64];
    snprintf(path, sizeof(path), "%s/%s", VECTOR_ROOT, file);
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    uint8_t h[64];
    bool ok = fread(h, 1, 64, f) == 64 && memcmp(h, "VPK1", 4) == 0;
    if (ok) {
      snprintf(p.file, sizeof(p.file), "%s", file);
      p.count = u32(h + 4); p.index_off = u32(h + 8); p.data_off = u32(h + 12);
      for (int k = 0; k < 4; k++) p.box[k] = (int32_t)u32(h + 16 + 4 * k);
      p.zmin = h[32]; p.zmax = h[33]; p.flags = h[34];
      memcpy(p.name, h + 36, 28);
      p.name[28] = '\0';
      fseek(f, 0, SEEK_END);
      p.size = (uint32_t)ftell(f);
      p.index = nullptr;
      uint32_t isz = p.count * 16;
      ok = p.index_off + isz <= p.size && p.data_off <= p.size;
      if (ok && isz <= INDEX_IN_RAM_MAX) {
        p.index = psramBuf<uint8_t>(isz ? isz : 1);
        if (p.index && (fseek(f, p.index_off, SEEK_SET) != 0 || !readFast(f, p.index, isz))) { free(p.index); p.index = nullptr; }
      }
    }
    fclose(f);
    return ok;
  }

  // Binary search of the index (in RAM, or read entry by entry).
  bool lookup(Pack& p, uint64_t key, uint32_t& off, uint32_t& len) {
    int lo = 0, hi = (int)p.count - 1;
    uint8_t e[16];
    FILE* f = nullptr;
    while (lo <= hi) {
      int mid = (lo + hi) / 2;
      const uint8_t* ent;
      if (p.index) {
        ent = p.index + 16 * mid;
      } else {
        if (!f) f = handle((int)(&p - _p));
        if (!f || fseek(f, p.index_off + 16 * mid, SEEK_SET) != 0 || fread(e, 1, 16, f) != 16) return false;
        ent = e;
      }
      uint64_t k = keyAt(ent);
      if (k == key) { off = u32(ent + 8); len = u32(ent + 12); return true; }
      if (k < key) lo = mid + 1; else hi = mid - 1;
    }
    return false;
  }
};

static VectorPacks s_vpacks;

}  // namespace mapview

#pragma once
// Live map tiles (fetched while browsing online) live apart from downloaded
// maps, under LIVE_ROOT/{z}/{x}/{y}.png, and only up to a size limit
// (Settings > Storage): an index remembers the order they came in, and the
// oldest are deleted once the tiles add up to more than the limit. Maps
// downloaded on purpose (TileDownloader jobs, the PC tool) are never touched.
//
// Index: LIVE_ROOT/index.bin, a header and a ring of RING records. Eviction
// runs a few files per loop (service()), so lowering the limit or clearing
// thousands of tiles never blocks the UI.
//
// Single-TU fragment: included by ui-lvgl/UITask.cpp only (after LvglPort.h).

#include <stdio.h>
#include <sys/stat.h>

namespace mapview {

static const char* const LIVE_ROOT = "/sdcard/maps-live";

// Settings > Storage > Live map tiles: "Keep up to".
static const uint32_t LIVE_CAP_MB[] = { 16, 64, 256, 1024 };
static const int LIVE_CAP_COUNT = sizeof(LIVE_CAP_MB) / sizeof(LIVE_CAP_MB[0]);
static const int LIVE_CAP_DEFAULT = 1;
static const char* const LIVE_CAP_OPTS = "16 MB\n64 MB\n256 MB\n1 GB";

class LiveCache {
public:
  static const uint32_t RING = 16384;   // tiles; ~400 MB of typical tiles, 256 KB of index

  void setLimit(uint64_t bytes) { _limit = bytes; }
  uint64_t limit() const { return _limit; }
  uint64_t bytes() { load(); return _h.bytes; }
  uint32_t count() { load(); return _h.count; }
  bool clearing() const { return _clearing; }

  // A tile was written to LIVE_ROOT: remember it (eviction follows in service()).
  void add(int z, int x, int y, uint32_t size) {
    load();
    if (_h.count >= RING) evictOne();   // the ring is full: make room right away
    Rec r = { (int32_t)z, x, y, size };
    uint32_t slot = (_h.head + _h.count) % RING;
    _h.count++;
    _h.bytes += size;
    writeRec(slot, r);
    writeHdr();
  }

  // Delete every live tile (a few per service() call).
  void clearAll() { load(); _clearing = true; }

  // Evicts while over the limit (or clearing), for up to `budget_ms`.
  void service(uint32_t budget_ms) {
    if (!_loaded && !_clearing) return;   // nothing written or asked for yet
    load();
    uint32_t t0 = millis();
    bool changed = false;
    while ((_clearing ? _h.count > 0 : _h.bytes > _limit) && millis() - t0 < budget_ms) {
      evictOne(false);
      changed = true;
    }
    if (changed) writeHdr();
    if (_clearing && _h.count == 0) { _clearing = false; _h.bytes = 0; writeHdr(); }
  }

  static void tilePath(char* out, size_t n, int z, int x, int y) {
    snprintf(out, n, "%s/%d/%d/%d.png", LIVE_ROOT, z, x, y);
  }

private:
  struct Hdr { uint32_t magic, ring, head, count; uint64_t bytes; };
  struct Rec { int32_t z, x, y; uint32_t size; };
  static const uint32_t MAGIC = 0x3143544C;   // "LTC1"

  Hdr  _h = { MAGIC, RING, 0, 0, 0 };
  bool _loaded = false, _clearing = false;
  uint64_t _limit = 64ULL << 20;

  static void indexPath(char* out, size_t n) { snprintf(out, n, "%s/index.bin", LIVE_ROOT); }

  void load() {
    if (_loaded) return;
    _loaded = true;
    char p[128];
    indexPath(p, sizeof(p));
    FILE* f = fopen(p, "rb");
    Hdr h;
    if (f && fread(&h, sizeof(h), 1, f) == 1 && h.magic == MAGIC && h.ring == RING && h.count <= RING && h.head < RING) _h = h;
    else _h = { MAGIC, RING, 0, 0, 0 };
    if (f) fclose(f);
  }

  FILE* openIndex() {
    char p[128];
    indexPath(p, sizeof(p));
    FILE* f = fopen(p, "r+b");
    if (!f) {
      mkdir(LIVE_ROOT, 0777);
      f = fopen(p, "w+b");
    }
    return f;
  }
  void writeHdr() {
    if (FILE* f = openIndex()) { fwrite(&_h, sizeof(_h), 1, f); fclose(f); }
  }
  void writeRec(uint32_t slot, const Rec& r) {
    if (FILE* f = openIndex()) {
      if (fseek(f, (long)(sizeof(Hdr) + slot * sizeof(Rec)), SEEK_SET) == 0) fwrite(&r, sizeof(r), 1, f);
      fclose(f);
    }
  }
  bool readRec(uint32_t slot, Rec& r) {
    char p[128];
    indexPath(p, sizeof(p));
    FILE* f = fopen(p, "rb");
    if (!f) return false;
    bool ok = fseek(f, (long)(sizeof(Hdr) + slot * sizeof(Rec)), SEEK_SET) == 0 && fread(&r, sizeof(r), 1, f) == 1;
    fclose(f);
    return ok;
  }

  void evictOne(bool save = true) {
    if (_h.count == 0) return;
    Rec r;
    if (readRec(_h.head, r)) {
      char path[128];
      tilePath(path, sizeof(path), r.z, r.x, r.y);
      remove(path);
      _h.bytes = _h.bytes > r.size ? _h.bytes - r.size : 0;
    }
    _h.head = (_h.head + 1) % RING;
    _h.count--;
    if (save) writeHdr();
  }
};

static LiveCache s_live_cache;

}  // namespace mapview

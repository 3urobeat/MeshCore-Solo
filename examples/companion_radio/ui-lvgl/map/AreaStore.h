#pragma once
// Downloaded map areas: what was fetched where, so it can be shown, named,
// refreshed, given trails or deleted later (Map tools > Map areas). One text
// line per area in <root>/areas.txt:
//   A lon0 lat0 lon1 lat1 zmin zmax flags created name
// An area is a box and a zoom range, whatever the tiles are; a vector source
// would keep its regions here too (a kind in `flags`).
//
// Deleting runs in steps (an area can be tens of thousands of files): a tile
// is removed only if no other area covers it at that zoom, so overlapping
// areas keep what they share. Packed columns (.pak, the PC tool) stay.
//
// Single-TU fragment: included by ui-lvgl/MapScreen.h after TileDownloader.h.

namespace mapview {

struct MapArea {
  TileArea box;
  uint8_t  flags;
  uint32_t created;   // RTC seconds, 0 unknown
  char     name[32];
};

class AreaStore {
public:
  static const int MAX = 32;
  enum : uint8_t { F_TRAILS = 1, F_COMPLETE = 2 };

  explicit AreaStore(const char* root) : _root(root) {}

  int count() { load(); return _n; }
  MapArea& at(int i) { load(); return _a[i]; }

  // A new area (the oldest goes when full); returns its index.
  int add(const TileArea& box, bool trails, uint32_t now) {
    load();
    if (_n >= MAX) { memmove(_a, _a + 1, sizeof(MapArea) * (MAX - 1)); _n--; }
    MapArea& m = _a[_n];
    m.box = box;
    m.flags = trails ? F_TRAILS : 0;
    m.created = now;
    int k = 1;   // "Area n": the first number not taken
    for (bool taken = true; taken; k++) {
      taken = false;
      char want[16];
      snprintf(want, sizeof(want), "Area %d", k);
      for (int i = 0; i < _n; i++) if (!strcmp(_a[i].name, want)) taken = true;
      if (!taken) snprintf(m.name, sizeof(m.name), "%s", want);
    }
    _n++;
    save();
    return _n - 1;
  }

  // The area a download job is for (same box and zooms).
  int find(const TileArea& b) {
    load();
    for (int i = _n - 1; i >= 0; i--) {
      const TileArea& a = _a[i].box;
      if (fabs(a.lon0 - b.lon0) < 1e-5 && fabs(a.lat0 - b.lat0) < 1e-5 && fabs(a.lon1 - b.lon1) < 1e-5 &&
          fabs(a.lat1 - b.lat1) < 1e-5 && a.zmin == b.zmin && a.zmax == b.zmax) return i;
    }
    return -1;
  }

  void setFlag(int i, uint8_t f, bool on) {
    if (i < 0 || i >= count()) return;
    if (on) _a[i].flags |= f; else _a[i].flags &= ~f;
    save();
  }
  void rename(int i, const char* name) {
    if (i < 0 || i >= count()) return;
    while (*name == ' ') name++;
    if (!*name) return;
    snprintf(_a[i].name, sizeof(_a[i].name), "%s", name);
    for (char* p = _a[i].name; *p; p++) if (*p == '\n' || *p == '\r') *p = ' ';
    save();
  }

  // Removes area i from the list and starts deleting its files: the base
  // tiles (`base`) and / or its trails, except what other areas cover.
  void remove(int i, bool base, bool trails) {
    if (i < 0 || i >= count()) return;
    _del = _a[i].box;
    _del_base = base;
    _del_trails = trails;
    if (base) {   // gone from the list; only trails: the area stays, without them
      memmove(_a + i, _a + i + 1, sizeof(MapArea) * (_n - i - 1));
      _n--;
    } else {
      _a[i].flags &= ~F_TRAILS;
    }
    save();
    _del_z = _del.zmin;
    tileRange(_del, _del_z, _dx0, _dy0, _dx1, _dy1);
    _dx = _dx0; _dy = _dy0;
    _deleting = true;
    _deleted = 0;
  }
  bool deleting() const { return _deleting; }
  uint32_t deleted() const { return _deleted; }

  // A slice of the delete; true when it just finished.
  bool service(uint32_t budget_ms) {
    if (!_deleting) return false;
    uint32_t t0 = millis();
    while (millis() - t0 < budget_ms) {
      if (_del_z > _del.zmax) { _deleting = false; return true; }
      char path[64];
      if (_del_base && !covered(_del_z, _dx, _dy, false)) {
        snprintf(path, sizeof(path), "%s/%d/%d/%d.png", _root, _del_z, _dx, _dy);
        if (::remove(path) == 0) _deleted++;
      }
      if (_del_trails && !covered(_del_z, _dx, _dy, true)) {
        snprintf(path, sizeof(path), "%s/%d/%d/%d.png", TRAILS_ROOT, _del_z, _dx, _dy);
        if (::remove(path) == 0) _deleted++;
      }
      if (++_dy > _dy1) {
        _dy = _dy0;
        if (++_dx > _dx1) {
          if (++_del_z <= _del.zmax) { tileRange(_del, _del_z, _dx0, _dy0, _dx1, _dy1); _dx = _dx0; _dy = _dy0; }
        }
      }
    }
    return false;
  }

private:
  const char* _root;
  MapArea _a[MAX];
  int _n = 0;
  bool _loaded = false;
  TileArea _del = {0, 0, 0, 0, 0, 0};
  bool _deleting = false, _del_base = false, _del_trails = false;
  int _del_z = 0, _dx = 0, _dy = 0, _dx0 = 0, _dy0 = 0, _dx1 = 0, _dy1 = 0;
  uint32_t _deleted = 0;

  // Another listed area has this tile (its trails, with `trails`).
  bool covered(int z, int x, int y, bool trails) {
    for (int i = 0; i < _n; i++) {
      const MapArea& m = _a[i];
      if (trails && !(m.flags & F_TRAILS)) continue;
      if (z < m.box.zmin || z > m.box.zmax) continue;
      int x0, y0, x1, y1;
      tileRange(m.box, z, x0, y0, x1, y1);
      if (x >= x0 && x <= x1 && y >= y0 && y <= y1) return true;
    }
    return false;
  }

  void path(char* out, size_t n) const { snprintf(out, n, "%s/areas.txt", _root); }

  void load() {
    if (_loaded) return;
    _loaded = true;
    _n = 0;
    char p[64];
    path(p, sizeof(p));
    FILE* f = fopen(p, "r");
    if (!f) return;
    char line[160];
    while (_n < MAX && fgets(line, sizeof(line), f)) {
      MapArea m;
      int flags = 0, off = 0;
      unsigned long created = 0;
      if (sscanf(line, "A %lf %lf %lf %lf %d %d %d %lu %n", &m.box.lon0, &m.box.lat0, &m.box.lon1, &m.box.lat1,
                 &m.box.zmin, &m.box.zmax, &flags, &created, &off) < 8 || off <= 0) continue;
      if (m.box.zmin < 0 || m.box.zmax > 22 || m.box.zmin > m.box.zmax) continue;
      m.flags = (uint8_t)flags;
      m.created = (uint32_t)created;
      snprintf(m.name, sizeof(m.name), "%s", line + off);
      size_t k = strlen(m.name);
      while (k > 0 && (m.name[k - 1] == '\n' || m.name[k - 1] == '\r' || m.name[k - 1] == ' ')) m.name[--k] = '\0';
      if (!m.name[0]) snprintf(m.name, sizeof(m.name), "Area");
      _a[_n++] = m;
    }
    fclose(f);
  }

  void save() {
    char p[64], tmp[70];
    path(p, sizeof(p));
    makeParents(p);
    snprintf(tmp, sizeof(tmp), "%s.tmp", p);
    FILE* f = fopen(tmp, "w");
    if (!f) return;
    for (int i = 0; i < _n; i++) {
      const MapArea& m = _a[i];
      fprintf(f, "A %.6f %.6f %.6f %.6f %d %d %d %lu %s\n", m.box.lon0, m.box.lat0, m.box.lon1, m.box.lat1,
              m.box.zmin, m.box.zmax, m.flags, (unsigned long)m.created, m.name);
    }
    bool ok = fclose(f) == 0;
    if (!ok) { ::remove(tmp); return; }
    ::remove(p);
    ::rename(tmp, p);
  }
};

}  // namespace mapview

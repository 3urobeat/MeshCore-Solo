#pragma once
// Vector map (spike): tiles from tools/maps/osm_vector.py under VECTOR_ROOT,
// {dz}/{x}/{y}.vt (format VT3) at data zooms 10, 12 and 14, drawn here into the 256x256 RGB565
// buffer the map asks for -- polygons by scanline (even-odd), lines as quads
// with round joins. No anti-aliasing; names of points in VectorLabels.h. Where there is no vector
// data (or below zoom 10) the raster provider draws instead, so the two mix.
//
// renderTile() times itself (lastMs()); the map shows it while this provider
// is on, to judge whether the approach holds on the ESP32-S3.
//
// Single-TU fragment: included by ui-lvgl/MapScreen.h after TileProvider.h.

namespace mapview {

static inline int32_t vmin(int32_t a, int32_t b) { return a < b ? a : b; }
static inline int32_t vmax(int32_t a, int32_t b) { return a > b ? a : b; }

static const char* const VECTOR_ROOT = "/sdcard/vmap";

class VectorTileProvider : public TileProvider {
public:
  explicit VectorTileProvider(TileProvider& fallback) : _fb(fallback) {}

  bool available() override {
    struct stat st;
    _have = stat(VECTOR_ROOT, &st) == 0 && S_ISDIR(st.st_mode);
    bool fb = _fb.available();
    return _have || fb;
  }

  bool renderTile(int z, int x, int y, uint16_t* out) override {
    if (!_have || z < 10) return _fb.renderTile(z, x, y, out);
    uint32_t t0 = micros();
    int dz = z >= 14 ? 14 : z >= 12 ? 12 : 10, k = z - dz;
    _t_fill = _t_line = 0;
    if (!loadData(dz, x >> k, y >> k)) return _fb.renderTile(z, x, y, out);
    _t_load = micros() - t0;
    int span = EXTENT >> k;
    _ox = (x & ((1 << k) - 1)) * span;
    _oy = (y & ((1 << k) - 1)) * span;
    _k = k;
    _z = z;
    _out = out;
    for (int i = 0; i < TILE_PX * TILE_PX; i++) out[i] = PAPER;
    for (int pass = 0; pass < 4; pass++) drawPass(pass);
    _last_ms = (micros() - t0 + 500) / 1000;
    return true;
  }

  const char* attribution() const override { return _have ? "\xC2\xA9 OpenStreetMap contributors (ODbL); contours: Terrain Tiles (SRTM, AWS Open Data)" : _fb.attribution(); }
  bool hasData() const { return _have; }
  uint32_t lastMs() const { return _last_ms; }
  // The last tile's time split: reading the data, areas, lines (ms).
  void lastSplit(uint32_t& load, uint32_t& fill, uint32_t& line) const { load = _t_load / 1000; fill = _t_fill / 1000; line = _t_line / 1000; }

private:
  static const int EXTENT = 4096;
  static const uint16_t PAPER = 0xF77C;   // #F2EFE9-ish

  TileProvider& _fb;
  bool _have = false;
  const uint8_t* _data = nullptr;   // the data tile being drawn
  size_t _data_len = 0;
  int _ox = 0, _oy = 0, _k = 0, _z = 0;
  const uint8_t* _end = nullptr;   // the feature's parts end here
  int16_t _bx = 0, _by = 0;        // its bbox corner: where point deltas start
  uint16_t* _out = nullptr;
  uint32_t _last_ms = 0;

  // Scratch for the polygon filler: edges (in 1/16 px), crossings of a row.
  struct Edge { int16_t r0, r1; int64_t x, dx; };   // rows [r0, r1), x / step 16.16 in 1/16 px
  static const int ACTIVE = 512;
  Edge* _edges = nullptr;
  int _edge_cap = 0, _ne = 0;
  Edge* _act[ACTIVE];
  int32_t _xs[ACTIVE];
  uint32_t _t_load = 0, _t_fill = 0, _t_line = 0;   // us, last tile

  // Unsigned LEB128 varint / zigzag-signed (the tile format, osm_vector.py).
  static inline uint32_t varint(const uint8_t*& p, const uint8_t* end) {
    uint32_t v = 0;
    for (int s = 0; p < end && s < 35; s += 7) {
      uint8_t b = *p++;
      v |= (uint32_t)(b & 0x7F) << s;
      if (!(b & 0x80)) break;
    }
    return v;
  }
  static inline int32_t zigzag(uint32_t v) { return (int32_t)(v >> 1) ^ -(int32_t)(v & 1); }

  static uint16_t rgb(uint32_t c) { return (uint16_t)(((c >> 8) & 0xF800) | ((c >> 5) & 0x07E0) | ((c >> 3) & 0x1F)); }

  // The data tiles last read (PSRAM), least recently used replaced: the map
  // draws several tiles out of each.
  struct Data { uint8_t* buf = nullptr; size_t len = 0, cap = 0; int dz = -1, x = -1, y = -1; bool ok = false; uint32_t used = 0; };
  static const int DATA_SLOTS = 4;
  Data _slot[DATA_SLOTS];
  uint32_t _tick = 0;

  bool loadData(int dz, int x, int y) {
    Data* d = nullptr;
    for (Data& s : _slot) if (s.dz == dz && s.x == x && s.y == y) d = &s;
    if (d) { d->used = ++_tick; _t_load = 0; _data = d->buf; _data_len = d->len; return d->ok; }
    d = &_slot[0];
    for (Data& s : _slot) if (s.used < d->used) d = &s;
    d->dz = dz; d->x = x; d->y = y; d->ok = false; d->used = ++_tick;
    char path[64];
    snprintf(path, sizeof(path), "%s/%d/%d/%d.vt", VECTOR_ROOT, dz, x, y);
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 6 || sz > 2 * 1024 * 1024) { fclose(f); return false; }
    if ((size_t)sz > d->cap) {
      if (d->buf) free(d->buf);
#if defined(ESP32)
      d->buf = (uint8_t*)heap_caps_malloc(sz, MALLOC_CAP_SPIRAM);
#else
      d->buf = (uint8_t*)malloc(sz);
#endif
      d->cap = d->buf ? sz : 0;
      if (!d->buf) { fclose(f); return false; }
    }
    d->len = readFast(f, d->buf, sz) ? sz : 0;
    fclose(f);
    d->ok = d->len == (size_t)sz && memcmp(d->buf, "VT3", 3) == 0;
    _data = d->buf;
    _data_len = d->len;
    return d->ok;
  }

  // Styles. Widths in px at zoom 14, scaled with the zoom.
  enum : uint8_t { L_CONTOUR = 15, L_CONTOUR_IDX = 16, L_STREAM = 20, L_RIVER = 21, L_PATH_HARD = 29, L_PATH = 30, L_TRACK = 31, L_SERVICE = 32, L_TRUNK = 37,
                   L_ROUTE = 50, L_ROUTES = 51 };
  // Waymark colours of L_ROUTES (index 1.., osm_vector.py PALETTE).
  static uint16_t routeColour(int i) {
    static const uint32_t P[] = { 0xE0302A, 0x2A5FE0, 0x2EA043, 0xE8C20E, 0x202020, 0xF08A1C, 0x9040C0, 0xF0F0F0, 0x8B5A2B, 0xD04040 };
    return rgb(i >= 1 && i <= 10 ? P[i - 1] : 0xD04040);
  }
  static uint16_t polyColour(uint8_t c) {
    switch (c) {
      case 1: return rgb(0xE6DED6);   // residential
      case 2: return rgb(0xE1ECC6);   // meadow
      case 3: return rgb(0xCFE2B2);   // scrub
      case 4: return rgb(0xB6D59A);   // forest
      case 5: return rgb(0xE2DED8);   // rock / scree
      case 6: return rgb(0xA6CBE0);   // water
      case 7: return rgb(0xD4C8BC);   // building
    }
    return PAPER;
  }
  static bool lineStyle(uint8_t c, uint16_t& col, float& w) {
    switch (c) {
      case L_CONTOUR:     col = rgb(0xB89668); w = 1.0f; return true;
      case L_CONTOUR_IDX: col = rgb(0x9A7448); w = 1.0f; return true;
      case L_STREAM: col = rgb(0x86B6D8); w = 1.0f; return true;
      case L_RIVER:  col = rgb(0x86B6D8); w = 3.0f; return true;
      case L_PATH:   col = rgb(0xA8502A); w = 1.2f; return true;
      case L_PATH_HARD: col = rgb(0x8A3A1E); w = 1.2f; return true;   // dotted
      case L_TRACK:  col = rgb(0x94683A); w = 1.6f; return true;
      case 32: col = 0xFFFF;          w = 2.2f; return true;   // service
      case 33: col = 0xFFFF;          w = 3.2f; return true;   // minor
      case 34: col = rgb(0xFFFBB0);   w = 3.8f; return true;   // tertiary
      case 35: col = rgb(0xF7D38A);   w = 4.2f; return true;   // secondary
      case 36: col = rgb(0xF2A860);   w = 4.8f; return true;   // primary
      case 37: col = rgb(0xE8808A);   w = 5.2f; return true;   // trunk
      case L_ROUTE: col = 0; w = 3.0f; return true;
    }
    return false;
  }
  float zoomScale() const {
    static const float S[] = { 0.4f, 0.45f, 0.6f, 0.8f, 1.0f, 1.3f, 1.7f, 2.2f, 2.8f };   // z10..z18
    int i = _z - 10;
    return S[i < 0 ? 0 : i > 8 ? 8 : i];
  }

  // Passes: 0 areas + water / paths / tracks, 1 road casings, 2 road fills,
  // 3 hiking routes -- so roads join cleanly and routes stay on top.
  void drawPass(int pass) {
    const uint8_t* p = _data + 6;
    const uint8_t* end = _data + _data_len;
    uint16_t count;
    memcpy(&count, _data + 4, 2);
    for (int f = 0; f < count && p + 12 <= end; f++) {
      uint8_t cls = p[0], nparts = p[1];
      uint16_t col;
      int16_t bb[4];
      memcpy(&col, p + 2, 2);
      memcpy(bb, p + 4, 8);
      p += 12;
      uint32_t len = varint(p, end);
      const uint8_t* parts = p;
      if (len > (uint32_t)(end - p)) return;
      p += len;   // the next feature
      _end = p;
      _bx = bb[0]; _by = bb[1];
      bool road = cls >= L_SERVICE && cls <= L_TRUNK;
      bool want = pass == 0 ? (cls < L_SERVICE) : pass == 3 ? (cls == L_ROUTE || cls == L_ROUTES) : road;
      if (!want) continue;
      if (_z < 13 && (cls == L_PATH || cls == L_PATH_HARD || cls == 7)) continue;
      if (_z < 15 && cls == L_CONTOUR) continue;   // every 20 m: too dense further out   // paths from z13, buildings from z14 (data)
      // Off the drawn tile (with a margin for the widest line): not even read.
      const int32_t M = 12 * 16, S = TILE_PX * 16;
      if (sx(bb[2]) < -M || sx(bb[0]) > S + M || sy(bb[3]) < -M || sy(bb[1]) > S + M) continue;
      uint32_t t0 = micros();
      if (cls < 10) { fillFeature(parts, nparts, polyColour(cls)); _t_fill += micros() - t0; continue; }
      if (cls == L_ROUTES) { strokeRoutes(parts, nparts, col); _t_line += micros() - t0; continue; }
      uint16_t c;
      float w;
      if (!lineStyle(cls, c, w)) continue;
      w *= zoomScale();
      if (cls == L_ROUTE) { c = col ? col : rgb(0xD04040); w = w < 2.5f ? 2.5f : w; }
      if (pass == 1) { c = rgb(0xB4ACA2); w += 1.6f; }   // casing
      strokeFeature(parts, nparts, c, w, cls == L_PATH_HARD);
      _t_line += micros() - t0;
    }
  }

  // Tile units -> 1/16 px of the drawn tile.
  inline int32_t sx(int16_t u) const { return (int32_t)(u - _ox) << _k; }
  inline int32_t sy(int16_t u) const { return (int32_t)(u - _oy) << _k; }

  bool reserveEdges(int n) {
    if (n <= _edge_cap) return true;
    int cap = n + 256;
    Edge* e = (Edge*)realloc(_edges, sizeof(Edge) * cap);
    if (!e) return false;
    _edges = e;
    _edge_cap = cap;
    return true;
  }
  // An edge, top to bottom, with its x at the first row centre it crosses and
  // its slope per row (both 16.16 px).
  void addEdge(int32_t x0, int32_t y0, int32_t x1, int32_t y1) {
    if (y0 == y1) return;
    if (y0 > y1) { int32_t t = x0; x0 = x1; x1 = t; t = y0; y0 = y1; y1 = t; }
    int r0 = (y0 - 8 + 15) >> 4, r1 = (y1 - 8 + 15) >> 4;   // rows r0 .. r1-1 (centre in [y0, y1))
    if (r1 <= r0 || r1 <= 0 || r0 >= TILE_PX) return;
    if (_ne >= _edge_cap && !reserveEdges(_ne + 1)) return;
    int64_t slope = ((int64_t)(x1 - x0) << 16) / (y1 - y0);   // 1/16 px of x per 1/16 px of y, 16.16
    int32_t yc = r0 * 16 + 8;
    Edge& e = _edges[_ne++];
    e.r0 = r0; e.r1 = r1;
    e.x = ((int64_t)x0 << 16) + slope * (yc - y0);
    e.dx = slope * 16;
  }

  static int cmpEdge(const void* a, const void* b) { return ((const Edge*)a)->r0 - ((const Edge*)b)->r0; }

  // Even-odd scanline fill of the edges collected (an active edge list).
  void fillEdges(uint16_t col) {
    if (!_ne) return;
    if (_ne > 4) qsort(_edges, _ne, sizeof(Edge), cmpEdge);
    else for (int i = 1; i < _ne; i++) { Edge v = _edges[i]; int j = i - 1; while (j >= 0 && _edges[j].r0 > v.r0) { _edges[j + 1] = _edges[j]; j--; } _edges[j + 1] = v; }
    int next = 0, na = 0;
    int row = _edges[0].r0 < 0 ? 0 : _edges[0].r0;
    int rmax = 0;
    for (int i = 0; i < _ne; i++) if (_edges[i].r1 > rmax) rmax = _edges[i].r1;
    if (rmax > TILE_PX) rmax = TILE_PX;
    for (; row < rmax; row++) {
      while (next < _ne && _edges[next].r0 <= row) {   // edges starting by this row join
        Edge& e = _edges[next++];
        if (e.r1 <= row) continue;
        if (e.r0 < row) e.x += e.dx * (row - e.r0);   // started above the tile
        if (na < ACTIVE) _act[na++] = &e;
      }
      int n = 0;
      for (int i = 0; i < na;) {   // drop finished edges, collect crossings
        Edge* e = _act[i];
        if (e->r1 <= row) { _act[i] = _act[--na]; continue; }
        if (n < ACTIVE) _xs[n++] = (int32_t)(e->x >> 16);
        e->x += e->dx;
        i++;
      }
      if (n < 2) { if (!na && next >= _ne) break; continue; }
      for (int i = 1; i < n; i++) {   // insertion sort: a handful per row
        int32_t v = _xs[i];
        int j = i - 1;
        while (j >= 0 && _xs[j] > v) { _xs[j + 1] = _xs[j]; j--; }
        _xs[j + 1] = v;
      }
      uint16_t* line = _out + row * TILE_PX;
      for (int i = 0; i + 1 < n; i += 2) {
        int a = (_xs[i] - 8 + 15) >> 4, b = (_xs[i + 1] - 8) >> 4;   // centres inside
        if (a < 0) a = 0;
        if (b > TILE_PX - 1) b = TILE_PX - 1;
        for (int x = a; x <= b; x++) line[x] = col;
      }
    }
  }

  // Points of a part, in 1/16 px, closer than half a pixel to the last one
  // dropped (the data is detailed enough for zoom 18).
  template <typename F> void forPoints(const uint8_t*& p, bool keep_last, F fn) {
    uint32_t n = varint(p, _end);
    int32_t lx = INT32_MIN, ly = 0;
    int32_t ux = _bx, uy = _by;
    for (uint32_t j = 0; j < n && p < _end; j++) {
      ux += zigzag(varint(p, _end));
      uy += zigzag(varint(p, _end));
      int32_t x = (ux - _ox) << _k, y = (uy - _oy) << _k;
      if (lx != INT32_MIN && abs(x - lx) + abs(y - ly) < 8 && !(keep_last && j == n - 1)) continue;
      fn(x, y, lx == INT32_MIN);
      lx = x; ly = y;
    }
  }

  void fillFeature(const uint8_t* p, int nparts, uint16_t col) {
    _ne = 0;
    for (int i = 0; i < nparts; i++) {
      int32_t fx = 0, fy = 0, px = 0, py = 0;
      forPoints(p, false, [&](int32_t x, int32_t y, bool first) {
        if (first) { fx = px = x; fy = py = y; return; }
        addEdge(px, py, x, y);
        px = x; py = y;
      });
      addEdge(px, py, fx, fy);   // closed
    }
    fillEdges(col);
  }

  void disc(int32_t cx, int32_t cy, int32_t r16, uint16_t col) {   // radius in 1/16 px
    int x0 = (cx - r16) >> 4, x1 = (cx + r16) >> 4, y0 = (cy - r16) >> 4, y1 = (cy + r16) >> 4;
    if (x1 < 0 || y1 < 0 || x0 >= TILE_PX || y0 >= TILE_PX) return;
    int32_t r2 = r16 * r16;
    for (int y = vmax(0, y0); y <= vmin(TILE_PX - 1, y1); y++) {
      int32_t dy = y * 16 + 8 - cy;
      uint16_t* line = _out + y * TILE_PX;
      for (int x = vmax(0, x0); x <= vmin(TILE_PX - 1, x1); x++) {
        int32_t dx = x * 16 + 8 - cx;
        if (dx * dx + dy * dy <= r2) line[x] = col;
      }
    }
  }

  // A thin line: a DDA with a 1 or 2 px pen.
  void thinSegment(int32_t ax, int32_t ay, int32_t bx, int32_t by, int pen, uint16_t col, bool dots = false) {
    int32_t dx = bx - ax, dy = by - ay;
    int steps = (vmax(abs(dx), abs(dy)) >> 4) + 1;
    int32_t x = ax << 8, y = ay << 8, ix = (dx << 8) / steps, iy = (dy << 8) / steps;   // 1/16 px << 8
    for (int i = 0; i <= steps; i++, x += ix, y += iy) {
      if (dots && (_dash++ & 3) >= 2) continue;   // 2 px on, 2 off, carried across segments
      int px = x >> 12, py = y >> 12;
      for (int oy = 0; oy < pen; oy++)
        for (int ox = 0; ox < pen; ox++) {
          int qx = px + ox, qy = py + oy;
          if ((unsigned)qx < (unsigned)TILE_PX && (unsigned)qy < (unsigned)TILE_PX) _out[qy * TILE_PX + qx] = col;
        }
    }
  }

  // A part's points (1/16 px) for the stroke, and an offset copy of them.
  static const int PTS = 2048;
  int32_t* _pts = nullptr;   // x, y pairs
  int32_t* _off = nullptr;
  uint32_t _dash = 0;

  bool reservePts() {
    if (!_pts) _pts = psramBuf<int32_t>(2 * PTS);
    if (!_off) _off = psramBuf<int32_t>(2 * PTS);
    return _pts && _off;
  }

  // Strokes each part: its points gathered (in runs of PTS), then `fn(n)`.
  template <typename F> void forParts(const uint8_t* p, int nparts, F fn) {
    for (int i = 0; i < nparts; i++) {
      int n = 0;
      forPoints(p, true, [&](int32_t x, int32_t y, bool) {
        if (n == PTS) {   // a very long part: draw what's gathered, go on from its end
          fn(n);
          _pts[0] = _pts[2 * (n - 1)]; _pts[1] = _pts[2 * (n - 1) + 1];
          n = 1;
        }
        _pts[2 * n] = x; _pts[2 * n + 1] = y;
        n++;
      });
      if (n >= 2) fn(n);
    }
  }

  void strokeFeature(const uint8_t* p, int nparts, uint16_t col, float w, bool dots = false) {
    if (!reservePts()) return;
    _dash = 0;
    forParts(p, nparts, [&](int n) { strokePoints(_pts, n, col, w, dots); });
  }

  // `pts` moved sideways by `off` (1/16 px, + to the left of the direction),
  // corners mitred (limited, so a hairpin doesn't shoot out) into _off.
  void offsetPoints(const int32_t* pts, int n, float off) {
    float pnx = 0, pny = 0;
    for (int i = 0; i < n; i++) {
      float nx = 0, ny = 0;   // the next segment's normal
      if (i + 1 < n) {
        float dx = pts[2 * i + 2] - pts[2 * i], dy = pts[2 * i + 3] - pts[2 * i + 1], len = sqrtf(dx * dx + dy * dy);
        if (len > 0) { nx = dy / len; ny = -dx / len; }
      } else { nx = pnx; ny = pny; }
      if (i == 0) { pnx = nx; pny = ny; }
      float mx = pnx + nx, my = pny + ny, m2 = mx * mx + my * my;
      float ox = nx, oy = ny;
      if (m2 > 0.01f) {
        float k = 2.0f / m2;   // mitre: (n1 + n2) / (1 + n1.n2)
        if (k > 4.0f) k = 4.0f;
        ox = mx * k; oy = my * k;
      }
      _off[2 * i] = pts[2 * i] + (int32_t)(ox * off);
      _off[2 * i + 1] = pts[2 * i + 1] + (int32_t)(oy * off);
      pnx = nx; pny = ny;
    }
  }

  // Hiking routes along a stretch: a stripe per route, side by side on a
  // white band, in PALETTE order.
  void strokeRoutes(const uint8_t* p, int nparts, uint16_t packed) {
    if (!reservePts()) return;
    uint16_t cols[4];
    int k = 0;
    for (int i = 0; i < 4; i++) {
      int c = (packed >> (4 * i)) & 15;
      if (c) cols[k++] = routeColour(c);
    }
    if (!k) return;
    float sw = 2.2f * zoomScale();   // a stripe
    sw = sw < 1.6f ? 1.6f : sw > 4.5f ? 4.5f : sw;
    forParts(p, nparts, [&](int n) {
      strokePoints(_pts, n, 0xFFFF, k * sw + 1.6f, false);
      for (int i = 0; i < k; i++) {
        if (k == 1) { strokePoints(_pts, n, cols[0], sw, false); break; }
        offsetPoints(_pts, n, (i - (k - 1) / 2.0f) * sw * 16);
        strokePoints(_off, n, cols[i], sw, false);
      }
    });
  }

  void strokePoints(const int32_t* pts, int n, uint16_t col, float w, bool dots) {
    int32_t h = (int32_t)(w * 8);   // half width, 1/16 px
    const int32_t S = TILE_PX * 16;
    bool thin = w < 2.2f;
    int pen = w < 1.8f ? 1 : 2;
    for (int i = 1; i < n; i++) {
      int32_t ax = pts[2 * i - 2], ay = pts[2 * i - 1], bx = pts[2 * i], by = pts[2 * i + 1];
      if (vmax(ax, bx) < -h || vmin(ax, bx) > S + h || vmax(ay, by) < -h || vmin(ay, by) > S + h) continue;
      if (thin) {
        if (pen == 2) { ax -= 8; ay -= 8; bx -= 8; by -= 8; }   // a 2 px pen centred on the line
        thinSegment(ax, ay, bx, by, pen, col, dots);
        continue;
      }
      float dx = bx - ax, dy = by - ay, len = sqrtf(dx * dx + dy * dy);
      if (len > 0) {
        int32_t nx = (int32_t)(-dy / len * h), ny = (int32_t)(dx / len * h);
        _ne = 0;
        addEdge(ax + nx, ay + ny, bx + nx, by + ny);
        addEdge(bx + nx, by + ny, bx - nx, by - ny);
        addEdge(bx - nx, by - ny, ax - nx, ay - ny);
        addEdge(ax - nx, ay - ny, ax + nx, ay + ny);
        fillEdges(col);
      }
      if (w >= 2.8f) disc(bx, by, h, col);   // round join
    }
  }
};

}  // namespace mapview

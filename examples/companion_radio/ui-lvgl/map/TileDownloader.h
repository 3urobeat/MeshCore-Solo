#pragma once
// On-device map download: fetches every tile of an area over a zoom range
// into <root>/{z}/{x}/{y}.png, the layout RasterTileProvider reads (the PC
// alternative is tools/maps/fetch_tiles.py). Tiles already on the card are
// skipped, so a download can be repeated to fill gaps or extend an area.
//
// The job (area + zoom range) is kept in <root>/.job until it completes, so a
// download cut short by a power-off, a lost network or Stop can be resumed:
// resuming re-runs the same job, and the tiles already written are skipped.
//
// Driven by loop() from the UI loop: one small step per call, the HTTP GET
// itself runs asynchronously in the board's fetcher (lvport::fetch*), so the
// mesh keeps being serviced during a long download.
//
// The server comes from <root>/source.txt (line 1: URL template with {z} {x}
// {y}, line 2: attribution); without it, OpenTopoMap. Whatever the source,
// its attribution is written to <root>/attribution.txt for the map to show.
//
// Single-TU fragment: included by ui-lvgl/UITask.cpp only (after LvglPort.h).

#include <math.h>
#include <sys/stat.h>
#include <errno.h>

namespace mapview {

static const char* const DEFAULT_TILE_URL = "https://a.tile.opentopomap.org/{z}/{x}/{y}.png";
static const char* const DEFAULT_TILE_ATTR = "\xC2\xA9 OpenStreetMap contributors, SRTM | \xC2\xA9 OpenTopoMap (CC-BY-SA)";

struct TileArea {
  double lon0, lat0, lon1, lat1;   // west, south, east, north
  int zmin, zmax;
};

static void tileRange(const TileArea& a, int z, int& x0, int& y0, int& x1, int& y1) {
  int n = 1 << z;
  auto tx = [n](double lon) { int x = (int)floor((lon + 180.0) / 360.0 * n); return x < 0 ? 0 : x >= n ? n - 1 : x; };
  auto ty = [n](double lat) {
    double r = lat * M_PI / 180.0;
    int y = (int)floor((1.0 - asinh(tan(r)) / M_PI) / 2.0 * n);
    return y < 0 ? 0 : y >= n ? n - 1 : y;
  };
  x0 = tx(a.lon0); x1 = tx(a.lon1);
  y0 = ty(a.lat1); y1 = ty(a.lat0);   // tile y grows southwards
}

static uint32_t countTiles(const TileArea& a) {
  uint32_t total = 0;
  for (int z = a.zmin; z <= a.zmax; z++) {
    int x0, y0, x1, y1;
    tileRange(a, z, x0, y0, x1, y1);
    total += (uint32_t)(x1 - x0 + 1) * (uint32_t)(y1 - y0 + 1);
  }
  return total;
}

// mkdir -p for the directories of `path` (not the file itself).
static void makeParents(const char* path) {
  char tmp[96];
  snprintf(tmp, sizeof(tmp), "%s", path);
  for (char* p = tmp + 1; *p; p++) {
    if (*p != '/') continue;
    *p = '\0';
    mkdir(tmp, 0775);   // EEXIST is fine
    *p = '/';
  }
}

class TileDownloader {
public:
  enum State : uint8_t { IDLE, CONNECTING, RUNNING, DONE, FAILED, CANCELLED };
  static const uint32_t MAX_TILES = 40000;   // ~1 GB; beyond that, the PC tool

  explicit TileDownloader(const char* root) : _root(root) {}

  State state() const       { return _state; }
  bool  active() const      { return _state == CONNECTING || _state == RUNNING; }
  uint32_t total() const    { return _total; }
  uint32_t processed() const { return _done + _skipped + _failed; }
  uint32_t downloaded() const { return _done; }
  uint32_t failed() const   { return _failed; }
  const char* message() const { return _msg; }
  const char* sourceHost() { loadSource(); return _host; }

  // Unfinished job from <root>/.job (read once, then tracked in memory).
  bool savedJob(TileArea& a) {
    if (!_job_checked) { _job_checked = true; _has_job = readJob(_job); }
    if (_has_job) a = _job;
    return _has_job;
  }
  void discardJob() {
    char path[64];
    jobPath(path, sizeof(path));
    remove(path);
    _has_job = false;
    _job_checked = true;
  }

  bool start(const TileArea& a, const char* ssid, const char* pass) {
    if (active()) return false;
    _area = a;
    _total = countTiles(a);
    _done = _skipped = _failed = _consec_fail = 0;
    _placeholder_hash = 0; _placeholder_hits = 0;
    _msg[0] = '\0';
    if (_total == 0 || _total > MAX_TILES) { fail("Area too large: use the PC tool"); return false; }
    loadSource();
    writeAttribution();
    writeJob(a);
    _z = a.zmin;
    tileRange(a, _z, _x0, _y0, _x1, _y1);
    _x = _x0; _y = _y0;
    _fetching = false;
    _state = CONNECTING;
    _connect_started = millis();
    lvport::netBegin(ssid, pass);
    return true;
  }

  void cancel() {
    if (!active()) return;
    if (_fetching) { lvport::fetchAbandon(); _fetching = false; }
    finish(CANCELLED, "Cancelled");
  }

  void loop() {
    if (_state == CONNECTING) {
      int ns = lvport::netState();
      if (ns == lvport::NET_UP) { _state = RUNNING; return; }
      if (ns == lvport::NET_FAILED || millis() - _connect_started > 20000) finish(FAILED, "WiFi: can't connect");
      return;
    }
    if (_state != RUNNING) return;

    if (_fetching) {
      int r = lvport::fetchPoll();
      if (r == 0) {   // still in flight
        if (millis() - _last_start < 60000) return;
        lvport::fetchAbandon();   // hung past every timeout: give up on this tile
        _fetching = false;
        onFailure("Request hung (60 s)");
        if (_consec_fail >= 8) { finish(FAILED, _msg); return; }
        advance();
        return;
      }
      _fetching = false;
      if (r > 0) {
        size_t len = 0;
        const uint8_t* data = lvport::fetchData(len);
        if (!looksLikeImage(data, len)) onFailure("Server didn't send a PNG tile");
        else if (isPlaceholder(data, len)) { lvport::fetchRelease(); finish(FAILED, "Server sends a placeholder (blocked / key?)"); return; }
        else if (!writeTile(data, len)) { lvport::fetchRelease(); finish(FAILED, "Can't write to the SD card"); return; }
        else { _done++; _consec_fail = 0; }
      } else {
        const char* why = lvport::fetchError();
        onFailure(r == -403 || r == -401 ? "Server refused (403)" : why[0] ? why : "Download failed");
      }
      lvport::fetchRelease();
      if (_consec_fail >= 8) { finish(FAILED, _msg[0] ? _msg : "Server not answering"); return; }
      advance();
      return;
    }

    // Skip tiles already on the card (a few stat()s per pass), then start the next GET.
    for (int i = 0; i < 16 && _state == RUNNING; i++) {
      if (_z > _area.zmax) { finish(DONE, "Done"); return; }
      char path[96];
      tilePath(path, sizeof(path), _z, _x, _y);
      struct stat st;
      if (stat(path, &st) == 0 && st.st_size > 0) { _skipped++; advance(); continue; }
      if (millis() - _last_start < 120) return;   // be gentle with the server
      char url[200];
      buildUrl(url, sizeof(url), _z, _x, _y);
      int r = lvport::fetchStart(url);
      if (r > 0) { _fetching = true; _last_start = millis(); }
      else if (r < 0) finish(FAILED, "Out of memory (download task)");
      return;
    }
  }

private:
  const char* _root;
  State    _state = IDLE;
  TileArea _area = {0, 0, 0, 0, 0, 0};
  uint32_t _total = 0, _done = 0, _skipped = 0, _failed = 0;
  uint8_t  _consec_fail = 0;
  int      _z = 0, _x = 0, _y = 0, _x0 = 0, _y0 = 0, _x1 = 0, _y1 = 0;
  bool     _fetching = false;
  uint32_t _last_start = 0, _connect_started = 0;
  uint32_t _placeholder_hash = 0;
  uint8_t  _placeholder_hits = 0;
  char     _url_tpl[160] = "";
  char     _attr[96] = "";
  char     _host[48] = "";
  char     _msg[64] = "";
  TileArea _job = {0, 0, 0, 0, 0, 0};
  bool     _has_job = false, _job_checked = false;

  void fail(const char* m) { snprintf(_msg, sizeof(_msg), "%s", m); _state = FAILED; }

  void finish(State s, const char* m) {
    snprintf(_msg, sizeof(_msg), "%s", m);
    _state = s;
    lvport::netEnd();
    if (s == DONE) discardJob();   // anything else stays resumable
  }

  void jobPath(char* out, size_t n) const { snprintf(out, n, "%s/.job", _root); }

  void writeJob(const TileArea& a) {
    char path[64];
    jobPath(path, sizeof(path));
    makeParents(path);
    if (FILE* f = fopen(path, "w")) {
      fprintf(f, "%.6f %.6f %.6f %.6f %d %d\n", a.lon0, a.lat0, a.lon1, a.lat1, a.zmin, a.zmax);
      fclose(f);
    }
    _job = a;
    _has_job = true;
    _job_checked = true;
  }

  bool readJob(TileArea& a) const {
    char path[64];
    jobPath(path, sizeof(path));
    FILE* f = fopen(path, "r");
    if (!f) return false;
    TileArea t;
    int n = fscanf(f, "%lf %lf %lf %lf %d %d", &t.lon0, &t.lat0, &t.lon1, &t.lat1, &t.zmin, &t.zmax);
    fclose(f);
    if (n != 6 || t.zmin < 0 || t.zmax > 20 || t.zmin > t.zmax) return false;
    a = t;
    return true;
  }

  void onFailure(const char* m) {
    _failed++;
    _consec_fail++;
    snprintf(_msg, sizeof(_msg), "%s", m);
  }

  void advance() {
    if (++_y <= _y1) return;
    _y = _y0;
    if (++_x <= _x1) return;
    if (++_z > _area.zmax) return;   // loop() finishes on the next pass
    tileRange(_area, _z, _x0, _y0, _x1, _y1);
    _x = _x0; _y = _y0;
  }

  void tilePath(char* out, size_t n, int z, int x, int y) const {
    snprintf(out, n, "%s/%d/%d/%d.png", _root, z, x, y);
  }

  void buildUrl(char* out, size_t n, int z, int x, int y) const {
    size_t o = 0;
    for (const char* p = _url_tpl; *p && o + 12 < n; p++) {
      if (p[0] == '{' && p[1] && p[2] == '}') {
        int v = p[1] == 'z' ? z : p[1] == 'x' ? x : p[1] == 'y' ? y : -1;
        if (v >= 0) { o += snprintf(out + o, n - o, "%d", v); p += 2; continue; }
      }
      out[o++] = *p;
    }
    out[o] = '\0';
  }

  void loadSource() {
    snprintf(_url_tpl, sizeof(_url_tpl), "%s", DEFAULT_TILE_URL);
    snprintf(_attr, sizeof(_attr), "%s", DEFAULT_TILE_ATTR);
    char path[64];
    snprintf(path, sizeof(path), "%s/source.txt", _root);
    if (FILE* f = fopen(path, "r")) {
      char line[160];
      if (fgets(line, sizeof(line), f)) { trim(line); if (strstr(line, "{z}")) snprintf(_url_tpl, sizeof(_url_tpl), "%s", line); }
      if (fgets(line, sizeof(line), f)) { trim(line); if (line[0]) snprintf(_attr, sizeof(_attr), "%s", line); }
      fclose(f);
    }
    const char* h = strstr(_url_tpl, "://");
    h = h ? h + 3 : _url_tpl;
    size_t n = strcspn(h, "/");
    if (n >= sizeof(_host)) n = sizeof(_host) - 1;
    memcpy(_host, h, n);
    _host[n] = '\0';
  }

  static void trim(char* s) {
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r' || s[n - 1] == ' ')) s[--n] = '\0';
  }

  void writeAttribution() {
    char path[64];
    snprintf(path, sizeof(path), "%s/attribution.txt", _root);
    makeParents(path);
    if (FILE* f = fopen(path, "w")) { fprintf(f, "%s\n", _attr); fclose(f); }
  }

  // PNG only for now: RasterTileProvider has no JPEG decoder yet.
  static bool looksLikeImage(const uint8_t* d, size_t n) {
    return d && n >= 8 && d[0] == 0x89 && d[1] == 'P' && d[2] == 'N' && d[3] == 'G';
  }

  // A blocked / key-required server answers every tile with the same picture:
  // four identical payloads among the first downloads stop the job.
  bool isPlaceholder(const uint8_t* d, size_t n) {
    if (_done >= 8) return false;
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < n; i++) h = (h ^ d[i]) * 16777619u;
    if (h == _placeholder_hash) return ++_placeholder_hits >= 3;
    _placeholder_hash = h;
    _placeholder_hits = 0;
    return false;
  }

  bool writeTile(const uint8_t* d, size_t n) {
    char path[96], tmp[100];
    tilePath(path, sizeof(path), _z, _x, _y);
    makeParents(path);
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    FILE* f = fopen(tmp, "wb");
    if (!f) return false;
    bool ok = fwrite(d, 1, n, f) == n;
    ok = (fclose(f) == 0) && ok;
    if (!ok) { remove(tmp); return false; }
    remove(path);
    return rename(tmp, path) == 0;
  }
};

}  // namespace mapview

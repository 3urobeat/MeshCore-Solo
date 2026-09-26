#pragma once
// LVGL <-> board glue: display flush, pointer input, the storage card (map
// tiles, mounted at /sdcard) and the network used to download map tiles (WiFi
// on the board, the browser in the simulator). One implementation per board;
// the rest of ui-lvgl never touches the hardware directly.
//
// Single-TU fragment: included by ui-lvgl/UITask.cpp only.

#if defined(SEEED_WIO_TRACKER_L2)
  #include <SD_MMC.h>
  #include <WiFi.h>
  #include <HTTPClient.h>
  #include <WiFiClientSecure.h>
  #include <Preferences.h>
  #include <SPIFFS.h>
  #include <esp_heap_caps.h>
  #include <esp_system.h>
  #include <esp_core_dump.h>
  #if ESP_ARDUINO_VERSION_MAJOR >= 3
    #include <esp_vfs_fat.h>
  #endif
  #include <mbedtls/platform.h>
#elif defined(SIM_PLATFORM) && defined(__EMSCRIPTEN__)
  #include <emscripten/fetch.h>
#endif

namespace lvport {

// Network for map downloads: state of the link, and one HTTP GET at a time.
enum NetState { NET_OFF, NET_CONNECTING, NET_UP, NET_FAILED };
static const int WIFI_SCAN_MAX = 20;

#if defined(SEEED_WIO_TRACKER_L2)
// LovyanGFX device (NV3031B QSPI panel + GT911 touch) from WioTrackerL2Display.

static lgfx::LGFX_Device* s_gfx = nullptr;
static bool s_swallow = false;   // ignore the touch that woke the display until it lifts
static uint32_t s_flush_us = 0;  // UI_PERF_TEST: time spent in flushCb

static void flushCb(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map) {
#ifdef UI_PERF_TEST
  uint32_t t = micros();
#endif
  int w = area->x2 - area->x1 + 1;
  int h = area->y2 - area->y1 + 1;
  s_gfx->startWrite();
  s_gfx->setAddrWindow(area->x1, area->y1, w, h);
  s_gfx->pushPixels((uint16_t*)px_map, (uint32_t)w * h, true /* LVGL RGB565 is little-endian */);
  s_gfx->endWrite();
#ifdef UI_PERF_TEST
  s_flush_us += micros() - t;
#endif
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

  // Two half-screen buffers (75 KB each) in PSRAM: a frame renders in two
  // passes instead of six (every pass walks the whole tree and lays text out
  // again) -- measured ~8% faster than 40 lines; internal RAM / DMA / -O2
  // made no difference (UI_PERF_TEST).
  const uint32_t buf_sz = (uint32_t)s_gfx->width() * 120 * 2;
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

// Backlight 1-100 % (the brightness slider), on the LP5814 PWM.
static void setBacklightPct(uint8_t pct) {
  if (pct > 100) pct = 100;
  s_gfx->setBrightness((uint8_t)(8 + (uint16_t)pct * 247 / 100));   // never fully dark
}

// microSD over SDMMC, 1-bit (CLK 2, CMD 3, D0 1); its power rail (expander
// P14) is switched on in WioTrackerL2Board::begin(). Retried on every call
// until it works, so a card inserted later is picked up the next time.
static bool mountStorage() {
  static bool mounted = false;
  if (mounted) return true;
  SD_MMC.setPins(2, 3, 1);
  mounted = SD_MMC.begin("/sdcard", true /* 1-bit */);
  return mounted;
}

// ── WiFi (station, only while a map download runs) ───────────────────────────
// Credentials in NVS, not in NodePrefs (whose on-flash layout stays fixed) and
// not on the removable card.
static bool loadWifi(char* ssid, size_t ssid_n, char* pass, size_t pass_n) {
  Preferences p;
  if (!p.begin("mc_wifi", true)) { ssid[0] = pass[0] = '\0'; return false; }
  p.getString("ssid", ssid, ssid_n);
  p.getString("pass", pass, pass_n);
  p.end();
  return ssid[0] != '\0';
}
static void saveWifi(const char* ssid, const char* pass) {
  Preferences p;
  if (!p.begin("mc_wifi", false)) return;
  p.putString("ssid", ssid);
  p.putString("pass", pass);
  p.end();
}
// Settings > WiFi's switch: off keeps the radio off for everything (scan, map
// download). Kept with the credentials.
static int8_t s_wifi_allowed = -1;   // read once (the status bar asks every second)
static bool wifiAllowed() {
  if (s_wifi_allowed < 0) {
    Preferences p;
    bool on = true;
    if (p.begin("mc_wifi", true)) { on = p.getBool("on", true); p.end(); }
    s_wifi_allowed = on;
  }
  return s_wifi_allowed;
}
static void setWifiAllowed(bool on) {
  s_wifi_allowed = on;
  Preferences p;
  if (!p.begin("mc_wifi", false)) return;
  p.putBool("on", on);
  p.end();
}
// Map tools > Live tiles: missing tiles fetched over WiFi while the map is open.
static bool liveTiles() {
  Preferences p;
  if (!p.begin("mc_wifi", true)) return true;
  bool on = p.getBool("live", true);
  p.end();
  return on;
}
static void setLiveTiles(bool on) {
  Preferences p;
  if (!p.begin("mc_wifi", false)) return;
  p.putBool("live", on);
  p.end();
}

// Screen-lock PIN (Settings > Display & power > Screen PIN): digits, "" = none.
// In NVS for the same reason as the WiFi credentials.
static void loadPin(char* out, size_t n) {
  Preferences p;
  out[0] = '\0';
  if (!p.begin("mc_lock", true)) return;
  p.getString("pin", out, n);
  p.end();
}
static void savePin(const char* pin) {
  Preferences p;
  if (!p.begin("mc_lock", false)) return;
  p.putString("pin", pin);
  p.end();
}

// Accent colour (Settings > Display & power): an index into theme::ACCENTS.
static int loadAccent() {
  Preferences p;
  if (!p.begin("mc_ui", true)) return 0;
  int v = p.getUChar("accent", 0);
  p.end();
  return v;
}
static void saveAccent(int idx) {
  Preferences p;
  if (!p.begin("mc_ui", false)) return;
  p.putUChar("accent", (uint8_t)idx);
  p.end();
}
// Settings > Storage > Kept per conversation (an index into histstore::KEEP).
static int loadHistKeep() {
  Preferences p;
  if (!p.begin("mc_ui", true)) return -1;
  int v = p.getChar("hkeep", -1);
  p.end();
  return v;
}
static void saveHistKeep(int idx) {
  Preferences p;
  if (!p.begin("mc_ui", false)) return;
  p.putChar("hkeep", (int8_t)idx);
  p.end();
}
// Settings > Storage > Live map tiles: index into mapview::LIVE_CAP_MB, -1 = default.
static int loadLiveCap() {
  Preferences p;
  if (!p.begin("mc_ui", true)) return -1;
  int v = p.getChar("ltcap", -1);
  p.end();
  return v;
}
static void saveLiveCap(int idx) {
  Preferences p;
  if (!p.begin("mc_ui", false)) return;
  p.putChar("ltcap", (int8_t)idx);
  p.end();
}
// Settings > Display & power > Tap to wake: a touch turns the dark screen on
// (off: only the side button does).
static bool loadTapWake() {
  Preferences p;
  if (!p.begin("mc_ui", true)) return true;
  bool on = p.getBool("tapwake", true);
  p.end();
  return on;
}
static void saveTapWake(bool on) {
  Preferences p;
  if (!p.begin("mc_ui", false)) return;
  p.putBool("tapwake", on);
  p.end();
}
// Filesystem size and space in use (Settings > Storage), plus the card's own
// size -- a card whose FAT partition is small (e.g. written by a Raspberry Pi
// imager) shows both. The first free-space count on a big card takes a moment.
static bool sdInfo(uint64_t& total, uint64_t& used, uint64_t& card) {
  if (!mountStorage()) return false;
  card = SD_MMC.cardSize();
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  uint64_t fs_free = 0;
  if (esp_vfs_fat_info("/sdcard", &total, &fs_free) == ESP_OK) {   // asks the mounted volume, not drive 0:
    used = total - fs_free;
    return total > 0;
  }
#endif
  total = SD_MMC.totalBytes();
  used = SD_MMC.usedBytes();
  return total > 0;
}
// Why the device last started (Diagnostics).
static const char* resetReason() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON:   return "Power on";
    case ESP_RST_SW:        return "Restart";
    case ESP_RST_PANIC:     return "Crash";
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT:       return "Watchdog";
    case ESP_RST_BROWNOUT:  return "Low voltage";
    case ESP_RST_DEEPSLEEP: return "Wake from sleep";
    case ESP_RST_EXT:       return "Reset pin";
#if ESP_ARDUINO_VERSION_MAJOR >= 3
    case ESP_RST_USB:       return "USB";
#endif
    default:                return "Other";
  }
}
// The last crash the core dump partition holds: task and address (decode
// the address with the build's firmware.elf, or read the whole dump over USB).
static bool crashSummary(char* out, size_t n) {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  if (esp_core_dump_image_check() != ESP_OK) return false;
  esp_core_dump_summary_t sum;
  if (esp_core_dump_get_summary(&sum) != ESP_OK) return false;
  snprintf(out, n, "%.10s %08lx", sum.exc_task, (unsigned long)sum.exc_pc);
  return true;
#else
  (void)out; (void)n;
  return false;
#endif
}
// The internal flash file system (contacts, channels, settings, trail...).
static const char* const FLASH_ROOT = "/spiffs";
static bool flashInfo(uint64_t& total, uint64_t& used) {
  total = SPIFFS.totalBytes();
  used = SPIFFS.usedBytes();
  return total > 0;
}

static void netBegin(const char* ssid, const char* pass) {
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, pass);
}
static int netState() {
  switch (WiFi.status()) {
    case WL_CONNECTED:      return NET_UP;
    case WL_CONNECT_FAILED:
    case WL_NO_SSID_AVAIL:  return NET_FAILED;
    case WL_IDLE_STATUS:
    case WL_DISCONNECTED:   return NET_CONNECTING;
    default:                return NET_CONNECTING;
  }
}
static void netEnd() {
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
}
// For the status bar: NET_UP once connected, else NET_OFF (radio off, a scan,
// still connecting).
static int netRadio() { return WiFi.getMode() != WIFI_OFF && WiFi.status() == WL_CONNECTED ? NET_UP : NET_OFF; }
// Async scan: scanStart(), then scanResults() returns -1 while running, else
// the count, filling `names` (strongest first, as the driver reports them).
static void scanStart() {
  WiFi.mode(WIFI_STA);
  WiFi.scanNetworks(true /* async */);
}
static int scanResults(char names[][33], int max) {
  int n = WiFi.scanComplete();
  if (n == WIFI_SCAN_RUNNING) return -1;
  if (n < 0) return 0;
  int k = 0;
  for (int i = 0; i < n && k < max; i++) {
    String s = WiFi.SSID(i);
    if (!s.length()) continue;
    bool dup = false;
    for (int j = 0; j < k; j++) if (strcmp(names[j], s.c_str()) == 0) { dup = true; break; }
    if (dup) continue;
    snprintf(names[k++], 33, "%s", s.c_str());
  }
  WiFi.scanDelete();
  return k;
}

// ── One HTTP GET at a time, on a worker task (core 0) ────────────────────────
// TLS handshakes and slow servers would stall the mesh loop, so the request
// runs on its own task; the UI loop polls. The HTTP connection is reused
// between tiles (keep-alive). Tiles are public pictures, so the TLS peer
// isn't verified (no CA bundle in flash for this). The task runs at the idle
// priority: HTTPClient's read loop only yields with delay(0), which never lets
// a lower-priority task in, so at priority 1 a slow transfer starved IDLE0
// until the task watchdog reset the device.
enum { F_IDLE, F_REQUESTED, F_BUSY, F_DONE, F_FAILED };
static volatile int s_fstate = F_IDLE;
static volatile int s_fcode = 0;
static char     s_furl[200];
static uint8_t* s_fbuf = nullptr;
static size_t   s_fcap = 0;
static volatile size_t s_flen = 0;
static volatile bool s_fabandoned = false;
static TaskHandle_t s_ftask = nullptr;

// Stream sink for HTTPClient::writeToStream() (which also undoes chunking).
class FetchSink : public Stream {
public:
  size_t write(uint8_t c) override { return write(&c, 1); }
  size_t write(const uint8_t* d, size_t n) override {
    if (s_flen + n > s_fcap) {
      size_t cap = s_fcap ? s_fcap : 32 * 1024;
      while (cap < s_flen + n) cap *= 2;
      if (cap > 512 * 1024) return 0;
      uint8_t* nb = (uint8_t*)heap_caps_realloc(s_fbuf, cap, MALLOC_CAP_SPIRAM);
      if (!nb) return 0;
      s_fbuf = nb; s_fcap = cap;
    }
    memcpy(s_fbuf + s_flen, d, n);
    s_flen += n;
    return n;
  }
  int available() override { return 0; }
  int read() override { return -1; }
  int peek() override { return -1; }
  void flush() override {}
};

static char s_ferr[64] = "";   // why the last request failed (HTTP / TLS), for the popup

// The framework builds mbedTLS with internal-RAM-only buffers (~40 KB per
// session), which Bluedroid + WiFi + LVGL leave too little of: put them in
// PSRAM, falling back to internal RAM.
static void* tlsCalloc(size_t n, size_t size) {
  return heap_caps_calloc_prefer(n, size, 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT, MALLOC_CAP_DEFAULT);
}

static void fetchTask(void*) {
  mbedtls_platform_set_calloc_free(tlsCalloc, heap_caps_free);
  WiFiClientSecure tls;
  tls.setInsecure();
  tls.setHandshakeTimeout(20);   // seconds (default 120)
  WiFiClient plain;
  HTTPClient http;
  http.setReuse(true);
  http.setConnectTimeout(10000);
  http.setTimeout(15000);
  http.setUserAgent("MeshCore-Solo/1.0 (LoRa handheld; offline map download)");
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    s_fstate = F_BUSY;
    s_flen = 0;
    bool https = strncmp(s_furl, "https:", 6) == 0;
    int code = -1;
    s_ferr[0] = '\0';
    if (https ? http.begin(tls, s_furl) : http.begin(plain, s_furl)) {
      code = http.GET();
      if (code == 200) {
        FetchSink sink;
        if (http.writeToStream(&sink) < 0) { code = -1; snprintf(s_ferr, sizeof(s_ferr), "read failed / out of memory"); }
      } else if (code < 0) {
        char tls_err[40] = "";
        if (https && tls.lastError(tls_err, sizeof(tls_err)) != 0 && tls_err[0])
          snprintf(s_ferr, sizeof(s_ferr), "TLS: %s", tls_err);
        else
          snprintf(s_ferr, sizeof(s_ferr), "%s", HTTPClient::errorToString(code).c_str());
      } else {
        snprintf(s_ferr, sizeof(s_ferr), "HTTP %d", code);
      }
      http.end();
    } else {
      snprintf(s_ferr, sizeof(s_ferr), "bad URL");
    }
    s_fcode = code;
    if (s_fabandoned) { s_fabandoned = false; s_fstate = F_IDLE; }   // nobody wants it any more
    else s_fstate = code == 200 ? F_DONE : F_FAILED;
  }
}

// 1 = started, 0 = busy (an abandoned request is still finishing), -1 = no
// memory for the worker task.
static int fetchStart(const char* url) {
  if (s_fstate != F_IDLE) return 0;
  if (!s_ftask && xTaskCreatePinnedToCore(fetchTask, "tilefetch", 10240, nullptr, tskIDLE_PRIORITY, &s_ftask, 0) != pdPASS) {
    s_ftask = nullptr;
    return -1;
  }
  snprintf(s_furl, sizeof(s_furl), "%s", url);
  s_fstate = F_REQUESTED;
  xTaskNotifyGive(s_ftask);
  return 1;
}
// 0 = in flight, 1 = done (fetchData valid until fetchRelease), <0 = failed (-HTTP status)
static int fetchPoll() {
  int st = s_fstate;
  if (st == F_DONE) return 1;
  if (st == F_FAILED) return s_fcode > 0 ? -s_fcode : -1;
  return 0;
}
static const uint8_t* fetchData(size_t& len) { len = s_flen; return s_fbuf; }
static void fetchRelease() { if (s_fstate == F_DONE || s_fstate == F_FAILED) s_fstate = F_IDLE; }
// Stop caring about the request in flight; it completes (or fails) on its own.
static void fetchAbandon() {
  if (s_fstate == F_REQUESTED || s_fstate == F_BUSY) s_fabandoned = true;
  else fetchRelease();
}
static const char* fetchError() { return s_ferr; }

#elif defined(SIM_PLATFORM) && defined(__EMSCRIPTEN__)
// Browser simulator (variants/sim/build_wasm_lvgl.sh): SimLcdDisplay blits to
// a <canvas>, the host page feeds the mouse in as touch.

static bool s_swallow = false;

static void flushCb(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map) {
  display.blit(area->x1, area->y1, area->x2 - area->x1 + 1, area->y2 - area->y1 + 1,
               (const uint16_t*)px_map);
  lv_display_flush_ready(disp);
}

static void touchCb(lv_indev_t* indev, lv_indev_data_t* data) {
  (void)indev;
  const SimLcdDisplay::Touch& t = SimLcdDisplay::touchState();
  if (s_swallow) {
    if (!t.down) s_swallow = false;
    data->state = LV_INDEV_STATE_RELEASED;
    return;
  }
  data->state = t.down ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
  data->point.x = t.x;
  data->point.y = t.y;
}

static bool begin() {
  lv_display_t* disp = lv_display_create(SimLcdDisplay::W, SimLcdDisplay::H);
  lv_display_set_flush_cb(disp, flushCb);
  static uint8_t buf[SimLcdDisplay::W * 40 * 2];   // same 40-line partial buffer as the board
  lv_display_set_buffers(disp, buf, nullptr, sizeof(buf), LV_DISPLAY_RENDER_MODE_PARTIAL);

  lv_indev_t* indev = lv_indev_create();
  lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(indev, touchCb);
  return true;
}

static bool touched() { return SimLcdDisplay::touchState().down; }
static void swallowTouch() { s_swallow = true; }
static void setBacklightPct(uint8_t pct) { (void)pct; }   // the browser canvas has no backlight

// The host page preloads map tiles into the in-memory FS under /sdcard/maps.
static bool mountStorage() { return true; }

// The browser is always online; WiFi credentials only live for the session.
static char s_ssid[33] = "", s_pass[65] = "";
static bool loadWifi(char* ssid, size_t ssid_n, char* pass, size_t pass_n) {
  snprintf(ssid, ssid_n, "%s", s_ssid);
  snprintf(pass, pass_n, "%s", s_pass);
  return ssid[0] != '\0';
}
static void saveWifi(const char* ssid, const char* pass) {
  snprintf(s_ssid, sizeof(s_ssid), "%s", ssid);
  snprintf(s_pass, sizeof(s_pass), "%s", pass);
}
static char s_pin[9] = "";   // the screen PIN, for the session
static void loadPin(char* out, size_t n) { snprintf(out, n, "%s", s_pin); }
static void savePin(const char* pin) { snprintf(s_pin, sizeof(s_pin), "%s", pin); }
static int s_accent = 0;
static int loadAccent() { return s_accent; }
static void saveAccent(int idx) { s_accent = idx; }
static int s_hist_keep = -1;
static int loadHistKeep() { return s_hist_keep; }
static void saveHistKeep(int idx) { s_hist_keep = idx; }
static int s_live_cap = -1;
static int loadLiveCap() { return s_live_cap; }
static void saveLiveCap(int idx) { s_live_cap = idx; }
static bool s_tap_wake = true;
static bool loadTapWake() { return s_tap_wake; }
static void saveTapWake(bool on) { s_tap_wake = on; }
// The browser has no card: a nominal 32 GB, used = what the files add up to
// (the storage screen counts them anyway; it passes that in).
static bool sdInfo(uint64_t& total, uint64_t& used, uint64_t& card) { total = card = 32ULL << 30; used = 0; return true; }
static const char* const FLASH_ROOT = "/sim_data";
static const char* resetReason() { return "Power on"; }
static bool crashSummary(char*, size_t) { return false; }
static bool flashInfo(uint64_t& total, uint64_t& used) { total = 1536ULL << 10; used = 0; return true; }
static bool s_wifi_on = true;
static bool wifiAllowed() { return s_wifi_on; }
static void setWifiAllowed(bool on) { s_wifi_on = on; }
static bool s_live_tiles = true;
static bool liveTiles() { return s_live_tiles; }
static void setLiveTiles(bool on) { s_live_tiles = on; }
static bool s_net_on = false;
static void netBegin(const char*, const char*) { s_net_on = true; }
static int  netState() { return NET_UP; }
static void netEnd() { s_net_on = false; }
static int  netRadio() { return s_net_on ? NET_UP : NET_OFF; }
static uint32_t s_scan_at = 0;
static void scanStart() { s_scan_at = millis(); }
static int scanResults(char names[][33], int max) {   // a pretend scan, for the UI
  if (millis() - s_scan_at < 800) return -1;
  static const char* const FAKE[] = { "Sim-Home", "Sim-Office", "Cafe Guest" };
  int n = 0;
  for (; n < 3 && n < max; n++) snprintf(names[n], 33, "%s", FAKE[n]);
  return n;
}

// fetch() through emscripten's Fetch API (the tile server must allow CORS,
// which OpenTopoMap does).
enum { F_IDLE, F_BUSY, F_DONE, F_FAILED };
static int      s_fstate = F_IDLE;
static int      s_fcode = 0;
static uint8_t* s_fbuf = nullptr;
static size_t   s_flen = 0;
static bool     s_fabandoned = false;

static void fetchDone(emscripten_fetch_t* f) {
  if (!s_fabandoned) {
    s_fcode = f->status;
    free(s_fbuf);
    s_fbuf = nullptr;
    s_flen = 0;
    if (f->status == 200 && f->numBytes > 0 && (s_fbuf = (uint8_t*)malloc(f->numBytes))) {
      memcpy(s_fbuf, f->data, f->numBytes);
      s_flen = f->numBytes;
      s_fstate = F_DONE;
    } else {
      s_fstate = F_FAILED;
    }
  } else {
    s_fstate = F_IDLE;
  }
  s_fabandoned = false;
  emscripten_fetch_close(f);
}

static int fetchStart(const char* url) {   // 1 started, 0 busy
  if (s_fstate != F_IDLE) return 0;
  emscripten_fetch_attr_t a;
  emscripten_fetch_attr_init(&a);
  strcpy(a.requestMethod, "GET");
  a.attributes = EMSCRIPTEN_FETCH_LOAD_TO_MEMORY;
  a.onsuccess = fetchDone;
  a.onerror = fetchDone;
  s_fstate = F_BUSY;
  emscripten_fetch(&a, url);
  return 1;
}
static int fetchPoll() {
  if (s_fstate == F_DONE) return 1;
  if (s_fstate == F_FAILED) return s_fcode > 0 ? -s_fcode : -1;
  return 0;
}
static const uint8_t* fetchData(size_t& len) { len = s_flen; return s_fbuf; }
static void fetchRelease() { if (s_fstate == F_DONE || s_fstate == F_FAILED) s_fstate = F_IDLE; }
static void fetchAbandon() { if (s_fstate == F_BUSY) s_fabandoned = true; else fetchRelease(); }
static const char* fetchError() { return s_fcode > 0 && s_fcode != 200 ? "HTTP error" : s_fcode == 0 ? "network / CORS" : ""; }

#else
  #error "ui-lvgl: no LVGL port for this board (see LvglPort.h)"
#endif

}  // namespace lvport

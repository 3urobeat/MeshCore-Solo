#include "UITask.h"
#include <helpers/TxtDataHelpers.h>
#include "../MyMesh.h"
#include "target.h"
#if defined(ESP32)
  #include <esp_heap_caps.h>
#endif
#include <new>
#include <stdarg.h>

// Flags: LVGL draws text one codepoint at a time, so a flag (a pair of regional
// indicator letters) would show as two letter tiles. Every label and span of
// this UI (all of it is this one file and the headers it includes) goes through
// these, which swap each pair for the codepoint fonts/ui_emoji.c keeps the
// flag's image under. Display only: what's stored and sent stays as it was.
// A label given the text it already shows is left alone: the status bar, the
// clocks and the live values are set every refresh, and each set is a redraw.
extern "C" char* ui_emoji_flags(const char* s);
static void ui_label_set_text(lv_obj_t* o, const char* t) {
  char* f = t ? ui_emoji_flags(t) : nullptr;   // t == NULL: LVGL's "redraw the current text"
  const char* s = f ? f : t;
  const char* cur = lv_label_get_text(o);
  if (!s || !cur || strcmp(cur, s) != 0) lv_label_set_text(o, s);
  lv_free(f);
}
static void ui_label_set_text_fmt(lv_obj_t* o, const char* fmt, ...) __attribute__((format(printf, 2, 3)));
static void ui_label_set_text_fmt(lv_obj_t* o, const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  char buf[160];
  int n = vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  if (n < (int)sizeof(buf)) { ui_label_set_text(o, buf); return; }
  char* big = (char*)lv_malloc(n + 1);
  if (!big) return;
  va_start(ap, fmt);
  vsnprintf(big, n + 1, fmt, ap);
  va_end(ap);
  ui_label_set_text(o, big);
  lv_free(big);
}
static void ui_span_set_text(lv_span_t* s, const char* t) {
  char* f = ui_emoji_flags(t);
  lv_span_set_text(s, f ? f : t);
  lv_free(f);
}
#define lv_label_set_text ui_label_set_text
#define lv_label_set_text_fmt ui_label_set_text_fmt
#define lv_span_set_text ui_span_set_text

// A zeroed buffer of `n` T in PSRAM when the board has it: internal RAM is what
// BLE, WiFi and TLS need. Allocated once (at startup or first use), never freed.
template <class T> static T* psramBuf(size_t n) {
#if defined(ESP32)
  if (void* p = heap_caps_calloc(n, sizeof(T), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)) return (T*)p;
#endif
  return (T*)calloc(n, sizeof(T));
}

#include "../ui-core/UiCore.h"   // shared UI Core (header-only, this TU)
#include "../ui-core/NearbyModel.h"
#include "../ui-core/EtaTracker.h"
#include "../ui-core/SettingsSchema.h"
#include "../ui-core/GpsAverager.h"
#include "../ui-core/TrackBack.h"
#include "../ui-core/RadioControl.h"
#include "../ui-core/RepeaterControl.h"
#include "../ui-core/Diagnostics.h"
#include "../ui-core/Battery.h"
#include "../ui-core/ChannelControl.h"
#include "../ui-core/BotConfig.h"
#include "../ui-core/SoundControl.h"
#include "../ui-core/SoundNotifier.h"
#include "../ui-core/MessageText.h"
#include "Theme.h"
#include "Anim.h"
#include "LvglPort.h"
#include "HistoryStore.h"
#include "map/LiveCache.h"
static histstore::SdArchive s_archive;   // message history on the SD card
namespace storeview { static void stopWalk(); }   // StorageScreen.h
#include "../ui-core/KeyboardData.h"
#include "Keyboard.h"

static const char* waypointsFull() {
  static char t[24];
  snprintf(t, sizeof(t), "Waypoints full (%d)", WaypointStore::CAPACITY);
  return t;
}

// ui-lvgl skeleton (docs/development/ui-core.md, step 5): status bar, home,
// conversation list, contact picker, conversation view with compose. Every
// piece of state it shows comes from the UI Core; this file only draws it.

static UITask* s_ui = nullptr;   // for LVGL's C callbacks

#if defined(SIM_PLATFORM) && defined(__EMSCRIPTEN__)
#include <emscripten.h>
// The board's one button, pressed from the simulator page (web/lvgl.html).
static bool s_sim_btn_click = false, s_sim_btn_hold = false, s_sim_wake = false;
extern "C" EMSCRIPTEN_KEEPALIVE void sim_lcd_button() { s_sim_btn_click = true; }
extern "C" EMSCRIPTEN_KEEPALIVE void sim_lcd_button_hold() { s_sim_btn_hold = true; }
extern "C" EMSCRIPTEN_KEEPALIVE void sim_wake_button() { s_sim_wake = true; }
// The speaker, polled every frame by the page's Web Audio oscillator.
extern "C" EMSCRIPTEN_KEEPALIVE int sim_buzzer_is_playing() { return s_ui && s_ui->isBuzzerPlaying() ? 1 : 0; }
extern "C" EMSCRIPTEN_KEEPALIVE int sim_buzzer_freq_hz() { return s_ui ? (int)s_ui->buzzerFreqHz() : 0; }
extern "C" EMSCRIPTEN_KEEPALIVE int sim_buzzer_get_volume() { return s_ui ? (int)s_ui->buzzerVolume() : 0; }
#endif

// ── Small helpers ─────────────────────────────────────────────────────────────

static void styleSurface(lv_obj_t* o, uint32_t bg) {
  lv_obj_set_style_bg_color(o, lv_color_hex(bg), 0);
  lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(o, 0, 0);
  lv_obj_set_style_radius(o, 0, 0);
  lv_obj_set_style_pad_all(o, 0, 0);
}

static lv_obj_t* label(lv_obj_t* parent, const char* text, const lv_font_t* font, uint32_t color) {
  lv_obj_t* l = lv_label_create(parent);
  lv_label_set_text(l, text);
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
  return l;
}

// A list filling the rest of a flex column, scrolling on its own (the
// scrollbar only while it moves).
static lv_obj_t* scrollList(lv_obj_t* parent) {
  lv_obj_t* l = lv_obj_create(parent);
  styleSurface(l, theme::BG);
  lv_obj_set_width(l, LV_PCT(100));
  lv_obj_set_flex_grow(l, 1);
  lv_obj_set_flex_flow(l, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(l, theme::GAP, 0);
  lv_obj_set_scrollbar_mode(l, LV_SCROLLBAR_MODE_ACTIVE);
  return l;
}

// The dimmed full-screen layer under a popup; it swallows taps on what's below.
static lv_obj_t* dimOverlay(lv_obj_t* parent) {
  lv_obj_t* o = lv_obj_create(parent);
  lv_obj_remove_style_all(o);
  lv_obj_set_size(o, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_bg_color(o, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(o, LV_OPA_60, 0);
  lv_obj_add_flag(o, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
  return o;
}

// A settings row: the label left (a muted hint under it, cut at `hint_w`),
// room on the right for a switch / dropdown / slider.
static lv_obj_t* settingRow(lv_obj_t* parent, const char* text, const char* hint, int hint_w = 150) {
  lv_obj_t* row = lv_obj_create(parent);
  styleSurface(row, theme::SURFACE);
  lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(row, LV_PCT(100), theme::ROW_H);
  lv_obj_set_style_radius(row, theme::RADIUS, 0);
  lv_obj_align(label(row, text, THEME_FONT_BODY, theme::TEXT), LV_ALIGN_TOP_LEFT, theme::PAD, hint ? 5 : 13);
  if (hint) {
    lv_obj_t* h = label(row, hint, THEME_FONT_SMALL, theme::TEXT_MUTED);
    lv_label_set_long_mode(h, LV_LABEL_LONG_DOT);
    lv_obj_set_size(h, hint_w, 15);   // fixed height: cut, don't wrap onto the label
    lv_obj_align(h, LV_ALIGN_BOTTOM_LEFT, theme::PAD, -5);
  }
  return row;
}

// A bare flex row / column sized to its content, taps passing through.
static lv_obj_t* flexBox(lv_obj_t* parent, lv_flex_flow_t flow) {
  lv_obj_t* b = lv_obj_create(parent);
  lv_obj_remove_style_all(b);
  lv_obj_remove_flag(b, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(b, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(b, flow);
  return b;
}

// A one-of-N choice: a row (or grid, with "\n" in the map) of checkable
// buttons, `sel` checked (-1: none). `item` is the unchecked button colour —
// SURFACE on a SURFACE_2 panel, SURFACE_2 on the page.
static lv_obj_t* segmented(lv_obj_t* parent, const char** map, int sel, int w, int h,
                           uint32_t item = theme::SURFACE_2) {
  lv_obj_t* m = lv_buttonmatrix_create(parent);
  lv_buttonmatrix_set_map(m, map);
  lv_buttonmatrix_set_button_ctrl_all(m, LV_BUTTONMATRIX_CTRL_CHECKABLE);
  lv_buttonmatrix_set_one_checked(m, true);
  if (sel >= 0) lv_buttonmatrix_set_button_ctrl(m, sel, LV_BUTTONMATRIX_CTRL_CHECKED);
  lv_obj_set_size(m, w, h);
  lv_obj_set_style_pad_all(m, 0, 0);
  lv_obj_set_style_pad_gap(m, 4, 0);
  lv_obj_set_style_bg_opa(m, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(m, 0, 0);
  lv_obj_set_style_bg_color(m, lv_color_hex(item), LV_PART_ITEMS);
  lv_obj_set_style_bg_color(m, lv_color_hex(theme::ACCENT_DIM), LV_PART_ITEMS | LV_STATE_CHECKED);
  lv_obj_set_style_text_color(m, lv_color_hex(theme::TEXT), LV_PART_ITEMS);
  lv_obj_set_style_text_font(m, THEME_FONT_SMALL, LV_PART_ITEMS);
  lv_obj_set_style_shadow_width(m, 0, LV_PART_ITEMS);
  lv_obj_set_style_radius(m, theme::RADIUS_SM, LV_PART_ITEMS);
  return m;
}

// A one-line text field, full width. The theme's padding leaves less than one
// line inside 34 px, which makes the field scroll vertically (text jumps as
// it's typed), so it's padded to fit exactly one body line: 34 = 2*1 border +
// 2*6 pad + 20. The theme draws the cursor only while FOCUSED.
static lv_obj_t* textField(lv_obj_t* parent, const char* placeholder = NULL) {
  lv_obj_t* ta = lv_textarea_create(parent);
  lv_textarea_set_one_line(ta, true);
  if (placeholder) lv_textarea_set_placeholder_text(ta, placeholder);
  lv_obj_set_size(ta, LV_PCT(100), 34);
  lv_obj_set_style_border_width(ta, 1, 0);
  lv_obj_set_style_pad_ver(ta, 6, 0);
  lv_obj_set_style_pad_hor(ta, 10, 0);
  lv_obj_set_scrollbar_mode(ta, LV_SCROLLBAR_MODE_OFF);
  lv_obj_set_style_border_color(ta, lv_color_hex(theme::ACCENT), LV_PART_CURSOR | LV_STATE_FOCUSED);
  lv_obj_set_style_border_width(ta, 2, LV_PART_CURSOR | LV_STATE_FOCUSED);
  return ta;
}

// The primary action of a screen or popup: full amber, dark text (Theme.h);
// call after its label is made.
static void stylePrimary(lv_obj_t* b) {
  lv_obj_set_style_bg_color(b, lv_color_hex(theme::ACCENT), 0);
  for (uint32_t i = 0; i < lv_obj_get_child_count(b); i++)
    lv_obj_set_style_text_color(lv_obj_get_child(b, i), lv_color_hex(theme::BG), 0);
}

// "@[nick]" -> "@nick" in place, for one-line text (previews, quotes) where
// the bubble's highlight doesn't reach.
static void plainMentions(char* t) {
  char* w = t;
  for (const char* r = t; *r;) {
    const char* close = r[0] == '@' && r[1] == '[' ? strchr(r + 2, ']') : nullptr;
    if (close && close - r <= 34) {
      *w++ = '@';
      for (const char* q = r + 2; q < close;) *w++ = *q++;
      r = close + 1;
    } else {
      *w++ = *r++;
    }
  }
  *w = '\0';
}

// Unread badge: amber pill with a count, on the right edge of `parent`.
static void badge(lv_obj_t* parent, int n, bool overflow) {
  if (n <= 0) return;
  lv_obj_t* b = lv_obj_create(parent);
  lv_obj_remove_flag(b, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(b, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(b, LV_SIZE_CONTENT, 20);
  lv_obj_set_style_bg_color(b, lv_color_hex(theme::ACCENT), 0);
  lv_obj_set_style_border_width(b, 0, 0);
  lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_pad_hor(b, 7, 0);
  lv_obj_set_style_pad_ver(b, 0, 0);
  lv_obj_align(b, LV_ALIGN_RIGHT_MID, -theme::PAD, 0);
  lv_obj_t* l = label(b, "", THEME_FONT_SMALL, theme::BG);
  lv_label_set_text_fmt(l, "%d%s", n, overflow ? "+" : "");
  lv_obj_center(l);
}

// Local time (NodePrefs::tz_offset_hours); false before the clock is set.
static bool localTime(const NodePrefs* p, struct tm& out) {
  uint32_t now = rtc_clock.getCurrentTime();
  if (now <= 1000000000UL) return false;
  time_t t = (time_t)((int64_t)now + (int64_t)(p ? p->tz_offset_hours : 0) * 3600);
  out = *gmtime(&t);
  return true;
}
// "14:05", or "2:05" (+ " PM" with suffix) with Settings > 12-hour clock;
// with `seconds`, ":09" follows unless Settings > Clock seconds is off.
static void fmtClock(char* b, size_t n, const struct tm& ti, const NodePrefs* p, bool suffix, bool seconds = false) {
  char sec[4] = "";
  if (seconds && p && !p->clock_hide_seconds) snprintf(sec, sizeof(sec), ":%02d", ti.tm_sec);
  if (p && p->clock_12h) {
    int h = ti.tm_hour % 12;
    snprintf(b, n, "%d:%02d%s%s", h ? h : 12, ti.tm_min, sec, suffix ? (ti.tm_hour < 12 ? " AM" : " PM") : "");
  } else {
    snprintf(b, n, "%02d:%02d%s", ti.tm_hour, ti.tm_min, sec);
  }
}
// "Thu 25 Sep 2026" ("PM  Thu 25 Sep 2026" on a 12-hour clock, whose big
// digits have no room for it).
static void fmtDate(char* b, size_t n, const struct tm& ti, const NodePrefs* p) {
  static const char* DOW[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
  static const char* MON[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun",
                               "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
  snprintf(b, n, "%s%s %d %s %d", (p && p->clock_12h) ? (ti.tm_hour < 12 ? "AM  " : "PM  ") : "",
           DOW[ti.tm_wday], ti.tm_mday, MON[ti.tm_mon], ti.tm_year + 1900);
}
// A message's age ("12s" / "5m" / "3h" / "2d"). After a restart the clock runs
// from the build date until GPS or the app sets it, so messages restored from
// the card look newer than "now": those show when they came ("25 Sep 14:05").
static const NodePrefs* s_prefs = nullptr;   // for the free helpers below; set in begin()
static bool clockBehind(uint32_t now, uint32_t ts) { return ts > 1000000000UL && ts > now + 120; }
static void fmtMsgAge(char* b, size_t n, uint32_t now, uint32_t ts, const NodePrefs* p) {
  if (!clockBehind(now, ts)) { geo::fmtAgeShort(b, (int)n, now, ts ? ts : now); return; }
  static const char* MON[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun",
                               "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
  time_t t = (time_t)((int64_t)ts + (int64_t)(p ? p->tz_offset_hours : 0) * 3600);
  struct tm ti = *gmtime(&t);
  char clk[12];
  fmtClock(clk, sizeof(clk), ti, p, true);
  snprintf(b, n, "%d %s %s", ti.tm_mday, MON[ti.tm_mon], clk);
}

static void contactName(const uint8_t* prefix, char* out, size_t n) {
  ContactInfo c;
  if (MessageHistory::contactByPrefix(prefix, c) && c.name[0]) snprintf(out, n, "%s", c.name);
  else snprintf(out, n, "%02X%02X%02X%02X", prefix[0], prefix[1], prefix[2], prefix[3]);
}

// ── Lifecycle ─────────────────────────────────────────────────────────────────

void UITask::begin(DisplayDriver* display_drv, SensorManager* sensors, NodePrefs* node_prefs) {
  s_ui = this;
  _display = display_drv;
  _sensors = sensors;
  _prefs = node_prefs;
  s_prefs = node_prefs;

  _core = new UiCore();
  _core->begin(node_prefs, sensors, this);
  {  // history kept on the SD card: back into the ring, then every new entry to it
    int hk = lvport::loadHistKeep();
    s_archive.setKeep(histstore::KEEP[hk >= 0 && hk < histstore::KEEP_COUNT ? hk : histstore::KEEP_DEFAULT]);
    int lc = lvport::loadLiveCap();
    mapview::s_live_cache.setLimit((uint64_t)mapview::LIVE_CAP_MB[lc >= 0 && lc < mapview::LIVE_CAP_COUNT ? lc : mapview::LIVE_CAP_DEFAULT] << 20);
    s_archive.restore(_core->history);
    _core->history.setArchive(&s_archive);
  }
  _tap_wake = lvport::loadTapWake();
  msgtext::seedQuick(node_prefs);   // "OK" in quick message 1 on first boot
  _nearby = new NearbyModel();
  _nearby->bindModel(_core, node_prefs);
  _scan = new NearbyModel();
  _scan->bindModel(_core, node_prefs);
  _scan->setSource(NearbyModel::SRC_SCAN);

#ifdef PIN_USER_BTN
  user_btn.begin();
#endif
#ifdef PIN_BUZZER
  soundctl::applyMode(_prefs, _buzzer, isClientConnected());
  _buzzer.setVolume(_prefs ? _prefs->buzzer_volume : 4);
  _buzzer.begin();   // plays the startup sound unless muted
#endif

  lv_init();
  lv_tick_set_cb([]() -> uint32_t { return (uint32_t)millis(); });
  if (!lvport::begin()) {
    Serial.println("ui-lvgl: no memory for display buffers");
    return;
  }
  theme::setAccent(lvport::loadAccent());
  theme::install(lv_display_get_default());

  buildStatusBar();
  applyDisplayPrefs();   // a slider percentage overrides the level main.cpp set
  showHome();
  lvport::loadPin(_pin, sizeof(_pin));
  if (_pin[0]) lockScreen();   // a reboot doesn't get round the PIN
  showSplash();                // over both; fades out by itself
}

MyMesh::Listener* UITask::meshListener() { return _core; }

// The USER button's hold: sound on / off (leaves Auto, as the Sound page's
// On / Off would).
void UITask::toggleMute() {
#ifdef PIN_BUZZER
  if (!_prefs) return;
  bool on = soundctl::mode(_prefs) != soundctl::MODE_ON;
  soundctl::setMode(_prefs, _buzzer, on ? soundctl::MODE_ON : soundctl::MODE_OFF, isClientConnected());
  the_mesh.savePrefs();
  if (on) _buzzer.playForced(soundctl::MEL_VOLUME);
  if (!_asleep) { showToast(on ? "Sound on" : "Sound off", 1200); refreshStatusBar(); }
#endif
}

void UITask::loop() {
  pollConnection();
  drainCoreEvents();

  // USER (BOOT, side) button: back; held, mutes / unmutes, also with the
  // screen off, which it doesn't wake. WAKE (top) button: screen off / on.
  // Either one silences a ringing alarm first.
  bool btn_click = false, btn_hold = false, wake_press = false;
#ifdef PIN_USER_BTN
  int ev = user_btn.check();
  btn_click = ev == BUTTON_EVENT_CLICK;
  btn_hold = ev == BUTTON_EVENT_LONG_PRESS;
#elif defined(SIM_PLATFORM) && defined(__EMSCRIPTEN__)
  btn_click = s_sim_btn_click;
  btn_hold = s_sim_btn_hold;
  wake_press = s_sim_wake;
  s_sim_btn_click = s_sim_btn_hold = s_sim_wake = false;
#endif
#if defined(SEEED_WIO_TRACKER_L2)
  if ((int32_t)(millis() - _next_wake_poll_ms) >= 0) {   // an I2C read on the expander
    _next_wake_poll_ms = millis() + 50;
    bool down = board.readWakeButton();
    wake_press = down && !_wake_down;
    _wake_down = down;
  }
#endif
  if ((btn_click || btn_hold || wake_press) && _core->clock.isRinging()) dismissRing();
  else if (wake_press) { if (_asleep) wake(); else sleep(); }
  else if (btn_hold) toggleMute();
  else if (btn_click) { if (!_asleep && !locked()) back(); }   // the side button doesn't wake: pockets

  if (_asleep) {
    // Tap to wake (off: the top button only). An I2C read on the touch panel.
    if (_tap_wake && (int32_t)(millis() - _next_touch_poll_ms) >= 0) {
      _next_touch_poll_ms = millis() + 50;
      if (lvport::touched()) { lvport::swallowTouch(); wake(); }
    }
  } else {
    uint32_t aoff = autoOffMillis();
    if (aoff > 0 && lv_display_get_inactive_time(NULL) > aoff && !_core->clock.isRinging() && !otaBusy()) sleep();
  }

  _core->loop();
  drainCoreEvents();
#if defined(UI_HEAP_REPORT) && defined(ESP32)
  // -D UI_HEAP_REPORT: internal / PSRAM heap once, 20 s after boot (the
  // framework comparison in docs/development/l2-roadmap.md).
  static bool heap_done = false;
  if (!heap_done && millis() > 20000) {
    heap_done = true;
    Serial.printf("HEAP internal free %u largest %u min %u | psram free %u | idf %s\n",
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                  (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
                  (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM), esp_get_idf_version());
  }
#endif
#if defined(UI_PERF_TEST) && defined(ESP32)
  // -D UI_PERF_TEST: walks through screens by itself and prints how long each
  // display refresh took during the transition that followed (render+flush).
  {
    static uint32_t next = 15000, sum_us = 0, max_us = 0, frames = 0, t0 = 0;
    static int step = 0;
    static const char* name = "";
    static bool hooked = false;
    if (!hooked) {
      hooked = true;
      lv_display_add_event_cb(lv_display_get_default(), [](lv_event_t* e) {
        if (lv_event_get_code(e) == LV_EVENT_REFR_START) t0 = micros();
        else { uint32_t d = micros() - t0; sum_us += d; if (d > max_us) max_us = d; frames++; }
      }, LV_EVENT_REFR_START, NULL);
      lv_display_add_event_cb(lv_display_get_default(), [](lv_event_t* e) {
        uint32_t d = micros() - t0; sum_us += d; if (d > max_us) max_us = d; frames++; (void)e;
      }, LV_EVENT_REFR_READY, NULL);
    }
    if (millis() >= next) {
      if (*name) Serial.printf("PERF %-10s frames %2lu avg %5.1f ms max %5.1f ms\n", name, (unsigned long)frames,
                               frames ? sum_us / 1000.0f / frames : 0.0f, max_us / 1000.0f);
      if (*name) Serial.printf("PERF   flush %5.1f ms per frame\n", frames ? lvport::s_flush_us / 1000.0f / frames : 0.0f);
      sum_us = max_us = frames = 0;
      lvport::s_flush_us = 0;
      lv_display_trigger_activity(NULL);   // no sleeping mid-test
      next = millis() + 1200;
      if (step == 0) unlockScreen();   // a PIN lock would be drawn over everything
      if (step % 8 == 0 && step > 0) {   // a static full-screen redraw of Home, for the baseline
        uint32_t fl0 = lvport::s_flush_us, t = micros();
        for (int i = 0; i < 10; i++) { lv_obj_invalidate(lv_screen_active()); lv_refr_now(NULL); }
        Serial.printf("PERF full redraw (Home) %5.1f ms, flush %5.1f ms\n", (micros() - t) / 10000.0f,
                      (lvport::s_flush_us - fl0) / 10000.0f);
      }
      switch (step++ % 8) {
        case 0: name = "settings"; showSettings(); break;
        case 1: name = "back-home"; back(); break;
        case 2: name = "messages"; showChats(); break;
        case 3: name = "back-home"; back(); break;
        case 4: name = "nearby"; showNearby(); break;
        case 5: name = "back-home"; back(); break;
        case 6: name = "page-2"; setHomePage(1); break;
        case 7: name = "page-1"; setHomePage(0); break;
      }
    }
  }
#endif
#ifdef PIN_BUZZER
  _buzzer.loop();
  if (soundctl::autoTick(_prefs, _buzzer, isClientConnected()) && !_asleep) refreshStatusBar();
  // The alarm melody repeats until dismissed or the ring window ends.
  if (_core->clock.isRinging() && !_buzzer.isPlaying()) playMelody(soundctl::MEL_ALARM);
#endif
  checkLowBattery();
  mapDownloadTick();   // a map download keeps running on any screen, and asleep
  otaTick();           // so does a firmware update
  if ((int32_t)(millis() - _next_trackback_ms) >= 0) {   // walking the trail back, on any screen
    _next_trackback_ms = millis() + 1000;
    navPollTrackBack();
  }

  uint32_t lv_next = 0;
  if (!_asleep) {
    if ((int32_t)(millis() - _next_status_ms) >= 0) {
      _next_status_ms = millis() + 1000;
      refreshStatusBar();
      refreshLock();
      if (_screen == SCR_HOME) refreshHome();
      refreshDiag();
      refreshCompass();
      refreshGps();
    }
    if (locked()) lockPoll();
    else if (_screen == SCR_HOME) homeSwipePoll();
    if ((_screen == SCR_NEARBY || _screen == SCR_NODE) && (int32_t)(millis() - _next_nearby_ms) >= 0) {
      // Scan results trickle in over a few seconds: poll fast while scanning.
      _next_nearby_ms = millis() + (_scanning ? 250 : 2000);
      if (_scanning && (int32_t)(millis() - _scan_until_ms) >= 0) _scanning = false;
      if (_screen == SCR_NEARBY) {
        refreshNearbyList();
        if (_scan_overlay) refreshScanPopup();
      } else {
        refreshNode();
      }
    }
    if (_screen == SCR_MAP) mapLoop();
    if (_screen == SCR_WIFI) pollWifiScan();
    if (_screen == SCR_STORAGE) pollStorage();
    if (_screen == SCR_ADMIN) adminPoll();
    roomPoll();
    if (_screen == SCR_CLOCK && (int32_t)(millis() - _next_clock_ms) >= 0) {
      _next_clock_ms = millis() + 100;   // stopwatch tenths
      refreshClock();
    }
    if (_screen == SCR_MELODY && (int32_t)(millis() - _next_clock_ms) >= 0) {
      _next_clock_ms = millis() + 30;    // the playing note's highlight keeps up
      refreshMelody();
    }
    if (_screen == SCR_THREAD && (int32_t)(millis() - _next_thread_check_ms) >= 0) {
      _next_thread_check_ms = millis() + 500;
      uint32_t sig = threadSignature();
      if (_thread_dirty || sig != _thread_sig) refreshThread();
    }
    lv_next = lv_timer_handler();
  }
  lvport::idle(idleMillis(lv_next));
}

// How long loop() may sleep: nothing is due before then. Not at all while the
// mesh has packets queued; briefly while a melody plays (its notes are timed
// here) or the app is connected (its frames are read one a pass).
uint32_t UITask::idleMillis(uint32_t lv_next) {
  if (the_mesh.hasPendingWork()) return 0;
#ifdef PIN_BUZZER
  if (_buzzer.isPlaying()) return 1;
#endif
  if (isClientConnected()) return 2;
  if (_asleep) return 20;   // the buttons and tap to wake are polled every 50 ms
  return lv_next < 10 ? lv_next : 10;   // LVGL's next timer: a refresh, an animation, input
}

void UITask::shutdown(bool restart) {
  the_mesh.savePrefs();
  the_mesh.saveRTCTime();
  the_mesh.flushDirtyContacts();
  _core->trail.onShutdown();
#ifdef PIN_BUZZER
  _buzzer.shutdown();   // the goodbye sound, unless muted
#ifndef SIM_PLATFORM   // the sim's single thread would freeze the page
  for (uint32_t t0 = millis(); _buzzer.isPlaying() && millis() - t0 < 2500; ) { _buzzer.loop(); delay(10); }
#endif
#endif
  if (restart) {
    _board->reboot();
  } else {
    if (_display) _display->turnOff();
    radio_driver.powerOff();
    if (_sensors) {
      LocationProvider* loc = _sensors->getLocationProvider();
      if (loc) loc->stop();
    }
    _board->powerOff();
  }
}

// Settings > Display & power > Battery shutdown (NodePrefs::low_batt_mv), as
// ui-new does it: a smoothed reading every 8 s, never while on USB power.
void UITask::checkLowBattery() {
  if ((int32_t)(millis() - _next_batt_ms) < 0) return;
  _next_batt_ms = millis() + 8000;
  uint16_t raw = getBattMilliVolts();
  if (raw > 0) _batt_mv = _batt_mv == 0 ? raw : (uint16_t)((_batt_mv * 4u + raw) / 5u);   // EMA, alpha 0.2
  uint16_t low = _prefs ? _prefs->low_batt_mv : 0;
  if (low == 0 || _batt_mv == 0 || _batt_mv >= low || _board->isExternalPowered()) return;
  wake();
  showToast("Low battery - shutting down", 3000);
  lv_timer_handler();
  delay(2000);
  shutdown();
}

uint32_t UITask::autoOffMillis() const {
  if (!_prefs || _prefs->auto_off_secs == 0) return 0;
  return (uint32_t)_prefs->auto_off_secs * 1000UL;
}

void UITask::sleep() {
  if (_asleep) return;
  _asleep = true;
  if (_display) _display->turnOff();
  lvport::powerSave(true, _tap_wake);
  if ((_prefs && _prefs->auto_lock) || _pin[0]) lockScreen();   // Lock screen, or a screen PIN
}

void UITask::wake() {
  lv_display_trigger_activity(NULL);
  if (!_asleep) return;
  _asleep = false;
  lvport::powerSave(false, _tap_wake);
  if (_display) _display->turnOn();
  refreshStatusBar();
  if (_screen == SCR_HOME) refreshHome();
  if (_screen == SCR_THREAD) refreshThread();
  lv_obj_invalidate(lv_screen_active());
}

// ── Core events ───────────────────────────────────────────────────────────────

void UITask::drainCoreEvents() {
  UiEvent ev;
  while (_core->events.pop(ev)) {
    switch (ev.type) {
    case UiEventType::MessageArrived:
      onMessageArrived(ev);
      break;
    case UiEventType::ClockAlert:
      showRing(ev.text);
      playMelody(soundctl::MEL_ALARM);
      break;
    case UiEventType::ClockRingEnded:
      stopMelody();
      hideRing();
      break;
    case UiEventType::LiveShareEnded:
      showToast("Live share ended");
      break;
    case UiEventType::LocatorCrossed:
      showToast(ev.text, 3000);
#ifdef PIN_BUZZER
      if (!_buzzer.isQuiet()) playMelody(ev.flag ? soundctl::MEL_ARRIVE : soundctl::MEL_LEAVE);
#endif
      break;
    case UiEventType::LocatorBeep:   // Settings > Proximity beeper
      playMelody(soundctl::MEL_TICK);
      break;
    case UiEventType::AdvertHeard:
      notify(ev.flag ? UIEventType::advertReceivedFlood : UIEventType::advertReceivedZeroHop);
      break;
    default:
      break;
    }
  }
}

void UITask::onMessageArrived(const UiEvent& ev) {
  if (ev.kind == UIEventType::contactMessage && ev.flag) {   // the sender's alert / melody overrides
    memcpy(_notif_dm_prefix, ev.key, 4);
    _notif_dm_valid = true;
  }
  if (ev.kind == UIEventType::channelMessage) _notif_ch_idx = ev.idx;
  notify(ev.kind);
  char buf[48];
  snprintf(buf, sizeof(buf), "Msg: %.20s", ev.text);
  // Wake for the message unless an app is already showing it, or the user
  // turned message-wake off.
  bool wake_disabled = _prefs && _prefs->msg_wake_screen_off;
  if (_asleep && !wake_disabled && !isClientConnected()) wake();
  else if (!_asleep) lv_display_trigger_activity(NULL);
  showToast(buf, 3000);
  if (_screen == SCR_CHATS && !_nav_overlay) buildChats();   // new unread counts (not under an open popup)
}

bool UITask::isViewingChannel(uint8_t channel_idx) {
  return _screen == SCR_THREAD && _thread_is_channel && _thread_channel == channel_idx;
}

bool UITask::isViewingDM(const uint8_t* pub_key) {
  return _screen == SCR_THREAD && !_thread_is_channel && memcmp(_thread_key, pub_key, 4) == 0;
}

// ── Status bar + toast (top layer, over every screen) ─────────────────────────

void UITask::buildStatusBar() {
  lv_obj_t* bar = lv_obj_create(lv_layer_top());
  styleSurface(bar, theme::BG);
  lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(bar, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(bar, LV_PCT(100), theme::STATUS_H);
  lv_obj_align(bar, LV_ALIGN_TOP_MID, 0, 0);
  lv_obj_set_style_pad_hor(bar, theme::PAD, 0);

  _status_time = label(bar, "--:--", THEME_FONT_SMALL, theme::TEXT);
  lv_obj_align(_status_time, LV_ALIGN_LEFT_MID, 0, 0);
  _status_batt = label(bar, "", THEME_FONT_ICONS, theme::TEXT_MUTED);
  lv_obj_align(_status_batt, LV_ALIGN_RIGHT_MID, 0, 0);
  _status_icons = lv_obj_create(bar);
  lv_obj_remove_style_all(_status_icons);
  lv_obj_remove_flag(_status_icons, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(_status_icons, 200, LV_PCT(100));   // fixed: packed to its right edge
  lv_obj_set_flex_flow(_status_icons, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(_status_icons, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(_status_icons, 2, 0);

  _toast = lv_obj_create(lv_layer_top());
  lv_obj_remove_flag(_toast, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(_toast, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(_toast, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_style_bg_color(_toast, lv_color_hex(theme::SURFACE_2), 0);
  lv_obj_set_style_border_color(_toast, lv_color_hex(theme::ACCENT), 0);
  lv_obj_set_style_border_width(_toast, 1, 0);
  lv_obj_set_style_radius(_toast, theme::RADIUS, 0);
  lv_obj_set_style_pad_hor(_toast, 12, 0);
  lv_obj_set_style_pad_ver(_toast, 6, 0);
  lv_obj_align(_toast, LV_ALIGN_BOTTOM_MID, 0, -12);
  lv_obj_t* tl = label(_toast, "", THEME_FONT_BODY, theme::TEXT);
  lv_label_set_long_mode(tl, LV_LABEL_LONG_WRAP);   // long texts wrap instead of running off the screen
  lv_obj_set_style_max_width(tl, lv_display_get_horizontal_resolution(NULL) - 40, 0);
  lv_obj_set_style_text_align(tl, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_add_flag(_toast, LV_OBJ_FLAG_HIDDEN);

  refreshStatusBar();
}

void UITask::refreshStatusBar() {
  if (!_status_time) return;
  struct tm ti;
  if (localTime(_prefs, ti)) {
    char clk[12];
    fmtClock(clk, sizeof(clk), ti, _prefs, true);
    lv_label_set_text(_status_time, clk);
  } else {
    lv_label_set_text(_status_time, "--:--");
  }

  // Battery: the icon, then % or volts per Settings > Battery display. The
  // smoothed reading (checkLowBattery): an ADC read blocks the loop for 10 ms.
  uint16_t mv = _batt_mv ? _batt_mv : getBattMilliVolts();
  int pct = battery::percent(mv, _prefs ? _prefs->low_batt_mv : 0);
  const char* batt = pct > 80 ? LV_SYMBOL_BATTERY_FULL : pct > 55 ? LV_SYMBOL_BATTERY_3
                   : pct > 30 ? LV_SYMBOL_BATTERY_2 : pct > 10 ? LV_SYMBOL_BATTERY_1 : LV_SYMBOL_BATTERY_EMPTY;
  char level[12] = "";
  switch (battery::mode(_prefs ? _prefs->batt_display_mode : 0)) {
    case battery::PERCENT: snprintf(level, sizeof(level), " %d%%", pct); break;
    case battery::VOLTAGE: snprintf(level, sizeof(level), " %u.%02u V", mv / 1000, (mv % 1000) / 10); break;
    default: break;
  }
  lv_label_set_text_fmt(_status_batt, "%s%s%s", _board->isExternalPowered() ? LV_SYMBOL_CHARGE : "", batt, level);

  // Status icons, as the original's status bar (ui-new): Bluetooth (bright
  // when the app is connected), WiFi (when switched on), GPS (green with a fix), the alarm, mute, then
  // the modes that keep running in the background -- auto-advert, trail, live
  // share, repeater, arrival alert -- in the accent colour. Right to left in
  // that order, next to the battery.
  struct Icon { const char* sym; uint32_t col; };
  Icon icons[11];
  int n = 0;
  if (isSerialEnabled()) icons[n++] = { LV_SYMBOL_BLUETOOTH, hasConnection() ? theme::TEXT : theme::TEXT_MUTED };
  if (lvport::wifiAllowed())   // WiFi switched on: bright while connected (map tiles, update)
    icons[n++] = { LV_SYMBOL_WIFI, lvport::netRadio() == lvport::NET_UP ? theme::TEXT : theme::TEXT_MUTED };
  int32_t lat, lon;
  bool fix = _core->course.currentLocation(lat, lon);
  if (_core->gpsEnabled() || fix) icons[n++] = { LV_SYMBOL_GPS, fix ? theme::OK : theme::TEXT_MUTED };
  if (_prefs && _prefs->alarm_on) icons[n++] = { LV_SYMBOL_BELL, theme::TEXT_MUTED };
#ifdef PIN_BUZZER
  if (_buzzer.isQuiet()) icons[n++] = { UI_SYMBOL_MUTE, theme::TEXT_MUTED };
#endif
  if (_prefs && _prefs->advert_auto_interval_sec > 0) icons[n++] = { UI_SYMBOL_RADIO, theme::ACCENT };
  if (_core->trail.isActive()) icons[n++] = { UI_SYMBOL_ROUTE, theme::ACCENT };
  if (_prefs && _prefs->loc_share_enabled) icons[n++] = { UI_SYMBOL_PIN, theme::ACCENT };
  if (_prefs && _prefs->client_repeat) icons[n++] = { LV_SYMBOL_LOOP, theme::ACCENT };
  if (_prefs && _prefs->locator_enabled && _prefs->locator_has_target) icons[n++] = { UI_SYMBOL_FLAG, theme::ACCENT };
  char sig[sizeof(_status_sig)];
  int o = 0;
  for (int i = 0; i < n && o < (int)sizeof(sig) - 12; i++) o += snprintf(sig + o, sizeof(sig) - o, "%s%lx", icons[i].sym, (unsigned long)icons[i].col);
  sig[o] = '\0';
  if (strcmp(sig, _status_sig) != 0) {   // rebuilt only when something changed
    strcpy(_status_sig, sig);
    lv_obj_clean(_status_icons);
    for (int i = n - 1; i >= 0; i--)   // the icon font gives each the same size and cell
      label(_status_icons, icons[i].sym, THEME_FONT_ICONS, icons[i].col);
  }
  lv_obj_align_to(_status_icons, _status_batt, LV_ALIGN_OUT_LEFT_MID, -5, 0);
}

void UITask::setGps(bool on) {
  if (!_core->setGpsEnabled(on)) { showToast("No GPS on this device"); return; }
  showToast(on ? "GPS on, waiting for a fix" : "GPS off");
  refreshStatusBar();
}

void UITask::botSetGPS(bool on) { if (_core->setGpsEnabled(on)) refreshStatusBar(); }

bool UITask::ensureGps() {
  int32_t lat, lon;
  if (_core->course.currentLocation(lat, lon)) return true;
  if (_core->gpsAvailable() && !_core->gpsEnabled()) setGps(true);
  else showToast("Waiting for a GPS fix");
  return false;
}

// One persistent timer, paused between toasts: hides the toast when it fires.
static void toastTimerCb(lv_timer_t* t) {
  anim::fadeHide((lv_obj_t*)lv_timer_get_user_data(t));
  lv_timer_pause(t);
}

void UITask::showToast(const char* text, uint32_t ms) {
  if (!_toast) return;
  lv_label_set_text(lv_obj_get_child(_toast, 0), text);
  bool shown = !lv_obj_has_flag(_toast, LV_OBJ_FLAG_HIDDEN);
  lv_anim_delete(_toast, NULL);   // a fade-out in progress
  lv_obj_set_style_opa(_toast, LV_OPA_COVER, 0);
  lv_obj_remove_flag(_toast, LV_OBJ_FLAG_HIDDEN);
  if (!shown) anim::rise(_toast, 12);   // a replaced text just changes
  if (!_toast_timer) _toast_timer = lv_timer_create(toastTimerCb, ms, _toast);
  lv_timer_set_period(_toast_timer, ms);
  lv_timer_reset(_toast_timer);
  lv_timer_resume(_toast_timer);
}

// ── Screens ───────────────────────────────────────────────────────────────────

static void onBack(lv_event_t* e) { (void)e; s_ui->back(); }

// A fresh screen below the status bar, loaded in place of the current one;
// returns its content area (flex column). The old screen is deleted
// asynchronously -- this usually runs from a click on one of its own widgets.
lv_obj_t* UITask::newScreen(const char* title, bool with_back) {
  _home_clock = _home_date = _home_unread = nullptr;
  _thread_list = _compose_ta = _keyboard = nullptr;
  _header = _body = nullptr;
  _nearby_list = _nearby_status = _nearby_sort_lbl = _nearby_chips = nullptr;
  _node_info = _node_ping = _node_delete_lbl = nullptr;
  _scan_overlay = _scan_list = _scan_status = nullptr;   // the popup went with the old screen
  _map_area = _map_marks = _map_me = _map_zoom_lbl = _map_hint = _map_dl_pill = nullptr;
  _dl_overlay = _dl_info = _dl_zoom_lbl = _dl_start_lbl = _dl_job_row = _dl_job_lbl = nullptr;
  _nav_bar = _nav_title = _nav_info = _nav_clear = nullptr;
  _nav_overlay = _nav_ta = _nav_kb = _nav_del_lbl = _nav_rec = _nav_avg_pill = nullptr;
  _nav_trail_lbl = _nav_trail_btn = _nav_reset_lbl = _nav_share_lbl = _nav_share_btn = _nav_tb_btn = nullptr;
  _wifi_ssid = _wifi_pass = _wifi_kb = _wifi_list = _wifi_status = nullptr;
  if (_wifi_scanning && !wifiInUse()) lvport::netEnd();   // left mid-scan: the radio goes off
  _wifi_scanning = false;
  _ota_status = _ota_bar = _ota_btn = _ota_btn_lbl = nullptr;
  for (lv_obj_t*& t : _map_tiles) t = nullptr;
  lv_obj_t* prev = _scr;
  lv_obj_t* scr = lv_obj_create(NULL);
  styleSurface(scr, theme::BG);
  lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

  int top = theme::STATUS_H;
  if (title) {
    lv_obj_t* hdr = lv_obj_create(scr);
    styleSurface(hdr, theme::BG);
    lv_obj_remove_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(hdr, LV_PCT(100), 32);
    lv_obj_set_pos(hdr, 0, top);
    _header = hdr;
    if (with_back) {
      lv_obj_t* b = lv_button_create(hdr);
      lv_obj_set_size(b, 40, 28);
      lv_obj_align(b, LV_ALIGN_LEFT_MID, 4, 0);
      lv_obj_set_style_bg_color(b, lv_color_hex(theme::SURFACE), 0);
      lv_obj_set_style_shadow_width(b, 0, 0);
      lv_obj_add_event_cb(b, onBack, LV_EVENT_CLICKED, NULL);
      lv_obj_center(label(b, LV_SYMBOL_LEFT, THEME_FONT_BODY, theme::TEXT));
    }
    lv_obj_t* t = label(hdr, title, THEME_FONT_TITLE, theme::TEXT);
    lv_label_set_long_mode(t, LV_LABEL_LONG_DOT);
    lv_obj_set_width(t, 230);
    lv_obj_align(t, LV_ALIGN_LEFT_MID, with_back ? 52 : theme::PAD, 0);
    top += 32;
  }

  lv_obj_t* body = lv_obj_create(scr);
  styleSurface(body, theme::BG);
  lv_obj_set_size(body, LV_PCT(100), lv_display_get_vertical_resolution(NULL) - top);
  lv_obj_set_pos(body, 0, top);
  lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all(body, theme::PAD, 0);
  lv_obj_set_style_pad_row(body, theme::GAP, 0);
  _body = body;
  // The new screen emerges from the middle (Anim.h); a screen rebuilt in
  // place (same screen, same title) swaps without motion.
  bool same = _screen == _shown_screen && strncmp(title ? title : "", _shown_title, sizeof(_shown_title) - 1) == 0;
  bool backward = _nav_back || screenDepth(_screen) < screenDepth(_shown_screen);
  _nav_back = false;
  _shown_screen = _screen;
  snprintf(_shown_title, sizeof(_shown_title), "%s", title ? title : "");
  _scr = scr;
  lv_screen_load(scr);
  if (prev) lv_obj_delete_async(prev);   // this usually runs from one of its own widgets
  if (prev && !same && !_asleep) anim::screenIn(scr, body, backward);
  return body;
}

// How deep a screen sits below Home, for the slide direction.
int UITask::screenDepth(Screen s) {
  switch (s) {
    case SCR_HOME: return 0;
    case SCR_CHATS: case SCR_NEARBY: case SCR_MAP: case SCR_SETTINGS:
    case SCR_COMPASS: case SCR_CLOCK: case SCR_BOT: case SCR_REPEATER: case SCR_ADMIN_PICK:
    case SCR_DIAG: case SCR_GPS: return 1;
    case SCR_THREAD: case SCR_CONTACTS: case SCR_CHANNEL_EDIT: case SCR_NODE:
    case SCR_SETTINGS_NAV: case SCR_ADMIN: return 2;
    default: return 3;   // pages under a settings page
  }
}

void UITask::back() {
  _nav_back = true;   // the screen shown from here slides back
  switch (_screen) {
    case SCR_THREAD:   if (_nav_overlay) navClosePopup(); else showChats(); break;
    case SCR_CONTACTS: showChats(); break;
    case SCR_CHANNEL_EDIT: showChats(); break;
    case SCR_BOT:      if (_nav_overlay) navClosePopup(); else showHome(); break;
    case SCR_DIAG:     if (_nav_overlay) navClosePopup(); else showHome(); break;
    case SCR_ADMIN_PICK: showHome(); break;
    case SCR_OTA:      otaLeave(); break;
    case SCR_COMPASS:  showHome(); break;
    case SCR_GPS:      if (_gps_from_settings) showSettings(); else showHome(); break;
    case SCR_ADMIN:    if (_nav_overlay) navClosePopup(); else adminLeave(); break;
    case SCR_SETTINGS: if (_nav_overlay) navClosePopup(); else showHome(); break;
    case SCR_SETTINGS_NAV:   // the map's options go back to the map
      if (_settings_page == settings::PG_NAV) openMap(true); else showSettings();
      break;
    case SCR_QUICK:    // back to Messages & contacts' bottom, where the row is
      if (_nav_overlay) { navClosePopup(); break; }
      showSchemaSettings(settings::PG_MESSAGES);
      if (_body) { lv_obj_update_layout(_body); lv_obj_scroll_by(_body, 0, -lv_obj_get_scroll_bottom(_body), LV_ANIM_OFF); }
      break;
    case SCR_MELODY:   // back to the Sound page's bottom, where the melodies are
      melodySave();
      stopMelody();
      showSchemaSettings(settings::PG_SOUND);
      if (_body) { lv_obj_update_layout(_body); lv_obj_scroll_by(_body, 0, -lv_obj_get_scroll_bottom(_body), LV_ANIM_OFF); }
      break;
    case SCR_CLOCK:    showHome(); break;
    case SCR_RADIO:
      if (radioPopupOpen()) radioCloseFreq();
      else if (_nav_overlay) navClosePopup();
      else showSettings();
      break;
    case SCR_REPEATER:
      if (radioPopupOpen()) radioCloseFreq();
      else if (_nav_overlay) navClosePopup();
      else showHome();
      break;
    case SCR_SCOPES:   // back to the Radio screen's bottom, where Scopes is
      if (_nav_overlay) { navClosePopup(); break; }
      showRadio();
      if (_body) { lv_obj_update_layout(_body); lv_obj_scroll_by(_body, 0, -lv_obj_get_scroll_bottom(_body), LV_ANIM_OFF); }
      break;
    case SCR_CHATS:    if (_nav_overlay) navClosePopup(); else showHome(); break;
    case SCR_NEARBY:
      if (_nav_overlay) navClosePopup();   // the advert popup
      else if (_scan_overlay) closeScanPopup();
      else showHome();
      break;
    case SCR_NODE:     // back to where the node was picked: map, the list, or the scan popup over it
      if (_node_from_map) { openMap(false); break; }
      _screen = SCR_NEARBY;
      buildNearby();
      if (_node_from_scan) showScanPopup();
      break;
    case SCR_MAP:
      if (_nav_overlay) navClosePopup();
      else if (_dl_overlay) mapDownloadClose();
      else if (!_map_nav) showNearby();   // the Nodes map opens from Nearby
      else showHome();
      break;
    case SCR_WIFI:     showSettings(); break;
    case SCR_STORAGE:  storeview::stopWalk(); showSettings(); break;
    default:           break;
  }
  _nav_back = false;   // only closed a popup
}

// ── Home ──────────────────────────────────────────────────────────────────────

static void onOpenChats(lv_event_t* e) { (void)e; s_ui->showChats(); }
static void onOpenSettings(lv_event_t* e) { (void)e; s_ui->showSettings(); }

static void onOpenNearby(lv_event_t* e) { (void)e; s_ui->showNearby(); }
static void onOpenNodesMap(lv_event_t* e) { (void)e; s_ui->openMap(false); }
static void onOpenNav(lv_event_t* e) { (void)e; s_ui->openMap(true); }
static void onOpenClock(lv_event_t* e);   // ClockScreen.h
static void onOpenBot(lv_event_t* e);     // BotScreen.h
static void onOpenCompass(lv_event_t* e);   // CompassScreen.h
static void onOpenDiag(lv_event_t* e);      // DiagScreen.h
static void onOpenGps(lv_event_t* e);       // GpsScreen.h
static void onOpenGpsFromSettings(lv_event_t* e);
static void onOpenRepeater(lv_event_t* e);  // RepeaterScreen.h
static void onOpenAdminPick(lv_event_t* e); // AdminScreen.h
static void onNodeName(lv_event_t* e);
static void onPowerRow(lv_event_t* e);

// Home: the favourites card first (swiped to, left of the main page), then
// the apps, PER_PAGE to a page, further pages swiped to (dots underneath), so
// new apps don't squeeze the row.
namespace home {
struct App { const char* icon; const char* text; lv_event_cb_t cb; bool unread; };
static const App APPS[] = {
  { LV_SYMBOL_ENVELOPE, "Messages", onOpenChats,    true  },
  { UI_SYMBOL_USERS,    "Nodes",    onOpenNearby,   false },
  { UI_SYMBOL_MAP,      "Map",      onOpenNav,      false },
  { LV_SYMBOL_SETTINGS, "Settings", onOpenSettings, false },
  // Page 2: tools
  { UI_SYMBOL_COMPASS,  "Compass",  onOpenCompass,  false },
  { UI_SYMBOL_CLOCK,    "Clock",    onOpenClock,    false },
  { LV_SYMBOL_CHARGE,   "Bot",      onOpenBot,      false },
  { LV_SYMBOL_GPS,      "GPS",      onOpenGps,      false },
  // Page 3: the device and the network (the map's tools and Nearby's advert
  // are in those screens, not tiles of their own)
  { LV_SYMBOL_LOOP,     "Repeater", onOpenRepeater, false },
  { UI_SYMBOL_KEY,      "Admin",    onOpenAdminPick, false },
  { UI_SYMBOL_CHART,    "Diagnostics", onOpenDiag,  false },
};
static const int COUNT = sizeof(APPS) / sizeof(APPS[0]);
static const int PER_PAGE = 4;
static const int FAVS = 0;   // page 0: the favourites card
static const int MAIN = 1;   // the first page of apps, where Home opens
static const int PAGES = 1 + (COUNT + PER_PAGE - 1) / PER_PAGE;
static lv_obj_t* s_row = nullptr;    // the current page's tiles (or favourites)
static lv_obj_t* s_dots = nullptr;
static lv_obj_t* s_top = nullptr;    // clock, date and name: hidden on the favourites card
static int s_page = MAIN;   // kept across rebuilds (the home screen is rebuilt on return)
static int s_fav_sig = -1;  // unread total the favourites card was drawn with

// A horizontal swipe anywhere on Home turns the page (the tiles row alone is
// too small a target), tracked from the touch itself: LVGL's gesture detector
// drops a slow swipe (its sum resets on every read without movement). Tapping
// a dot also goes to that page.
static bool     s_touching = false, s_swiped = false;
static lv_point_t s_start;
static const int SWIPE_PX = 40;
static void onDot(lv_event_t* e) { s_ui->setHomePage((int)(uintptr_t)lv_event_get_user_data(e)); }
}  // namespace home

// Home tile: icon over label, one of a page's row; optional amber value
// (unread count) in its top-right corner.
static lv_obj_t* homeTile(lv_obj_t* parent, const char* icon, const char* text, lv_event_cb_t cb,
                          lv_obj_t** value_out) {
  lv_obj_t* tile = lv_button_create(parent);
  lv_obj_set_height(tile, 64);
  lv_obj_set_flex_grow(tile, 1);
  lv_obj_set_style_bg_color(tile, lv_color_hex(theme::SURFACE), 0);
  lv_obj_set_style_bg_color(tile, lv_color_hex(theme::SURFACE_2), LV_STATE_PRESSED);
  lv_obj_set_style_radius(tile, theme::RADIUS, 0);
  lv_obj_set_style_shadow_width(tile, 0, 0);
  lv_obj_set_style_pad_all(tile, 4, 0);
  lv_obj_add_event_cb(tile, cb, LV_EVENT_CLICKED, NULL);
  lv_obj_align(label(tile, icon, THEME_FONT_LARGE, theme::TEXT), LV_ALIGN_TOP_MID, 0, 6);
  lv_obj_align(label(tile, text, THEME_FONT_SMALL, theme::TEXT_MUTED), LV_ALIGN_BOTTOM_MID, 0, -4);
  if (value_out) {
    *value_out = label(tile, "", THEME_FONT_SMALL, theme::ACCENT);
    lv_obj_align(*value_out, LV_ALIGN_TOP_RIGHT, -2, 0);
  }
  return tile;
}

void UITask::showHome() {
  _screen = SCR_HOME;
  _share_text[0] = '\0';   // a share not sent to anyone
  buildHome();
  refreshHome();
}

void UITask::buildHome() {
  lv_obj_t* body = newScreen(NULL, false);
  lv_obj_set_flex_align(body, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  home::s_top = lv_obj_create(body);
  lv_obj_remove_style_all(home::s_top);
  lv_obj_remove_flag(home::s_top, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(home::s_top, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(home::s_top, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(home::s_top, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  _home_clock = label(home::s_top, "--:--", THEME_FONT_CLOCK, theme::TEXT);
  _home_date = label(home::s_top, "", THEME_FONT_BODY, theme::TEXT_MUTED);
  lv_obj_t* name = label(home::s_top, the_mesh.getNodeName(), THEME_FONT_BODY, theme::ACCENT);
  lv_obj_set_style_pad_bottom(name, 4, 0);

  lv_obj_remove_flag(body, LV_OBJ_FLAG_SCROLLABLE);   // a drag is a page swipe, not a scroll
  home::s_row = lv_obj_create(body);
  styleSurface(home::s_row, theme::BG);
  lv_obj_remove_flag(home::s_row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(home::s_row, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(home::s_row, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_column(home::s_row, theme::GAP, 0);
  lv_obj_set_style_pad_row(home::s_row, theme::GAP, 0);
  home::s_dots = nullptr;
  if (home::PAGES > 1) {
    home::s_dots = lv_obj_create(body);
    lv_obj_remove_style_all(home::s_dots);
    lv_obj_add_flag(home::s_dots, LV_OBJ_FLAG_IGNORE_LAYOUT);   // at the bottom whichever card is up
    lv_obj_set_size(home::s_dots, LV_SIZE_CONTENT, 22);
    lv_obj_align(home::s_dots, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_flex_flow(home::s_dots, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(home::s_dots, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(home::s_dots, 12, 0);
    for (int p = 0; p < home::PAGES; p++) {
      lv_obj_t* d = lv_obj_create(home::s_dots);
      lv_obj_remove_style_all(d);
      lv_obj_set_size(d, 8, 8);
      lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
      lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
      lv_obj_set_ext_click_area(d, 8);
      lv_obj_add_flag(d, LV_OBJ_FLAG_CLICKABLE);
      lv_obj_add_event_cb(d, home::onDot, LV_EVENT_CLICKED, (void*)(uintptr_t)p);
    }
  }
  setHomePage(home::s_page);
}

// From loop() on Home: turn the page once a press has moved SWIPE_PX sideways.
void UITask::homeSwipePoll() {
  lv_indev_t* in = lv_indev_get_next(NULL);
  if (!in || home::PAGES < 2) return;
  bool down = lv_indev_get_state(in) == LV_INDEV_STATE_PRESSED;
  lv_point_t p;
  lv_indev_get_point(in, &p);
  if (!down) { home::s_touching = false; return; }
  if (!home::s_touching) { home::s_touching = true; home::s_swiped = false; home::s_start = p; return; }
  if (home::s_swiped) return;
  int dx = p.x - home::s_start.x, dy = p.y - home::s_start.y;
  if (abs(dx) < home::SWIPE_PX || abs(dx) < 2 * abs(dy)) return;
  home::s_swiped = true;
  lv_indev_wait_release(in);   // the swipe isn't also a tap on the tile it started on
  setHomePage(home::s_page + (dx < 0 ? 1 : -1));
}

void UITask::setHomePage(int page) {
  if (_screen != SCR_HOME || !home::s_row) return;   // s_row went with an older screen
  if (page < 0 || page >= home::PAGES) return;
  int from = home::s_page;
  home::s_page = page;
  lv_obj_clean(home::s_row);
  _home_unread = nullptr;
  bool favs = page == home::FAVS;
  if (favs) lv_obj_add_flag(home::s_top, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_remove_flag(home::s_top, LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_flex_flow(home::s_row, favs ? LV_FLEX_FLOW_ROW_WRAP : LV_FLEX_FLOW_ROW);
  if (favs) {
    home::s_fav_sig = unreadTotal();
    favGrid(home::s_row);   // DeviceScreen.h
  }
  int first = (page - home::MAIN) * home::PER_PAGE;
  for (int i = first; !favs && i < first + home::PER_PAGE; i++) {
    if (i < home::COUNT) {
      const home::App& a = home::APPS[i];
      homeTile(home::s_row, a.icon, a.text, a.cb, a.unread ? &_home_unread : NULL);
    } else {   // keep a short last page's tiles the same width
      lv_obj_t* gap = lv_obj_create(home::s_row);
      lv_obj_remove_style_all(gap);
      lv_obj_set_height(gap, 1);
      lv_obj_set_flex_grow(gap, 1);
    }
  }
  for (int i = 0; home::s_dots && i < (int)lv_obj_get_child_count(home::s_dots); i++)
    lv_obj_set_style_bg_color(lv_obj_get_child(home::s_dots, i), lv_color_hex(i == page ? theme::ACCENT : theme::SURFACE_2), 0);
  if (page != from) anim::slideIn(home::s_row, page > from ? 48 : -48);
  refreshHome();
}

int UITask::unreadTotal() {
  return _core->dmUnreadTotal() + _core->history.getTotalChannelUnread() + _core->roomUnread();
}

void UITask::refreshHome() {
  if (!_home_clock) return;
  struct tm ti;
  if (localTime(_prefs, ti)) {
    char clk[12], date[48];
    fmtClock(clk, sizeof(clk), ti, _prefs, false, true);
    fmtDate(date, sizeof(date), ti, _prefs);
    lv_label_set_text(_home_clock, clk);
    lv_label_set_text(_home_date, date);
  } else {
    lv_label_set_text(_home_clock, "--:--");
    lv_label_set_text(_home_date, "time not synced");
  }
  if (home::s_page == home::FAVS && home::s_row && unreadTotal() != home::s_fav_sig) {
    setHomePage(home::FAVS);   // new messages: the favourites' badges redrawn
    return;
  }
  if (!_home_unread) return;
  int unread = unreadTotal();
  if (unread > 0) lv_label_set_text_fmt(_home_unread, "%d", unread);
  else lv_label_set_text(_home_unread, "");
}

// ── Conversation list ─────────────────────────────────────────────────────────

static void onOpenChannel(lv_event_t* e) {
  s_ui->openChannel((uint8_t)(uintptr_t)lv_event_get_user_data(e));
}

// DM rows carry a 4-byte prefix; kept in a static table the rows point into.
static uint8_t s_dm_rows[MessageHistory::DM_HIST_MAX][4];
static const int CONTACT_ROWS_MAX = 256;   // "All" with a full contact table stays usable
static uint8_t (*s_contact_rows)[PUB_KEY_SIZE] = psramBuf<uint8_t[PUB_KEY_SIZE]>(CONTACT_ROWS_MAX);

static void onOpenDMRow(lv_event_t* e) {
  s_ui->openDM(s_dm_rows[(uintptr_t)lv_event_get_user_data(e)]);
}
static void onOpenContactRow(lv_event_t* e) {
  s_ui->openDM(s_contact_rows[(uintptr_t)lv_event_get_user_data(e)]);
}
static void onNewChat(lv_event_t* e) { (void)e; s_ui->showContacts(); }
static void onChanRowHold(lv_event_t* e);      // ChannelScreen.h
static void onChanAdd(lv_event_t* e);
static void onChanThreadMenu(lv_event_t* e);
static const int ROOM_ROWS_MAX = 32;
static uint8_t (*s_room_rows)[PUB_KEY_SIZE] = psramBuf<uint8_t[PUB_KEY_SIZE]>(ROOM_ROWS_MAX);
static void onDMRowHold(lv_event_t* e);        // ConversationScreen.h
static void onRoomRow(lv_event_t* e);
static void onRoomRowHold(lv_event_t* e);
static void onConvThreadMenu(lv_event_t* e);
static void onMsgHold(lv_event_t* e);
static void onChatFilter(lv_event_t* e);
enum : uint8_t { CF_CHANNELS, CF_ROOMS, CF_CONTACTS };   // favourites-only filters

// One tappable list row: title, optional muted subtitle, optional badge.
static lv_obj_t* listRow(lv_obj_t* parent, const char* title, const char* sub,
                         lv_event_cb_t cb, void* user) {
  lv_obj_t* row = lv_button_create(parent);
  lv_obj_set_size(row, LV_PCT(100), theme::ROW_H);
  lv_obj_set_style_bg_color(row, lv_color_hex(theme::SURFACE), 0);
  lv_obj_set_style_bg_color(row, lv_color_hex(theme::SURFACE_2), LV_STATE_PRESSED);
  lv_obj_set_style_radius(row, theme::RADIUS, 0);
  lv_obj_set_style_shadow_width(row, 0, 0);
  lv_obj_set_style_pad_all(row, 0, 0);
  lv_obj_add_event_cb(row, cb, LV_EVENT_CLICKED, user);
  lv_obj_t* t = label(row, title, THEME_FONT_BODY, theme::TEXT);
  lv_label_set_long_mode(t, LV_LABEL_LONG_DOT);
  lv_obj_set_width(t, 230);
  lv_obj_align(t, LV_ALIGN_TOP_LEFT, theme::PAD, sub ? 5 : 13);
  if (sub) {
    lv_obj_t* s = label(row, sub, THEME_FONT_SMALL, theme::TEXT_MUTED);
    lv_label_set_long_mode(s, LV_LABEL_LONG_DOT);
    lv_obj_set_size(s, 230, 15);   // fixed height: LONG_DOT cuts instead of wrapping over the title
    lv_obj_align(s, LV_ALIGN_BOTTOM_LEFT, theme::PAD, -5);
  }
  return row;
}

static lv_obj_t* sectionTitle(lv_obj_t* parent, const char* text) {
  lv_obj_t* l = label(parent, text, THEME_FONT_SMALL, theme::TEXT_MUTED);
  lv_obj_set_style_pad_top(l, 4, 0);
  return l;
}

// Section title with an "All" / "★ Fav" pill on the right that flips the
// section's favourites-only filter.
static void sectionWithFilter(lv_obj_t* parent, const char* text, bool fav_only, uint8_t which) {
  lv_obj_t* row = lv_obj_create(parent);
  styleSurface(row, theme::BG);
  lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(row, LV_PCT(100), 26);
  lv_obj_align(label(row, text, THEME_FONT_SMALL, theme::TEXT_MUTED), LV_ALIGN_BOTTOM_LEFT, 0, -2);
  lv_obj_t* b = lv_button_create(row);
  lv_obj_set_size(b, LV_SIZE_CONTENT, 24);
  lv_obj_set_style_pad_hor(b, 10, 0);
  lv_obj_set_style_pad_ver(b, 0, 0);
  lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_shadow_width(b, 0, 0);
  lv_obj_set_style_bg_color(b, lv_color_hex(fav_only ? theme::ACCENT_DIM : theme::SURFACE), 0);
  lv_obj_align(b, LV_ALIGN_RIGHT_MID, -2, 0);
  lv_obj_add_event_cb(b, onChatFilter, LV_EVENT_CLICKED, (void*)(uintptr_t)which);
  lv_obj_center(label(b, fav_only ? UI_SYMBOL_STAR " Fav" : "All", THEME_FONT_SMALL, theme::TEXT));
}

void UITask::showChats() {
  _screen = SCR_CHATS;
  buildChats();
}

static void onMarkAllRead(lv_event_t* e) { (void)e; s_ui->markAllRead(); }
static lv_obj_t* headerButton(lv_obj_t* hdr, const char* text, lv_event_cb_t cb, int right, lv_obj_t** label_out);

void UITask::buildChats() {
  lv_obj_t* body = newScreen("Messages", true);
  if (_header && unreadTotal() > 0)
    headerButton(_header, LV_SYMBOL_OK " Read all", onMarkAllRead, 4, NULL);

  // Channels: favourites first (unless turned off), hold a row for its options
  bool ch_fav_only = _prefs && _prefs->ch_fav_only;
  sectionWithFilter(body, "CHANNELS", ch_fav_only, CF_CHANNELS);
  bool fav_first = !(_prefs && _prefs->fav_sort_off);
  int ch_rows = 0;
  for (int pass = fav_first ? 0 : 1; pass < 2; pass++) {
    for (int i = 0; i < MAX_GROUP_CHANNELS; i++) {
      ChannelDetails ch;
      if (!the_mesh.getChannel(i, ch) || ch.name[0] == '\0') continue;
      bool fav = chanctl::favourite(_prefs, i);
      if (ch_fav_only && !fav) continue;
      if (fav_first && fav != (pass == 0)) continue;
      ch_rows++;
      char title[48], sub[64] = "";
      snprintf(title, sizeof(title), "%s%s%s", fav ? UI_SYMBOL_STAR " " : "", ch.name,
               chanctl::notif(_prefs, i) == chanctl::NOTIF_MUTED ? "  " UI_SYMBOL_MUTE : "");
      int n = _core->history.histCountForChannel(i);
      if (n > 0) {
        const ChHistEntry& e = _core->history.chAtPos(_core->history.histEntryForChannel(i, 0));
        snprintf(sub, sizeof(sub), "%s", e.text);
        plainMentions(sub);
      }
      lv_obj_t* row = listRow(body, title, sub[0] ? sub : NULL, onOpenChannel, (void*)(uintptr_t)i);
      lv_obj_add_event_cb(row, onChanRowHold, LV_EVENT_LONG_PRESSED, (void*)(uintptr_t)i);
      badge(row, _core->history.chUnread(i), _core->history.chUnreadOverflow(i));
    }
  }
  if (ch_rows == 0 && ch_fav_only) label(body, "No favourite channels", THEME_FONT_SMALL, theme::TEXT_MUTED);
  lv_obj_t* add_ch = lv_button_create(body);
  lv_obj_set_size(add_ch, LV_PCT(100), 32);
  lv_obj_set_style_bg_color(add_ch, lv_color_hex(theme::BG), 0);
  lv_obj_set_style_border_color(add_ch, lv_color_hex(theme::SURFACE_2), 0);
  lv_obj_set_style_border_width(add_ch, 1, 0);
  lv_obj_set_style_radius(add_ch, theme::RADIUS, 0);
  lv_obj_set_style_shadow_width(add_ch, 0, 0);
  lv_obj_add_event_cb(add_ch, onChanAdd, LV_EVENT_CLICKED, NULL);
  lv_obj_center(label(add_ch, LV_SYMBOL_PLUS "  Add channel", THEME_FONT_SMALL, theme::TEXT_MUTED));

  // Recent direct conversations (DM ring, newest first, one row per contact)
  sectionTitle(body, "DIRECT");
  int rows = 0;
  for (int j = 0; j < _core->history.dmHistCount() && rows < MessageHistory::DM_HIST_MAX; j++) {
    const DmHistEntry& e = _core->history.dmAtPos(_core->history.dmHistPosNewest(j));
    bool seen = false;
    for (int r = 0; r < rows; r++) if (memcmp(s_dm_rows[r], e.prefix, 4) == 0) { seen = true; break; }
    if (seen) continue;
    ContactInfo c;
    bool known = MessageHistory::contactByPrefix(e.prefix, c);
    if (known && c.type == ADV_TYPE_ROOM) continue;   // rooms have their own section
    memcpy(s_dm_rows[rows], e.prefix, 4);
    char name[48];
    contactName(e.prefix, name, sizeof(name));
    if (known && contactctl::favourite(c)) { char t[48]; snprintf(t, sizeof(t), UI_SYMBOL_STAR " %s", name); strcpy(name, t); }
    if (known && contactctl::notif(_prefs, c.id.pub_key) == contactctl::NOTIF_MUTED) strncat(name, "  " UI_SYMBOL_MUTE, sizeof(name) - strlen(name) - 1);
    char sub[64];
    snprintf(sub, sizeof(sub), "%s%s", e.outgoing ? "Me: " : "", e.text);
    plainMentions(sub);
    lv_obj_t* row = listRow(body, name, sub, onOpenDMRow, (void*)(uintptr_t)rows);
    if (known) lv_obj_add_event_cb(row, onDMRowHold, LV_EVENT_LONG_PRESSED, (void*)(uintptr_t)rows);
    badge(row, _core->dmUnread(e.prefix), _core->dmUnreadOverflow(e.prefix));
    rows++;
  }
  if (rows == 0) label(body, "No conversations yet", THEME_FONT_SMALL, theme::TEXT_MUTED);

  // Room servers: tap logs in (saved password or ask) and opens; hold: options
  bool room_fav_only = _prefs && _prefs->room_fav_only;
  int room_unread = _core->roomUnread();
  char rt[32];
  if (room_unread > 0) snprintf(rt, sizeof(rt), "ROOMS  -  %d new", room_unread);
  else snprintf(rt, sizeof(rt), "ROOMS");
  sectionWithFilter(body, rt, room_fav_only, CF_ROOMS);
  int nrooms = 0, total = the_mesh.getNumContacts();
  const int MAX_ROOMS = ROOM_ROWS_MAX;
  for (int pass = fav_first ? 0 : 1; pass < 2; pass++) {
    for (int i = 0; i < total && nrooms < MAX_ROOMS; i++) {
      ContactInfo c;
      if (!the_mesh.getContactByIdx(MAX_ANON_CONTACTS + i, c) || c.type != ADV_TYPE_ROOM) continue;
      bool fav = contactctl::favourite(c);
      if (room_fav_only && !fav) continue;
      if (fav_first && fav != (pass == 0)) continue;
      memcpy(s_room_rows[nrooms], c.id.pub_key, PUB_KEY_SIZE);
      char title[48], sub[64];
      snprintf(title, sizeof(title), "%s%s", fav ? UI_SYMBOL_STAR " " : "", c.name);
      if (_core->history.dmHistCountForContact(c.id.pub_key) > 0) {
        const DmHistEntry& e = _core->history.dmAtPos(_core->history.dmHistEntryForContact(c.id.pub_key, 0));
        snprintf(sub, sizeof(sub), "%s%s", e.outgoing ? "Me: " : "", e.text);
        plainMentions(sub);
      } else {
        snprintf(sub, sizeof(sub), "%s", _core->rooms.isLoggedIn(c.id.pub_key) ? "Logged in" : "Tap to log in");
      }
      lv_obj_t* row = listRow(body, title, sub, onRoomRow, (void*)(uintptr_t)nrooms);
      lv_obj_add_event_cb(row, onRoomRowHold, LV_EVENT_LONG_PRESSED, (void*)(uintptr_t)nrooms);
      nrooms++;
    }
  }
  if (nrooms == 0) label(body, room_fav_only ? "No favourite rooms" : "No room servers known", THEME_FONT_SMALL, theme::TEXT_MUTED);

  lv_obj_t* add = lv_button_create(body);
  lv_obj_set_size(add, LV_PCT(100), 36);
  lv_obj_set_style_bg_color(add, lv_color_hex(theme::ACCENT_DIM), 0);
  lv_obj_set_style_radius(add, theme::RADIUS, 0);
  lv_obj_set_style_shadow_width(add, 0, 0);
  lv_obj_add_event_cb(add, onNewChat, LV_EVENT_CLICKED, NULL);
  lv_obj_center(label(add, LV_SYMBOL_PLUS "  New message", THEME_FONT_BODY, theme::TEXT));
  stylePrimary(add);
}

// ── Contact picker (start a DM) ───────────────────────────────────────────────

void UITask::showContacts() {
  _screen = SCR_CONTACTS;
  buildContacts();
}

void UITask::buildContacts() {
  lv_obj_t* body = newScreen("New message", true);
  bool fav_only = _prefs && !_prefs->dm_show_all;   // ui-new's default: favourites only
  sectionWithFilter(body, "CONTACTS", fav_only, CF_CONTACTS);
  int total = the_mesh.getNumContacts();
  int rows = 0;
  // +MAX_ANON_CONTACTS: getContactByIdx() takes the raw table index (see
  // MessageHistory::contactByPrefix()).
  for (int i = 0; i < total && rows < CONTACT_ROWS_MAX; i++) {
    ContactInfo c;
    if (!the_mesh.getContactByIdx(MAX_ANON_CONTACTS + i, c)) continue;
    if (c.type != ADV_TYPE_CHAT) continue;
    if (fav_only && !contactctl::favourite(c)) continue;
    memcpy(s_contact_rows[rows], c.id.pub_key, PUB_KEY_SIZE);
    listRow(body, c.name, NULL, onOpenContactRow, (void*)(uintptr_t)rows);
    rows++;
  }
  if (rows == 0) label(body, fav_only ? "No favourites - tap All" : "No contacts yet", THEME_FONT_BODY, theme::TEXT_MUTED);
}

// ── Nearby ────────────────────────────────────────────────────────────────────
// Contacts / live shares / heard adverts from NearbyModel, filtered by type
// chips, sorted by distance or recency. Tap a row for detail. Scan (nodes that
// answer a discover request right now) is a popup over the list with its own
// model, since it is a different set: who is in range, not who is known.

static NearbyModel::Entry s_node;   // the node open in SCR_NODE (a copy: the list re-sorts)
enum : uint8_t { NODE_MSG, NODE_PING, NODE_FAV, NODE_ADD, NODE_DELETE, NODE_NAV, NODE_ADMIN, NODE_WAYPOINT, NODE_PIN };

static void onNearbyChip(lv_event_t* e) { s_ui->setNearbyFilter((uint8_t)(uintptr_t)lv_event_get_user_data(e)); }
static void onNearbySort(lv_event_t* e) { (void)e; s_ui->toggleNearbySort(); }
static void onNearbyScan(lv_event_t* e) { (void)e; s_ui->startNearbyScan(); }
static void onAdvertRow(lv_event_t* e);   // QuickScreen.h
static void onNearbyRow(lv_event_t* e)  { s_ui->openNode((int)(uintptr_t)lv_event_get_user_data(e)); }
static void onScanRow(lv_event_t* e)    { s_ui->openScanNode((int)(uintptr_t)lv_event_get_user_data(e)); }
static void onScanClose(lv_event_t* e)  { (void)e; s_ui->closeScanPopup(); }
static void onNodeAction(lv_event_t* e) { s_ui->nodeAction((uint8_t)(uintptr_t)lv_event_get_user_data(e)); }

static lv_obj_t* headerButton(lv_obj_t* hdr, const char* text, lv_event_cb_t cb, int right, lv_obj_t** label_out) {
  lv_obj_t* b = lv_button_create(hdr);
  lv_obj_set_size(b, LV_SIZE_CONTENT, 28);
  lv_obj_set_style_pad_hor(b, 10, 0);
  lv_obj_set_style_pad_ver(b, 0, 0);
  lv_obj_set_style_bg_color(b, lv_color_hex(theme::SURFACE), 0);
  lv_obj_set_style_bg_color(b, lv_color_hex(theme::SURFACE_2), LV_STATE_PRESSED);
  lv_obj_set_style_shadow_width(b, 0, 0);
  lv_obj_align(b, LV_ALIGN_RIGHT_MID, -right, 0);
  lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t* l = label(b, text, THEME_FONT_SMALL, theme::TEXT);
  lv_obj_center(l);
  if (label_out) *label_out = l;
  return b;
}

void UITask::showNearby() {
  _screen = SCR_NEARBY;
  _scanning = false;
  buildNearby();   // filter / sort persist
}

void UITask::buildNearby() {
  lv_obj_t* body = newScreen("Nodes", true);
  lv_obj_set_style_pad_row(body, 4, 0);
  if (_header) {
    headerButton(_header, LV_SYMBOL_REFRESH, onNearbyScan, 4, NULL);   // scan
    lv_obj_set_width(headerButton(_header, "", onNearbySort, 46, &_nearby_sort_lbl), 62);   // fits "Recent" / "Dist"
    headerButton(_header, UI_SYMBOL_MAP, onOpenNodesMap, 114, NULL);   // the Nodes map
    headerButton(_header, UI_SYMBOL_RADIO, onAdvertRow, 156, NULL);    // send advert, auto-advert
  }

  // Type filter chips
  _nearby_chips = lv_obj_create(body);
  styleSurface(_nearby_chips, theme::BG);
  lv_obj_remove_flag(_nearby_chips, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(_nearby_chips, LV_PCT(100), 26);
  lv_obj_set_flex_flow(_nearby_chips, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_column(_nearby_chips, 4, 0);
  for (uint8_t f = 0; f < NearbyModel::F_COUNT; f++) {
    lv_obj_t* c = lv_button_create(_nearby_chips);
    lv_obj_set_height(c, 26);
    lv_obj_set_flex_grow(c, 1);
    lv_obj_set_style_pad_all(c, 0, 0);
    lv_obj_set_style_radius(c, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_shadow_width(c, 0, 0);
    bool on = f == _nearby->filter();
    lv_obj_set_style_bg_color(c, lv_color_hex(on ? theme::ACCENT_DIM : theme::SURFACE), 0);   // selected (Theme.h)
    lv_obj_add_event_cb(c, onNearbyChip, LV_EVENT_CLICKED, (void*)(uintptr_t)f);
    lv_obj_center(label(c, NearbyModel::filterLabel(f), THEME_FONT_SMALL, theme::TEXT));
  }

  _nearby_status = label(body, "", THEME_FONT_SMALL, theme::TEXT_MUTED);

  _nearby_list = scrollList(body);

  _nearby_sig = 0;
  refreshNearbyList();
}

void UITask::setNearbyFilter(uint8_t f) {
  _nearby->setFilter(f);
  buildNearby();
}

void UITask::toggleNearbySort() {
  _nearby->setSortMode(_nearby->sortMode() == NearbyModel::SORT_DIST ? NearbyModel::SORT_TIME
                                                                     : NearbyModel::SORT_DIST);
  _nearby_sig = 0;
  refreshNearbyList();
}

void UITask::startNearbyScan() {
  _scanning = true;
  _scan_until_ms = millis() + 8000;
  _next_nearby_ms = millis() + 250;
  the_mesh.sendNodeDiscoverReq();
  if (!_scan_overlay) showScanPopup();
  else { _scan_sig = 0; refreshScanPopup(); }
}

// Dimmed full-screen overlay (swallows taps) holding a panel with the results.
// A child of the current screen, so it goes away with it.
void UITask::showScanPopup() {
  _scan_overlay = dimOverlay(screen());

  lv_obj_t* panel = lv_obj_create(_scan_overlay);
  anim::popup(_scan_overlay);
  lv_obj_set_size(panel, lv_display_get_horizontal_resolution(NULL) - 16,
                  lv_display_get_vertical_resolution(NULL) - theme::STATUS_H - 12);
  lv_obj_set_pos(panel, 8, theme::STATUS_H + 6);
  lv_obj_set_style_bg_color(panel, lv_color_hex(theme::BG), 0);
  lv_obj_set_style_border_color(panel, lv_color_hex(theme::ACCENT), 0);
  lv_obj_set_style_border_width(panel, 1, 0);
  lv_obj_set_style_radius(panel, theme::RADIUS, 0);
  lv_obj_set_style_pad_all(panel, theme::PAD, 0);
  lv_obj_set_style_pad_row(panel, 4, 0);
  lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
  lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* hdr = lv_obj_create(panel);
  styleSurface(hdr, theme::BG);
  lv_obj_remove_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(hdr, LV_PCT(100), 28);
  lv_obj_align(label(hdr, "In range now", THEME_FONT_TITLE, theme::TEXT), LV_ALIGN_LEFT_MID, 0, 0);
  headerButton(hdr, LV_SYMBOL_CLOSE, onScanClose, 0, NULL);
  headerButton(hdr, LV_SYMBOL_REFRESH " Again", onNearbyScan, 44, NULL);

  _scan_status = label(panel, "", THEME_FONT_SMALL, theme::TEXT_MUTED);
  _scan_list = scrollList(panel);

  _scan_sig = 0;
  refreshScanPopup();
}

void UITask::closeScanPopup() {
  if (_scan_overlay) lv_obj_delete_async(_scan_overlay);   // may be closing from its own button
  _scan_overlay = _scan_list = _scan_status = nullptr;
  _scanning = false;
}

void UITask::refreshScanPopup() {
  if (!_scan_list) return;
  _scan->refreshScan();
  int n = _scan->count();
  if (_scanning) lv_label_set_text_fmt(_scan_status, LV_SYMBOL_REFRESH "  Listening for replies... %d", n);
  else lv_label_set_text_fmt(_scan_status, "%d node%s answered the discover request", n, n == 1 ? "" : "s");

  uint32_t sig = (uint32_t)n + (_scanning ? 0x10000u : 0);
  for (int i = 0; i < n; i++) {
    const NearbyModel::Entry& e = _scan->at(i);
    sig = sig * 31 + e.rssi * 7 + e.snr_x4 + e.is_known;
    for (int k = 0; k < 4; k++) sig = sig * 31 + e.pub_key[k];
  }
  if (sig == _scan_sig) return;
  _scan_sig = sig;

  lv_obj_clean(_scan_list);
  for (int i = 0; i < n; i++) {
    const NearbyModel::Entry& e = _scan->at(i);
    char title[40], sub[48], right[12];
    if (e.name[0]) snprintf(title, sizeof(title), "%s", e.name);
    else snprintf(title, sizeof(title), "%s %02X%02X%02X%02X", NearbyModel::typeName(e.type),
                  e.pub_key[0], e.pub_key[1], e.pub_key[2], e.pub_key[3]);
    snprintf(sub, sizeof(sub), "%s  -  SNR %.1f / %.1f%s", NearbyModel::typeName(e.type),
             e.snr_x4 / 4.0f, e.remote_snr_x4 / 4.0f, e.is_known ? "" : "  -  new");
    snprintf(right, sizeof(right), "%d dBm", e.rssi);
    lv_obj_t* row = listRow(_scan_list, title, sub, onScanRow, (void*)(uintptr_t)i);
    lv_obj_align(label(row, right, THEME_FONT_SMALL, theme::TEXT_MUTED), LV_ALIGN_RIGHT_MID, -theme::PAD, 0);
  }
  if (n == 0) {
    lv_obj_t* l = label(_scan_list, _scanning ? "Repeaters and rooms in range will answer."
                                              : "Nobody answered. Try again later or move.",
                        THEME_FONT_BODY, theme::TEXT_MUTED);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(l, LV_PCT(100));
    lv_obj_set_style_pad_top(l, 8, 0);
  }
}

void UITask::openScanNode(int row) {
  if (row < 0 || row >= _scan->count()) return;
  s_node = _scan->at(row);
  _node_from_scan = true;
  _node_from_map = false;
  _scanning = false;
  _pinging = false;
  _delete_armed_ms = 0;
  _screen = SCR_NODE;
  buildNode();
}

// Changes whenever a rebuild would show something different.
uint32_t UITask::nearbySignature() const {
  uint32_t sig = (uint32_t)_nearby->count() * 2654435761u + _nearby->sortMode() + (_scanning ? 7 : 0);
  for (int i = 0; i < _nearby->count(); i++) {
    const NearbyModel::Entry& e = _nearby->at(i);
    sig = sig * 31 + e.contact_idx + (uint32_t)e.lastmod + (uint32_t)(e.dist_km * 100) + e.rssi + e.fav;
    for (const char* p = e.name; *p; p++) sig = sig * 31 + (uint8_t)*p;
  }
  // Ages are shown in minutes, so let the list re-render once a minute anyway.
  return sig + rtc_clock.getCurrentTime() / 60;
}

void UITask::refreshNearbyList() {
  if (!_nearby_list) return;
  _nearby->refreshModel();
  uint32_t sig = nearbySignature();
  int n = _nearby->count();

  if (_nearby_sort_lbl)
    lv_label_set_text(_nearby_sort_lbl, _nearby->sortMode() == NearbyModel::SORT_TIME ? "Recent" : "Dist");
  int32_t lat, lon;
  bool gps = _nearby->ownPosition(lat, lon);
  lv_label_set_text_fmt(_nearby_status, "%d node%s%s", n, n == 1 ? "" : "s",
                        gps ? "" : "  -  no GPS fix, distances unknown");
  if (sig == _nearby_sig) return;
  _nearby_sig = sig;

  int32_t scroll = lv_obj_get_scroll_y(_nearby_list);
  lv_obj_clean(_nearby_list);
  uint32_t now = rtc_clock.getCurrentTime();
  bool imperial = _prefs && _prefs->units_imperial;
  for (int i = 0; i < n; i++) {
    const NearbyModel::Entry& e = _nearby->at(i);
    char title[48], sub[48], right[16] = "";
    const char* name = e.name[0] ? e.name : "(unknown)";
    snprintf(title, sizeof(title), "%s%s", e.fav ? UI_SYMBOL_STAR " " : "", name);
    char age[8];
    geo::fmtAgeShort(age, sizeof(age), now, e.lastmod);
    if (e.dist_km >= 0.0f) geo::fmtDist(right, sizeof(right), e.dist_km, imperial);
    else if (age[0]) snprintf(right, sizeof(right), "%s", age);
    const char* kind = e.contact_idx >= 0 ? NearbyModel::typeName(e.type) : "not a contact";
    snprintf(sub, sizeof(sub), "%s%s%s%s", kind, e.is_live ? "  -  live" : "",
             (e.dist_km >= 0.0f && age[0]) ? "  -  " : "", (e.dist_km >= 0.0f && age[0]) ? age : "");
    lv_obj_t* row = listRow(_nearby_list, title, sub, onNearbyRow, (void*)(uintptr_t)i);
    if (e.fav) lv_obj_set_style_text_color(lv_obj_get_child(row, 0), lv_color_hex(theme::ACCENT), 0);
    if (right[0]) {
      lv_obj_t* r = label(row, right, THEME_FONT_SMALL, e.is_live ? theme::OK : theme::TEXT_MUTED);
      lv_obj_align(r, LV_ALIGN_RIGHT_MID, -theme::PAD, 0);
    }
  }
  if (n == 0) {
    lv_obj_t* l = label(_nearby_list, "Nobody here yet. Tap Scan to look around.",
                        THEME_FONT_BODY, theme::TEXT_MUTED);
    lv_obj_set_style_pad_top(l, 12, 0);
  }
  lv_obj_update_layout(_nearby_list);
  lv_obj_scroll_to_y(_nearby_list, scroll, LV_ANIM_OFF);
}

// ── Node detail ───────────────────────────────────────────────────────────────

void UITask::openNode(int row) {
  if (row < 0 || row >= _nearby->count()) return;
  s_node = _nearby->at(row);
  _node_from_scan = false;
  _node_from_map = false;
  _pinging = false;
  _delete_armed_ms = 0;
  _screen = SCR_NODE;
  buildNode();
}

static lv_obj_t* actionButton(lv_obj_t* parent, const char* text, uint8_t action, bool accent) {
  lv_obj_t* b = lv_button_create(parent);
  lv_obj_set_height(b, 40);
  lv_obj_set_flex_grow(b, 1);
  lv_obj_set_style_pad_hor(b, 4, 0);
  lv_obj_set_style_radius(b, theme::RADIUS, 0);
  lv_obj_set_style_shadow_width(b, 0, 0);
  lv_obj_set_style_bg_color(b, lv_color_hex(accent ? theme::ACCENT_DIM : theme::SURFACE), 0);
  lv_obj_set_style_bg_color(b, lv_color_hex(theme::SURFACE_2), LV_STATE_PRESSED);
  lv_obj_add_event_cb(b, onNodeAction, LV_EVENT_CLICKED, (void*)(uintptr_t)action);
  lv_obj_t* l = label(b, text, THEME_FONT_SMALL, theme::TEXT);
  lv_obj_center(l);
  return l;
}

void UITask::buildNode() {
  const NearbyModel::Entry& e = s_node;
  lv_obj_t* body = newScreen(e.name[0] ? e.name : "(unknown)", true);

  _node_info = label(body, "", THEME_FONT_BODY, theme::TEXT);
  lv_label_set_long_mode(_node_info, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(_node_info, LV_PCT(100));
  lv_obj_set_style_text_line_space(_node_info, 3, 0);
  _node_ping = label(body, "", THEME_FONT_BODY, theme::ACCENT);

  lv_obj_t* spacer = lv_obj_create(body);   // pushes the actions to the bottom
  lv_obj_remove_style_all(spacer);
  lv_obj_set_width(spacer, 1);
  lv_obj_set_flex_grow(spacer, 1);

  lv_obj_t* acts = lv_obj_create(body);
  styleSurface(acts, theme::BG);
  lv_obj_remove_flag(acts, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(acts, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(acts, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_column(acts, theme::GAP, 0);
  bool contact = e.contact_idx >= 0;
  bool admin = contact && (e.type == ADV_TYPE_REPEATER || e.type == ADV_TYPE_ROOM);
  bool pos = e.lat_e6 != 0 || e.lon_e6 != 0;
  struct Act { const char* icon; const char* text; uint8_t action; bool accent; } list[9];
  int n = 0;
  if (contact && e.type == ADV_TYPE_CHAT) list[n++] = { LV_SYMBOL_ENVELOPE, " Message", NODE_MSG, true };
  if (e.has_key) list[n++] = { LV_SYMBOL_LOOP, " Ping", NODE_PING, false };
  if (pos) list[n++] = { UI_SYMBOL_COMPASS, "", NODE_NAV, false };
  if (pos) list[n++] = { UI_SYMBOL_FLAG, "", NODE_WAYPOINT, false };   // save where it was seen
  if (contact) list[n++] = { UI_SYMBOL_STAR, admin ? "" : e.fav ? " Unfav" : " Fav", NODE_FAV, e.fav && admin };
  if (admin) list[n++] = { LV_SYMBOL_SETTINGS, " Admin", NODE_ADMIN, false };
  if (!contact && e.has_key && !e.is_known) list[n++] = { LV_SYMBOL_PLUS, " Add", NODE_ADD, true };
  if (contact && e.has_key) list[n++] = { UI_SYMBOL_PIN, "", NODE_PIN, favslots::findContact(_prefs, e.pub_key) >= 0 };
  if (contact) list[n++] = { LV_SYMBOL_TRASH, "", NODE_DELETE, false };
  for (int i = 0; i < n; i++) {   // five or more: icons only, so every button fits one row
    char t[24];
    snprintf(t, sizeof(t), "%s%s", list[i].icon, n >= 5 ? "" : list[i].text);
    lv_obj_t* l = actionButton(acts, t, list[i].action, list[i].accent);
    if (list[i].action == NODE_DELETE) _node_delete_lbl = l;
  }

  refreshNode();
}

void UITask::refreshNode() {
  if (!_node_info) return;
  const NearbyModel::Entry& e = s_node;
  // Refresh position / age / signal from its model while the node is still listed.
  NearbyModel* model = _node_from_scan ? _scan : _nearby;
  model->refreshModel();
  for (int i = 0; i < model->count(); i++) {
    const NearbyModel::Entry& m = model->at(i);
    bool same = e.has_key ? (m.has_key && memcmp(m.pub_key, e.pub_key, PUB_KEY_SIZE) == 0)
              : (e.contact_idx >= 0) ? m.contact_idx == e.contact_idx
              : strncmp(m.name, e.name, sizeof(m.name)) == 0;
    if (same) { s_node = m; break; }
  }

  char buf[320];
  int o = 0;
  // A scan row carries no contact index; is_known says whether it's in the contacts.
  bool known = e.contact_idx >= 0 || (_node_from_scan && e.is_known);
  o += snprintf(buf + o, sizeof(buf) - o, "%s%s%s%s", NearbyModel::typeName(e.type),
                known ? "" : "  -  not a contact",
                e.is_live ? (e.live_verified ? "  -  live position" : "  -  live position (channel)") : "",
                e.fav ? "  -  favourite" : "");
  int32_t lat, lon;
  bool gps = _nearby->ownPosition(lat, lon);
  if (e.lat_e6 != 0 || e.lon_e6 != 0) {
    if (gps && e.dist_km >= 0.0f) {
      char d[16];
      geo::fmtDist(d, sizeof(d), e.dist_km, _prefs && _prefs->units_imperial);
      int az = geo::bearingDeg(lat, lon, e.lat_e6, e.lon_e6);
      o += snprintf(buf + o, sizeof(buf) - o, "\n%s  %d\xC2\xB0 %s", d, az, geo::bearingCardinal(az));
    }
    o += snprintf(buf + o, sizeof(buf) - o, "\n%.5f, %.5f", e.lat_e6 / 1e6, e.lon_e6 / 1e6);
  } else if (!_node_from_scan) {
    o += snprintf(buf + o, sizeof(buf) - o, "\nNo position shared");
  }
  char age[8];
  geo::fmtAgeShort(age, sizeof(age), rtc_clock.getCurrentTime(), e.lastmod);
  if (age[0]) o += snprintf(buf + o, sizeof(buf) - o, "\nHeard %s ago", age);
  if (_node_from_scan)
    o += snprintf(buf + o, sizeof(buf) - o, "\nRSSI %d dBm  -  SNR %.1f / %.1f dB", e.rssi,
                  e.snr_x4 / 4.0f, e.remote_snr_x4 / 4.0f);
  if (e.has_prefix)
    o += snprintf(buf + o, sizeof(buf) - o, "\nID %02X%02X%02X%02X", e.pub_key[0], e.pub_key[1], e.pub_key[2], e.pub_key[3]);
  lv_label_set_text(_node_info, buf);

  // Ping result (PingEngine releases its slot on reply; the view owns the timeout)
  if (_pinging) {
    int16_t out = 0, back = 0; uint32_t rtt = 0;
    _core->ping.getResult(out, back, rtt);
    if (!_core->ping.isActive() && (out || back || rtt)) {
      lv_label_set_text_fmt(_node_ping, "Ping %lu ms  -  SNR out %.1f  back %.1f", (unsigned long)rtt,
                            out / 4.0f, back / 4.0f);
      _pinging = false;
    } else if (millis() - _ping_started_ms > 3000) {
      _core->ping.clear();
      lv_label_set_text(_node_ping, "Ping: no reply");
      _pinging = false;
    }
  }
  if (_node_delete_lbl && _delete_armed_ms && millis() - _delete_armed_ms > 3000) {
    _delete_armed_ms = 0;
    lv_label_set_text(_node_delete_lbl, LV_SYMBOL_TRASH);
  }
}

void UITask::nodeAction(uint8_t action) {
  NearbyModel::Entry& e = s_node;
  switch (action) {
    case NODE_MSG:
      openDM(e.pub_key);
      break;
    case NODE_PING: {
      if (_pinging) break;
      PingEngine::StartResult r = _core->ping.start(e.pub_key);
      if (r == PingEngine::STARTED) {
        _pinging = true;
        _ping_started_ms = millis();
        _next_nearby_ms = millis() + 250;
        lv_label_set_text(_node_ping, "Pinging...");
      } else {
        lv_label_set_text(_node_ping, r == PingEngine::UNSUPPORTED ? "Ping needs 1-2 byte path hashes"
                                                                   : "Ping failed");
      }
      break;
    }
    case NODE_FAV:
      if (contactctl::setFavourite(e.pub_key, !e.fav)) {
        e.fav = !e.fav;
        buildNode();
      }
      break;
    case NODE_ADD:
      if (the_mesh.addDiscoveredContact(e.pub_key, e.name, e.type)) {
        showToast("Contact added");
        _screen = SCR_NEARBY;
        buildNearby();
      } else {
        showToast("Contacts full");
      }
      break;
    case NODE_NAV:
      // Followed by key when there is one (the Locator resolves live share,
      // then advert position); otherwise a fixed point where it was seen.
      if (e.has_prefix) navToNode(e.pub_key, e.lat_e6, e.lon_e6, e.name[0] ? e.name : "Node");
      else {
        _core->locator.setTarget(0, nullptr, e.lat_e6, e.lon_e6, e.name[0] ? e.name : "Node");
        the_mesh.savePrefs();
        openMap(true);
        navFrameTarget();
      }
      break;
    case NODE_ADMIN:
      openAdmin(e.pub_key);
      break;
    case NODE_WAYPOINT: {
      if (_core->waypoints.full()) { showToast(waypointsFull()); break; }
      char t[48];
      if (_core->waypoints.add(e.lat_e6, e.lon_e6, rtc_clock.getCurrentTime(), e.name[0] ? e.name : "Node")) {
        snprintf(t, sizeof(t), "Saved %s", _core->waypoints.at(_core->waypoints.count() - 1).label);
        showToast(t);
      }
      break;
    }
    case NODE_PIN:   // to the favourites dial (Home), as from a chat's options
      pinPopup(false, 0, e.pub_key);
      break;
    case NODE_DELETE:
      if (!_delete_armed_ms) {   // destructive: second tap within 3 s confirms
        _delete_armed_ms = millis();
        if (_delete_armed_ms == 0) _delete_armed_ms = 1;
        lv_label_set_text(_node_delete_lbl, LV_SYMBOL_TRASH "?");   // fits an icon-only button
        showToast("Tap again to delete the contact", 2500);
        break;
      }
      if (the_mesh.deleteContactByKey(e.pub_key)) {
        showToast("Contact deleted");
        _screen = SCR_NEARBY;
        buildNearby();
      }
      break;
  }
}

static lv_obj_t* switchRow(lv_obj_t* parent, const char* text, const char* sub, uint8_t* pref);   // below
#include "MapScreen.h"
#include "NavMap.h"
#include "ClockScreen.h"
#include "RadioScreen.h"
#include "WifiScreen.h"
#include "ChannelScreen.h"
#include "AdminScreen.h"
#include "BotScreen.h"

// ── Conversation ──────────────────────────────────────────────────────────────

static void onKeyboard(lv_event_t* e) {
  lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_READY) s_ui->sendFromCompose();
  else if (code == LV_EVENT_CANCEL) s_ui->setKeyboardVisible(false);
}

// Compose limit is in UTF-8 bytes (the over-the-air limit), not characters:
// Cyrillic / Greek / accented letters take two bytes each. Leaves room for the
// "Name: " prefix a channel send adds.
static const size_t COMPOSE_MAX_BYTES = MAX_TEXT_LEN - 40;

static void onComposeInsert(lv_event_t* e) {
  lv_obj_t* ta = (lv_obj_t*)lv_event_get_target(e);
  const char* ins = (const char*)lv_event_get_param(e);
  if (ins && strlen(lv_textarea_get_text(ta)) + strlen(ins) > COMPOSE_MAX_BYTES)
    lv_textarea_set_insert_replace(ta, "");   // reject: would exceed the byte limit
}

static void onComposeClicked(lv_event_t* e) { (void)e; s_ui->setKeyboardVisible(true); }
static void onComposeMore(lv_event_t* e) { (void)e; s_ui->quickPopup(); }

// Keyboard up: the screen header goes away and the body takes its 32 px, so
// a line or two of the conversation stays visible above the compose field
// (240 px can't fit header + list + field + keys). The field is FOCUSED
// while the keyboard is up -- that is what makes LVGL draw its cursor.
void UITask::setKeyboardVisible(bool show) {
  if (!_keyboard || !_compose_ta) return;
  if (show == !lv_obj_has_flag(_keyboard, LV_OBJ_FLAG_HIDDEN)) return;
  if (show) {
    lv_obj_remove_flag(_keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_state(_compose_ta, LV_STATE_FOCUSED);
  } else {
    lv_obj_add_flag(_keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_state(_compose_ta, LV_STATE_FOCUSED);
  }
  if (_header && _body) {
    int top = theme::STATUS_H + (show ? 0 : lv_obj_get_height(_header));
    if (show) lv_obj_add_flag(_header, LV_OBJ_FLAG_HIDDEN);
    else      lv_obj_remove_flag(_header, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_pos(_body, 0, top);
    lv_obj_set_height(_body, lv_display_get_vertical_resolution(NULL) - top);
  }
  if (_thread_list) {   // keep the newest message in view
    lv_obj_update_layout(_thread_list);
    lv_obj_scroll_to_y(_thread_list, LV_COORD_MAX, LV_ANIM_OFF);
  }
}

void UITask::openChannel(uint8_t channel_idx) {
  _thread_is_channel = true;
  _thread_channel = channel_idx;
  _thread_skip = 0;
  _core->history.setChUnread(channel_idx, 0);
  _screen = SCR_THREAD;
  buildThread();
}

void UITask::openDM(const uint8_t* pub_key) {
  _thread_is_channel = false;
  _thread_skip = 0;
  memset(_thread_key, 0, sizeof(_thread_key));
  ContactInfo c;
  if (MessageHistory::contactByPrefix(pub_key, c)) memcpy(_thread_key, c.id.pub_key, PUB_KEY_SIZE);
  else memcpy(_thread_key, pub_key, 4);
  _core->clearDMUnread(_thread_key);
  _screen = SCR_THREAD;
  buildThread();
}

void UITask::buildThread() {
  char title[40];
  bool can_send = true;
  if (_thread_is_channel) {
    ChannelDetails ch;
    if (the_mesh.getChannel(_thread_channel, ch)) snprintf(title, sizeof(title), "%s", ch.name);   // as named: "#" marks a hashtag channel
    else snprintf(title, sizeof(title), "Channel %d", _thread_channel);
  } else {
    contactName(_thread_key, title, sizeof(title));
    ContactInfo c;
    can_send = MessageHistory::contactByPrefix(_thread_key, c) && (c.type == ADV_TYPE_CHAT || c.type == ADV_TYPE_ROOM);
  }
  lv_obj_t* body = newScreen(title, true);
  lv_obj_set_style_pad_all(body, 0, 0);
  lv_obj_set_style_pad_row(body, 0, 0);
  if (_header && (_thread_is_channel || can_send)) {   // channel / conversation options
    lv_obj_set_width(lv_obj_get_child(_header, 1), 200);   // title, clear of the button
    headerButton(_header, LV_SYMBOL_SETTINGS, _thread_is_channel ? onChanThreadMenu : onConvThreadMenu, 4, NULL);
  }

  _thread_list = scrollList(body);
  lv_obj_set_style_pad_all(_thread_list, theme::PAD, 0);

  _compose_ta = nullptr;
  _keyboard = nullptr;
  if (can_send) {
    lv_obj_t* bar = lv_obj_create(body);
    styleSurface(bar, theme::BG);
    lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(bar, LV_PCT(100), 42);
    lv_obj_set_style_pad_all(bar, 4, 0);
    lv_obj_t* more = lv_button_create(bar);   // quick messages, placeholders
    lv_obj_set_size(more, 34, 34);
    lv_obj_align(more, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_pad_all(more, 0, 0);
    lv_obj_set_style_radius(more, theme::RADIUS, 0);
    lv_obj_set_style_shadow_width(more, 0, 0);
    lv_obj_set_style_bg_color(more, lv_color_hex(theme::SURFACE), 0);
    lv_obj_add_event_cb(more, onComposeMore, LV_EVENT_CLICKED, NULL);
    lv_obj_center(label(more, LV_SYMBOL_PLUS, THEME_FONT_BODY, theme::TEXT));
    _compose_ta = textField(bar, "Message");   // FOCUSED while the keyboard is up
    lv_obj_add_event_cb(_compose_ta, onComposeInsert, LV_EVENT_INSERT, NULL);
    lv_obj_set_width(_compose_ta, lv_display_get_horizontal_resolution(NULL) - 8 - 34 - 4);   // beside "+"
    lv_obj_align(_compose_ta, LV_ALIGN_RIGHT_MID, 0, 0);

    // In the body's flex column below the compose bar: showing it shrinks the
    // message list, so the text field stays visible just above the keys.
    _keyboard = kb::create(body, _prefs);   // phone-style, scripts from prefs, hold for accents (Keyboard.h)
    lv_obj_set_size(_keyboard, LV_PCT(100), 124);
    lv_keyboard_set_textarea(_keyboard, _compose_ta);
    lv_obj_add_event_cb(_keyboard, onKeyboard, LV_EVENT_READY, _keyboard);
    lv_obj_add_event_cb(_keyboard, onKeyboard, LV_EVENT_CANCEL, _keyboard);
    lv_obj_add_flag(_keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(_compose_ta, onComposeClicked, LV_EVENT_CLICKED, _keyboard);
  }
  refreshThread();
  if (_compose_ta && _share_text[0]) {   // shareToMessage(): the text waits in the field
    lv_textarea_set_text(_compose_ta, _share_text);
    _share_text[0] = '\0';
    lv_obj_update_layout(screen());
    setKeyboardVisible(true);
  }
}

// Changes whenever the open conversation's content or delivery markers change.
uint32_t UITask::threadSignature() const {
  const MessageHistory& h = _core->history;
  uint32_t sig = 0;
  if (_thread_is_channel) {
    int n = h.histCountForChannel(_thread_channel);
    sig = n;
    for (int j = 0; j < n && j < 8; j++) {
      const ChHistEntry& e = h.chAtPos(h.histEntryForChannel(_thread_channel, j));
      sig = sig * 31 + e.timestamp + e.relay_status + e.path_len;   // path_len: repeaters heard
    }
  } else {
    int n = h.dmHistCountForContact(_thread_key);
    sig = n;
    for (int j = 0; j < n && j < 8; j++) {
      const DmHistEntry& e = h.dmAtPos(h.dmHistEntryForContact(_thread_key, j));
      sig = sig * 31 + e.timestamp + h.dmEffectiveStatus(e);
    }
  }
  return sig;
}

// Positions found in the shown messages ([WAY] / [LOC] / plain "lat,lon"),
// for the Go / Save buttons under such a bubble.
struct MsgLoc { int32_t lat, lon; char label[WAYPOINT_LABEL_LEN * 2]; };
static const int THREAD_MAX_SHOWN = 50;   // newest bubbles built per conversation
static MsgLoc* s_msg_locs = psramBuf<MsgLoc>(THREAD_MAX_SHOWN);
static int    s_msg_loc_n = 0;

// The messages shown, oldest first: copies from the SD card's history (which
// holds far more than the RAM ring) or from the ring, one page at a time.
static ChHistEntry* s_th_ch = psramBuf<ChHistEntry>(THREAD_MAX_SHOWN);
static DmHistEntry* s_th_dm = psramBuf<DmHistEntry>(THREAD_MAX_SHOWN);

// What a held bubble is about: its entry (index into s_th_ch / s_th_dm),
// sender, position slot.
struct MsgMeta { int pos; bool channel; bool own; int loc; char from[32]; };
static MsgMeta* s_msg_meta = psramBuf<MsgMeta>(THREAD_MAX_SHOWN);
static int     s_msg_meta_n = 0;

static int noteMsgMeta(int pos, bool channel, bool own, int loc, const char* from) {
  if (s_msg_meta_n >= THREAD_MAX_SHOWN) return -1;
  MsgMeta& m = s_msg_meta[s_msg_meta_n];
  m.pos = pos; m.channel = channel; m.own = own; m.loc = loc;
  snprintf(m.from, sizeof(m.from), "%s", from ? from : "");
  return s_msg_meta_n++;
}

static void onMsgLoc(lv_event_t* e) {
  uintptr_t v = (uintptr_t)lv_event_get_user_data(e);
  s_ui->messageLocationAction((int)(v >> 1), (v & 1) != 0);
}

static void msgLocButton(lv_obj_t* parent, const char* text, int idx, bool save, bool accent) {
  lv_obj_t* b = lv_button_create(parent);
  lv_obj_set_size(b, LV_SIZE_CONTENT, 28);
  lv_obj_set_style_pad_hor(b, 10, 0);
  lv_obj_set_style_pad_ver(b, 0, 0);
  lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_shadow_width(b, 0, 0);
  lv_obj_set_style_bg_color(b, lv_color_hex(accent ? theme::ACCENT : theme::SURFACE_2), 0);
  lv_obj_add_event_cb(b, onMsgLoc, LV_EVENT_CLICKED, (void*)(uintptr_t)((idx << 1) | (save ? 1 : 0)));
  lv_obj_center(label(b, text, THEME_FONT_SMALL, accent ? theme::BG : theme::TEXT));
}

// A message's text, wrapped at `max_w` and shrunk to fit. "@[nick]" mentions
// (how a reply names who it answers) show as "@nick" in the accent -- in the
// text colour on our own amber bubbles -- and underlined when the nick is
// ours (*mentions_me set). Plain text stays a label.
static lv_obj_t* msgText(lv_obj_t* parent, const char* text, bool own, int max_w, bool* mentions_me) {
  *mentions_me = false;
  if (!strstr(text, "@[")) {
    lv_obj_t* t = label(parent, text, THEME_FONT_BODY, theme::TEXT);
    lv_label_set_long_mode(t, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_max_width(t, max_w, 0);
    lv_obj_set_width(t, LV_SIZE_CONTENT);
    return t;
  }
  lv_obj_t* sg = lv_spangroup_create(parent);
  lv_obj_remove_flag(sg, LV_OBJ_FLAG_CLICKABLE);   // a hold still reaches the bubble (its menu)
  lv_obj_set_style_text_font(sg, THEME_FONT_BODY, 0);
  lv_obj_set_style_text_color(sg, lv_color_hex(theme::TEXT), 0);
  lv_spangroup_set_mode(sg, LV_SPAN_MODE_BREAK);
  const char* me = the_mesh.getNodeName();
  char part[MAX_TEXT_LEN + 1];
  const char* p = text;
  while (*p) {
    const char* at = strstr(p, "@[");
    const char* close = at ? strchr(at + 2, ']') : nullptr;
    if (at && (!close || close - at > 34)) close = nullptr;   // not a mention: a nick is <= 31 chars
    const char* end = at && close ? at : p + strlen(p);
    if (at && !close) end = at + 2;   // "@[" alone: plain, carry on after it
    if (end > p) {
      size_t n = end - p < (ptrdiff_t)sizeof(part) ? end - p : sizeof(part) - 1;
      memcpy(part, p, n);
      part[n] = '\0';
      lv_span_set_text(lv_spangroup_new_span(sg), part);
    }
    if (!at || !close) { p = end; continue; }
    size_t n = close - (at + 2);
    part[0] = '@';
    memcpy(part + 1, at + 2, n);
    part[n + 1] = '\0';
    bool mine = strlen(me) == n && strncasecmp(me, at + 2, n) == 0;
    lv_span_t* sp = lv_spangroup_new_span(sg);
    lv_span_set_text(sp, part);
    lv_style_t* st = lv_span_get_style(sp);
    lv_style_set_text_color(st, lv_color_hex(own ? theme::TEXT : theme::ACCENT));
    if (mine) { lv_style_set_text_decor(st, LV_TEXT_DECOR_UNDERLINE); *mentions_me = true; }
    p = close + 1;
  }
  lv_spangroup_refr_mode(sg);
  uint32_t w = lv_spangroup_get_expand_width(sg, 0);
  lv_obj_set_width(sg, w > (uint32_t)max_w ? max_w : (int32_t)w + 1);
  lv_obj_set_height(sg, LV_SIZE_CONTENT);
  return sg;
}

// One message bubble. Own messages right-aligned in amber, others left.
// loc_idx >= 0: the text carries a position (s_msg_locs[loc_idx]).
static void bubble(lv_obj_t* list, const char* from, const char* text, bool own,
                   uint32_t ts, const char* status, uint32_t status_col, int loc_idx = -1, int meta_idx = -1,
                   int relays = 0) {
  // Full-width row that pushes the bubble to its side.
  lv_obj_t* row = lv_obj_create(list);
  styleSurface(row, theme::BG);
  lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
  lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(row, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, own ? LV_FLEX_ALIGN_END : LV_FLEX_ALIGN_START,
                        LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

  lv_obj_t* b = lv_obj_create(row);
  lv_obj_remove_flag(b, LV_OBJ_FLAG_SCROLLABLE);
  if (meta_idx >= 0) {   // hold: reply / path / target (ConversationScreen.h)
    lv_obj_add_event_cb(b, onMsgHold, LV_EVENT_LONG_PRESSED, (void*)(uintptr_t)meta_idx);
    lv_obj_set_style_bg_color(b, lv_color_hex(theme::SURFACE_2), LV_STATE_PRESSED);
  } else {
    lv_obj_remove_flag(b, LV_OBJ_FLAG_CLICKABLE);
  }
  lv_obj_set_size(b, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_style_max_width(b, 250, 0);
  lv_obj_set_style_bg_color(b, lv_color_hex(own ? theme::ACCENT_DIM : theme::SURFACE), 0);
  lv_obj_set_style_border_width(b, 0, 0);
  lv_obj_set_style_radius(b, theme::RADIUS, 0);
  lv_obj_set_style_pad_all(b, 6, 0);
  lv_obj_set_style_pad_row(b, 2, 0);
  lv_obj_set_flex_flow(b, LV_FLEX_FLOW_COLUMN);

  char meta[32];
  uint32_t now = rtc_clock.getCurrentTime();
  fmtMsgAge(meta, sizeof(meta), now, ts, s_prefs);   // "12s" / "5m" / "3h" / "2d"

  // Channel messages: "Sender  5m" on one line above the text, which keeps
  // the bubble two lines tall -- matters with the keyboard up.
  bool meta_in_header = from && from[0] && !status;
  if (from && from[0]) {
    lv_obj_t* hdr = flexBox(b, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(hdr, 8, 0);
    label(hdr, from, THEME_FONT_SMALL, theme::ACCENT);   // names are <= 31 chars: fits the bubble
    if (meta_in_header) label(hdr, meta, THEME_FONT_SMALL, theme::TEXT_MUTED);
  }
  bool mentions_me;
  msgText(b, text, own, 238, &mentions_me);
  if (mentions_me && !own) {   // someone answering us: the bubble outlined
    lv_obj_set_style_border_width(b, 1, 0);
    lv_obj_set_style_border_color(b, lv_color_hex(theme::ACCENT), 0);
  }

  if (loc_idx >= 0) {   // position: navigate there / keep it as a waypoint
    lv_obj_t* acts = flexBox(b, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(acts, 6, 0);
    lv_obj_set_style_pad_top(acts, 2, 0);
    msgLocButton(acts, UI_SYMBOL_COMPASS " Go", loc_idx, false, true);
    msgLocButton(acts, UI_SYMBOL_FLAG " Save", loc_idx, true, false);
  }

  if (!meta_in_header) {   // the age, then the delivery mark in its colour (as L1)
    lv_obj_t* line = flexBox(b, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(line, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(line, 6, 0);
    label(line, meta, THEME_FONT_SMALL, theme::TEXT_MUTED);
    if (relays > 0) {   // as L1: the count alone says it got out, no check beside it
      lv_obj_t* c = lv_obj_create(line);
      lv_obj_remove_style_all(c);
      lv_obj_remove_flag(c, LV_OBJ_FLAG_CLICKABLE);
      lv_obj_set_size(c, LV_SIZE_CONTENT, 15);
      lv_obj_set_style_min_width(c, 15, 0);
      lv_obj_set_style_pad_hor(c, 4, 0);
      lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
      lv_obj_set_style_bg_color(c, lv_color_hex(theme::OK), 0);
      lv_obj_set_style_radius(c, LV_RADIUS_CIRCLE, 0);
      lv_obj_t* n = label(c, "", THEME_FONT_SMALL, theme::BG);
      lv_label_set_text_fmt(n, "%d", relays);
      lv_obj_center(n);
    } else if (status && status[0]) {
      label(line, status, THEME_FONT_SMALL, status_col);
    }
  }
}

// Remember the position in `text` (if any) for its bubble's buttons; label
// from a [WAY] tag, else the sender. Returns the slot or -1.
static int noteMsgLocation(const char* text, const char* sender) {
  if (s_msg_loc_n >= THREAD_MAX_SHOWN) return -1;
  MsgLoc& m = s_msg_locs[s_msg_loc_n];
  if (!geo::parseLatLon(text, m.lat, m.lon, m.label, sizeof(m.label))) return -1;
  if (!m.label[0]) snprintf(m.label, sizeof(m.label), "%s", sender && sender[0] ? sender : "Msg loc");
  return s_msg_loc_n++;
}

void UITask::messageLocationAction(int idx, bool save) {
  if (idx < 0 || idx >= s_msg_loc_n) return;
  const MsgLoc& m = s_msg_locs[idx];
  if (save) {
    if (_core->waypoints.full()) { showToast(waypointsFull()); return; }
    if (_core->waypoints.add(m.lat, m.lon, rtc_clock.getCurrentTime(), m.label)) {
      char t[48];
      snprintf(t, sizeof(t), "Saved %s", _core->waypoints.at(_core->waypoints.count() - 1).label);
      showToast(t);
    }
    return;
  }
  // A place snapshotted from the text (kind 0): nothing to keep re-resolving.
  navSetTarget(0, nullptr, m.lat, m.lon, m.label);
  openMap(true);
  navFrameTarget();
  refreshNavBar();
}

// The page of the open conversation into s_th_ch / s_th_dm (oldest first):
// THREAD_MAX_SHOWN messages ending _thread_skip before the newest. From the
// SD card's copy when it has at least what the ring has, else from the ring.
// `total` = messages in that source.
int UITask::loadThreadPage(int& total) {
  const MessageHistory& h = _core->history;
  char path[64];
  if (_thread_is_channel) {
    int ring = h.histCountForChannel(_thread_channel);
    int arc = s_archive.ready() && s_archive.chPath(_thread_channel, path, sizeof(path))
            ? s_archive.total<ChHistEntry>(path) : 0;
    if (arc > 0 && arc >= ring) {
      total = arc;
      return s_archive.window(path, _thread_skip, THREAD_MAX_SHOWN, s_th_ch);
    }
    total = ring;
    int m = ring - _thread_skip;
    if (m > THREAD_MAX_SHOWN) m = THREAD_MAX_SHOWN;
    for (int i = 0; i < m; i++) s_th_ch[i] = h.chAtPos(h.histEntryForChannel(_thread_channel, _thread_skip + m - 1 - i));
    return m > 0 ? m : 0;
  }
  int ring = h.dmHistCountForContact(_thread_key);
  int arc = 0;
  if (s_archive.ready()) {
    histstore::SdArchive::dmPath(_thread_key, path, sizeof(path));
    arc = s_archive.total<DmHistEntry>(path);
  }
  if (arc > 0 && arc >= ring) {
    total = arc;
    return s_archive.window(path, _thread_skip, THREAD_MAX_SHOWN, s_th_dm);
  }
  total = ring;
  int m = ring - _thread_skip;
  if (m > THREAD_MAX_SHOWN) m = THREAD_MAX_SHOWN;
  for (int i = 0; i < m; i++) s_th_dm[i] = h.dmAtPos(h.dmHistEntryForContact(_thread_key, _thread_skip + m - 1 - i));
  return m > 0 ? m : 0;
}

static void onThreadPage(lv_event_t* e) { s_ui->threadPage((int)(intptr_t)lv_event_get_user_data(e)); }

// "Older messages" / "Newer messages" at the ends of a page.
static void pageButton(lv_obj_t* list, const char* text, int dir) {
  lv_obj_t* b = lv_button_create(list);
  lv_obj_set_size(b, LV_PCT(100), 32);
  lv_obj_set_style_bg_color(b, lv_color_hex(theme::SURFACE), 0);
  lv_obj_add_event_cb(b, onThreadPage, LV_EVENT_CLICKED, (void*)(intptr_t)dir);
  lv_obj_center(label(b, text, THEME_FONT_SMALL, theme::TEXT_MUTED));
}

void UITask::threadPage(int dir) {
  _thread_skip += dir > 0 ? THREAD_MAX_SHOWN : -THREAD_MAX_SHOWN;
  if (_thread_skip < 0) _thread_skip = 0;
  _thread_scroll_top = dir < 0;   // newer: read on from the top of that page
  refreshThread();
}

void UITask::refreshThread() {
  if (!_thread_list) return;
  _thread_dirty = false;
  _thread_sig = threadSignature();
  lv_obj_clean(_thread_list);
  const MessageHistory& h = _core->history;
  s_msg_loc_n = 0;
  s_msg_meta_n = 0;
  int total = 0;
  int n = loadThreadPage(total);
  if (n == 0 && _thread_skip > 0) {   // the page went away (history trimmed)
    _thread_skip = 0;
    n = loadThreadPage(total);
  }
  if (total > _thread_skip + n) {
    char t[40];
    snprintf(t, sizeof(t), LV_SYMBOL_UP "  Older messages (%d)", total - _thread_skip - n);
    pageButton(_thread_list, t, 1);
  }

  if (_thread_is_channel) {
    for (int i = 0; i < n; i++) {   // oldest first
      const ChHistEntry& e = s_th_ch[i];
      // Channel text is "Sender: body"; our own posts are filed as "Me: body".
      char from[40] = "";
      const char* body = e.text;
      const char* sep = strstr(e.text, ": ");
      if (sep && sep - e.text < (int)sizeof(from)) {
        memcpy(from, e.text, sep - e.text);
        from[sep - e.text] = '\0';
        body = sep + 2;
      }
      bool own = strcmp(from, "Me") == 0;
      // Own posts: once a repeater echoed it, how many distinct repeaters did
      // (markChannelRelayed), or a check if that's unknown; nothing before --
      // no echo is normal.
      const char* st = NULL; uint32_t col = theme::TEXT_MUTED;
      int nrel = 0;
      if (own && e.relay_status == ACK_OK) {
        nrel = e.path_len & 63;
        st = LV_SYMBOL_OK; col = theme::OK;
      } else if (own) st = "";
      int loc = own ? -1 : noteMsgLocation(body, from);
      bubble(_thread_list, own ? NULL : from, body, own, e.timestamp, st, col,
             loc, noteMsgMeta(i, true, own, loc, own ? "" : from), nrel);
    }
  } else {
    ContactInfo tc;
    bool room = MessageHistory::contactByPrefix(_thread_key, tc) && tc.type == ADV_TYPE_ROOM;
    for (int i = 0; i < n; i++) {
      const DmHistEntry& e = s_th_dm[i];
      const char* st = NULL; uint32_t col = theme::TEXT_MUTED;
      if (e.outgoing) {
        switch (h.dmEffectiveStatus(e)) {
          case ACK_OK:      st = LV_SYMBOL_OK; col = theme::OK; break;
          case ACK_FAIL:    st = LV_SYMBOL_CLOSE; col = theme::FAIL; break;
          case ACK_PENDING: st = "..."; break;
          default:          st = ""; break;
        }
      }
      // A room post is filed "Author: text": shown under its author.
      char who[33] = "";
      const char* text = e.text;
      if (!e.outgoing && room) text = contactctl::splitRoomPost(e.text, who, sizeof(who));
      else if (!e.outgoing) contactName(_thread_key, who, sizeof(who));
      int loc = e.outgoing ? -1 : noteMsgLocation(text, who);
      bubble(_thread_list, room && !e.outgoing ? who : NULL, text, e.outgoing, e.timestamp, st, col,
             loc, e.outgoing ? -1 : noteMsgMeta(i, false, false, loc, who));   // nothing to show for our own DM
    }
  }
  if (_thread_skip > 0) pageButton(_thread_list, LV_SYMBOL_DOWN "  Newer messages", -1);
  if (n == 0) label(_thread_list, "No messages yet", THEME_FONT_BODY, theme::TEXT_MUTED);
  lv_obj_update_layout(_thread_list);
  lv_obj_scroll_to_y(_thread_list, _thread_scroll_top ? 0 : LV_COORD_MAX, LV_ANIM_OFF);
  _thread_scroll_top = false;
}

// To the open conversation (channel or DM). The caller refreshes the thread.
bool UITask::sendThreadText(const char* text) {
  bool ok;
  if (_thread_is_channel) {
    ok = _core->sendChannelText(_thread_channel, text);
  } else {
    ContactInfo c;
    ok = MessageHistory::contactByPrefix(_thread_key, c) && _core->sendDirectText(c, text);
  }
  if (!ok) showToast("Send failed");
  return ok;
}

void UITask::sendFromCompose() {
  if (!_compose_ta) return;
  const char* typed = lv_textarea_get_text(_compose_ta);
  if (!typed || !typed[0]) return;
  char text[MSG_TEXT_BUF];
  msgtext::expandOutgoing(typed, text, sizeof(text), _prefs);   // {loc}, {time}, ... (MessageText.h)
  if (!sendThreadText(text)) return;
  lv_textarea_set_text(_compose_ta, "");
  setKeyboardVisible(false);
  refreshThread();
}

// ── Settings (first rows; the declarative schema replaces this later) ────────

static lv_obj_t* s_kb_main_dd = nullptr;
static lv_obj_t* s_kb_alt_dd = nullptr;

static void onKeyboardAlphabet(lv_event_t* e) {
  (void)e;
  s_ui->setKeyboardAlphabets(lv_dropdown_get_selected(s_kb_main_dd), lv_dropdown_get_selected(s_kb_alt_dd));
}

// One settings row: label left, dropdown right.
static lv_obj_t* dropdownRow(lv_obj_t* parent, const char* text, const char* options, int sel) {
  lv_obj_t* row = settingRow(parent, text, NULL);
  lv_obj_t* dd = lv_dropdown_create(row);
  lv_dropdown_set_options_static(dd, options);
  lv_dropdown_set_selected(dd, sel);
  lv_obj_set_width(dd, 130);
  lv_obj_align(dd, LV_ALIGN_RIGHT_MID, -4, 0);
  lv_obj_add_event_cb(dd, onKeyboardAlphabet, LV_EVENT_VALUE_CHANGED, NULL);
  return dd;
}

// A preference toggle (0/1 byte in NodePrefs), saved on change.
static void onPrefSwitch(lv_event_t* e) {
  uint8_t* pref = (uint8_t*)lv_event_get_user_data(e);
  *pref = lv_obj_has_state((lv_obj_t*)lv_event_get_target(e), LV_STATE_CHECKED) ? 1 : 0;
  the_mesh.savePrefs();
}

static void onGpsSwitch(lv_event_t* e) {
  s_ui->setGps(lv_obj_has_state((lv_obj_t*)lv_event_get_target(e), LV_STATE_CHECKED));
}

// pref == nullptr: the caller wires the switch itself.
static lv_obj_t* switchRow(lv_obj_t* parent, const char* text, const char* sub, uint8_t* pref) {
  lv_obj_t* row = settingRow(parent, text, sub, 236);   // the hint clear of the switch
  lv_obj_t* sw = lv_switch_create(row);
  lv_obj_set_size(sw, 46, 24);
  lv_obj_align(sw, LV_ALIGN_RIGHT_MID, -theme::PAD, 0);
  if (pref) {
    if (*pref) lv_obj_add_state(sw, LV_STATE_CHECKED);
    lv_obj_add_event_cb(sw, onPrefSwitch, LV_EVENT_VALUE_CHANGED, pref);
  }
  return sw;
}

// ── Schema-driven settings (ui-core/SettingsSchema.h) ─────────────────────────

static void onSchemaSwitch(lv_event_t* e) {
  s_ui->setSchemaValue((int)(uintptr_t)lv_event_get_user_data(e),
                       lv_obj_has_state((lv_obj_t*)lv_event_get_target(e), LV_STATE_CHECKED) ? 1 : 0);
}
static void onSchemaDropdown(lv_event_t* e) {
  s_ui->setSchemaValue((int)(uintptr_t)lv_event_get_user_data(e),
                       (int)lv_dropdown_get_selected((lv_obj_t*)lv_event_get_target(e)));
}
static void onOpenSchemaPage(lv_event_t* e) { s_ui->showSchemaSettings((int)(uintptr_t)lv_event_get_user_data(e)); }
static void onPruneContacts(lv_event_t* e) { (void)e; s_ui->pruneContacts(); }
static void onOpenQuickMsgs(lv_event_t* e);
static void onPinSetup(lv_event_t* e);   // DeviceScreen.h
static void onOpenOta(lv_event_t* e);    // OtaScreen.h
static void bluetoothRow(lv_obj_t* body);
static void onVolumeSlider(lv_event_t* e) {
  s_ui->setSoundVolume((int)lv_slider_get_value((lv_obj_t*)lv_event_get_target(e)));
}

void UITask::showSchemaSettings(int page) {
  _screen = SCR_SETTINGS_NAV;
  _settings_page = (uint8_t)(page < settings::PG_COUNT ? page : 0);
  buildSchemaSettings();
}

// Contacts > prune now: first tap shows how many, the second (within 3 s) removes them.
void UITask::pruneContacts() {
  int n = the_mesh.countStaleContacts();
  if (n == 0) {
    showToast(_prefs && _prefs->contact_expiry_idx == 0 ? "Contact expiry is off" : "No inactive contacts");
    return;
  }
  if (!_prune_armed_ms || millis() - _prune_armed_ms > 3000) {
    _prune_armed_ms = millis() | 1;
    if (_prune_lbl) lv_label_set_text_fmt(_prune_lbl, LV_SYMBOL_TRASH "  Remove %d contact%s?", n, n == 1 ? "" : "s");
    return;
  }
  _prune_armed_ms = 0;
  int removed = the_mesh.pruneStaleContacts();
  char t[40];
  snprintf(t, sizeof(t), "Removed %d contact%s", removed, removed == 1 ? "" : "s");
  showToast(t);
  if (_prune_lbl) lv_label_set_text(_prune_lbl, LV_SYMBOL_TRASH "  Remove inactive contacts now");
}

void UITask::applyDisplayPrefs() {
  if (!_prefs) return;
  if (_prefs->display_brightness_pct) lvport::setBacklightPct(_prefs->display_brightness_pct);
  else if (_display) _display->setBrightness(_prefs->display_brightness);
}

// Brightness slider: live while dragging, saved on release.
static void onBrightnessSlider(lv_event_t* e) {
  lv_obj_t* sl = (lv_obj_t*)lv_event_get_target(e);
  s_ui->setBrightnessPct((uint8_t)lv_slider_get_value(sl), lv_event_get_code(e) == LV_EVENT_RELEASED);
}

void UITask::setBrightnessPct(uint8_t pct, bool save) {
  if (!_prefs) return;
  _prefs->display_brightness_pct = pct;
  _prefs->display_brightness = (uint8_t)((pct + 12) / 25 > 4 ? 4 : (pct + 12) / 25);   // nearest level, for anything reading it
  applyDisplayPrefs();
  if (save) the_mesh.savePrefs();
}

static void onTapWake(lv_event_t* e) {
  s_ui->setTapWake(lv_obj_has_state((lv_obj_t*)lv_event_get_target(e), LV_STATE_CHECKED));
}

void UITask::setTapWake(bool on) {
  _tap_wake = on;
  lvport::saveTapWake(on);
}

void UITask::buildSchemaSettings() {
  lv_obj_t* body = newScreen(settings::pageTitle(_settings_page), true);
  _prune_lbl = nullptr;
  _prune_armed_ms = 0;
  schemaRows(body, _settings_page);
  if (_settings_page == settings::PG_MESSAGES) {   // the action that goes with "Contact expiry"
    lv_obj_t* b = lv_button_create(body);
    lv_obj_set_size(b, LV_PCT(100), 38);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_radius(b, theme::RADIUS, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(theme::SURFACE), 0);
    lv_obj_add_event_cb(b, onPruneContacts, LV_EVENT_CLICKED, NULL);
    _prune_lbl = label(b, LV_SYMBOL_TRASH "  Remove inactive contacts now", THEME_FONT_BODY, theme::TEXT);
    lv_obj_center(_prune_lbl);
  }
  if (_settings_page == settings::PG_DEVICE) {   // after Lock screen's section, in NVS not the schema
    sectionTitle(body, "WAKE");
    lv_obj_t* tw = switchRow(body, "Tap to wake", "Off: only the top button turns it on", nullptr);
    if (_tap_wake) lv_obj_add_state(tw, LV_STATE_CHECKED);
    lv_obj_add_event_cb(tw, onTapWake, LV_EVENT_VALUE_CHANGED, NULL);
    sectionTitle(body, "SECURITY");
    listRow(body, LV_SYMBOL_EYE_CLOSE "  Screen PIN", _pin[0] ? "On  -  asked when the screen wakes" : "Off",
            onPinSetup, NULL);
    sectionTitle(body, "LOOK");
    accentRow(body);   // DeviceScreen.h
  }
  if (_settings_page == settings::PG_SOUND) buildSoundRows(body, false);   // the melodies
  if (_settings_page == settings::PG_MESSAGES) {
    sectionTitle(body, "QUICK MESSAGES");
    char sub[48];
    snprintf(sub, sizeof(sub), "%d of %d set  -  sent with one tap", msgtext::quickUsed(_prefs), msgtext::QUICK_COUNT);
    listRow(body, LV_SYMBOL_EDIT "  Quick messages", sub, onOpenQuickMsgs, NULL);
  }
}

// A page's schema rows under their section titles (Settings pages and the tools' options).
void UITask::schemaRows(lv_obj_t* body, uint8_t page) {
  uint8_t sec = 0xFF;
  for (int i = 0; i < settings::COUNT; i++) {
    const settings::Setting& st = settings::ALL[i];
    if (settings::sectionPage(st.section) != page) continue;
    if (st.section != sec) {
      sec = st.section;
      sectionTitle(body, settings::sectionTitle(sec));
      if (sec == settings::SEC_SOUND) buildSoundRows(body, true);   // On / Off / Auto
    }
    uint8_t v = settings::get(*_prefs, st);
    if (st.offset == offsetof(NodePrefs, buzzer_volume)) {   // a five-step slider, heard on release
      lv_obj_t* row = settingRow(body, st.label, NULL);
      lv_obj_t* sl = lv_slider_create(row);
      lv_slider_set_range(sl, 0, 4);
      lv_slider_set_value(sl, v, LV_ANIM_OFF);
      lv_obj_set_size(sl, 170, 10);
      lv_obj_align(sl, LV_ALIGN_RIGHT_MID, -18, 0);
      lv_obj_set_ext_click_area(sl, 14);
      lv_obj_add_event_cb(sl, onVolumeSlider, LV_EVENT_RELEASED, NULL);
      continue;
    }
    if (st.offset == offsetof(NodePrefs, display_brightness)) {   // a slider here instead of five steps
      lv_obj_t* row = settingRow(body, st.label, NULL);
      lv_obj_t* sl = lv_slider_create(row);
      lv_slider_set_range(sl, 5, 100);
      uint8_t pct = _prefs->display_brightness_pct ? _prefs->display_brightness_pct
                                                   : (uint8_t)(_prefs->display_brightness * 25 > 5 ? _prefs->display_brightness * 25 : 5);
      lv_slider_set_value(sl, pct, LV_ANIM_OFF);
      lv_obj_set_size(sl, 170, 10);
      lv_obj_align(sl, LV_ALIGN_RIGHT_MID, -18, 0);
      lv_obj_set_ext_click_area(sl, 14);
      lv_obj_add_event_cb(sl, onBrightnessSlider, LV_EVENT_VALUE_CHANGED, NULL);
      lv_obj_add_event_cb(sl, onBrightnessSlider, LV_EVENT_RELEASED, NULL);
      continue;
    }
    if (!st.option) {
      lv_obj_t* sw = switchRow(body, st.label, st.hint, nullptr);
      if (v) lv_obj_add_state(sw, LV_STATE_CHECKED);
      lv_obj_add_event_cb(sw, onSchemaSwitch, LV_EVENT_VALUE_CHANGED, (void*)(uintptr_t)i);
      continue;
    }
    char opts[160];
    int o = 0;
    for (uint8_t k = 0; k < st.count && o < (int)sizeof(opts) - 16; k++) {
      if (k) opts[o++] = '\n';
      st.option(k, opts + o, sizeof(opts) - o, *_prefs);
      o += strlen(opts + o);
    }
    opts[o] = '\0';
    lv_obj_t* row = settingRow(body, st.label, st.hint, 176);
    lv_obj_t* dd = lv_dropdown_create(row);
    lv_dropdown_set_options(dd, opts);
    lv_dropdown_set_selected(dd, v);
    lv_obj_set_width(dd, 112);
    lv_obj_align(dd, LV_ALIGN_RIGHT_MID, -4, 0);
    lv_obj_add_event_cb(dd, onSchemaDropdown, LV_EVENT_VALUE_CHANGED, (void*)(uintptr_t)i);
  }
}

void UITask::setSchemaValue(int idx, int v) {
  if (!_prefs || idx < 0 || idx >= settings::COUNT) return;
  const settings::Setting& st = settings::ALL[idx];
  settings::set(*_prefs, st, (uint8_t)v);
  if (st.changed) st.changed(*_core);
  the_mesh.savePrefs();
  if (st.offset == offsetof(NodePrefs, units_imperial)) {   // other labels depend on it
    lv_obj_t* body = _body;
    int32_t y = body ? lv_obj_get_scroll_y(body) : 0;
    buildSchemaSettings();
    if (_body) { lv_obj_update_layout(_body); lv_obj_scroll_to_y(_body, y, LV_ANIM_OFF); }
  }
}

void UITask::showSettings() {
  _screen = SCR_SETTINGS;
  buildSettings();
}

static void onOpenRadio(lv_event_t* e);   // RadioScreen.h
static void onOpenStorage(lv_event_t* e); // StorageScreen.h

void UITask::buildSettings() {
  // The order of the original (ui-new): display, sound, radio, system,
  // keyboard, contacts and messages. Tools have their own Home tiles.
  lv_obj_t* body = newScreen("Settings", true);
  if (_prefs) {
    sectionTitle(body, "DISPLAY");
    listRow(body, LV_SYMBOL_EYE_OPEN "  Display & power", "Screen, battery, time, units",
            onOpenSchemaPage, (void*)(uintptr_t)settings::PG_DEVICE);
    sectionTitle(body, "SOUND");
    char sub[48], vol[12];
    settings::optVolume(_prefs->buzzer_volume, vol, sizeof(vol), *_prefs);
    snprintf(sub, sizeof(sub), "%s, %s  -  alerts, melodies", soundctl::modeLabel(soundctl::mode(_prefs)), vol);
    listRow(body, LV_SYMBOL_VOLUME_MAX "  Sound", sub, onOpenSchemaPage, (void*)(uintptr_t)settings::PG_SOUND);
    sectionTitle(body, "RADIO");
    int pi = radioctl::currentPreset(_prefs);
    const char* pn = "Custom"; float f, b; uint8_t sf, cr;
    if (pi >= 0) radioctl::presetAt(_prefs, pi, pn, f, b, sf, cr);
    snprintf(sub, sizeof(sub), "%s  -  %.3f MHz, %d dBm", pn, _prefs->freq, _prefs->tx_power_dbm);
    listRow(body, UI_SYMBOL_RADIO "  Radio", sub, onOpenRadio, NULL);
  } else {
    sectionTitle(body, "RADIO");
  }
  bluetoothRow(body);
  wifiRow(body);
  sectionTitle(body, "SYSTEM");
  listRow(body, LV_SYMBOL_EDIT "  Name", the_mesh.getNodeName(), onNodeName, NULL);
  listRow(body, LV_SYMBOL_DOWNLOAD "  Firmware update", FIRMWARE_VERSION, onOpenOta, NULL);
  if (_core->gpsAvailable()) {
    lv_obj_t* sw = switchRow(body, "GPS", "For maps and sharing", nullptr);
    if (_core->gpsEnabled()) lv_obj_add_state(sw, LV_STATE_CHECKED);
    lv_obj_add_event_cb(sw, onGpsSwitch, LV_EVENT_VALUE_CHANGED, NULL);
    listRow(body, LV_SYMBOL_GPS "  GPS details", "Satellites, signal, sky view", onOpenGpsFromSettings, NULL);
  }
  listRow(body, LV_SYMBOL_SD_CARD "  Storage", "SD card, message history", onOpenStorage, NULL);
  listRow(body, LV_SYMBOL_REFRESH "  Reboot", NULL, onPowerRow, (void*)(uintptr_t)1);
  listRow(body, LV_SYMBOL_POWER "  Power off", NULL, onPowerRow, (void*)(uintptr_t)0);
  sectionTitle(body, "KEYBOARD");
  uint8_t main_a = _prefs ? _prefs->keyboard_main_alphabet : 0;
  uint8_t alt_a  = _prefs ? _prefs->keyboard_alt_alphabet : 0;
  if (main_a >= NodePrefs::KB_ALPHABET_COUNT) main_a = 0;
  if (alt_a >= NodePrefs::KB_ALPHABET_COUNT) alt_a = main_a;
  // Order matches NodePrefs::KB_ALPHABET_* (Latin, Cyrillic, Greek).
  s_kb_main_dd = dropdownRow(body, "Main", "Latin\nCyrillic\nGreek", main_a);
  // "None" = no second script (stored as alt == main, as ui-new does).
  s_kb_alt_dd  = dropdownRow(body, "Additional", "None\nLatin\nCyrillic\nGreek",
                             alt_a == main_a ? 0 : alt_a + 1);
  label(body, "Hold a letter for accents and other variants.", THEME_FONT_SMALL, theme::TEXT_MUTED);

  if (_prefs) {
    sectionTitle(body, "CONTACTS & MESSAGES");
    listRow(body, LV_SYMBOL_ENVELOPE "  Messages & contacts", "Resend, expiry, quick messages",
            onOpenSchemaPage, (void*)(uintptr_t)settings::PG_MESSAGES);
  }

  sectionTitle(body, "ABOUT");
  char about[300], built[24] = "";
  if (!strstr(FIRMWARE_VERSION, FIRMWARE_BUILD_DATE)) snprintf(built, sizeof(built), " (%s)", FIRMWARE_BUILD_DATE);
  snprintf(about, sizeof(about), "%s\nFirmware %s%s\n\nMap data: %s\nEmoji: Twemoji \xC2\xA9 Twitter, Inc. and contributors (CC\xE2\x80\x91" "BY 4.0)",
           the_mesh.getNodeName(), FIRMWARE_VERSION, built,
           (lvport::mountStorage() && mapview::s_provider->available()) ? mapview::s_provider->attribution()
                                                                         : "\xC2\xA9 OpenStreetMap contributors");
  lv_obj_t* a = label(body, about, THEME_FONT_SMALL, theme::TEXT);
  lv_label_set_long_mode(a, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(a, LV_PCT(100));
}

void UITask::setKeyboardAlphabets(int main_idx, int alt_sel) {
  if (!_prefs) return;
  _prefs->keyboard_main_alphabet = (uint8_t)main_idx;
  _prefs->keyboard_alt_alphabet  = (uint8_t)(alt_sel == 0 ? main_idx : alt_sel - 1);
  the_mesh.savePrefs();
}

#include "ConversationScreen.h"
#include "DeviceScreen.h"
#include "DiagScreen.h"
#include "CompassScreen.h"
#include "GpsScreen.h"
#include "RadioExtras.h"
#include "RepeaterScreen.h"
#include "SoundScreen.h"
#include "QuickScreen.h"
#include "OtaScreen.h"
#include "StorageScreen.h"
#include "Splash.h"

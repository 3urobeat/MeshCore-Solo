#include "UITask.h"
#include <helpers/TxtDataHelpers.h>
#include "../MyMesh.h"
#include "target.h"
#if defined(ESP32)
  #include <esp_heap_caps.h>
#endif

#include "../ui-core/UiCore.h"   // shared UI Core (header-only, this TU)
#include "Theme.h"
#include "LvglPort.h"
#include "../ui-core/KeyboardData.h"
#include "Keyboard.h"

// ui-lvgl skeleton (docs/development/ui-core.md, step 5): status bar, home,
// conversation list, contact picker, conversation view with compose. Every
// piece of state it shows comes from the UI Core; this file only draws it.

static UITask* s_ui = nullptr;   // for LVGL's C callbacks

#if defined(SIM_PLATFORM) && defined(__EMSCRIPTEN__)
#include <emscripten.h>
// The board's one button, pressed from the simulator page (web/lvgl.html).
static bool s_sim_btn_click = false;
extern "C" EMSCRIPTEN_KEEPALIVE void sim_lcd_button() { s_sim_btn_click = true; }
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

// Unread badge: amber pill with a count, on the right edge of `parent`.
static void badge(lv_obj_t* parent, int n, bool overflow) {
  if (n <= 0) return;
  lv_obj_t* b = lv_obj_create(parent);
  lv_obj_remove_flag(b, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(b, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(b, LV_SIZE_CONTENT, 20);
  lv_obj_set_style_bg_color(b, lv_color_hex(theme::ACCENT), 0);
  lv_obj_set_style_border_width(b, 0, 0);
  lv_obj_set_style_radius(b, 10, 0);
  lv_obj_set_style_pad_hor(b, 7, 0);
  lv_obj_set_style_pad_ver(b, 0, 0);
  lv_obj_align(b, LV_ALIGN_RIGHT_MID, -theme::PAD, 0);
  lv_obj_t* l = label(b, "", THEME_FONT_SMALL, theme::BG);
  lv_label_set_text_fmt(l, "%d%s", n, overflow ? "+" : "");
  lv_obj_center(l);
}

// "12s" / "5m" / "3h" / "2d" since a unix timestamp.
static void formatAge(char* buf, size_t n, uint32_t ts) {
  uint32_t now = rtc_clock.getCurrentTime();
  uint32_t d = (ts && now > ts) ? now - ts : 0;
  if (d < 60)          snprintf(buf, n, "%lus", (unsigned long)d);
  else if (d < 3600)   snprintf(buf, n, "%lum", (unsigned long)(d / 60));
  else if (d < 86400)  snprintf(buf, n, "%luh", (unsigned long)(d / 3600));
  else                 snprintf(buf, n, "%lud", (unsigned long)(d / 86400));
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

  _core = new UiCore();
  _core->begin(node_prefs, sensors, this);

#ifdef PIN_USER_BTN
  user_btn.begin();
#endif

  lv_init();
  lv_tick_set_cb([]() -> uint32_t { return (uint32_t)millis(); });
  if (!lvport::begin()) {
    Serial.println("ui-lvgl: no memory for display buffers");
    return;
  }
  lv_theme_t* th = lv_theme_default_init(lv_display_get_default(), lv_color_hex(theme::ACCENT),
                                         lv_color_hex(theme::ACCENT_DIM), true, THEME_FONT_BODY);
  lv_display_set_theme(lv_display_get_default(), th);

  buildStatusBar();
  showHome();
}

MyMesh::Listener* UITask::meshListener() { return _core; }

void UITask::loop() {
  pollConnection();
  drainCoreEvents();

  bool btn_click = false;
#ifdef PIN_USER_BTN
  btn_click = user_btn.check() == BUTTON_EVENT_CLICK;
#elif defined(SIM_PLATFORM) && defined(__EMSCRIPTEN__)
  btn_click = s_sim_btn_click;
  s_sim_btn_click = false;
#endif
  if (btn_click) {
    if (_asleep) wake();
    else if (_screen == SCR_HOME) sleep();
    else back();
  }

  if (_asleep) {
    if (lvport::touched()) { lvport::swallowTouch(); wake(); }
  } else {
    uint32_t aoff = autoOffMillis();
    if (aoff > 0 && lv_display_get_inactive_time(NULL) > aoff) sleep();
  }

  _core->loop();
  drainCoreEvents();

  if (!_asleep) {
    if ((int32_t)(millis() - _next_status_ms) >= 0) {
      _next_status_ms = millis() + 1000;
      refreshStatusBar();
      if (_screen == SCR_HOME) refreshHome();
    }
    if (_screen == SCR_THREAD && (int32_t)(millis() - _next_thread_check_ms) >= 0) {
      _next_thread_check_ms = millis() + 500;
      uint32_t sig = threadSignature();
      if (_thread_dirty || sig != _thread_sig) refreshThread();
    }
    lv_timer_handler();
  }
}

void UITask::shutdown(bool restart) {
  the_mesh.savePrefs();
  the_mesh.saveRTCTime();
  the_mesh.flushDirtyContacts();
  _core->trail.onShutdown();
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

uint32_t UITask::autoOffMillis() const {
  if (!_prefs || _prefs->auto_off_secs == 0) return 0;
  return (uint32_t)_prefs->auto_off_secs * 1000UL;
}

void UITask::sleep() {
  if (_asleep) return;
  _asleep = true;
  if (_display) _display->turnOff();
}

void UITask::wake() {
  lv_display_trigger_activity(NULL);
  if (!_asleep) return;
  _asleep = false;
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
      wake();
      showToast(ev.text, ClockEngine::RING_MS);
      break;
    case UiEventType::ClockRingEnded:
      break;
    case UiEventType::LiveShareEnded:
      showToast("Live share ended");
      break;
    case UiEventType::LocatorCrossed:
      showToast(ev.text, 3000);
      break;
    default:
      break;
    }
  }
}

void UITask::onMessageArrived(const UiEvent& ev) {
  char buf[48];
  snprintf(buf, sizeof(buf), "Msg: %.20s", ev.text);
  // Wake for the message unless an app is already showing it, or the user
  // turned message-wake off.
  bool wake_disabled = _prefs && _prefs->msg_wake_screen_off;
  if (_asleep && !wake_disabled && !isClientConnected()) wake();
  else if (!_asleep) lv_display_trigger_activity(NULL);
  showToast(buf, 3000);
  if (_screen == SCR_CHATS) buildChats();   // new unread counts
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
  _status_icons = label(bar, "", THEME_FONT_SMALL, theme::TEXT_MUTED);
  lv_obj_align(_status_icons, LV_ALIGN_RIGHT_MID, 0, 0);

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
  label(_toast, "", THEME_FONT_BODY, theme::TEXT);
  lv_obj_add_flag(_toast, LV_OBJ_FLAG_HIDDEN);

  refreshStatusBar();
}

void UITask::refreshStatusBar() {
  if (!_status_time) return;
  uint32_t now = rtc_clock.getCurrentTime();
  if (now > 1000000000UL) {
    time_t t = (time_t)((int64_t)now + (int64_t)(_prefs ? _prefs->tz_offset_hours : 0) * 3600);
    struct tm* ti = gmtime(&t);
    lv_label_set_text_fmt(_status_time, "%02d:%02d", ti->tm_hour, ti->tm_min);
  } else {
    lv_label_set_text(_status_time, "--:--");
  }

  uint16_t mv = getBattMilliVolts();
  int pct = mv <= 3300 ? 0 : mv >= 4200 ? 100 : (int)(mv - 3300) * 100 / 900;
  const char* batt = pct > 80 ? LV_SYMBOL_BATTERY_FULL : pct > 55 ? LV_SYMBOL_BATTERY_3
                   : pct > 30 ? LV_SYMBOL_BATTERY_2 : pct > 10 ? LV_SYMBOL_BATTERY_1 : LV_SYMBOL_BATTERY_EMPTY;
  bool gps = false;
  if (_sensors) {
    LocationProvider* loc = _sensors->getLocationProvider();
    gps = loc && loc->isValid();
  }
  lv_label_set_text_fmt(_status_icons, "%s%s%s %d%%",
                        hasConnection() ? LV_SYMBOL_BLUETOOTH "  " : "",
                        gps ? LV_SYMBOL_GPS "  " : "",
                        batt, pct);
}

// One persistent timer, paused between toasts: hides the toast when it fires.
static void toastTimerCb(lv_timer_t* t) {
  lv_obj_add_flag((lv_obj_t*)lv_timer_get_user_data(t), LV_OBJ_FLAG_HIDDEN);
  lv_timer_pause(t);
}

void UITask::showToast(const char* text, uint32_t ms) {
  if (!_toast) return;
  lv_label_set_text(lv_obj_get_child(_toast, 0), text);
  lv_obj_remove_flag(_toast, LV_OBJ_FLAG_HIDDEN);
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
  lv_obj_t* prev = lv_screen_active();
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
  lv_screen_load(scr);
  if (prev && prev != scr) lv_obj_delete_async(prev);
  return body;
}

void UITask::back() {
  switch (_screen) {
    case SCR_THREAD:   showChats(); break;
    case SCR_CONTACTS: showChats(); break;
    case SCR_SETTINGS: showHome(); break;
    case SCR_CHATS:    showHome(); break;
    default:           break;
  }
}

// ── Home ──────────────────────────────────────────────────────────────────────

static void onOpenChats(lv_event_t* e) { (void)e; s_ui->showChats(); }
static void onOpenSettings(lv_event_t* e) { (void)e; s_ui->showSettings(); }

// Wide home tile: icon + label left, optional value label right.
static lv_obj_t* homeTile(lv_obj_t* parent, const char* text, lv_event_cb_t cb, lv_obj_t** value_out) {
  lv_obj_t* tile = lv_button_create(parent);
  lv_obj_set_size(tile, LV_PCT(100), 42);
  lv_obj_set_style_bg_color(tile, lv_color_hex(theme::SURFACE), 0);
  lv_obj_set_style_radius(tile, theme::RADIUS, 0);
  lv_obj_set_style_shadow_width(tile, 0, 0);
  lv_obj_add_event_cb(tile, cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t* tl = label(tile, text, THEME_FONT_TITLE, theme::TEXT);
  lv_obj_align(tl, LV_ALIGN_LEFT_MID, theme::PAD, 0);
  if (value_out) {
    *value_out = label(tile, "", THEME_FONT_TITLE, theme::ACCENT);
    lv_obj_align(*value_out, LV_ALIGN_RIGHT_MID, -theme::PAD, 0);
  }
  return tile;
}

void UITask::showHome() {
  _screen = SCR_HOME;
  buildHome();
  refreshHome();
}

void UITask::buildHome() {
  lv_obj_t* body = newScreen(NULL, false);
  lv_obj_set_flex_align(body, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  _home_clock = label(body, "--:--", THEME_FONT_CLOCK, theme::TEXT);
  _home_date = label(body, "", THEME_FONT_BODY, theme::TEXT_MUTED);
  lv_obj_t* name = label(body, the_mesh.getNodeName(), THEME_FONT_BODY, theme::ACCENT);
  lv_obj_set_style_pad_bottom(name, 4, 0);

  homeTile(body, LV_SYMBOL_ENVELOPE "  Messages", onOpenChats, &_home_unread);
  homeTile(body, LV_SYMBOL_SETTINGS "  Settings", onOpenSettings, NULL);
}

void UITask::refreshHome() {
  if (!_home_clock) return;
  uint32_t now = rtc_clock.getCurrentTime();
  if (now > 1000000000UL) {
    time_t t = (time_t)((int64_t)now + (int64_t)(_prefs ? _prefs->tz_offset_hours : 0) * 3600);
    struct tm* ti = gmtime(&t);
    static const char* DOW[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
    static const char* MON[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                 "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
    lv_label_set_text_fmt(_home_clock, "%02d:%02d", ti->tm_hour, ti->tm_min);
    lv_label_set_text_fmt(_home_date, "%s %d %s %d", DOW[ti->tm_wday], ti->tm_mday, MON[ti->tm_mon],
                          ti->tm_year + 1900);
  } else {
    lv_label_set_text(_home_clock, "--:--");
    lv_label_set_text(_home_date, "time not synced");
  }
  int unread = _core->dmUnreadTotal() + _core->history.getTotalChannelUnread() + _core->roomUnread();
  if (unread > 0) lv_label_set_text_fmt(_home_unread, "%d new", unread);
  else lv_label_set_text(_home_unread, "");
}

// ── Conversation list ─────────────────────────────────────────────────────────

static void onOpenChannel(lv_event_t* e) {
  s_ui->openChannel((uint8_t)(uintptr_t)lv_event_get_user_data(e));
}

// DM rows carry a 4-byte prefix; kept in a static table the rows point into.
static uint8_t s_dm_rows[MessageHistory::DM_HIST_MAX][4];
static uint8_t s_contact_rows[64][PUB_KEY_SIZE];

static void onOpenDMRow(lv_event_t* e) {
  s_ui->openDM(s_dm_rows[(uintptr_t)lv_event_get_user_data(e)]);
}
static void onOpenContactRow(lv_event_t* e) {
  s_ui->openDM(s_contact_rows[(uintptr_t)lv_event_get_user_data(e)]);
}
static void onNewChat(lv_event_t* e) { (void)e; s_ui->showContacts(); }

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
    lv_obj_set_width(s, 230);
    lv_obj_align(s, LV_ALIGN_BOTTOM_LEFT, theme::PAD, -5);
  }
  return row;
}

static lv_obj_t* sectionTitle(lv_obj_t* parent, const char* text) {
  lv_obj_t* l = label(parent, text, THEME_FONT_SMALL, theme::TEXT_MUTED);
  lv_obj_set_style_pad_top(l, 4, 0);
  return l;
}

void UITask::showChats() {
  _screen = SCR_CHATS;
  buildChats();
}

void UITask::buildChats() {
  lv_obj_t* body = newScreen("Messages", true);

  // Channels
  sectionTitle(body, "CHANNELS");
  for (int i = 0; i < MAX_GROUP_CHANNELS; i++) {
    ChannelDetails ch;
    if (!the_mesh.getChannel(i, ch) || ch.name[0] == '\0') continue;
    char sub[64] = "";
    int n = _core->history.histCountForChannel(i);
    if (n > 0) {
      const ChHistEntry& e = _core->history.chAtPos(_core->history.histEntryForChannel(i, 0));
      snprintf(sub, sizeof(sub), "%s", e.text);
    }
    lv_obj_t* row = listRow(body, ch.name, sub[0] ? sub : NULL, onOpenChannel, (void*)(uintptr_t)i);
    badge(row, _core->history.chUnread(i), _core->history.chUnreadOverflow(i));
  }

  // Recent direct conversations (DM ring, newest first, one row per contact)
  sectionTitle(body, "DIRECT");
  int rows = 0;
  for (int j = 0; j < _core->history.dmHistCount() && rows < MessageHistory::DM_HIST_MAX; j++) {
    const DmHistEntry& e = _core->history.dmAtPos(_core->history.dmHistPosNewest(j));
    bool seen = false;
    for (int r = 0; r < rows; r++) if (memcmp(s_dm_rows[r], e.prefix, 4) == 0) { seen = true; break; }
    if (seen) continue;
    memcpy(s_dm_rows[rows], e.prefix, 4);
    char name[33];
    contactName(e.prefix, name, sizeof(name));
    char sub[64];
    snprintf(sub, sizeof(sub), "%s%s", e.outgoing ? "Me: " : "", e.text);
    lv_obj_t* row = listRow(body, name, sub, onOpenDMRow, (void*)(uintptr_t)rows);
    badge(row, _core->dmUnread(e.prefix), _core->dmUnreadOverflow(e.prefix));
    rows++;
  }
  int room = _core->roomUnread();
  if (room > 0) {
    char t[32];
    snprintf(t, sizeof(t), "%d unread room post%s", room, room == 1 ? "" : "s");
    sectionTitle(body, t);
  }

  lv_obj_t* add = lv_button_create(body);
  lv_obj_set_size(add, LV_PCT(100), 36);
  lv_obj_set_style_bg_color(add, lv_color_hex(theme::ACCENT_DIM), 0);
  lv_obj_set_style_radius(add, theme::RADIUS, 0);
  lv_obj_set_style_shadow_width(add, 0, 0);
  lv_obj_add_event_cb(add, onNewChat, LV_EVENT_CLICKED, NULL);
  lv_obj_center(label(add, LV_SYMBOL_PLUS "  New message", THEME_FONT_BODY, theme::TEXT));
}

// ── Contact picker (start a DM) ───────────────────────────────────────────────

void UITask::showContacts() {
  _screen = SCR_CONTACTS;
  buildContacts();
}

void UITask::buildContacts() {
  lv_obj_t* body = newScreen("New message", true);
  int total = the_mesh.getNumContacts();
  int rows = 0;
  // +MAX_ANON_CONTACTS: getContactByIdx() takes the raw table index (see
  // MessageHistory::contactByPrefix()).
  for (int i = 0; i < total && rows < (int)(sizeof(s_contact_rows) / sizeof(s_contact_rows[0])); i++) {
    ContactInfo c;
    if (!the_mesh.getContactByIdx(MAX_ANON_CONTACTS + i, c)) continue;
    if (c.type != ADV_TYPE_CHAT) continue;
    memcpy(s_contact_rows[rows], c.id.pub_key, PUB_KEY_SIZE);
    listRow(body, c.name, NULL, onOpenContactRow, (void*)(uintptr_t)rows);
    rows++;
  }
  if (rows == 0) label(body, "No contacts yet", THEME_FONT_BODY, theme::TEXT_MUTED);
}

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
  _core->history.setChUnread(channel_idx, 0);
  _screen = SCR_THREAD;
  buildThread();
}

void UITask::openDM(const uint8_t* pub_key) {
  _thread_is_channel = false;
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
    if (the_mesh.getChannel(_thread_channel, ch)) snprintf(title, sizeof(title), "# %s", ch.name);
    else snprintf(title, sizeof(title), "Channel %d", _thread_channel);
  } else {
    contactName(_thread_key, title, sizeof(title));
    ContactInfo c;
    can_send = MessageHistory::contactByPrefix(_thread_key, c) && c.type == ADV_TYPE_CHAT;
  }
  lv_obj_t* body = newScreen(title, true);
  lv_obj_set_style_pad_all(body, 0, 0);
  lv_obj_set_style_pad_row(body, 0, 0);

  _thread_list = lv_obj_create(body);
  styleSurface(_thread_list, theme::BG);
  lv_obj_set_width(_thread_list, LV_PCT(100));
  lv_obj_set_flex_grow(_thread_list, 1);
  lv_obj_set_flex_flow(_thread_list, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all(_thread_list, theme::PAD, 0);
  lv_obj_set_style_pad_row(_thread_list, theme::GAP, 0);
  lv_obj_set_scrollbar_mode(_thread_list, LV_SCROLLBAR_MODE_ACTIVE);   // only while scrolling

  _compose_ta = nullptr;
  _keyboard = nullptr;
  if (can_send) {
    lv_obj_t* bar = lv_obj_create(body);
    styleSurface(bar, theme::BG);
    lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(bar, LV_PCT(100), 42);
    lv_obj_set_style_pad_all(bar, 4, 0);
    _compose_ta = lv_textarea_create(bar);
    lv_textarea_set_one_line(_compose_ta, true);
    lv_textarea_set_placeholder_text(_compose_ta, "Message");
    lv_obj_add_event_cb(_compose_ta, onComposeInsert, LV_EVENT_INSERT, NULL);
    lv_obj_set_size(_compose_ta, LV_PCT(100), 34);
    lv_obj_align(_compose_ta, LV_ALIGN_LEFT_MID, 0, 0);
    // The theme's padding leaves less than one line inside 36 px, which makes
    // the field scroll vertically (text jumps as it's typed). Pad so exactly one
    // body line (20 px) fits: 34 = 2*1 border + 2*6 pad + 20.
    lv_obj_set_style_border_width(_compose_ta, 1, 0);
    lv_obj_set_style_pad_ver(_compose_ta, 6, 0);
    lv_obj_set_style_pad_hor(_compose_ta, 10, 0);
    lv_obj_set_scrollbar_mode(_compose_ta, LV_SCROLLBAR_MODE_OFF);
    // Cursor: the theme draws it only while FOCUSED (set while the keyboard is up).
    lv_obj_set_style_border_color(_compose_ta, lv_color_hex(theme::ACCENT), LV_PART_CURSOR | LV_STATE_FOCUSED);
    lv_obj_set_style_border_width(_compose_ta, 2, LV_PART_CURSOR | LV_STATE_FOCUSED);

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
      sig = sig * 31 + e.timestamp + e.relay_status;
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

// One message bubble. Own messages right-aligned in amber, others left.
static void bubble(lv_obj_t* list, const char* from, const char* text, bool own,
                   uint32_t ts, const char* status, uint32_t status_col) {
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
  lv_obj_remove_flag(b, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(b, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_style_max_width(b, 250, 0);
  lv_obj_set_style_bg_color(b, lv_color_hex(own ? theme::ACCENT_DIM : theme::SURFACE), 0);
  lv_obj_set_style_border_width(b, 0, 0);
  lv_obj_set_style_radius(b, theme::RADIUS, 0);
  lv_obj_set_style_pad_all(b, 6, 0);
  lv_obj_set_style_pad_row(b, 2, 0);
  lv_obj_set_flex_flow(b, LV_FLEX_FLOW_COLUMN);

  char meta[32];
  formatAge(meta, sizeof(meta), ts);

  // Channel messages: "Sender  5m" on one line above the text, which keeps
  // the bubble two lines tall -- matters with the keyboard up.
  bool meta_in_header = from && from[0] && !status;
  if (from && from[0]) {
    lv_obj_t* hdr = lv_obj_create(b);
    styleSurface(hdr, theme::BG);
    lv_obj_set_style_bg_opa(hdr, LV_OPA_TRANSP, 0);
    lv_obj_remove_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(hdr, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(hdr, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(hdr, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(hdr, 8, 0);
    label(hdr, from, THEME_FONT_SMALL, theme::ACCENT);   // names are <= 31 chars: fits the bubble
    if (meta_in_header) label(hdr, meta, THEME_FONT_SMALL, theme::TEXT_MUTED);
  }
  lv_obj_t* t = label(b, text, THEME_FONT_BODY, theme::TEXT);
  lv_label_set_long_mode(t, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_max_width(t, 238, 0);
  lv_obj_set_width(t, LV_SIZE_CONTENT);

  if (!meta_in_header) {
    lv_obj_t* m = label(b, meta, THEME_FONT_SMALL, theme::TEXT_MUTED);
    if (status) {
      lv_label_set_text_fmt(m, "%s  %s", meta, status);
      lv_obj_set_style_text_color(m, lv_color_hex(status_col), 0);
    }
  }
}

void UITask::refreshThread() {
  if (!_thread_list) return;
  _thread_dirty = false;
  _thread_sig = threadSignature();
  lv_obj_clean(_thread_list);
  const MessageHistory& h = _core->history;
  const int MAX_SHOWN = 30;

  if (_thread_is_channel) {
    int n = h.histCountForChannel(_thread_channel);
    if (n > MAX_SHOWN) n = MAX_SHOWN;
    for (int j = n - 1; j >= 0; j--) {   // oldest first
      const ChHistEntry& e = h.chAtPos(h.histEntryForChannel(_thread_channel, j));
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
      const char* st = NULL; uint32_t col = theme::TEXT_MUTED;
      if (own && e.relay_status == ACK_OK)      { st = LV_SYMBOL_OK " relayed"; col = theme::OK; }
      else if (own && e.relay_status == ACK_PENDING) st = "sent";
      bubble(_thread_list, own ? NULL : from, body, own, e.timestamp, st, col);
    }
  } else {
    int n = h.dmHistCountForContact(_thread_key);
    if (n > MAX_SHOWN) n = MAX_SHOWN;
    for (int j = n - 1; j >= 0; j--) {
      const DmHistEntry& e = h.dmAtPos(h.dmHistEntryForContact(_thread_key, j));
      const char* st = NULL; uint32_t col = theme::TEXT_MUTED;
      if (e.outgoing) {
        switch (h.dmEffectiveStatus(e)) {
          case ACK_OK:      st = LV_SYMBOL_OK " delivered"; col = theme::OK; break;
          case ACK_FAIL:    st = LV_SYMBOL_CLOSE " not delivered"; col = theme::FAIL; break;
          case ACK_PENDING: st = "sending..."; break;
          default:          st = "sent"; break;
        }
      }
      bubble(_thread_list, NULL, e.text, e.outgoing, e.timestamp, st, col);
    }
  }
  if (lv_obj_get_child_count(_thread_list) == 0)
    label(_thread_list, "No messages yet", THEME_FONT_BODY, theme::TEXT_MUTED);
  lv_obj_update_layout(_thread_list);
  lv_obj_scroll_to_y(_thread_list, LV_COORD_MAX, LV_ANIM_OFF);
}

void UITask::sendFromCompose() {
  if (!_compose_ta) return;
  const char* text = lv_textarea_get_text(_compose_ta);
  if (!text || !text[0]) return;
  bool ok;
  if (_thread_is_channel) {
    ok = _core->sendChannelText(_thread_channel, text);
  } else {
    ContactInfo c;
    ok = MessageHistory::contactByPrefix(_thread_key, c) && _core->sendDirectText(c, text);
  }
  if (ok) {
    lv_textarea_set_text(_compose_ta, "");
    setKeyboardVisible(false);
    refreshThread();
  } else {
    showToast("Send failed");
  }
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
  lv_obj_t* row = lv_obj_create(parent);
  styleSurface(row, theme::SURFACE);
  lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(row, LV_PCT(100), theme::ROW_H);
  lv_obj_set_style_radius(row, theme::RADIUS, 0);
  lv_obj_t* l = label(row, text, THEME_FONT_BODY, theme::TEXT);
  lv_obj_align(l, LV_ALIGN_LEFT_MID, theme::PAD, 0);
  lv_obj_t* dd = lv_dropdown_create(row);
  lv_dropdown_set_options_static(dd, options);
  lv_dropdown_set_selected(dd, sel);
  lv_obj_set_width(dd, 130);
  lv_obj_align(dd, LV_ALIGN_RIGHT_MID, -4, 0);
  lv_obj_add_event_cb(dd, onKeyboardAlphabet, LV_EVENT_VALUE_CHANGED, NULL);
  return dd;
}

void UITask::showSettings() {
  _screen = SCR_SETTINGS;
  buildSettings();
}

void UITask::buildSettings() {
  lv_obj_t* body = newScreen("Settings", true);
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
}

void UITask::setKeyboardAlphabets(int main_idx, int alt_sel) {
  if (!_prefs) return;
  _prefs->keyboard_main_alphabet = (uint8_t)main_idx;
  _prefs->keyboard_alt_alphabet  = (uint8_t)(alt_sel == 0 ? main_idx : alt_sel - 1);
  the_mesh.savePrefs();
}

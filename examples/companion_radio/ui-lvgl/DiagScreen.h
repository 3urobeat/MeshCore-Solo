#pragma once
// Settings > Diagnostics -- ui-new's Tools > Diagnostics. Tabs Live / System /
// Font; the rows come from ui-core/Diagnostics.h. Live refreshes every second
// and its header button resets the counters (after a confirm).
//
// Single-TU fragment: included by ui-lvgl/UITask.cpp after DeviceScreen.h.

namespace diagview {

enum : uint8_t { TAB_LIVE, TAB_SYSTEM, TAB_FONT, TAB_COUNT };
static uint8_t s_tab = TAB_LIVE;   // kept across visits
static lv_obj_t* s_list = nullptr;
static const int EXTRA = 3;   // GPS, last reset, last crash (L2 only)
static lv_obj_t* s_vals[diag::MAX_ROWS + EXTRA];
static int s_rows = 0;

static void extraRow(diag::Row* rows, int& n, const char* lbl, const char* fmt, ...) {
  rows[n].label = lbl;
  va_list ap; va_start(ap, fmt);
  vsnprintf(rows[n].value, sizeof(rows[n].value), fmt, ap);
  va_end(ap);
  n++;
}

// ui-core's live rows plus the receiver's state and why the device last
// started -- what to look at when GPS gets no fix or the device restarted.
static int allRows(diag::Row* rows, bool gps_on) {
  int n = diag::liveRows(rows);
#if defined(SEEED_WIO_TRACKER_L2)
  static uint32_t s_chars = 0, s_moved_ms = 0;
  uint32_t c = gps.rxChars();
  if (c != s_chars) { s_chars = c; s_moved_ms = millis(); }
  bool data = c > 0 && millis() - s_moved_ms < 3000;
  if (!gps_on) extraRow(rows, n, "GPS", "off");
  else if (!data) extraRow(rows, n, "GPS", "no data (%lu B)", (unsigned long)c);
  else extraRow(rows, n, "GPS", "%s, %ld sats", gps.isValid() ? "fix" : "no fix", gps.satellitesCount());
#else
  (void)gps_on;
#endif
  extraRow(rows, n, "Last start", "%s", lvport::resetReason());
  char crash[32];
  if (lvport::crashSummary(crash, sizeof(crash))) extraRow(rows, n, "Last crash", "%s", crash);
  return n;
}

}  // namespace diagview

static void onOpenDiag(lv_event_t* e) { (void)e; s_ui->showDiag(); }
static void onDiagTab(lv_event_t* e) {
  s_ui->diagTab((int)lv_buttonmatrix_get_selected_button((lv_obj_t*)lv_event_get_target(e)));
}
static void onDiagReset(lv_event_t* e)   { (void)e; s_ui->diagResetPopup(); }
static void onDiagResetGo(lv_event_t* e) { (void)e; s_ui->diagReset(); }

void UITask::showDiag() {
  _screen = SCR_DIAG;
  buildDiag();
}

void UITask::diagTab(int tab) {
  if (tab < 0 || tab >= diagview::TAB_COUNT) return;
  diagview::s_tab = (uint8_t)tab;
  buildDiag();
}

void UITask::buildDiag() {
  using namespace diagview;
  lv_obj_t* body = newScreen("Diagnostics", true);
  lv_obj_set_style_pad_row(body, 4, 0);
  if (s_tab == TAB_LIVE && _header) headerButton(_header, LV_SYMBOL_REFRESH " Reset", onDiagReset, 4, NULL);

  static const char* TABS[] = { "Live", "System", "Font", "" };
  lv_obj_t* tabs = segmented(body, TABS, s_tab, lv_pct(100), 34);
  lv_obj_set_style_bg_color(tabs, lv_color_hex(theme::SURFACE), LV_PART_ITEMS);
  lv_obj_add_event_cb(tabs, onDiagTab, LV_EVENT_VALUE_CHANGED, NULL);

  s_list = lv_obj_create(body);
  styleSurface(s_list, theme::SURFACE);
  lv_obj_set_style_radius(s_list, theme::RADIUS, 0);
  lv_obj_set_style_pad_all(s_list, theme::PAD, 0);
  lv_obj_set_style_pad_row(s_list, 3, 0);
  lv_obj_set_width(s_list, LV_PCT(100));
  lv_obj_set_flex_grow(s_list, 1);
  lv_obj_set_flex_flow(s_list, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_scrollbar_mode(s_list, LV_SCROLLBAR_MODE_ACTIVE);
  s_rows = 0;

  if (s_tab == TAB_LIVE) {
    diag::Row rows[diag::MAX_ROWS + EXTRA];
    s_rows = allRows(rows, _core->gpsEnabled());
    for (int i = 0; i < s_rows; i++) {
      lv_obj_t* r = lv_obj_create(s_list);
      lv_obj_remove_style_all(r);
      lv_obj_remove_flag(r, LV_OBJ_FLAG_SCROLLABLE);
      lv_obj_set_size(r, LV_PCT(100), LV_SIZE_CONTENT);
      lv_obj_align(label(r, rows[i].label, THEME_FONT_BODY, theme::TEXT_MUTED), LV_ALIGN_LEFT_MID, 0, 0);
      s_vals[i] = label(r, rows[i].value, THEME_FONT_BODY, theme::TEXT);
      lv_obj_align(s_vals[i], LV_ALIGN_RIGHT_MID, 0, 0);
    }
    return;
  }
  diag::Line lines[diag::MAX_LINES];
  int n = s_tab == TAB_SYSTEM ? diag::systemLines(lines) : diag::fontLines(lines);
  for (int i = 0; i < n; i++) {
    lv_obj_t* l = label(s_list, lines[i], s_tab == TAB_FONT ? THEME_FONT_TITLE : THEME_FONT_BODY, theme::TEXT);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(l, LV_PCT(100));
  }
}

// From loop(), once a second: the Live values in place.
void UITask::refreshDiag() {
  using namespace diagview;
  if (_screen != SCR_DIAG || s_tab != TAB_LIVE || _nav_overlay) return;
  diag::Row rows[diag::MAX_ROWS + EXTRA];
  int n = allRows(rows, _core->gpsEnabled());
  if (n != s_rows) { buildDiag(); return; }
  for (int i = 0; i < n; i++)
    if (strcmp(lv_label_get_text(s_vals[i]), rows[i].value) != 0) lv_label_set_text(s_vals[i], rows[i].value);
}

void UITask::diagResetPopup() {
  lv_obj_t* panel = navPopupPanel("Reset counters?", false);
  lv_obj_t* t = label(panel, "Zeroes packet and error counts.", THEME_FONT_SMALL,
                      theme::TEXT_MUTED);
  lv_label_set_long_mode(t, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(t, LV_PCT(100));
  lv_obj_t* b = lv_button_create(panel);
  lv_obj_set_size(b, LV_PCT(100), 40);
  lv_obj_set_style_shadow_width(b, 0, 0);
  lv_obj_set_style_radius(b, theme::RADIUS, 0);
  lv_obj_set_style_bg_color(b, lv_color_hex(theme::FAIL), 0);
  lv_obj_add_event_cb(b, onDiagResetGo, LV_EVENT_CLICKED, NULL);
  lv_obj_center(label(b, LV_SYMBOL_REFRESH "  Reset", THEME_FONT_BODY, theme::TEXT));
}

void UITask::diagReset() {
  diag::resetCounters();
  navClosePopup();
  refreshDiag();
  showToast("Counters reset");
}

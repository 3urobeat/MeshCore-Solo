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
static const int EXTRA = 4;   // GPS, power, last reset, last crash (L2 only)
// A Live row's value labels: `in` / `out` for a packet count ("12/3" from
// ui-core), else `val` alone.
struct LiveVal { lv_obj_t* val; lv_obj_t* in; lv_obj_t* out; };
static LiveVal s_vals[diag::MAX_ROWS + EXTRA];
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
  extraRow(rows, n, "Power", "%s", board.isExternalPowered() ? "USB" : "battery");
#else
  (void)gps_on;
#endif
  extraRow(rows, n, "Last start", "%s", lvport::resetReason());
  char crash[32];
  if (lvport::crashSummary(crash, sizeof(crash))) extraRow(rows, n, "Last crash", "%s", crash);
  return n;
}

// ui-core's labels are cut for a 128 px OLED; here each gets a section and a
// full name. Packet rows are "received/sent" pairs, shown in two columns.
enum : uint8_t { SEC_DEVICE, SEC_PACKETS, SEC_RADIO, SEC_MEMORY, SEC_COUNT };
static const char* const SEC_TITLE[SEC_COUNT] = { "DEVICE", "PACKETS", "RADIO", "MEMORY" };
struct Name { const char* core; uint8_t sec; const char* text; bool pair; };
static const Name NAMES[] = {
  { "Uptime", SEC_DEVICE, "Uptime", false },          { "Last start", SEC_DEVICE, "Last start", false },
  { "Last crash", SEC_DEVICE, "Last crash", false },  { "Power", SEC_DEVICE, "Power", false },
  { "GPS", SEC_DEVICE, "GPS", false },
  { "Total rx/tx", SEC_PACKETS, "All", true },         { "Msg", SEC_PACKETS, "Messages", true },
  { "Advert", SEC_PACKETS, "Adverts", true },          { "Ack/Path", SEC_PACKETS, "Acks & paths", true },
  { "Other", SEC_PACKETS, "Other", true },             { "Forwarded", SEC_PACKETS, "Forwarded", false },
  { "Noise floor", SEC_RADIO, "Noise floor", false },  { "RSSI/SNR", SEC_RADIO, "Last packet", false },
  { "Queue", SEC_RADIO, "Send queue", false },         { "Errors", SEC_RADIO, "Errors", false },
  { "RXPS wd s/h", SEC_RADIO, "RX watchdog soft / hard", false },
  { "Heap free", SEC_MEMORY, "Heap free", false },     { "Stack free", SEC_MEMORY, "Stack free", false },
  { "Pool free", SEC_MEMORY, "Packet pool free", false },
};
static const Name* nameOf(const char* core) {
  for (const Name& n : NAMES) if (!strcmp(n.core, core)) return &n;
  return nullptr;
}

// A value as shown: "-60/40.0" -> "-60 dBm, SNR 40.0"; "12/456KB" -> "12 of 456 KB".
static void showValue(const char* core, const char* v, char* out, size_t n) {
  const char* slash = strchr(v, '/');
  if (!strcmp(core, "RSSI/SNR") && slash) snprintf(out, n, "%.*s dBm, SNR %s", (int)(slash - v), v, slash + 1);
  else if (!strcmp(core, "Heap free") && slash) snprintf(out, n, "%.*s of %s", (int)(slash - v), v, slash + 1);
  else snprintf(out, n, "%s", v);
  size_t l = strlen(out);   // "KB" / "B" units get their space
  if (l > 2 && !strcmp(out + l - 2, "KB") && out[l - 3] != ' ') snprintf(out + l - 2, n - (l - 2), " KB");
  else if (l > 1 && out[l - 1] == 'B' && out[l - 2] >= '0' && out[l - 2] <= '9') snprintf(out + l - 1, n - (l - 1), " B");
}

static void setLive(int i, const diag::Row& r) {
  LiveVal& lv = s_vals[i];
  if (lv.in) {
    const char* slash = strchr(r.value, '/');
    char a[12];
    snprintf(a, sizeof(a), "%.*s", slash ? (int)(slash - r.value) : (int)strlen(r.value), r.value);
    lv_label_set_text(lv.in, a);
    lv_label_set_text(lv.out, slash ? slash + 1 : "");
    return;
  }
  char v[40];
  showValue(r.label, r.value, v, sizeof(v));
  lv_label_set_text(lv.val, v);
  if (!strcmp(r.label, "Errors")) lv_obj_set_style_text_color(lv.val, lv_color_hex(strcmp(r.value, "OK") ? theme::FAIL : theme::OK), 0);
}

// A packets row: the name, then received and sent in fixed columns.
static const int COL_W = 56;
static lv_obj_t* pairLine(lv_obj_t* card, const char* name, lv_obj_t** in, lv_obj_t** out, bool head) {
  lv_obj_t* r = infoLine(card);
  lv_obj_t* k = label(r, name, THEME_FONT_SMALL, theme::TEXT_MUTED);
  lv_obj_align(k, LV_ALIGN_LEFT_MID, 0, 0);
  const lv_font_t* f = head ? THEME_FONT_SMALL : THEME_FONT_BODY;
  uint32_t col = head ? theme::TEXT_MUTED : theme::TEXT;
  *out = label(r, head ? "Sent" : "", f, col);
  lv_obj_set_width(*out, COL_W);
  lv_obj_set_style_text_align(*out, LV_TEXT_ALIGN_RIGHT, 0);
  lv_obj_align(*out, LV_ALIGN_RIGHT_MID, 0, 0);
  *in = label(r, head ? "Received" : "", f, col);
  lv_obj_set_width(*in, COL_W + 16);
  lv_obj_set_style_text_align(*in, LV_TEXT_ALIGN_RIGHT, 0);
  lv_obj_align(*in, LV_ALIGN_RIGHT_MID, -COL_W, 0);
  return r;
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

  s_list = scrollList(body);
  lv_obj_set_style_pad_row(s_list, 4, 0);
  s_rows = 0;

  if (s_tab == TAB_LIVE) {
    // Section by section, in ui-core's order within each.
    diag::Row rows[diag::MAX_ROWS + EXTRA];
    s_rows = allRows(rows, _core->gpsEnabled());
    for (uint8_t sec = 0; sec < SEC_COUNT; sec++) {
      lv_obj_t* card = nullptr;
      for (int i = 0; i < s_rows; i++) {
        const Name* nm = nameOf(rows[i].label);
        if ((nm ? nm->sec : SEC_DEVICE) != sec) continue;
        if (!card) {
          sectionTitle(s_list, SEC_TITLE[sec]);
          card = infoCard(s_list);
          if (sec == SEC_PACKETS) { lv_obj_t *a, *b; pairLine(card, "", &a, &b, true); }
        }
        s_vals[i] = { nullptr, nullptr, nullptr };
        if (nm && nm->pair) pairLine(card, nm->text, &s_vals[i].in, &s_vals[i].out, false);
        else s_vals[i].val = infoRow(card, nm ? nm->text : rows[i].label, "");
        setLive(i, rows[i]);
      }
    }
    return;
  }

  lv_obj_t* card = infoCard(s_list);
  if (s_tab == TAB_FONT) {   // "Latin ABCabc": the script, then its sample
    diag::Line lines[diag::MAX_LINES];
    int n = diag::fontLines(lines);
    for (int i = 0; i < n; i++) {
      char* sp = strchr(lines[i], ' ');
      if (sp) *sp = '\0';
      infoRow(card, lines[i], sp ? sp + 1 : "", theme::TEXT, THEME_FONT_TITLE);
    }
    return;
  }
  // System: this build and the radio settings.
  char v[48];
  diag::shortVersion(v, sizeof(v));
  infoRow(card, "Firmware", v);
  infoRow(card, "Built", FIRMWARE_BUILD_DATE);
#ifdef MESHCORE_VERSION
  infoRow(card, "MeshCore", MESHCORE_VERSION);
#endif
  infoRow(card, "Device", board.getManufacturerName());
  infoRow(card, "Node name", the_mesh.getNodeName());
  if (NodePrefs* p = the_mesh.getNodePrefs()) {
    sectionTitle(s_list, "RADIO");
    card = infoCard(s_list);
    snprintf(v, sizeof(v), "%.3f MHz", p->freq);
    infoRow(card, "Frequency", v);
    snprintf(v, sizeof(v), "%.1f kHz", p->bw);
    if (strstr(v, ".0 ")) snprintf(v, sizeof(v), "%.0f kHz", p->bw);
    infoRow(card, "Bandwidth", v);
    snprintf(v, sizeof(v), "%u", (unsigned)p->sf);
    infoRow(card, "Spreading factor", v);
    snprintf(v, sizeof(v), "4/%u", (unsigned)p->cr);
    infoRow(card, "Coding rate", v);
    snprintf(v, sizeof(v), "%d dBm", (int)p->tx_power_dbm);
    infoRow(card, "TX power", v);
  }
}

// From loop(), once a second: the Live values in place.
void UITask::refreshDiag() {
  using namespace diagview;
  if (_screen != SCR_DIAG || s_tab != TAB_LIVE || _nav_overlay) return;
  diag::Row rows[diag::MAX_ROWS + EXTRA];
  int n = allRows(rows, _core->gpsEnabled());
  if (n != s_rows) { buildDiag(); return; }
  for (int i = 0; i < n; i++) setLive(i, rows[i]);   // unchanged text isn't redrawn (UITask.cpp)
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

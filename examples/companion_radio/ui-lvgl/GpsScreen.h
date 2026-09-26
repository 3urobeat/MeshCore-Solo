#pragma once
// Home > GPS (also Settings > System > GPS details): what the receiver sees.
// A sky plot of the satellites in view (centre = overhead, rings at 30 and 60
// degrees elevation, filled = used in the fix, colour = signal), the fix
// state with time to first fix and the dilutions of precision, and a bar per
// satellite with its C/N0 (dB-Hz) -- the number that tells a weak antenna or
// a noisy board from a sky that just isn't visible. Refreshes every second.
//
// The data is helpers/sensors/GpsSky.h (-D GPS_SKYVIEW), fed by the board's
// NMEA stream; the sim feeds it made-up NMEA so the parser runs there too.
//
// Single-TU fragment: included by ui-lvgl/UITask.cpp after CompassScreen.h.

#include <helpers/sensors/GpsSky.h>

namespace gpsview {

static lv_obj_t* s_sky = nullptr;      // sky plot (drawn)
static lv_obj_t* s_bars = nullptr;     // signal bars (drawn, as wide as the satellites need)
static lv_obj_t* s_status = nullptr;
static lv_obj_t* s_used = nullptr;
static lv_obj_t* s_info = nullptr;
static lv_obj_t* s_systems = nullptr;
static lv_obj_t* s_note = nullptr;
static lv_obj_t* s_on_btn = nullptr;
static const int SKY = 150;
static const int BAR_PITCH = 26, BAR_W = 16, BARS_H = 104;

// Snapshot the screen draws from, sorted: used first, then by signal.
static GpsSky::Sat s_sats[GpsSky::MAX_SATS];
static bool s_used_flag[GpsSky::MAX_SATS];
static int s_n = 0;

// Fixed signal colours (not the accent: the user may pick green or red).
static uint32_t snrColor(int snr) {
  if (snr < 0)  return theme::TEXT_MUTED;
  if (snr < 20) return 0xE5534B;
  if (snr < 30) return 0xE5C04B;
  return 0x6FCF6F;
}

#if defined(SIM_PLATFORM) && defined(GPS_SKYVIEW)
// The sim has no receiver: a made-up sky, sent through the real parser as
// NMEA once a second. The fix comes 20 s after the screen first asks.
static GpsSky s_sim_sky;
static void simSentence(const char* body) {
  uint8_t cs = 0;
  for (const char* p = body; *p; p++) cs ^= (uint8_t)*p;
  char line[120];
  snprintf(line, sizeof(line), "$%s*%02X\r\n", body, cs);
  for (const char* p = line; *p; p++) s_sim_sky.feed(*p);
}
static void simFeed() {
  static uint32_t s_next = 0, s_t0 = 0;
  uint32_t now = millis();
  if (!s_t0) s_t0 = now;
  if ((int32_t)(now - s_next) < 0) return;
  s_next = now + 1000;
  struct SimSat { const char* talker; int prn, elev, az, snr; };
  static const SimSat SATS[] = {
    { "GP", 2, 72, 40, 44 }, { "GP", 5, 48, 120, 41 }, { "GP", 12, 31, 205, 36 }, { "GP", 15, 18, 290, 27 },
    { "GP", 20, 63, 310, 43 }, { "GP", 25, 9, 75, 18 }, { "GP", 29, 22, 160, 31 }, { "GP", 44, 30, 190, 33 },
    { "BD", 7, 55, 250, 38 }, { "BD", 10, 40, 20, 35 }, { "BD", 23, 12, 140, -1 }, { "BD", 37, 67, 95, 42 },
    { "GL", 71, 35, 330, 29 }, { "GL", 72, 50, 260, 34 }, { "GL", 80, 6, 30, -1 },
  };
  const int N = sizeof(SATS) / sizeof(SATS[0]);
  float t = (now - s_t0) / 1000.0f;
  bool fix = t > 20;
  char b[110];
  for (const char* talker : { "GP", "BD", "GL" }) {
    const SimSat* mine[8];
    int k = 0;
    for (int i = 0; i < N; i++) if (!strcmp(SATS[i].talker, talker)) mine[k++] = &SATS[i];
    int msgs = (k + 3) / 4;
    for (int m = 0; m < msgs; m++) {
      int o = snprintf(b, sizeof(b), "%sGSV,%d,%d,%02d", talker, msgs, m + 1, k);
      for (int j = m * 4; j < k && j < m * 4 + 4; j++) {
        const SimSat& s = *mine[j];
        int az = ((int)(s.az + t / 6) % 360);
        int snr = s.snr < 0 ? -1 : s.snr - (fix ? 0 : 8) + (int)(3 * sinf(t / 3 + s.prn));
        if (snr >= 0) o += snprintf(b + o, sizeof(b) - o, ",%02d,%02d,%03d,%02d", s.prn, s.elev, az, snr);
        else o += snprintf(b + o, sizeof(b) - o, ",%02d,%02d,%03d,", s.prn, s.elev, az);
      }
      simSentence(b);
    }
  }
  if (fix) {
    simSentence("GNGSA,A,3,02,05,12,15,20,29,,,,,,,1.6,0.9,1.3,1");
    simSentence("GNGSA,A,3,07,10,37,,,,,,,,,,1.6,0.9,1.3,4");
    simSentence("GNGSA,A,3,72,,,,,,,,,,,,1.6,0.9,1.3,2");
    simSentence("GNGGA,120000.00,5213.0000,N,02100.0000,E,1,10,0.9,112.4,M,34.5,M,,");
    simSentence("GNRMC,120000.00,A,5213.0000,N,02100.0000,E,0.4,87.0,260926,,,A");
  } else {
    simSentence("GNGSA,A,1,,,,,,,,,,,,,99.9,99.9,99.9,1");
    simSentence("GNGGA,120000.00,,,,,0,00,99.9,,,,,,");
    simSentence("GNRMC,120000.00,V,,,,,,,260926,,,N");
  }
}
#endif

// The receiver's data, or nullptr on a board without the parser.
static GpsSky* sky() {
#if defined(SEEED_WIO_TRACKER_L2) && defined(GPS_SKYVIEW)
  return &gps.sky();
#elif defined(SIM_PLATFORM) && defined(GPS_SKYVIEW)
  simFeed();
  return &s_sim_sky;
#else
  return nullptr;
#endif
}

static void snapshot(GpsSky& g) {
  g.expire();
  s_n = g.count;
  int idx[GpsSky::MAX_SATS];
  for (int i = 0; i < s_n; i++) idx[i] = i;
  // Insertion sort (<= 64): used first, then strongest; untracked last.
  for (int i = 1; i < s_n; i++) {
    int v = idx[i], j = i - 1;
    auto key = [&](int k) { return (g.used(g.sats[k]) ? 1000 : 0) + g.sats[k].snr; };
    while (j >= 0 && key(idx[j]) < key(v)) { idx[j + 1] = idx[j]; j--; }
    idx[j + 1] = v;
  }
  for (int i = 0; i < s_n; i++) { s_sats[i] = g.sats[idx[i]]; s_used_flag[i] = g.used(g.sats[idx[i]]); }
}

static void dot(lv_layer_t* layer, int32_t x, int32_t y, int32_t r, uint32_t color, bool filled) {
  lv_draw_rect_dsc_t d;
  lv_draw_rect_dsc_init(&d);
  d.radius = LV_RADIUS_CIRCLE;
  d.bg_color = lv_color_hex(color);
  d.bg_opa = filled ? LV_OPA_COVER : LV_OPA_TRANSP;
  d.border_color = lv_color_hex(color);
  d.border_width = 2;
  d.border_opa = LV_OPA_COVER;
  lv_area_t a = { x - r, y - r, x + r, y + r };
  lv_draw_rect(layer, &d, &a);
}

}  // namespace gpsview

static void onOpenGps(lv_event_t* e) { (void)e; s_ui->showGps(false); }
static void onOpenGpsFromSettings(lv_event_t* e) { (void)e; s_ui->showGps(true); }
static void onGpsTurnOn(lv_event_t* e) { (void)e; s_ui->setGps(true); s_ui->refreshGps(); }

// Sky plot: horizon ring, 30 / 60 degree rings, N-S / E-W lines, one dot per
// satellite at its azimuth (N up) and elevation (overhead in the centre).
static void onGpsSkyDraw(lv_event_t* e) {
  using namespace gpsview;
  lv_obj_t* o = (lv_obj_t*)lv_event_get_target(e);
  lv_layer_t* layer = lv_event_get_layer(e);
  lv_area_t a;
  lv_obj_get_coords(o, &a);
  int32_t cx = (a.x1 + a.x2) / 2, cy = (a.y1 + a.y2) / 2;
  int32_t r = SKY / 2 - 10;

  lv_draw_arc_dsc_t arc;
  lv_draw_arc_dsc_init(&arc);
  arc.center.x = cx; arc.center.y = cy;
  arc.start_angle = 0; arc.end_angle = 360;
  arc.color = lv_color_hex(theme::SURFACE_2);
  for (int el = 0; el < 90; el += 30) {
    arc.radius = (uint16_t)(r * (90 - el) / 90);
    arc.width = el == 0 ? 2 : 1;
    lv_draw_arc(layer, &arc);
  }
  lv_draw_line_dsc_t ld;
  lv_draw_line_dsc_init(&ld);
  ld.color = lv_color_hex(theme::SURFACE_2);
  ld.width = 1;
  ld.p1.x = cx; ld.p1.y = cy - r; ld.p2.x = cx; ld.p2.y = cy + r;
  lv_draw_line(layer, &ld);
  ld.p1.x = cx - r; ld.p1.y = cy; ld.p2.x = cx + r; ld.p2.y = cy;
  lv_draw_line(layer, &ld);

  lv_draw_label_dsc_t td;
  lv_draw_label_dsc_init(&td);
  td.font = THEME_FONT_SMALL;
  td.align = LV_TEXT_ALIGN_CENTER;
  static const char* CARD[4] = { "N", "E", "S", "W" };
  int32_t lh = lv_font_get_line_height(td.font);
  for (int i = 0; i < 4; i++) {
    float ang = i * (float)M_PI / 2;
    int32_t px = cx + (int32_t)(sinf(ang) * (r + 1)), py = cy - (int32_t)(cosf(ang) * (r + 1));
    td.text = CARD[i];
    td.color = lv_color_hex(i == 0 ? theme::ACCENT : theme::TEXT_MUTED);
    lv_area_t la = { px - 8, py - lh / 2, px + 8, py + lh / 2 };
    lv_draw_rect_dsc_t bg;   // a patch of background so the ring doesn't cross the letter
    lv_draw_rect_dsc_init(&bg);
    bg.bg_color = lv_color_hex(theme::BG);
    bg.radius = 4;
    lv_area_t ba = { px - 6, py - lh / 2 + 1, px + 6, py + lh / 2 - 1 };
    lv_draw_rect(layer, &bg, &ba);
    lv_draw_label(layer, &td, &la);
  }

  // Unused first, so the used ones sit on top where they overlap.
  for (int pass = 0; pass < 2; pass++) {
    for (int i = 0; i < s_n; i++) {
      const GpsSky::Sat& s = s_sats[i];
      if (s.elev < 0 || s.azim < 0 || s_used_flag[i] != (pass == 1)) continue;
      float rr = r * (90 - (s.elev > 90 ? 90 : s.elev)) / 90.0f;
      float ang = s.azim * (float)M_PI / 180.0f;
      dot(layer, cx + (int32_t)(sinf(ang) * rr), cy - (int32_t)(cosf(ang) * rr), 5, snrColor(s.snr), s_used_flag[i]);
    }
  }
}

// One bar per satellite: C/N0 on top, the bar (solid = used in the fix), the
// constellation letter and number under it. 50 dB-Hz fills the height.
static void onGpsBarsDraw(lv_event_t* e) {
  using namespace gpsview;
  lv_obj_t* o = (lv_obj_t*)lv_event_get_target(e);
  lv_layer_t* layer = lv_event_get_layer(e);
  lv_area_t a;
  lv_obj_get_coords(o, &a);
  lv_draw_label_dsc_t td;
  lv_draw_label_dsc_init(&td);
  td.font = THEME_FONT_SMALL;
  td.align = LV_TEXT_ALIGN_CENTER;
  int32_t lh = lv_font_get_line_height(td.font);
  int32_t base = a.y2 - lh - 2, top = a.y1 + lh + 2, full = base - top;

  lv_draw_line_dsc_t ld;   // the 30 dB-Hz line: above it, a good signal
  lv_draw_line_dsc_init(&ld);
  ld.color = lv_color_hex(theme::SURFACE_2);
  ld.width = 1;
  ld.dash_width = 3; ld.dash_gap = 3;
  ld.p1.x = a.x1; ld.p2.x = a.x2;
  ld.p1.y = ld.p2.y = base - full * 30 / 50;
  lv_draw_line(layer, &ld);

  for (int i = 0; i < s_n; i++) {
    const GpsSky::Sat& s = s_sats[i];
    int32_t x = a.x1 + i * BAR_PITCH + (BAR_PITCH - BAR_W) / 2;
    int snr = s.snr < 0 ? 0 : (s.snr > 50 ? 50 : s.snr);
    int32_t h = full * snr / 50;
    if (h < 2) h = 2;
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.radius = 3;
    d.bg_color = lv_color_hex(snrColor(s.snr));
    d.bg_opa = s_used_flag[i] ? LV_OPA_COVER : LV_OPA_30;
    d.border_color = d.bg_color;
    d.border_width = s_used_flag[i] ? 0 : 1;
    lv_area_t ba = { x, base - h, x + BAR_W - 1, base };
    lv_draw_rect(layer, &d, &ba);

    char t[8];
    if (s.snr >= 0) {
      snprintf(t, sizeof(t), "%d", s.snr);
      td.text = t;
      td.color = lv_color_hex(theme::TEXT);
      lv_area_t va = { x - 6, base - h - lh - 1, x + BAR_W + 5, base - h - 1 };
      lv_draw_label(layer, &td, &va);
    }
    char p[8];
    snprintf(p, sizeof(p), "%c%d", GpsSky::sysLetter(s.sys), s.prn);
    td.text = p;
    td.color = lv_color_hex(s_used_flag[i] ? theme::TEXT : theme::TEXT_MUTED);
    lv_area_t pa = { x - (BAR_PITCH - BAR_W) / 2, base + 2, x + BAR_W + (BAR_PITCH - BAR_W) / 2 - 1, base + 2 + lh };
    lv_draw_label(layer, &td, &pa);
  }
}

void UITask::showGps(bool from_settings) {
  _gps_from_settings = from_settings;
  _screen = SCR_GPS;
  buildGps();
}

void UITask::buildGps() {
  using namespace gpsview;
  lv_obj_t* body = newScreen("GPS", true);
  s_sky = s_bars = s_status = s_used = s_info = s_systems = s_note = s_on_btn = nullptr;

  // Sky plot left, the fix right.
  lv_obj_t* top = lv_obj_create(body);
  lv_obj_remove_style_all(top);
  lv_obj_set_size(top, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(top, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(top, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_set_style_pad_column(top, theme::GAP, 0);
  s_sky = lv_obj_create(top);
  styleSurface(s_sky, theme::BG);
  lv_obj_remove_flag(s_sky, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(s_sky, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(s_sky, SKY, SKY);
  lv_obj_add_event_cb(s_sky, onGpsSkyDraw, LV_EVENT_DRAW_MAIN_END, NULL);

  lv_obj_t* col = lv_obj_create(top);
  lv_obj_remove_style_all(col);
  lv_obj_set_flex_grow(col, 1);
  lv_obj_set_height(col, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(col, 2, 0);
  lv_obj_set_style_pad_top(col, 4, 0);
  s_status = label(col, "", THEME_FONT_LARGE, theme::TEXT);
  s_used = label(col, "", THEME_FONT_BODY, theme::TEXT);
  s_info = label(col, "", THEME_FONT_SMALL, theme::TEXT_MUTED);
  lv_obj_set_style_text_line_space(s_info, 2, 0);
  for (lv_obj_t* l : { s_used, s_info }) {
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(l, LV_PCT(100));
  }
  s_on_btn = lv_button_create(col);
  lv_obj_set_height(s_on_btn, 34);
  lv_obj_set_style_radius(s_on_btn, theme::RADIUS, 0);
  lv_obj_set_style_shadow_width(s_on_btn, 0, 0);
  stylePrimary(s_on_btn);
  lv_obj_center(label(s_on_btn, LV_SYMBOL_GPS "  Turn on", THEME_FONT_BODY, theme::TEXT));
  lv_obj_add_event_cb(s_on_btn, onGpsTurnOn, LV_EVENT_CLICKED, NULL);
  lv_obj_add_flag(s_on_btn, LV_OBJ_FLAG_HIDDEN);

  sectionTitle(body, "SIGNAL (dB-Hz)");
  lv_obj_t* sc = lv_obj_create(body);   // scrolls sideways when the satellites don't fit
  styleSurface(sc, theme::BG);
  lv_obj_set_size(sc, LV_PCT(100), BARS_H + 8);
  lv_obj_set_scroll_dir(sc, LV_DIR_HOR);
  lv_obj_set_scrollbar_mode(sc, LV_SCROLLBAR_MODE_AUTO);
  s_bars = lv_obj_create(sc);
  lv_obj_remove_style_all(s_bars);
  lv_obj_remove_flag(s_bars, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(s_bars, LV_OBJ_FLAG_EVENT_BUBBLE);
  lv_obj_set_size(s_bars, BAR_PITCH, BARS_H);
  lv_obj_add_event_cb(s_bars, onGpsBarsDraw, LV_EVENT_DRAW_MAIN_END, NULL);

  s_systems = label(body, "", THEME_FONT_SMALL, theme::TEXT_MUTED);
  lv_label_set_long_mode(s_systems, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(s_systems, LV_PCT(100));
  s_note = label(body, "Solid = used in the fix. Green is a strong signal (30+ dB-Hz); "
                 "indoors, below 25 a fix is unlikely.", THEME_FONT_SMALL, theme::TEXT_MUTED);
  lv_label_set_long_mode(s_note, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(s_note, LV_PCT(100));
  refreshGps();
}

// From loop(), once a second.
void UITask::refreshGps() {
  using namespace gpsview;
  if (_screen != SCR_GPS || !s_status) return;
  GpsSky* g = sky();
  bool off = _core->gpsAvailable() && !_core->gpsEnabled();
  bool data = g && g->last_ms && millis() - g->last_ms < 3000;
  if (off || !data) s_n = 0;
  else snapshot(*g);
  if (off) lv_obj_remove_flag(s_on_btn, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_add_flag(s_on_btn, LV_OBJ_FLAG_HIDDEN);

  char used[48] = "", info[200] = "";
  if (!g) {
    lv_label_set_text(s_status, "Not available");
    lv_obj_set_style_text_color(s_status, lv_color_hex(theme::TEXT_MUTED), 0);
  } else if (off) {
    lv_label_set_text(s_status, "GPS is off");
    lv_obj_set_style_text_color(s_status, lv_color_hex(theme::TEXT_MUTED), 0);
  } else if (!data) {
    lv_label_set_text(s_status, "No data");
    lv_obj_set_style_text_color(s_status, lv_color_hex(theme::FAIL), 0);
#if defined(SEEED_WIO_TRACKER_L2)
    snprintf(info, sizeof(info), "Nothing from the receiver\n(%lu bytes so far)", (unsigned long)gps.rxChars());
#else
    snprintf(info, sizeof(info), "Nothing from the receiver");
#endif
  } else {
    int tracked = 0, used_n = 0;
    for (int i = 0; i < s_n; i++) { if (s_sats[i].snr >= 0) tracked++; if (s_used_flag[i]) used_n++; }
    if (g->hasFix()) {
      lv_label_set_text(s_status, g->fix_mode == 2 ? "2D fix" : "3D fix");
      lv_obj_set_style_text_color(s_status, lv_color_hex(0x6FCF6F), 0);
    } else {
      lv_label_set_text(s_status, "Searching");
      lv_obj_set_style_text_color(s_status, lv_color_hex(theme::ACCENT), 0);
    }
    snprintf(used, sizeof(used), "%d of %d used", g->hasFix() ? (used_n ? used_n : g->sats_used) : 0, s_n);
    int o = snprintf(info, sizeof(info), "%d with a signal\n", tracked);
    uint32_t since = g->start_ms ? (millis() - g->start_ms) / 1000 : 0;
    if (g->ttff_ms) o += snprintf(info + o, sizeof(info) - o, "First fix after %lu s\n", (unsigned long)(g->ttff_ms / 1000));
    else o += snprintf(info + o, sizeof(info) - o, "Searching for %lu:%02lu\n", (unsigned long)(since / 60), (unsigned long)(since % 60));
    if (g->hasFix() && g->hdop > 0)
      o += snprintf(info + o, sizeof(info) - o, "DOP  H %.1f  V %.1f  P %.1f\n", g->hdop, g->vdop, g->pdop);
    int32_t lat, lon;
    if (g->hasFix() && _core->course.currentLocation(lat, lon))
      o += snprintf(info + o, sizeof(info) - o, "%.5f, %.5f\n", lat / 1e6, lon / 1e6);
    if (g->hasFix() && g->alt_valid)
      o += snprintf(info + o, sizeof(info) - o, "%.0f m  -  %.1f km/h\n", g->alt_m, g->speed_kmh);
    if (g->utc_valid) o += snprintf(info + o, sizeof(info) - o, "UTC %02u:%02u:%02u", g->utc_h, g->utc_m, g->utc_s);
  }
  lv_label_set_text(s_used, used);
  lv_label_set_text(s_info, info);

  // Per constellation: used / in view.
  char sys[160] = "";
  int o = 0;
  for (uint8_t k = 0; k < GpsSky::SYS_COUNT; k++) {
    int inview = 0, u = 0;
    for (int i = 0; i < s_n; i++) if (s_sats[i].sys == k) { inview++; if (s_used_flag[i]) u++; }
    if (inview) o += snprintf(sys + o, sizeof(sys) - o, "%s%c %s %d/%d", o ? "   " : "", GpsSky::sysLetter(k),
                              GpsSky::sysName(k), u, inview);
  }
  lv_label_set_text(s_systems, sys);

  int32_t w = s_n * BAR_PITCH;
  if (w < BAR_PITCH) w = BAR_PITCH;
  lv_obj_set_width(s_bars, w);
  lv_obj_invalidate(s_sky);
  lv_obj_invalidate(s_bars);
}

#pragma once
// Navigation map (Home > Map): one of the map screen's two modes. It shows only
// what you navigate by: saved waypoints, people sharing their position live,
// the recorded trail, and the Locator's active target with a line to it and a
// bar with distance, bearing, course and ETA. The Nodes map (Nearby > map
// button) is the one with every contact.
//
// Everything shown lives in the UI Core (waypoints, locator target, live
// shares, trail), so ui-new's Trail / Waypoints / Locator screens and this map
// work on the same data. Here: the pin button opens the waypoint list (add at
// the GPS position or by coordinates; tap one for its menu: navigate, rename,
// share, delete), a long-press on the map offers that spot as a waypoint or a
// target, the bar opens the target list.
//
// Single-TU fragment: included by ui-lvgl/UITask.cpp after MapScreen.h.

namespace navmap {

enum : uint8_t { WP_NAV, WP_RENAME, WP_SHARE, WP_DELETE };

// Trail: normalised Web-Mercator coords (0..1) cached when the trail is
// re-read, so a pan only scales and offsets them. One lv_line per recorded
// segment (a pause starts a new one); extra segments join the last.
static const int TRAIL_SEGS = 8;
// Stored as float offsets from the first point (s_ox/s_oy): a float holds a
// small offset exactly enough, but not a whole 0..1 coordinate at street zoom
// (2^18 * 256 px across -- a float is off by pixels there).
static float* s_nx = nullptr;   // TrailStore::CAPACITY each, in PSRAM (first trail drawn)
static float* s_ny = nullptr;
static double s_ox = 0, s_oy = 0;
static lv_point_precise_t* s_pts = nullptr;
static int s_seg_first[TRAIL_SEGS], s_seg_len[TRAIL_SEGS], s_segs = 0;
static lv_obj_t* s_trail[TRAIL_SEGS];
static lv_obj_t* s_target_line = nullptr;
static lv_point_precise_t s_target_pts[2];
static lv_obj_t* s_target_ring = nullptr;
static lv_obj_t* s_target_dot = nullptr;
static const int RING_D = 30;   // clear of a 12 px marker dot inside it
static navview::EtaTracker s_eta;
static GpsAverager s_avg;   // "mark here" with Settings > Waypoint averaging
static TrackBack   s_tb;    // walking the trail back: its breadcrumb is the target
static const uint32_t TRAIL_COLOR = 0x4FA3FF;

static void fmtDuration(char* b, int n, uint32_t secs) {
  if (secs >= 3600) snprintf(b, n, "%luh%02lu", (unsigned long)(secs / 3600), (unsigned long)(secs % 3600 / 60));
  else snprintf(b, n, "%lum", (unsigned long)((secs + 59) / 60));
}

// Web-Mercator 0..1. double: at z18 one pixel is 1/67M of the world.
static double normX(int32_t lon_e6) { return (lon_e6 / 1e6 + 180.0) / 360.0; }
static double normY(int32_t lat_e6) {
  double r = lat_e6 / 1e6 * M_PI / 180.0;
  return (1.0 - asinh(tan(r)) / M_PI) / 2.0;
}

// Waypoint labels are 11 bytes of UTF-8 (Waypoint.h): reject input past that,
// so a two-byte letter is never cut in half.
static void onLabelInsert(lv_event_t* e) {
  lv_obj_t* ta = (lv_obj_t*)lv_event_get_target(e);
  const char* ins = (const char*)lv_event_get_param(e);
  if (ins && strlen(lv_textarea_get_text(ta)) + strlen(ins) > WAYPOINT_LABEL_LEN - 1)
    lv_textarea_set_insert_replace(ta, "");
}

}  // namespace navmap

static void onNavList(lv_event_t* e)    { (void)e; s_ui->navTargetsPopup(); }
static void onNavMark(lv_event_t* e)    { (void)e; s_ui->navWaypointsPopup(); }
static void onNavAvgCancel(lv_event_t* e) { (void)e; s_ui->navAveragingCancel(); }
static void onNavClear(lv_event_t* e)   { (void)e; s_ui->navPick(navmap::code(navmap::T_CLEAR, 0)); }
static void onNavPick(lv_event_t* e)    { s_ui->navPick((int)(uintptr_t)lv_event_get_user_data(e)); }
static void onNavWpMenu(lv_event_t* e)  { s_ui->navWaypointMenu((int)(uintptr_t)lv_event_get_user_data(e)); }
static void onNavWpAction(lv_event_t* e){ s_ui->navWaypointAction((uint8_t)(uintptr_t)lv_event_get_user_data(e)); }
static void onNavPopupClose(lv_event_t* e) { (void)e; s_ui->navClosePopup(); }
static void onNavRenameKb(lv_event_t* e) { s_ui->navRenameDone(lv_event_get_code(e) == LV_EVENT_READY); }

// ── Layers and controls (built by buildMap in nav mode) ───────────────────────

void UITask::buildNavLayers() {
  for (int i = 0; i < navmap::TRAIL_SEGS; i++) {
    lv_obj_t* l = lv_line_create(_map_marks);
    lv_obj_set_size(l, LV_PCT(100), LV_PCT(100));   // clip to the view, whatever the points
    lv_obj_set_style_line_width(l, 3, 0);
    lv_obj_set_style_line_color(l, lv_color_hex(navmap::TRAIL_COLOR), 0);
    lv_obj_set_style_line_rounded(l, true, 0);
    lv_obj_remove_flag(l, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(l, LV_OBJ_FLAG_HIDDEN);
    navmap::s_trail[i] = l;
  }
  navmap::s_target_line = lv_line_create(_map_marks);
  lv_obj_set_size(navmap::s_target_line, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_line_width(navmap::s_target_line, 2, 0);
  lv_obj_set_style_line_color(navmap::s_target_line, lv_color_hex(theme::ACCENT), 0);
  lv_obj_set_style_line_dash_width(navmap::s_target_line, 8, 0);
  lv_obj_set_style_line_dash_gap(navmap::s_target_line, 6, 0);
  lv_obj_remove_flag(navmap::s_target_line, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(navmap::s_target_line, LV_OBJ_FLAG_HIDDEN);

  navmap::s_target_ring = lv_obj_create(_map_marks);
  lv_obj_remove_style_all(navmap::s_target_ring);
  lv_obj_set_size(navmap::s_target_ring, navmap::RING_D, navmap::RING_D);
  lv_obj_set_style_radius(navmap::s_target_ring, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_border_color(navmap::s_target_ring, lv_color_hex(theme::ACCENT), 0);
  lv_obj_set_style_border_width(navmap::s_target_ring, 3, 0);
  lv_obj_remove_flag(navmap::s_target_ring, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(navmap::s_target_ring, LV_OBJ_FLAG_HIDDEN);
  // Centre dot, for a target no marker stands on (a map point, a message
  // position, a person whose live share went stale: last known place).
  navmap::s_target_dot = lv_obj_create(navmap::s_target_ring);
  lv_obj_remove_style_all(navmap::s_target_dot);
  lv_obj_set_size(navmap::s_target_dot, 8, 8);
  lv_obj_set_style_radius(navmap::s_target_dot, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(navmap::s_target_dot, lv_color_hex(theme::ACCENT), 0);
  lv_obj_set_style_bg_opa(navmap::s_target_dot, LV_OPA_COVER, 0);
  lv_obj_remove_flag(navmap::s_target_dot, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_center(navmap::s_target_dot);
  navmap::s_segs = 0;
}

void UITask::buildNavControls(lv_obj_t* body) {
  // The target list opens from the bar; the left column: back, mark the spot, tools.
  lv_obj_align(mapButton(body, UI_SYMBOL_PIN, onNavMark), LV_ALIGN_TOP_LEFT, 6, 52);

  _nav_bar = lv_button_create(body);   // tap: the target list
  lv_obj_set_size(_nav_bar, LV_PCT(100), navmap::BAR_H);
  lv_obj_align(_nav_bar, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_set_style_radius(_nav_bar, 0, 0);
  lv_obj_set_style_shadow_width(_nav_bar, 0, 0);
  lv_obj_set_style_bg_color(_nav_bar, lv_color_hex(theme::BG), 0);
  lv_obj_set_style_bg_opa(_nav_bar, LV_OPA_90, 0);
  lv_obj_set_style_bg_color(_nav_bar, lv_color_hex(theme::SURFACE_2), LV_STATE_PRESSED);
  lv_obj_set_style_border_side(_nav_bar, LV_BORDER_SIDE_TOP, 0);
  lv_obj_set_style_border_color(_nav_bar, lv_color_hex(theme::SURFACE_2), 0);
  lv_obj_set_style_border_width(_nav_bar, 1, 0);
  lv_obj_set_style_pad_hor(_nav_bar, theme::PAD, 0);
  lv_obj_set_style_pad_ver(_nav_bar, 3, 0);
  lv_obj_add_event_cb(_nav_bar, onNavList, LV_EVENT_CLICKED, NULL);
  _nav_title = label(_nav_bar, "", THEME_FONT_BODY, theme::ACCENT);
  lv_label_set_long_mode(_nav_title, LV_LABEL_LONG_DOT);
  lv_obj_set_size(_nav_title, 260, 19);   // fixed height: LONG_DOT cuts instead of wrapping
  lv_obj_align(_nav_title, LV_ALIGN_TOP_LEFT, 0, 0);
  _nav_info = label(_nav_bar, "", THEME_FONT_SMALL, theme::TEXT);
  lv_label_set_long_mode(_nav_info, LV_LABEL_LONG_DOT);
  lv_obj_set_size(_nav_info, 270, 16);
  lv_obj_align(_nav_info, LV_ALIGN_BOTTOM_LEFT, 0, 0);
  _nav_clear = lv_button_create(_nav_bar);
  lv_obj_set_size(_nav_clear, 34, 34);
  lv_obj_align(_nav_clear, LV_ALIGN_RIGHT_MID, 4, 0);
  lv_obj_set_style_shadow_width(_nav_clear, 0, 0);
  lv_obj_set_style_radius(_nav_clear, theme::RADIUS, 0);
  lv_obj_set_style_bg_color(_nav_clear, lv_color_hex(theme::SURFACE), 0);
  lv_obj_add_event_cb(_nav_clear, onNavClear, LV_EVENT_CLICKED, NULL);
  lv_obj_center(label(_nav_clear, LV_SYMBOL_CLOSE, THEME_FONT_BODY, theme::TEXT));
  _nav_avg_pill = mapPill(body, "");   // tap: cancel averaging
  lv_obj_set_style_text_color(_nav_avg_pill, lv_color_hex(theme::ACCENT), 0);
  lv_obj_set_style_pad_all(_nav_avg_pill, 8, 0);
  lv_obj_align(_nav_avg_pill, LV_ALIGN_CENTER, 0, -30);
  lv_obj_add_flag(_nav_avg_pill, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(_nav_avg_pill, onNavAvgCancel, LV_EVENT_CLICKED, NULL);
  lv_obj_add_flag(_nav_avg_pill, LV_OBJ_FLAG_HIDDEN);
  _nav_rec = mapPill(body, "");   // trail recording / live share running
  lv_obj_set_style_text_color(_nav_rec, lv_color_hex(theme::FAIL), 0);
  lv_obj_align(_nav_rec, LV_ALIGN_TOP_MID, 0, 40);
  lv_obj_add_flag(_nav_rec, LV_OBJ_FLAG_HIDDEN);
  _next_nav_bar_ms = 0;
  navmap::s_eta.reset();
  refreshNavBar();
}

// ── Markers, trail, target (rebuilt every few seconds, laid out on every pan) ─

void UITask::rebuildNavMarkers() {
  const WaypointModel& wp = _core->waypoints;
  for (int i = 0; i < wp.count(); i++) {
    const Waypoint& w = wp.at(i);
    char t[WAYPOINT_LABEL_LEN + 8];
    snprintf(t, sizeof(t), UI_SYMBOL_FLAG " %s", w.label[0] ? w.label : "?");
    addMapMark(mapview::MK_WAYPOINT, i, w.lat_1e6, w.lon_1e6, theme::ACCENT, t);
  }
  const LiveTrackStore& lt = _core->live_share.track();
  uint32_t now = rtc_clock.getCurrentTime();
  for (int i = 0; i < LiveTrackStore::CAPACITY; i++) {
    if (!lt.isActive(i, now)) continue;
    const LiveTrackStore::Entry& e = lt.slotAt(i);
    char age[8], t[LiveTrackStore::NAME_LEN + 12];
    geo::fmtAgeShort(age, sizeof(age), now, e.ts);
    snprintf(t, sizeof(t), "%s%s%s", e.name, age[0] ? "  " : "", age);
    addMapMark(mapview::MK_LIVE, i, e.lat_1e6, e.lon_1e6, theme::OK, t);
  }

  // Trail -> normalised points in segments.
  TrailStore& ts = _core->trail.store();
  navmap::s_segs = 0;
  int n = ts.count();
  if (!navmap::s_pts) {
    navmap::s_nx = psramBuf<float>(TrailStore::CAPACITY);
    navmap::s_ny = psramBuf<float>(TrailStore::CAPACITY);
    navmap::s_pts = psramBuf<lv_point_precise_t>(TrailStore::CAPACITY);
  }
  if (!navmap::s_nx || !navmap::s_ny || !navmap::s_pts) n = 0;
  for (int i = 0; i < n; i++) {
    const TrailPoint& p = ts.at(i);
    if (i == 0) { navmap::s_ox = navmap::normX(p.lon_1e6); navmap::s_oy = navmap::normY(p.lat_1e6); }
    navmap::s_nx[i] = (float)(navmap::normX(p.lon_1e6) - navmap::s_ox);
    navmap::s_ny[i] = (float)(navmap::normY(p.lat_1e6) - navmap::s_oy);
    bool new_seg = i == 0 || ((p.flags & TRAIL_FLAG_SEG_START) && navmap::s_segs < navmap::TRAIL_SEGS);
    if (new_seg) {
      navmap::s_seg_first[navmap::s_segs] = i;
      navmap::s_seg_len[navmap::s_segs] = 0;
      navmap::s_segs++;
    }
    navmap::s_seg_len[navmap::s_segs - 1]++;
  }
}

void UITask::layoutNav() {
  if (!navmap::s_target_line) return;
  double scale = (double)(1 << _map_z) * mapview::TILE_PX;
  for (int s = 0; s < navmap::TRAIL_SEGS; s++) {
    lv_obj_t* l = navmap::s_trail[s];
    if (s >= navmap::s_segs || navmap::s_seg_len[s] < 2) { lv_obj_add_flag(l, LV_OBJ_FLAG_HIDDEN); continue; }
    int f = navmap::s_seg_first[s], len = navmap::s_seg_len[s];
    for (int i = f; i < f + len; i++) {
      navmap::s_pts[i].x = (lv_value_precise_t)lround((navmap::s_ox + navmap::s_nx[i]) * scale - mapview::s_left);
      navmap::s_pts[i].y = (lv_value_precise_t)lround((navmap::s_oy + navmap::s_ny[i]) * scale - mapview::s_top);
    }
    lv_line_set_points(l, &navmap::s_pts[f], len);
    lv_obj_remove_flag(l, LV_OBJ_FLAG_HIDDEN);
  }

  int32_t tlat, tlon, mlat, mlon;
  char name[8];
  bool person, set;
  bool target = navCurrentTarget(tlat, tlon, name, sizeof(name), person, set);
  if (target) {
    int tx = (int)lround(navmap::normX(tlon) * scale - mapview::s_left);
    int ty = (int)lround(navmap::normY(tlat) * scale - mapview::s_top);
    lv_obj_set_pos(navmap::s_target_ring, tx - navmap::RING_D / 2, ty - navmap::RING_D / 2);
    lv_obj_remove_flag(navmap::s_target_ring, LV_OBJ_FLAG_HIDDEN);
    bool marked = false;   // a waypoint / live marker already sits on the target
    const int half = mapview::MARK_D / 2;
    for (int k = 0; k < mapview::s_mark_count && !marked; k++) {
      lv_obj_t* m = mapview::s_marks[k].obj;
      marked = abs(lv_obj_get_x(m) + half - tx) <= 3 && abs(lv_obj_get_y(m) + lv_obj_get_y(mapview::s_marks[k].dot) + half - ty) <= 3;
    }
    if (marked) lv_obj_add_flag(navmap::s_target_dot, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_remove_flag(navmap::s_target_dot, LV_OBJ_FLAG_HIDDEN);
    if (_core->course.currentLocation(mlat, mlon)) {
      navmap::s_target_pts[0].x = (lv_value_precise_t)lround(navmap::normX(mlon) * scale - mapview::s_left);
      navmap::s_target_pts[0].y = (lv_value_precise_t)lround(navmap::normY(mlat) * scale - mapview::s_top);
      navmap::s_target_pts[1].x = tx;
      navmap::s_target_pts[1].y = ty;
      lv_line_set_points(navmap::s_target_line, navmap::s_target_pts, 2);
      lv_obj_remove_flag(navmap::s_target_line, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(navmap::s_target_line, LV_OBJ_FLAG_HIDDEN);
    }
  } else {
    lv_obj_add_flag(navmap::s_target_ring, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(navmap::s_target_line, LV_OBJ_FLAG_HIDDEN);
  }
}

// Bottom bar: the target, then distance / bearing / own course / ETA.
void UITask::refreshNavBar() {
  if (!_nav_bar) return;
  refreshNavTools();
  {
    char t[48] = "";
    int o = 0;
    TrailStore& ts = _core->trail.store();
    if (ts.isActive()) {
      char d[12];
      geo::fmtDist(d, sizeof(d), ts.totalDistanceMeters() / 1000.0f, _prefs && _prefs->units_imperial);
      o += snprintf(t + o, sizeof(t) - o, "%s %s", ts.isPaused() ? "PAUSED" : "REC", d);
    }
    if (_prefs && _prefs->loc_share_enabled) {
      char left[12];
      navmap::fmtDuration(left, sizeof(left), _core->live_share.remainingSecs());
      o += snprintf(t + o, sizeof(t) - o, "%sLIVE %s", o ? "  " : "", left);
    }
    lv_label_set_text(_nav_rec, t);
    if (o) lv_obj_remove_flag(_nav_rec, LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(_nav_rec, LV_OBJ_FLAG_HIDDEN);
  }
  int32_t tlat, tlon;
  char name[40];
  bool person = false, set = false;
  bool pos = navCurrentTarget(tlat, tlon, name, sizeof(name), person, set);
  if (!set) {
    lv_label_set_text(_nav_title, "No target");
    lv_obj_set_style_text_color(_nav_title, lv_color_hex(theme::TEXT_MUTED), 0);
    lv_label_set_text(_nav_info, "Tap here to pick a target, or hold the map on a spot");
    lv_obj_add_flag(_nav_clear, LV_OBJ_FLAG_HIDDEN);
    return;
  }
  lv_obj_set_style_text_color(_nav_title, lv_color_hex(theme::ACCENT), 0);
  lv_obj_remove_flag(_nav_clear, LV_OBJ_FLAG_HIDDEN);
  lv_label_set_text_fmt(_nav_title, "%s %s", navmap::s_tb.active() ? LV_SYMBOL_LOOP : person ? UI_SYMBOL_USERS : UI_SYMBOL_FLAG,
                        name);
  if (!pos) {
    lv_label_set_text(_nav_info, "Position unknown (not shared recently)");
    return;
  }
  int32_t lat, lon;
  if (!_core->course.currentLocation(lat, lon)) {
    lv_label_set_text(_nav_info, _core->gpsEnabled() || !_core->gpsAvailable()
                                 ? "Waiting for a GPS fix..." : "GPS is off  -  tap the crosshair to turn it on");
    return;
  }
  bool imperial = _prefs->units_imperial;
  float km = geo::haversineKm(lat, lon, tlat, tlon);
  navmap::s_eta.update(km, millis());
  char dist[12], eta[12], hdg[16] = "";
  geo::fmtDist(dist, sizeof(dist), km, imperial);
  int to = geo::bearingDeg(lat, lon, tlat, tlon);
  int cog;
  if (_core->course.currentCourse(cog)) snprintf(hdg, sizeof(hdg), "  -  you %d\xC2\xB0", cog);
  if (navmap::s_eta.eta(km, eta, sizeof(eta)))
    lv_label_set_text_fmt(_nav_info, "%s  %d\xC2\xB0 %s%s  -  ETA %s", dist, to, geo::bearingCardinal(to), hdg, eta);
  else
    lv_label_set_text_fmt(_nav_info, "%s  %d\xC2\xB0 %s%s", dist, to, geo::bearingCardinal(to), hdg);
}

// ── Target selection ──────────────────────────────────────────────────────────

// What the map navigates to: the track-back breadcrumb while walking the trail
// back, else the Locator target. `set`: there is a target at all; returns
// whether its position is known.
bool UITask::navCurrentTarget(int32_t& lat, int32_t& lon, char* name, int n, bool& person, bool& set) {
  person = false;
  TrailStore& ts = _core->trail.store();
  if (navmap::s_tb.active() && navmap::s_tb.index() < ts.count()) {
    set = true;
    navmap::s_tb.label(name, n);
    lat = ts.at(navmap::s_tb.index()).lat_1e6;
    lon = ts.at(navmap::s_tb.index()).lon_1e6;
    return true;
  }
  set = _prefs && _prefs->locator_has_target;
  if (!set) return false;
  person = _prefs->locator_target_kind != 0;
  snprintf(name, n, "%s", _prefs->locator_label[0] ? _prefs->locator_label : "Target");
  return _core->locator.activeTargetPos(lat, lon);
}

// From loop(), once a second on any screen: advance along the trail.
void UITask::navPollTrackBack() {
  if (!navmap::s_tb.active()) return;
  int32_t lat = 0, lon = 0;
  bool fix = _core->course.currentLocation(lat, lon);
  TrackBack::Step st = navmap::s_tb.poll(_core->trail.store(), fix, lat, lon);
  if (st == TrackBack::ADVANCED) navmap::s_eta.reset();
  else if (st == TrackBack::ARRIVED) showToast("Back at the trail start", 4000);
  if (st != TrackBack::NONE && _screen == SCR_MAP && _map_nav) { layoutMap(); refreshNavBar(); }
}

// Frame own position and the target together (or just centre the target).
void UITask::navFrameTarget() {
  int32_t tlat, tlon, lat, lon;
  char name[8];
  bool person, set;
  if (!navCurrentTarget(tlat, tlon, name, sizeof(name), person, set)) return;
  _map_follow = false;
  int w = _map_area ? lv_obj_get_width(_map_area) : 320;
  int h = (_map_area ? lv_obj_get_height(_map_area) : 218) - navmap::BAR_H;
  double tx = navmap::normX(tlon), ty = navmap::normY(tlat);
  double cx = tx, cy = ty;
  int z = _map_z < 14 ? 14 : _map_z;
  if (_core->course.currentLocation(lat, lon)) {
    double mx = navmap::normX(lon), my = navmap::normY(lat);
    cx = (tx + mx) / 2; cy = (ty + my) / 2;
    double dx = fabs(tx - mx) * mapview::TILE_PX, dy = fabs(ty - my) * mapview::TILE_PX;
    z = mapview::MAX_Z - 1;
    while (z > mapview::MIN_Z && (dx * (1 << z) > w - 150 || dy * (1 << z) > h - 110)) z--;   // clear of the buttons
  }
  _map_z = z;
  double n = (double)(1 << z);
  _map_cx = cx * n;
  _map_cy = cy * n + navmap::BAR_H / 2.0 / mapview::TILE_PX;   // keep it clear of the bar
  layoutMap();
}

void UITask::navSetTarget(uint8_t kind, const uint8_t* key, int32_t lat, int32_t lon, const char* name) {
  navmap::s_tb.stop();   // a chosen target replaces walking the trail back
  _core->locator.setTarget(kind, key, lat, lon, name);
  the_mesh.savePrefs();
  navmap::s_eta.reset();
  char t[40];
  snprintf(t, sizeof(t), "Navigating to %s", name);
  showToast(t);
}

void UITask::navPick(int code) {
  uint8_t type = (uint8_t)(code >> 8);
  int idx = code & 0xFF;
  switch (type) {
    case navmap::T_CLEAR:
      if (navmap::s_tb.active()) { navmap::s_tb.stop(); showToast("Track back stopped"); break; }
      _core->locator.clearTarget();
      the_mesh.savePrefs();
      showToast("Target cleared");
      break;
    case navmap::T_WAYPOINT: {
      if (idx >= _core->waypoints.count()) return;
      const Waypoint& w = _core->waypoints.at(idx);
      navSetTarget(0, nullptr, w.lat_1e6, w.lon_1e6, w.label[0] ? w.label : "Waypoint");
      break;
    }
    case navmap::T_TRAILSTART: {
      TrailStore& ts = _core->trail.store();
      if (ts.empty()) return;
      navSetTarget(0, nullptr, ts.first().lat_1e6, ts.first().lon_1e6, "Trail start");
      break;
    }
    case navmap::T_LIVE: {
      const LiveTrackStore& lt = _core->live_share.track();
      if (idx >= LiveTrackStore::CAPACITY || !lt.isActive(idx, rtc_clock.getCurrentTime())) return;
      const LiveTrackStore::Entry& e = lt.slotAt(idx);
      // Followed as the person moves: a verified (DM) share by key, a channel
      // share by sender name.
      navSetTarget(e.verified ? 1 : 2, e.verified ? e.key : nullptr, e.lat_1e6, e.lon_1e6, e.name);
      break;
    }
  }
  navClosePopup();
  if (_screen != SCR_MAP || !_map_nav) { openMap(true); }
  navFrameTarget();
  refreshNavBar();
}

// Node detail > Navigate: a contact as the target (followed by key).
void UITask::navToNode(const uint8_t* key, int32_t lat, int32_t lon, const char* name) {
  navSetTarget(1, key, lat, lon, name);
  openMap(true);
  navFrameTarget();
  refreshNavBar();
}

// ── Popups: target list, waypoint menu, rename ────────────────────────────────

lv_obj_t* UITask::navPopupPanel(const char* title, bool full) {
  navClosePopup();
  _nav_overlay = lv_obj_create(lv_screen_active());
  lv_obj_remove_style_all(_nav_overlay);
  lv_obj_set_size(_nav_overlay, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_bg_color(_nav_overlay, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(_nav_overlay, LV_OPA_60, 0);
  lv_obj_add_flag(_nav_overlay, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_flag(_nav_overlay, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* panel = lv_obj_create(_nav_overlay);
  int w = lv_display_get_horizontal_resolution(NULL) - 16;
  if (full) {
    lv_obj_set_size(panel, w, lv_display_get_vertical_resolution(NULL) - theme::STATUS_H - 12);
    lv_obj_set_pos(panel, 8, theme::STATUS_H + 6);
  } else {   // as tall as its content, scrolling past the screen's height
    lv_obj_set_size(panel, w, LV_SIZE_CONTENT);
    lv_obj_set_style_max_height(panel, lv_display_get_vertical_resolution(NULL) - theme::STATUS_H - 12, 0);
    lv_obj_align(panel, LV_ALIGN_CENTER, 0, theme::STATUS_H / 2);
  }
  lv_obj_set_style_bg_color(panel, lv_color_hex(theme::BG), 0);
  lv_obj_set_style_border_color(panel, lv_color_hex(theme::ACCENT), 0);
  lv_obj_set_style_border_width(panel, 1, 0);
  lv_obj_set_style_radius(panel, theme::RADIUS, 0);
  lv_obj_set_style_pad_all(panel, theme::PAD, 0);
  lv_obj_set_style_pad_row(panel, 6, 0);
  lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
  if (full) lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);   // its own list scrolls

  lv_obj_t* hdr = lv_obj_create(panel);
  styleSurface(hdr, theme::BG);
  lv_obj_remove_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(hdr, LV_PCT(100), 28);
  lv_obj_t* t = label(hdr, title, THEME_FONT_TITLE, theme::TEXT);
  lv_label_set_long_mode(t, LV_LABEL_LONG_DOT);
  lv_obj_set_width(t, 230);
  lv_obj_align(t, LV_ALIGN_LEFT_MID, 0, 0);
  headerButton(hdr, LV_SYMBOL_CLOSE, onNavPopupClose, 0, NULL);
  return panel;
}

void UITask::navClosePopup() {
  if (_nav_overlay) lv_obj_delete_async(_nav_overlay);   // may be closing from its own button
  _nav_overlay = _nav_ta = _nav_kb = _nav_del_lbl = nullptr;
  _nav_trail_lbl = _nav_trail_btn = _nav_reset_lbl = _nav_share_lbl = _nav_share_btn = _nav_tb_btn = nullptr;
  _nav_del_armed_ms = 0;
}

static void navRowRight(lv_obj_t* row, const char* text, uint32_t col, int right) {
  lv_obj_align(label(row, text, THEME_FONT_SMALL, col), LV_ALIGN_RIGHT_MID, -right, 0);
}

void UITask::navTargetsPopup() {
  lv_obj_t* panel = navPopupPanel("Navigate to", true);
  lv_obj_t* list = lv_obj_create(panel);
  styleSurface(list, theme::BG);
  lv_obj_set_width(list, LV_PCT(100));
  lv_obj_set_flex_grow(list, 1);
  lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(list, theme::GAP, 0);
  lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_ACTIVE);

  int32_t lat = 0, lon = 0;
  bool gps = _core->course.currentLocation(lat, lon);
  bool imperial = _prefs && _prefs->units_imperial;
  auto dist = [&](int32_t la, int32_t lo, char* out, size_t n) {
    if (gps) geo::fmtDist(out, n, geo::haversineKm(lat, lon, la, lo), imperial);
    else out[0] = '\0';
  };

  if (_prefs && _prefs->locator_has_target) {
    char sub[40];
    snprintf(sub, sizeof(sub), "Now: %s", _prefs->locator_label[0] ? _prefs->locator_label : "target");
    listRow(list, LV_SYMBOL_CLOSE "  Clear target", sub, onNavPick, (void*)(uintptr_t)navmap::code(navmap::T_CLEAR, 0));
  }

  const WaypointModel& wp = _core->waypoints;
  char sec[32];
  snprintf(sec, sizeof(sec), "WAYPOINTS  %d/%d", wp.count(), WaypointStore::CAPACITY);
  sectionTitle(list, sec);
  for (int i = 0; i < wp.count(); i++) {
    const Waypoint& w = wp.at(i);
    char title[WAYPOINT_LABEL_LEN + 8], sub[40], d[12];
    snprintf(title, sizeof(title), UI_SYMBOL_FLAG "  %s", w.label[0] ? w.label : "(unnamed)");
    snprintf(sub, sizeof(sub), "%.5f, %.5f", w.lat_1e6 / 1e6, w.lon_1e6 / 1e6);
    lv_obj_t* row = listRow(list, title, sub, onNavPick, (void*)(uintptr_t)navmap::code(navmap::T_WAYPOINT, i));
    lv_obj_t* more = lv_button_create(row);   // the waypoint menu
    lv_obj_set_size(more, 40, 34);
    lv_obj_align(more, LV_ALIGN_RIGHT_MID, -4, 0);
    lv_obj_set_style_shadow_width(more, 0, 0);
    lv_obj_set_style_radius(more, theme::RADIUS, 0);
    lv_obj_set_style_bg_color(more, lv_color_hex(theme::SURFACE_2), 0);
    lv_obj_add_event_cb(more, onNavWpMenu, LV_EVENT_CLICKED, (void*)(uintptr_t)i);
    lv_obj_center(label(more, LV_SYMBOL_EDIT, THEME_FONT_BODY, theme::TEXT));
    dist(w.lat_1e6, w.lon_1e6, d, sizeof(d));
    if (d[0]) navRowRight(row, d, theme::TEXT_MUTED, 52);
  }
  if (wp.count() == 0) {
    lv_obj_t* l = label(list, "None yet. Hold the map to drop one, or tap the pin to mark where you are.",
                        THEME_FONT_SMALL, theme::TEXT_MUTED);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(l, LV_PCT(100));
  }
  TrailStore& ts = _core->trail.store();
  if (!ts.empty()) {
    char d[12], sub[24];
    dist(ts.first().lat_1e6, ts.first().lon_1e6, d, sizeof(d));
    snprintf(sub, sizeof(sub), "%d trail points", ts.count());
    lv_obj_t* row = listRow(list, LV_SYMBOL_HOME "  Trail start", sub, onNavPick,
                            (void*)(uintptr_t)navmap::code(navmap::T_TRAILSTART, 0));
    if (d[0]) navRowRight(row, d, theme::TEXT_MUTED, theme::PAD);
  }

  const LiveTrackStore& lt = _core->live_share.track();
  uint32_t now = rtc_clock.getCurrentTime();
  sectionTitle(list, "SHARING LIVE");
  int live = 0;
  for (int i = 0; i < LiveTrackStore::CAPACITY; i++) {
    if (!lt.isActive(i, now)) continue;
    const LiveTrackStore::Entry& e = lt.slotAt(i);
    char age[8], sub[40], d[12];
    geo::fmtAgeShort(age, sizeof(age), now, e.ts);
    snprintf(sub, sizeof(sub), "%s  -  %s ago", e.verified ? "direct" : "channel", age[0] ? age : "0s");
    lv_obj_t* row = listRow(list, e.name, sub, onNavPick, (void*)(uintptr_t)navmap::code(navmap::T_LIVE, i));
    dist(e.lat_1e6, e.lon_1e6, d, sizeof(d));
    if (d[0]) navRowRight(row, d, theme::OK, theme::PAD);
    live++;
  }
  if (live == 0) label(list, "Nobody is sharing their position.", THEME_FONT_SMALL, theme::TEXT_MUTED);
}

void UITask::navWaypointMenu(int idx) {
  if (idx < 0 || idx >= _core->waypoints.count()) return;
  _nav_wp = idx;
  const Waypoint& w = _core->waypoints.at(idx);
  lv_obj_t* panel = navPopupPanel(w.label[0] ? w.label : "(unnamed)", false);
  char info[80];
  int o = snprintf(info, sizeof(info), "%.5f, %.5f", w.lat_1e6 / 1e6, w.lon_1e6 / 1e6);
  int32_t lat, lon;
  if (_core->course.currentLocation(lat, lon)) {
    char d[12];
    geo::fmtDist(d, sizeof(d), geo::haversineKm(lat, lon, w.lat_1e6, w.lon_1e6), _prefs && _prefs->units_imperial);
    int az = geo::bearingDeg(lat, lon, w.lat_1e6, w.lon_1e6);
    snprintf(info + o, sizeof(info) - o, "\n%s  %d\xC2\xB0 %s", d, az, geo::bearingCardinal(az));
  }
  label(panel, info, THEME_FONT_BODY, theme::TEXT);

  lv_obj_t* acts = lv_obj_create(panel);
  styleSurface(acts, theme::BG);
  lv_obj_remove_flag(acts, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(acts, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(acts, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_column(acts, theme::GAP, 0);
  struct { const char* text; uint8_t act; bool accent; } btns[] = {
    { UI_SYMBOL_COMPASS " Go", navmap::WP_NAV, true },
    { LV_SYMBOL_EDIT " Name", navmap::WP_RENAME, false },
    { LV_SYMBOL_UPLOAD " Share", navmap::WP_SHARE, false },
    { LV_SYMBOL_TRASH, navmap::WP_DELETE, false },
  };
  for (auto& b : btns) {
    lv_obj_t* bt = lv_button_create(acts);
    lv_obj_set_height(bt, 40);
    lv_obj_set_flex_grow(bt, 1);
    lv_obj_set_style_pad_hor(bt, 4, 0);
    lv_obj_set_style_radius(bt, theme::RADIUS, 0);
    lv_obj_set_style_shadow_width(bt, 0, 0);
    lv_obj_set_style_bg_color(bt, lv_color_hex(b.accent ? theme::ACCENT_DIM : theme::SURFACE), 0);
    lv_obj_add_event_cb(bt, onNavWpAction, LV_EVENT_CLICKED, (void*)(uintptr_t)b.act);
    lv_obj_t* l = label(bt, b.text, THEME_FONT_SMALL, theme::TEXT);
    lv_obj_center(l);
    if (b.act == navmap::WP_DELETE) _nav_del_lbl = l;
  }
}

void UITask::navWaypointAction(uint8_t act) {
  int i = _nav_wp;
  if (i < 0 || i >= _core->waypoints.count()) { navClosePopup(); return; }
  switch (act) {
    case navmap::WP_NAV:
      navPick(navmap::code(navmap::T_WAYPOINT, i));
      break;
    case navmap::WP_RENAME:
      navRenamePopup(i);
      break;
    case navmap::WP_SHARE: {
      char text[80];
      _core->waypoints.shareText(i, text, sizeof(text));
      navClosePopup();
      shareToMessage(text);
      break;
    }
    case navmap::WP_DELETE:
      if (!_nav_del_armed_ms || millis() - _nav_del_armed_ms > 3000) {   // second tap within 3 s confirms
        _nav_del_armed_ms = millis() | 1;
        if (_nav_del_lbl) lv_label_set_text(_nav_del_lbl, "Delete?");
        break;
      }
      _core->waypoints.remove(i);
      navClosePopup();
      showToast("Waypoint deleted");
      rebuildMapMarkers();
      layoutMap();
      refreshNavBar();
      break;
  }
}

void UITask::navRenamePopup(int idx) {
  _nav_wp = idx;
  lv_obj_t* panel = navPopupPanel("Waypoint name", false);
  lv_obj_align(panel, LV_ALIGN_TOP_MID, 0, theme::STATUS_H + 4);   // above the keyboard
  _nav_ta = textField(panel);
  lv_textarea_set_text(_nav_ta, _core->waypoints.at(idx).label);
  lv_obj_add_state(_nav_ta, LV_STATE_FOCUSED);   // draws the cursor
  lv_obj_add_event_cb(_nav_ta, navmap::onLabelInsert, LV_EVENT_INSERT, NULL);

  _nav_kb = kb::create(_nav_overlay, _prefs);
  lv_obj_set_size(_nav_kb, LV_PCT(100), 124);
  lv_obj_align(_nav_kb, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_keyboard_set_textarea(_nav_kb, _nav_ta);
  lv_obj_add_event_cb(_nav_kb, onNavRenameKb, LV_EVENT_READY, NULL);
  lv_obj_add_event_cb(_nav_kb, onNavRenameKb, LV_EVENT_CANCEL, NULL);
}

void UITask::navRenameDone(bool ok) {
  if (ok && _nav_ta && _nav_wp >= 0 && _nav_wp < _core->waypoints.count()) {
    const Waypoint& w = _core->waypoints.at(_nav_wp);
    bool was_target = _prefs && _prefs->locator_has_target && _prefs->locator_target_kind == 0 &&
                      _prefs->locator_lat_1e6 == w.lat_1e6 && _prefs->locator_lon_1e6 == w.lon_1e6;
    _core->waypoints.rename(_nav_wp, lv_textarea_get_text(_nav_ta));
    if (was_target) {   // the bar shows the target's saved label
      snprintf(_prefs->locator_label, sizeof(_prefs->locator_label), "%s", _core->waypoints.at(_nav_wp).label);
      the_mesh.savePrefs();
    }
    rebuildMapMarkers();
    layoutMap();
    refreshNavBar();
  }
  navClosePopup();
}

// ── Adding waypoints ──────────────────────────────────────────────────────────

void UITask::navAddWaypoint(int32_t lat, int32_t lon) {
  if (_core->waypoints.full()) { showToast("Waypoints full (16)"); return; }
  if (!_core->waypoints.add(lat, lon, rtc_clock.getCurrentTime(), "")) return;
  int i = _core->waypoints.count() - 1;
  char t[48];
  snprintf(t, sizeof(t), "Saved %s", _core->waypoints.at(i).label);
  showToast(t);
  rebuildMapMarkers();
  layoutMap();
}

void UITask::navMarkHere() {
  int32_t lat, lon;
  if (!ensureGps() || !_core->course.currentLocation(lat, lon)) return;
  uint16_t secs = NodePrefs::gpsAvgSecs(_prefs ? _prefs->gps_avg_idx : 0);
  if (secs == 0) { navAddWaypoint(lat, lon); return; }
  if (_core->waypoints.full()) { showToast("Waypoints full (16)"); return; }
  navmap::s_avg.start(secs, lat, lon);
  navPollAveraging();
}

void UITask::navAveragingCancel() {
  navmap::s_avg.cancel();
  if (_nav_avg_pill) lv_obj_add_flag(_nav_avg_pill, LV_OBJ_FLAG_HIDDEN);
  showToast("Mark cancelled");
}

// From mapLoop(): sample, update the pill, mark the mean when the window closes.
void UITask::navPollAveraging() {
  if (!navmap::s_avg.active()) return;
  int32_t lat = 0, lon = 0, mlat, mlon;
  bool fix = _core->course.currentLocation(lat, lon);
  GpsAverager::Step st = navmap::s_avg.poll(fix, lat, lon, mlat, mlon);
  if (_nav_avg_pill) {
    if (st == GpsAverager::RUNNING) {
      lv_label_set_text_fmt(_nav_avg_pill, UI_SYMBOL_PIN "  Averaging GPS...  %d s  (%lu fixes)  " LV_SYMBOL_CLOSE,
                            navmap::s_avg.remainingSecs(), (unsigned long)navmap::s_avg.samples());
      lv_obj_remove_flag(_nav_avg_pill, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(_nav_avg_pill, LV_OBJ_FLAG_HIDDEN);
    }
  }
  if (st == GpsAverager::DONE) navAddWaypoint(mlat, mlon);
  else if (st == GpsAverager::NO_FIX) showToast("No GPS fix");
}

// Long-press on the map: what to do with that spot (nothing is added until asked).
void UITask::navDropAt(int x, int y) {
  int32_t lat, lon;
  mapPointToLatLon(x, y, lat, lon);
  navSpotPopup(lat, lon);
}

// ── Share into a conversation ─────────────────────────────────────────────────

// Pick a conversation; the text is waiting in its compose field.
void UITask::shareToMessage(const char* text) {
  snprintf(_share_text, sizeof(_share_text), "%s", text);
  showChats();
  showToast("Pick a conversation to share it in");
}

// ── Tools panel: trail recording, live share, map download ────────────────────

namespace navmap {
enum : uint8_t { TL_TRAIL_TOGGLE, TL_TRAIL_SAVE, TL_TRAIL_LOAD, TL_TRAIL_RESET, TL_TRAIL_GPX, TL_TRACKBACK,
                 TL_SHARE_TOGGLE, TL_SHARE_ONCE, TL_DOWNLOAD,
                 TL_WP_HERE, TL_WP_COORDS, TL_SPOT_ADD, TL_SPOT_GO };

// Live-share targets offered in the dropdown: channels, then favourite contacts.
static const int SHARE_TARGETS = MAX_GROUP_CHANNELS + 16;
static uint8_t s_share_kind[SHARE_TARGETS];               // 0 = channel, 1 = contact
static uint8_t s_share_ch[SHARE_TARGETS];
static uint8_t s_share_key[SHARE_TARGETS][NodePrefs::FAVOURITE_PREFIX_LEN];
static int     s_share_n = 0;

// Print sink over a FILE* for TrailStore's GPX writer (same duck type as
// ui-new's BoundedSerialPrint).
struct FilePrint {
  FILE* f;
  bool  ok;
  explicit FilePrint(FILE* file) : f(file), ok(true) {}
  size_t write(const uint8_t* b, size_t n) {
    if (!ok) return 0;
    size_t w = fwrite(b, 1, n, f);
    if (w != n) ok = false;
    return w;
  }
  size_t print(const char* s) { return write((const uint8_t*)s, strlen(s)); }
  size_t print(const __FlashStringHelper* s) { return print(reinterpret_cast<const char*>(s)); }
};

}  // namespace navmap

static void onNavTools(lv_event_t* e)       { (void)e; s_ui->navToolsPopup(); }
static void onNavTool(lv_event_t* e)        { s_ui->navToolAction((uint8_t)(uintptr_t)lv_event_get_user_data(e)); }
static void onNavShareTarget(lv_event_t* e) {
  s_ui->navSetShareTarget((int)lv_dropdown_get_selected((lv_obj_t*)lv_event_get_target(e)));
}

static lv_obj_t* toolRow(lv_obj_t* parent) {
  lv_obj_t* r = lv_obj_create(parent);
  styleSurface(r, theme::BG);
  lv_obj_remove_flag(r, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(r, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_column(r, theme::GAP, 0);
  return r;
}

static lv_obj_t* toolButton(lv_obj_t* row, const char* text, uint8_t act, bool accent) {
  lv_obj_t* b = lv_button_create(row);
  lv_obj_set_height(b, 36);
  lv_obj_set_flex_grow(b, 1);
  lv_obj_set_style_pad_hor(b, 4, 0);
  lv_obj_set_style_radius(b, theme::RADIUS, 0);
  lv_obj_set_style_shadow_width(b, 0, 0);
  lv_obj_set_style_bg_color(b, lv_color_hex(accent ? theme::ACCENT_DIM : theme::SURFACE), 0);
  lv_obj_set_style_bg_color(b, lv_color_hex(theme::SURFACE_2), LV_STATE_PRESSED);
  lv_obj_add_event_cb(b, onNavTool, LV_EVENT_CLICKED, (void*)(uintptr_t)act);
  lv_obj_t* l = label(b, text, THEME_FONT_SMALL, theme::TEXT);
  lv_obj_center(l);
  return l;
}

void UITask::navToolsPopup() {
  lv_obj_t* panel = navPopupPanel("Trail & sharing", true);
  lv_obj_t* list = lv_obj_create(panel);
  styleSurface(list, theme::BG);
  lv_obj_set_width(list, LV_PCT(100));
  lv_obj_set_flex_grow(list, 1);
  lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(list, theme::GAP, 0);
  lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_ACTIVE);

  sectionTitle(list, "TRAIL");
  _nav_trail_lbl = label(list, "", THEME_FONT_SMALL, theme::TEXT);
  lv_obj_t* r = toolRow(list);
  _nav_trail_btn = toolButton(r, "", navmap::TL_TRAIL_TOGGLE, true);
  toolButton(r, LV_SYMBOL_SAVE " Save", navmap::TL_TRAIL_SAVE, false);
  toolButton(r, LV_SYMBOL_DIRECTORY " Load", navmap::TL_TRAIL_LOAD, false);
  r = toolRow(list);
  _nav_tb_btn = toolButton(r, "", navmap::TL_TRACKBACK, false);
  toolButton(r, LV_SYMBOL_SD_CARD " GPX", navmap::TL_TRAIL_GPX, false);
  _nav_reset_lbl = toolButton(r, LV_SYMBOL_TRASH " Reset", navmap::TL_TRAIL_RESET, false);

  sectionTitle(list, "LIVE SHARE");
  _nav_share_lbl = label(list, "", THEME_FONT_SMALL, theme::TEXT);
  lv_label_set_long_mode(_nav_share_lbl, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(_nav_share_lbl, LV_PCT(100));
  // Target: channels, then favourite contacts.
  char opts[MAX_GROUP_CHANNELS * 24 + 16 * 36];
  int o = 0, sel = 0;
  navmap::s_share_n = 0;
  for (int i = 0; i < MAX_GROUP_CHANNELS && navmap::s_share_n < navmap::SHARE_TARGETS; i++) {
    ChannelDetails ch;
    if (!the_mesh.getChannel(i, ch) || !ch.name[0]) continue;
    if (_prefs->loc_share_target_type == 0 && _prefs->loc_share_channel_idx == i) sel = navmap::s_share_n;
    navmap::s_share_kind[navmap::s_share_n] = 0;
    navmap::s_share_ch[navmap::s_share_n++] = (uint8_t)i;
    o += snprintf(opts + o, sizeof(opts) - o, "%s# %s", o ? "\n" : "", ch.name);
  }
  int favs = 0;
  for (int i = 0; i < the_mesh.getNumContacts() && favs < 16 && navmap::s_share_n < navmap::SHARE_TARGETS; i++) {
    ContactInfo c;
    if (!the_mesh.getContactByIdx(MAX_ANON_CONTACTS + i, c)) continue;
    if (c.type != ADV_TYPE_CHAT || !contactctl::favourite(c)) continue;
    if (_prefs->loc_share_target_type == 1 &&
        memcmp(_prefs->loc_share_dm_prefix, c.id.pub_key, NodePrefs::FAVOURITE_PREFIX_LEN) == 0) sel = navmap::s_share_n;
    navmap::s_share_kind[navmap::s_share_n] = 1;
    memcpy(navmap::s_share_key[navmap::s_share_n++], c.id.pub_key, NodePrefs::FAVOURITE_PREFIX_LEN);
    o += snprintf(opts + o, sizeof(opts) - o, "%s%s", o ? "\n" : "", c.name);
    favs++;
  }
  lv_obj_t* tr = lv_obj_create(list);
  styleSurface(tr, theme::SURFACE);
  lv_obj_remove_flag(tr, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(tr, LV_PCT(100), 40);
  lv_obj_set_style_radius(tr, theme::RADIUS, 0);
  lv_obj_align(label(tr, "Send to", THEME_FONT_BODY, theme::TEXT), LV_ALIGN_LEFT_MID, theme::PAD, 0);
  lv_obj_t* dd = lv_dropdown_create(tr);
  lv_dropdown_set_options(dd, o ? opts : "(no channels)");
  lv_dropdown_set_selected(dd, sel);
  lv_obj_set_width(dd, 170);
  lv_obj_align(dd, LV_ALIGN_RIGHT_MID, -4, 0);
  lv_obj_add_event_cb(dd, onNavShareTarget, LV_EVENT_VALUE_CHANGED, NULL);
  r = toolRow(list);
  _nav_share_btn = toolButton(r, "", navmap::TL_SHARE_TOGGLE, true);
  toolButton(r, LV_SYMBOL_UPLOAD " Send once", navmap::TL_SHARE_ONCE, false);
  lv_obj_t* hint = label(list, "Timing and duration: Settings > Trail, live share, alerts.", THEME_FONT_SMALL,
                         theme::TEXT_MUTED);
  lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(hint, LV_PCT(100));

  sectionTitle(list, "MAP");
  r = toolRow(list);
  toolButton(r, LV_SYMBOL_DOWNLOAD " Download this area", navmap::TL_DOWNLOAD, false);

  _nav_reset_armed_ms = 0;
  refreshNavTools();
}

void UITask::refreshNavTools() {
  if (!_nav_trail_lbl) return;
  TrailStore& ts = _core->trail.store();
  bool imperial = _prefs && _prefs->units_imperial;
  char dist[12], dur[12];
  geo::fmtDist(dist, sizeof(dist), ts.totalDistanceMeters() / 1000.0f, imperial);
  navmap::fmtDuration(dur, sizeof(dur), ts.elapsedSeconds());
  const char* state = !ts.isActive() ? (ts.empty() ? "Not recording" : "Stopped")
                    : ts.isPaused() ? "Paused (standing still)" : "Recording";
  if (ts.empty()) lv_label_set_text(_nav_trail_lbl, state);
  else lv_label_set_text_fmt(_nav_trail_lbl, "%s  -  %s, %s, %d points", state, dist, dur, ts.count());
  lv_label_set_text(_nav_trail_btn, ts.isActive() ? LV_SYMBOL_STOP " Stop" : LV_SYMBOL_PLAY " Record");
  lv_label_set_text(_nav_tb_btn, navmap::s_tb.active() ? LV_SYMBOL_STOP " Stop back" : LV_SYMBOL_LOOP " Track back");
  if (_nav_reset_armed_ms && millis() - _nav_reset_armed_ms > 3000) {
    _nav_reset_armed_ms = 0;
    lv_label_set_text(_nav_reset_lbl, LV_SYMBOL_TRASH " Reset");
  }

  if (_prefs->loc_share_enabled) {
    char left[12];
    navmap::fmtDuration(left, sizeof(left), _core->live_share.remainingSecs());
    lv_label_set_text_fmt(_nav_share_lbl, "Sharing your position  -  %s left", left);
  } else {
    lv_label_set_text(_nav_share_lbl, "Off. Sends your position while you move, then stops by itself.");
  }
  lv_label_set_text(_nav_share_btn, _prefs->loc_share_enabled ? LV_SYMBOL_STOP " Stop sharing" : LV_SYMBOL_PLAY " Share live");
}

void UITask::navSetShareTarget(int sel) {
  if (sel < 0 || sel >= navmap::s_share_n) return;
  _prefs->loc_share_target_type = navmap::s_share_kind[sel];
  if (navmap::s_share_kind[sel] == 0) _prefs->loc_share_channel_idx = navmap::s_share_ch[sel];
  else memcpy(_prefs->loc_share_dm_prefix, navmap::s_share_key[sel], NodePrefs::FAVOURITE_PREFIX_LEN);
  the_mesh.savePrefs();
}

// GPX of the live trail (with the waypoints) to /sdcard/trails/.
bool UITask::exportTrailGpx(char* name_out, size_t n) {
  if (!lvport::mountStorage()) return false;
  char path[64];
  uint32_t now = rtc_clock.getCurrentTime();
  if (now > 1000000000UL) {
    time_t t = (time_t)((int64_t)now + (int64_t)_prefs->tz_offset_hours * 3600);
    struct tm* ti = gmtime(&t);
    snprintf(path, sizeof(path), "/sdcard/trails/trail-%04d%02d%02d-%02d%02d.gpx", ti->tm_year + 1900,
             ti->tm_mon + 1, ti->tm_mday, ti->tm_hour, ti->tm_min);
  } else {
    snprintf(path, sizeof(path), "/sdcard/trails/trail-%lu.gpx", (unsigned long)(millis() / 1000));
  }
  mapview::makeParents(path);
  FILE* f = fopen(path, "w");
  if (!f) return false;
  navmap::FilePrint out(f);
  _core->trail.store().exportGpx(out, _core->waypoints.store());
  bool ok = out.ok;
  ok = (fclose(f) == 0) && ok;
  snprintf(name_out, n, "%s", strrchr(path, '/') + 1);
  return ok;
}

void UITask::navToolAction(uint8_t act) {
  TrailEngine& tr = _core->trail;
  switch (act) {
    case navmap::TL_TRAIL_TOGGLE:
      if (!tr.isActive()) {
        tr.setActive(true);
        int32_t lat, lon;
        if (_core->course.currentLocation(lat, lon)) showToast("Recording the trail");
        else ensureGps();   // turns GPS on, or says it's waiting
      } else {
        tr.setActive(false);
        showToast("Trail stopped");
      }
      break;
    case navmap::TL_TRAIL_SAVE: {
      TrailEngine::FileResult r = tr.save();
      showToast(r == TrailEngine::FILE_OK ? "Trail saved" : "Save failed");
      break;
    }
    case navmap::TL_TRAIL_LOAD: {
      TrailEngine::FileResult r = tr.load();
      showToast(r == TrailEngine::FILE_OK ? "Trail loaded" : r == TrailEngine::FILE_MISSING ? "No saved trail" : "Load failed");
      rebuildMapMarkers();
      layoutMap();
      break;
    }
    case navmap::TL_TRAIL_RESET:
      if (!_nav_reset_armed_ms || millis() - _nav_reset_armed_ms > 3000) {   // second tap within 3 s confirms
        _nav_reset_armed_ms = millis() | 1;
        lv_label_set_text(_nav_reset_lbl, "Reset?");
        return;
      }
      tr.reset();
      _nav_reset_armed_ms = 0;
      showToast("Trail cleared");
      rebuildMapMarkers();
      layoutMap();
      break;
    case navmap::TL_TRACKBACK: {
      if (navmap::s_tb.active()) { navmap::s_tb.stop(); showToast("Track back stopped"); break; }
      int32_t lat = 0, lon = 0;
      bool fix = _core->course.currentLocation(lat, lon);
      if (!navmap::s_tb.start(tr.store(), fix, lat, lon)) { showToast("No trail to walk back"); break; }
      navmap::s_eta.reset();
      if (!fix) ensureGps();
      navClosePopup();
      navFrameTarget();
      refreshNavBar();
      return;
    }
    case navmap::TL_TRAIL_GPX: {
      if (tr.store().empty()) { showToast("No trail to export"); break; }
      char name[40], t[64];
      if (exportTrailGpx(name, sizeof(name))) { snprintf(t, sizeof(t), "Saved trails/%s", name); showToast(t, 3500); }
      else showToast("Can't write to the SD card");
      break;
    }
    case navmap::TL_SHARE_TOGGLE:
      if (_prefs->loc_share_enabled) {
        _prefs->loc_share_enabled = 0;
        showToast("Live share stopped");
      } else {
        _prefs->loc_share_enabled = 1;
        _core->live_share.restartSession();
        int32_t lat, lon;
        if (_core->course.currentLocation(lat, lon)) showToast("Sharing your position");
        else ensureGps();
      }
      the_mesh.savePrefs();
      break;
    case navmap::TL_SHARE_ONCE: {
      int32_t lat, lon;
      if (!ensureGps() || !_core->course.currentLocation(lat, lon)) break;
      // With a session on, straight to its target; otherwise pick a conversation.
      if (_prefs->loc_share_enabled && _core->live_share.send(lat, lon)) { showToast("Position sent"); break; }
      char text[40];
      snprintf(text, sizeof(text), LOCATION_MSG_TAG "%.5f,%.5f", lat / 1e6, lon / 1e6);
      navClosePopup();
      shareToMessage(text);
      return;
    }
    case navmap::TL_DOWNLOAD:
      navClosePopup();
      mapDownloadPopup();
      return;
    case navmap::TL_WP_HERE:
      navClosePopup();
      navMarkHere();
      return;
    case navmap::TL_WP_COORDS:
      navCoordsPopup();
      return;
    case navmap::TL_SPOT_ADD:
      navClosePopup();
      navAddWaypoint(_nav_spot_lat, _nav_spot_lon);
      return;
    case navmap::TL_SPOT_GO:
      navClosePopup();
      navSetTarget(0, nullptr, _nav_spot_lat, _nav_spot_lon, "Map point");
      navFrameTarget();
      refreshNavBar();
      return;
  }
  refreshNavTools();
  refreshNavBar();
}

// ── Waypoint list, add by coordinates, long-press spot ────────────────────────

static void onNavCoordsKb(lv_event_t* e) { s_ui->navCoordsDone(lv_event_get_code(e) == LV_EVENT_READY); }

// The pin button: every waypoint (tap for its menu), plus the two ways to add
// one that aren't a long-press -- here (GPS, averaged per Settings) or typed.
void UITask::navWaypointsPopup() {
  const WaypointModel& wp = _core->waypoints;
  char title[32];
  snprintf(title, sizeof(title), "Waypoints  %d/%d", wp.count(), WaypointStore::CAPACITY);
  lv_obj_t* panel = navPopupPanel(title, true);
  lv_obj_t* r = toolRow(panel);
  toolButton(r, UI_SYMBOL_PIN " Here (GPS)", navmap::TL_WP_HERE, true);
  toolButton(r, LV_SYMBOL_KEYBOARD " Coordinates", navmap::TL_WP_COORDS, false);

  lv_obj_t* list = lv_obj_create(panel);
  styleSurface(list, theme::BG);
  lv_obj_set_width(list, LV_PCT(100));
  lv_obj_set_flex_grow(list, 1);
  lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(list, theme::GAP, 0);
  lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_ACTIVE);
  int32_t lat = 0, lon = 0;
  bool gps = _core->course.currentLocation(lat, lon);
  for (int i = 0; i < wp.count(); i++) {
    const Waypoint& w = wp.at(i);
    char t[WAYPOINT_LABEL_LEN + 8], sub[40];
    snprintf(t, sizeof(t), UI_SYMBOL_FLAG "  %s", w.label[0] ? w.label : "(unnamed)");
    snprintf(sub, sizeof(sub), "%.5f, %.5f", w.lat_1e6 / 1e6, w.lon_1e6 / 1e6);
    lv_obj_t* row = listRow(list, t, sub, onNavWpMenu, (void*)(uintptr_t)i);
    if (gps) {
      char d[12];
      geo::fmtDist(d, sizeof(d), geo::haversineKm(lat, lon, w.lat_1e6, w.lon_1e6), _prefs && _prefs->units_imperial);
      navRowRight(row, d, theme::TEXT_MUTED, theme::PAD);
    }
  }
  if (wp.count() == 0) {
    lv_obj_t* l = label(list, "None yet. Add one here, type its coordinates, or hold the map on a spot.",
                        THEME_FONT_SMALL, theme::TEXT_MUTED);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(l, LV_PCT(100));
  }
}

// "50.06142, 19.93721 Name" (decimal degrees; comma or space between them; the
// name is optional) -> a new waypoint.
void UITask::navCoordsPopup() {
  if (_core->waypoints.full()) { showToast("Waypoints full (16)"); return; }
  lv_obj_t* panel = navPopupPanel("Add by coordinates", false);
  lv_obj_align(panel, LV_ALIGN_TOP_MID, 0, theme::STATUS_H + 4);   // above the keyboard
  _nav_ta = textField(panel);
  lv_textarea_set_placeholder_text(_nav_ta, "50.06142, 19.93721 Name");
  lv_obj_add_state(_nav_ta, LV_STATE_FOCUSED);   // draws the cursor

  _nav_kb = kb::create(_nav_overlay, _prefs);
  lv_obj_set_size(_nav_kb, LV_PCT(100), 124);
  lv_obj_align(_nav_kb, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_keyboard_set_textarea(_nav_kb, _nav_ta);
  kb::apply(_nav_kb, kb::L_SYM);   // digits first; "abc" gives letters for the name
  lv_obj_add_event_cb(_nav_kb, onNavCoordsKb, LV_EVENT_READY, NULL);
  lv_obj_add_event_cb(_nav_kb, onNavCoordsKb, LV_EVENT_CANCEL, NULL);
}

void UITask::navCoordsDone(bool ok) {
  if (!ok || !_nav_ta) { navWaypointsPopup(); return; }
  // parseLatLon wants "lat,lon"; accept a space too, and read what follows as
  // the name the way a [WAY] share carries it.
  char buf[80] = WAYPOINT_MSG_TAG;
  size_t o = strlen(buf);
  const char* in = lv_textarea_get_text(_nav_ta);
  while (*in == ' ') in++;
  bool sep = strchr(in, ',') != nullptr;
  for (; *in && o < sizeof(buf) - 1; in++) {
    if (!sep && *in == ' ') { buf[o++] = ','; sep = true; while (in[1] == ' ') in++; continue; }
    buf[o++] = *in;
  }
  buf[o] = '\0';
  int32_t lat, lon;
  char name[WAYPOINT_LABEL_LEN * 2];
  if (!geo::parseLatLon(buf, lat, lon, name, sizeof(name))) {
    showToast("Use decimal degrees, e.g. 50.06142, 19.93721");
    return;   // keep the field for a fix
  }
  navClosePopup();
  if (!_core->waypoints.add(lat, lon, rtc_clock.getCurrentTime(), name)) { showToast("Waypoints full (16)"); return; }
  const Waypoint& w = _core->waypoints.at(_core->waypoints.count() - 1);
  char t[40];
  snprintf(t, sizeof(t), "Saved %s", w.label);
  showToast(t);
  // Show where it is.
  double n = (double)(1 << _map_z);
  _map_follow = false;
  _map_cx = navmap::normX(lon) * n;
  _map_cy = navmap::normY(lat) * n + mapCenterBias();
  rebuildMapMarkers();
  layoutMap();
}

void UITask::navSpotPopup(int32_t lat, int32_t lon) {
  _nav_spot_lat = lat;
  _nav_spot_lon = lon;
  lv_obj_t* panel = navPopupPanel("This spot", false);
  char info[64];
  int o = snprintf(info, sizeof(info), "%.5f, %.5f", lat / 1e6, lon / 1e6);
  int32_t mlat, mlon;
  if (_core->course.currentLocation(mlat, mlon)) {
    char d[12];
    geo::fmtDist(d, sizeof(d), geo::haversineKm(mlat, mlon, lat, lon), _prefs && _prefs->units_imperial);
    int az = geo::bearingDeg(mlat, mlon, lat, lon);
    snprintf(info + o, sizeof(info) - o, "\n%s  %d\xC2\xB0 %s", d, az, geo::bearingCardinal(az));
  }
  label(panel, info, THEME_FONT_BODY, theme::TEXT);
  lv_obj_t* r = toolRow(panel);
  toolButton(r, UI_SYMBOL_FLAG " Add waypoint", navmap::TL_SPOT_ADD, true);
  toolButton(r, UI_SYMBOL_COMPASS " Go here", navmap::TL_SPOT_GO, false);
}

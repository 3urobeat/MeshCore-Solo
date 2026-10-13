#pragma once
// Route: the repeaters between this node and one contact, edited, traced and
// saved (node detail > Route). The list, the out-and-back trace and the save
// are the UI Core's PathEditor (`_core->route`); ui-new's Path screen is a
// view over the same. This draws the rows, a picker of repeaters by distance,
// and -- from the map -- picking the hops by tapping them (MapScreen.h,
// `_route_pick`).
//
// Single-TU fragment: included by ui-lvgl/UITask.cpp after AdminScreen.h.

namespace routeview {

enum : uint8_t { RT_ADD, RT_MAP, RT_TRACE, RT_SAVE, RT_PIN };
enum : uint8_t { OP_UP, OP_DOWN, OP_DEL };

static const int PICK_MAX = 96;
struct Pick { uint16_t idx; float km; };   // raw contact index, distance (<0: no position)
static Pick* s_pick = psramBuf<Pick>(PICK_MAX);
static int s_pick_n = 0;

static int cmpPick(const void* a, const void* b) {
  float x = ((const Pick*)a)->km, y = ((const Pick*)b)->km;
  if ((x < 0) != (y < 0)) return x < 0 ? 1 : -1;   // those with no position last
  return x < y ? -1 : x > y ? 1 : 0;
}

// A hop's text from an SNR x4.
static void snrText(char* out, size_t n, int16_t snr_x4) { snprintf(out, n, "%.1f", snr_x4 / 4.0f); }

}  // namespace routeview

static void onRouteAct(lv_event_t* e)     { s_ui->routeAct((uint8_t)(uintptr_t)lv_event_get_user_data(e)); }
static void onRouteOp(lv_event_t* e)      { s_ui->routeOp((int)(uintptr_t)lv_event_get_user_data(e)); }
static void onRoutePickRow(lv_event_t* e) { s_ui->routePick((int)vlist::arg(e)); }

// A small square button on a hop's card: user data = op << 8 | hop index.
static lv_obj_t* routeSquare(lv_obj_t* card, const char* sym, int code) {
  lv_obj_t* b = lv_button_create(card);
  lv_obj_set_size(b, 30, 30);
  lv_obj_set_style_pad_all(b, 0, 0);
  lv_obj_set_style_radius(b, theme::RADIUS, 0);
  lv_obj_set_style_shadow_width(b, 0, 0);
  lv_obj_set_style_bg_color(b, lv_color_hex(theme::SURFACE_2), 0);
  lv_obj_add_event_cb(b, onRouteOp, LV_EVENT_CLICKED, (void*)(uintptr_t)code);
  lv_obj_center(label(b, sym, THEME_FONT_SMALL, theme::TEXT));
  return b;
}

// One row of the route: a number, the name and what the trace heard.
static lv_obj_t* routeCard(lv_obj_t* list, const char* num, const char* name, const char* sub, uint32_t col) {
  lv_obj_t* c = lv_obj_create(list);
  styleSurface(c, theme::SURFACE);
  lv_obj_remove_flag(c, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(c, LV_PCT(100), 44);
  lv_obj_set_style_radius(c, theme::RADIUS, 0);
  lv_obj_set_style_pad_hor(c, 8, 0);
  lv_obj_set_style_pad_ver(c, 0, 0);
  lv_obj_set_flex_flow(c, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(c, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(c, 6, 0);
  lv_obj_t* n = label(c, num, THEME_FONT_BODY, theme::ACCENT);
  lv_obj_set_width(n, 18);
  lv_obj_t* box = lv_obj_create(c);
  lv_obj_remove_style_all(box);
  lv_obj_set_height(box, LV_SIZE_CONTENT);
  lv_obj_set_flex_grow(box, 1);
  lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
  lv_obj_t* nm = label(box, name, THEME_FONT_BODY, col);
  lv_label_set_long_mode(nm, LV_LABEL_LONG_DOT);
  lv_obj_set_width(nm, LV_PCT(100));
  if (sub && sub[0]) {
    lv_obj_t* s = label(box, sub, THEME_FONT_SMALL, theme::TEXT_MUTED);
    lv_label_set_long_mode(s, LV_LABEL_LONG_DOT);
    lv_obj_set_width(s, LV_PCT(100));
  }
  return c;
}

// Node detail > Route.
void UITask::showRoute(const uint8_t* pub_key) {
  if (!_core->route.open(pub_key)) { showToast("Not a contact"); return; }
  _route_pick = false;
  _screen = SCR_ROUTE;
  buildRoute();
}

void UITask::buildRoute() {
  lv_obj_t* body = newScreen("Route", true);
  _route_status = noteLabel(body, "", THEME_FONT_SMALL, theme::TEXT_MUTED);
  _route_list = scrollList(body);
  lv_obj_t* bar = buttonBar(body);
  barButton(bar, LV_SYMBOL_PLUS " Add", onRouteAct, routeview::RT_ADD);
  barButton(bar, UI_SYMBOL_MAP " Map", onRouteAct, routeview::RT_MAP);
  _route_trace_btn = barButton(bar, LV_SYMBOL_LOOP " Trace", onRouteAct, routeview::RT_TRACE, true);
  barButton(bar, LV_SYMBOL_OK " Save", onRouteAct, routeview::RT_SAVE);
  _route_pin_btn = barButton(bar, UI_SYMBOL_TACK " Keep", onRouteAct, routeview::RT_PIN, _core->route.pinned());
  _route_shown_state = 255;
  routeRebuild();
}

// The rows and the line above them, from the editor's state.
void UITask::routeRebuild() {
  if (!_route_list || !_route_status) return;
  PathEditor& R = _core->route;
  lv_obj_clean(_route_list);
  const int n = R.count();
  const bool done = R.state() == PathEditor::DONE;
  char sub[48], a[12], b[12];

  int16_t back = 0;
  sub[0] = '\0';
  if (done && R.snrBackX4(back)) { routeview::snrText(a, sizeof(a), back); snprintf(sub, sizeof(sub), "" LV_SYMBOL_LEFT " %s dB", a); }
  routeCard(_route_list, "", "This device", sub, theme::TEXT_MUTED);

  for (int i = 0; i < n; i++) {
    int16_t out = 0, ret = 0;
    sub[0] = '\0';
    if (done && R.hopSnrX4(i, out)) {
      routeview::snrText(a, sizeof(a), out);
      if (R.hopSnrX4(2 * n - i, ret)) {
        routeview::snrText(b, sizeof(b), ret);
        snprintf(sub, sizeof(sub), "" LV_SYMBOL_RIGHT " %s   " LV_SYMBOL_LEFT " %s dB", a, b);
      } else {
        snprintf(sub, sizeof(sub), "" LV_SYMBOL_RIGHT " %s dB", a);
      }
    } else if (R.hop(i).known < 4) {
      snprintf(sub, sizeof(sub), "not a known contact");
    }
    char num[4];
    snprintf(num, sizeof(num), "%d", i + 1);
    lv_obj_t* c = routeCard(_route_list, num, R.hop(i).name, sub, theme::TEXT);
    if (i > 0) routeSquare(c, LV_SYMBOL_UP, routeview::OP_UP << 8 | i);
    if (i < n - 1) routeSquare(c, LV_SYMBOL_DOWN, routeview::OP_DOWN << 8 | i);
    routeSquare(c, LV_SYMBOL_CLOSE, routeview::OP_DEL << 8 | i);
  }

  int16_t out = 0;
  sub[0] = '\0';
  if (done && R.hopSnrX4(n, out)) { routeview::snrText(a, sizeof(a), out); snprintf(sub, sizeof(sub), "" LV_SYMBOL_RIGHT " %s dB", a); }
  routeCard(_route_list, "", R.targetName()[0] ? R.targetName() : "(unnamed)", sub, theme::ACCENT);

  if (n == 0)
    noteLabel(_route_list, R.hasStoredPath()
                  ? "Direct: no repeaters between. Save with repeaters to route through them."
                  : "No route stored, so messages are flooded. Add the repeaters the node should be reached through.",
              THEME_FONT_SMALL, theme::TEXT_MUTED);

  routeStatus();
}

void UITask::routeStatus() {
  PathEditor& R = _core->route;
  char t[96];
  switch (R.state()) {
    case PathEditor::RUNNING: snprintf(t, sizeof(t), "Tracing %d hops, out and back...", R.traceEntries()); break;
    case PathEditor::DONE: {
      int16_t worst = 0;
      if (R.worstOutX4(worst)) snprintf(t, sizeof(t), "Heard back in %lu ms  -  weakest hop %.1f dB", (unsigned long)R.rttMs(), worst / 4.0f);
      else snprintf(t, sizeof(t), "Heard back in %lu ms", (unsigned long)R.rttMs());
      break;
    }
    case PathEditor::TIMEOUT: snprintf(t, sizeof(t), "No reply: a hop didn't answer, or its answer wasn't heard here"); break;
    case PathEditor::FAILED: snprintf(t, sizeof(t), "Can't trace: a repeater has no key known (add it from contacts)"); break;
    default:
      snprintf(t, sizeof(t), "Route to %s: %d repeater%s%s", R.targetName(), R.count(), R.count() == 1 ? "" : "s",
               R.pinned() ? " (kept)" : "");
  }
  setText(_route_status, t);
  if (_route_trace_btn) setText(lv_obj_get_child(_route_trace_btn, 0), R.state() == PathEditor::RUNNING ? LV_SYMBOL_CLOSE " Stop" : LV_SYMBOL_LOOP " Trace");
  _route_shown_state = R.state();
}

// From loop() on SCR_ROUTE: a running trace's reply or timeout.
void UITask::routePoll() {
  PathEditor& R = _core->route;
  R.poll();
  if (R.state() != _route_shown_state) routeRebuild();
}

void UITask::routeAct(uint8_t act) {
  PathEditor& R = _core->route;
  switch (act) {
    case routeview::RT_ADD:
      R.cancelTrace();
      showRoutePick();
      break;
    case routeview::RT_MAP:
      R.cancelTrace();
      _route_pick = true;
      openMap(false);   // the nodes map: repeaters are tapped into the route
      routeMapFrame();
      break;
    case routeview::RT_TRACE:
      if (R.state() == PathEditor::RUNNING) { R.cancelTrace(); routeRebuild(); break; }
      if (R.trace()) {
        _next_route_ms = millis() + 200;
        routeRebuild();
      } else if (R.state() == PathEditor::FAILED) {
        routeRebuild();
      } else {
        showToast("Busy, try again");
      }
      break;
    case routeview::RT_PIN:   // kept: not replaced by a path heard, nor dropped after a failed send
      R.setPinned(!R.pinned());
      barButtonOn(_route_pin_btn, R.pinned());
      showToast(R.pinned() ? (R.hasStoredPath() ? "Route kept" : "Kept once you save it") : "Route is learned again");
      break;
    case routeview::RT_SAVE:
      if (R.count() == 0) { showToast("Add a repeater first (Reset path floods)"); break; }
      showToast(R.save() ? "Route saved" : "Can't save this route");
      routeRebuild();
      break;
  }
}

void UITask::routeOp(int code) {
  PathEditor& R = _core->route;
  int i = code & 0xFF, op = code >> 8;
  if (op == routeview::OP_UP) R.move(i, -1);
  else if (op == routeview::OP_DOWN) R.move(i, +1);
  else if (op == routeview::OP_DEL) R.remove(i);
  routeRebuild();
}

// ── Picker: repeaters by distance ─────────────────────────────────────────────

void UITask::showRoutePick() {
  _screen = SCR_ROUTE_PICK;
  buildRoutePick();
}

void UITask::buildRoutePick() {
  lv_obj_t* body = newScreen("Add repeater", true);
  vlist::attach(body);
  vlist::begin(body);
  int32_t mlat, mlon;
  bool own = _core->course.currentLocation(mlat, mlon);
  routeview::s_pick_n = 0;
  for (int i = 0; i < the_mesh.getNumContacts() && routeview::s_pick_n < routeview::PICK_MAX; i++) {
    ContactInfo c;
    if (!the_mesh.getContactByIdx(MAX_ANON_CONTACTS + i, c) || c.type != ADV_TYPE_REPEATER) continue;
    bool has_pos = c.gps_lat != 0 || c.gps_lon != 0;
    routeview::s_pick[routeview::s_pick_n++] = { (uint16_t)(MAX_ANON_CONTACTS + i),
                                                 own && has_pos ? geo::haversineKm(mlat, mlon, c.gps_lat, c.gps_lon) : -1.0f };
  }
  qsort(routeview::s_pick, routeview::s_pick_n, sizeof(routeview::Pick), routeview::cmpPick);
  static const vlist::Kind ROW = {
    theme::ROW_H,
    [](lv_obj_t* list) { return vRow(list, onRoutePickRow, NULL); },
    [](lv_obj_t* row, intptr_t i) {
      ContactInfo c;
      if (!the_mesh.getContactByIdx(routeview::s_pick[i].idx, c)) { rowSet(row, "?", NULL); return; }
      char sub[40] = "";
      float km = routeview::s_pick[i].km;
      if (km >= 0) geo::fmtDist(sub, sizeof(sub), km, (s_ui->_prefs && s_ui->_prefs->units_imperial));
      else snprintf(sub, sizeof(sub), "no position");
      int at = s_ui->_core->route.indexOf(c.id.pub_key);
      if (at >= 0) { char t[48]; snprintf(t, sizeof(t), "%s  -  hop %d", sub, at + 1); rowSet(row, c.name, t); }
      else rowSet(row, c.name, sub);
    },
  };
  for (int i = 0; i < routeview::s_pick_n; i++) vlist::add(body, &ROW, i);
  if (!routeview::s_pick_n)
    vlist::own(body, noteLabel(body, "No repeaters yet. They show up here once their advert is heard.", THEME_FONT_BODY, theme::TEXT_MUTED));
  vlist::end(body);
}

void UITask::routePick(int row) {
  if (row < 0 || row >= routeview::s_pick_n) return;
  ContactInfo c;
  if (!the_mesh.getContactByIdx(routeview::s_pick[row].idx, c)) return;
  PathEditor& R = _core->route;
  if (R.full()) { showToast("The route is full"); return; }
  if (!R.addRepeater(c.id.pub_key)) { showToast("That repeater is already the last hop"); return; }
  _screen = SCR_ROUTE;
  _nav_back = true;
  buildRoute();
  _nav_back = false;
}

// ── From the map ──────────────────────────────────────────────────────────────

// A tap on a repeater of the nodes map while the route is picked: in, or out
// again if it is already a hop.
void UITask::routeMapTap(const uint8_t* pub_key, uint8_t type) {
  PathEditor& R = _core->route;
  if (type != ADV_TYPE_REPEATER) { showToast("Pick repeaters"); return; }
  int at = R.indexOf(pub_key);
  if (at >= 0) R.remove(at);
  else if (R.full()) { showToast("The route is full"); return; }
  else if (!R.addRepeater(pub_key)) { showToast("That repeater is already the last hop"); return; }
  routeMapRefresh();
}

void UITask::routeMapDone() {
  _route_pick = false;
  _screen = SCR_ROUTE;
  _nav_back = true;
  buildRoute();
  _nav_back = false;
}

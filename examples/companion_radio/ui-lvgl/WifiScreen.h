#pragma once
// WiFi settings (Settings > WiFi, or from the map's download popup): the
// network map downloads connect to. Scan lists nearby networks to pick from;
// the password field uses the same keyboard as compose. Saved through
// lvport::saveWifi() (NVS on the board). WiFi itself stays off except while a
// download runs.
//
// Single-TU fragment: included by ui-lvgl/UITask.cpp after MapScreen.h.

static char s_wifi_names[lvport::WIFI_SCAN_MAX][33];

static void onWifiScan(lv_event_t* e) { (void)e; s_ui->wifiScan(); }
static void onWifiSave(lv_event_t* e) { (void)e; s_ui->wifiSave(); }
static void onWifiPick(lv_event_t* e) { s_ui->wifiPick((int)(uintptr_t)lv_event_get_user_data(e)); }
static void onWifiField(lv_event_t* e) { s_ui->wifiEdit((lv_obj_t*)lv_event_get_target(e)); }
static void onWifiKb(lv_event_t* e) {
  if (lv_event_get_code(e) == LV_EVENT_READY) s_ui->wifiSave();
  else s_ui->wifiKeyboardHide();
}

static lv_obj_t* wifiField(lv_obj_t* parent, const char* placeholder, bool password) {
  lv_obj_t* ta = lv_textarea_create(parent);
  lv_textarea_set_one_line(ta, true);
  lv_textarea_set_placeholder_text(ta, placeholder);
  lv_textarea_set_password_mode(ta, password);
  lv_obj_set_width(ta, LV_PCT(100));
  lv_obj_set_height(ta, 34);   // one 20 px line: 2*1 border + 2*6 pad + 20 (as the compose field)
  lv_obj_set_style_border_width(ta, 1, 0);
  lv_obj_set_style_pad_ver(ta, 6, 0);
  lv_obj_set_style_pad_hor(ta, 10, 0);
  lv_obj_set_scrollbar_mode(ta, LV_SCROLLBAR_MODE_OFF);
  lv_obj_set_style_border_color(ta, lv_color_hex(theme::ACCENT), LV_PART_CURSOR | LV_STATE_FOCUSED);
  lv_obj_set_style_border_width(ta, 2, LV_PART_CURSOR | LV_STATE_FOCUSED);
  lv_obj_add_event_cb(ta, onWifiField, LV_EVENT_CLICKED, NULL);
  return ta;
}

void UITask::showWifi(bool from_map) {
  _wifi_from_map = from_map;
  _screen = SCR_WIFI;
  buildWifi();
}

void UITask::buildWifi() {
  lv_obj_t* body = newScreen("WiFi", true);
  lv_obj_set_style_pad_row(body, 4, 0);
  label(body, "Used only to download maps; off otherwise.", THEME_FONT_SMALL, theme::TEXT_MUTED);

  lv_obj_t* row = lv_obj_create(body);
  styleSurface(row, theme::BG);
  lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_column(row, theme::GAP, 0);
  _wifi_ssid = wifiField(row, "Network name", false);
  lv_obj_set_width(_wifi_ssid, 0);
  lv_obj_set_flex_grow(_wifi_ssid, 1);
  lv_obj_t* scan = lv_button_create(row);
  lv_obj_set_size(scan, 64, 34);
  lv_obj_set_style_shadow_width(scan, 0, 0);
  lv_obj_set_style_radius(scan, theme::RADIUS, 0);
  lv_obj_set_style_bg_color(scan, lv_color_hex(theme::SURFACE), 0);
  lv_obj_add_event_cb(scan, onWifiScan, LV_EVENT_CLICKED, NULL);
  lv_obj_center(label(scan, "Scan", THEME_FONT_SMALL, theme::TEXT));

  _wifi_list = lv_obj_create(body);   // scan results, shown after a scan
  styleSurface(_wifi_list, theme::BG);
  lv_obj_remove_flag(_wifi_list, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(_wifi_list, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(_wifi_list, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_style_pad_column(_wifi_list, 4, 0);
  lv_obj_set_style_pad_row(_wifi_list, 4, 0);
  lv_obj_add_flag(_wifi_list, LV_OBJ_FLAG_HIDDEN);

  _wifi_pass = wifiField(body, "Password", true);

  lv_obj_t* save = lv_button_create(body);
  lv_obj_set_size(save, LV_PCT(100), 38);
  lv_obj_set_style_shadow_width(save, 0, 0);
  lv_obj_set_style_radius(save, theme::RADIUS, 0);
  lv_obj_set_style_bg_color(save, lv_color_hex(theme::ACCENT_DIM), 0);
  lv_obj_add_event_cb(save, onWifiSave, LV_EVENT_CLICKED, NULL);
  lv_obj_center(label(save, LV_SYMBOL_OK " Save", THEME_FONT_BODY, theme::TEXT));
  _wifi_status = label(body, "", THEME_FONT_SMALL, theme::TEXT_MUTED);

  char ssid[33], pass[65];
  lvport::loadWifi(ssid, sizeof(ssid), pass, sizeof(pass));
  lv_textarea_set_text(_wifi_ssid, ssid);
  lv_textarea_set_text(_wifi_pass, pass);

  // Keyboard over the bottom of the screen; the body shrinks above it while
  // it is up, so the field being edited stays visible.
  _wifi_kb = kb::create(lv_screen_active(), _prefs);
  lv_obj_set_size(_wifi_kb, LV_PCT(100), 124);
  lv_obj_align(_wifi_kb, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_add_event_cb(_wifi_kb, onWifiKb, LV_EVENT_READY, NULL);
  lv_obj_add_event_cb(_wifi_kb, onWifiKb, LV_EVENT_CANCEL, NULL);
  lv_obj_add_flag(_wifi_kb, LV_OBJ_FLAG_HIDDEN);
}

void UITask::wifiEdit(lv_obj_t* ta) {
  if (!_wifi_kb) return;
  lv_obj_remove_state(_wifi_ssid, LV_STATE_FOCUSED);
  lv_obj_remove_state(_wifi_pass, LV_STATE_FOCUSED);
  lv_obj_add_state(ta, LV_STATE_FOCUSED);   // draws the cursor
  lv_keyboard_set_textarea(_wifi_kb, ta);
  if (lv_obj_has_flag(_wifi_kb, LV_OBJ_FLAG_HIDDEN)) {
    lv_obj_remove_flag(_wifi_kb, LV_OBJ_FLAG_HIDDEN);
    if (_body) lv_obj_set_height(_body, lv_obj_get_height(_body) - lv_obj_get_height(_wifi_kb));
  }
  lv_obj_update_layout(lv_screen_active());
  lv_obj_scroll_to_view(ta, LV_ANIM_OFF);
}

void UITask::wifiKeyboardHide() {
  if (!_wifi_kb || lv_obj_has_flag(_wifi_kb, LV_OBJ_FLAG_HIDDEN)) return;
  lv_obj_add_flag(_wifi_kb, LV_OBJ_FLAG_HIDDEN);
  lv_obj_remove_state(_wifi_ssid, LV_STATE_FOCUSED);
  lv_obj_remove_state(_wifi_pass, LV_STATE_FOCUSED);
  if (_body) lv_obj_set_height(_body, lv_obj_get_height(_body) + lv_obj_get_height(_wifi_kb));
}

void UITask::wifiScan() {
  if (_wifi_scanning) return;
  lvport::scanStart();
  _wifi_scanning = true;
  lv_label_set_text(_wifi_status, LV_SYMBOL_REFRESH "  Scanning...");
}

void UITask::pollWifiScan() {
  if (!_wifi_scanning || !_wifi_list) return;
  int n = lvport::scanResults(s_wifi_names, lvport::WIFI_SCAN_MAX);
  if (n < 0) return;
  _wifi_scanning = false;
  lv_obj_clean(_wifi_list);
  for (int i = 0; i < n; i++) {
    lv_obj_t* b = lv_button_create(_wifi_list);
    lv_obj_set_height(b, 30);
    lv_obj_set_style_pad_hor(b, 10, 0);
    lv_obj_set_style_pad_ver(b, 0, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_radius(b, 15, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(theme::SURFACE), 0);
    lv_obj_add_event_cb(b, onWifiPick, LV_EVENT_CLICKED, (void*)(uintptr_t)i);
    lv_obj_center(label(b, s_wifi_names[i], THEME_FONT_SMALL, theme::TEXT));
  }
  if (n > 0) lv_obj_remove_flag(_wifi_list, LV_OBJ_FLAG_HIDDEN);
  lv_label_set_text(_wifi_status, n > 0 ? "Tap a network, then enter its password." : "No networks found.");
  if (!mapview::s_dl.active()) lvport::netEnd();   // the scan switched the radio on
}

void UITask::wifiPick(int idx) {
  if (idx < 0 || idx >= lvport::WIFI_SCAN_MAX) return;
  lv_textarea_set_text(_wifi_ssid, s_wifi_names[idx]);
  lv_obj_add_flag(_wifi_list, LV_OBJ_FLAG_HIDDEN);
  wifiEdit(_wifi_pass);
}

void UITask::wifiSave() {
  const char* ssid = lv_textarea_get_text(_wifi_ssid);
  const char* pass = lv_textarea_get_text(_wifi_pass);
  if (!ssid[0]) { lv_label_set_text(_wifi_status, "Enter a network name."); return; }
  lvport::saveWifi(ssid, pass);
  wifiKeyboardHide();
  showToast("WiFi saved");
  if (_wifi_from_map) { showMap(); mapDownloadPopup(); }
  else lv_label_set_text(_wifi_status, "Saved.");
}

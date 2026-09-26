#pragma once
// Settings > Radio: preset, frequency, SF / bandwidth / coding rate, TX power
// and Adaptive Power Control -- ui-new's Settings > Radio; the saved presets
// and scopes below them are RadioExtras.h. Every change is
// applied to the radio at once and saved (ui-core/RadioControl.h does the
// applying for both frontends).
//
// Single-TU fragment: included by ui-lvgl/UITask.cpp after ClockScreen.h.

namespace radioview {

enum : uint8_t { R_PRESET, R_SF, R_BW, R_CR, R_TX, R_APC };
static const int TX_MIN = 2;
#ifdef LORA_TX_POWER
static const int TX_MAX = LORA_TX_POWER;
#else
static const int TX_MAX = 22;
#endif

static lv_obj_t* s_overlay = nullptr;   // frequency entry
static lv_obj_t* s_ta = nullptr;
static bool s_freq_rpt = false;         // the entry is for the repeater profile (RepeaterScreen.h)
static const int OPTS_LEN = 1024;
static char* s_opts = psramBuf<char>(OPTS_LEN);   // dropdown options (LVGL copies them)

// Label + optional hint on the left of a settings row; the control goes on the right.
static lv_obj_t* settingRow(lv_obj_t* parent, const char* text, const char* hint) {
  lv_obj_t* row = lv_obj_create(parent);
  styleSurface(row, theme::SURFACE);
  lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(row, LV_PCT(100), theme::ROW_H);
  lv_obj_set_style_radius(row, theme::RADIUS, 0);
  lv_obj_align(label(row, text, THEME_FONT_BODY, theme::TEXT), LV_ALIGN_TOP_LEFT, theme::PAD, hint ? 5 : 13);
  if (hint) {
    lv_obj_t* h = label(row, hint, THEME_FONT_SMALL, theme::TEXT_MUTED);
    lv_label_set_long_mode(h, LV_LABEL_LONG_DOT);
    lv_obj_set_size(h, 150, 15);
    lv_obj_align(h, LV_ALIGN_BOTTOM_LEFT, theme::PAD, -5);
  }
  return row;
}

static lv_obj_t* rowDropdown(lv_obj_t* row, const char* opts, int sel, int width, lv_event_cb_t cb, uintptr_t which) {
  lv_obj_t* dd = lv_dropdown_create(row);
  lv_dropdown_set_options(dd, opts);
  if (sel >= 0) lv_dropdown_set_selected(dd, sel);
  lv_obj_set_width(dd, width);
  lv_obj_align(dd, LV_ALIGN_RIGHT_MID, -4, 0);
  lv_obj_add_event_cb(dd, cb, LV_EVENT_VALUE_CHANGED, (void*)which);
  return dd;
}

// A few choices side by side at the right of a row (a dropdown's list can run
// off the bottom of a popup). `map` ends with "".
static lv_obj_t* rowSegmented(lv_obj_t* row, const char** map, int sel, int width, lv_event_cb_t cb, uintptr_t which) {
  lv_obj_t* seg = segmented(row, map, sel, width, 36);
  lv_obj_align(seg, LV_ALIGN_RIGHT_MID, -4, 0);
  lv_obj_add_event_cb(seg, cb, LV_EVENT_VALUE_CHANGED, (void*)which);
  return seg;
}

}  // namespace radioview

static void onRadioDropdown(lv_event_t* e) {
  s_ui->radioSet((int)(uintptr_t)lv_event_get_user_data(e), (int)lv_dropdown_get_selected((lv_obj_t*)lv_event_get_target(e)));
}
static void onRadioSwitch(lv_event_t* e) {
  s_ui->radioSet((int)(uintptr_t)lv_event_get_user_data(e),
                 lv_obj_has_state((lv_obj_t*)lv_event_get_target(e), LV_STATE_CHECKED) ? 1 : 0);
}
static void onRadioFreq(lv_event_t* e)   { (void)e; s_ui->radioFreqPopup(false); }
static void onRadioFreqKb(lv_event_t* e) { s_ui->radioFreqDone(lv_event_get_code(e) == LV_EVENT_READY); }
static void onOpenRadio(lv_event_t* e)   { (void)e; s_ui->showRadio(); }

void UITask::showRadio() {
  _screen = SCR_RADIO;
  buildRadio();
}

void UITask::buildRadio() {
  using namespace radioview;
  lv_obj_t* body = newScreen("Radio", true);
  s_overlay = s_ta = nullptr;
  NodePrefs* p = _prefs;

  // Preset: "Custom" first (current params match none), then the list.
  int o = snprintf(s_opts, OPTS_LEN, "Custom");
  const char* name; float f, b; uint8_t sf, cr;
  for (int i = 0; radioctl::presetAt(p, i, name, f, b, sf, cr) && o < OPTS_LEN - 24; i++)
    o += snprintf(s_opts + o, OPTS_LEN - o, "\n%s", name);
  lv_obj_t* row = settingRow(body, "Preset", nullptr);
  lv_obj_t* dd = rowDropdown(row, s_opts, radioctl::currentPreset(p) + 1, 200, onRadioDropdown, R_PRESET);
  lv_dropdown_set_dir(dd, LV_DIR_BOTTOM);

  // Frequency: tap to type it.
  row = settingRow(body, "Frequency", "MHz");
  lv_obj_t* fb = lv_button_create(row);
  lv_obj_set_size(fb, 120, 34);
  lv_obj_align(fb, LV_ALIGN_RIGHT_MID, -4, 0);
  lv_obj_set_style_shadow_width(fb, 0, 0);
  lv_obj_set_style_radius(fb, theme::RADIUS, 0);
  lv_obj_set_style_bg_color(fb, lv_color_hex(theme::SURFACE_2), 0);
  lv_obj_add_event_cb(fb, onRadioFreq, LV_EVENT_CLICKED, NULL);
  char fs[16];
  snprintf(fs, sizeof(fs), "%.3f", p->freq);
  lv_obj_center(label(fb, fs, THEME_FONT_BODY, theme::TEXT));

  o = 0;
  for (int v = 5; v <= 12; v++) o += snprintf(s_opts + o, OPTS_LEN - o, v > 5 ? "\n%d" : "%d", v);
  rowDropdown(settingRow(body, "Spreading factor", "Higher = longer range"), s_opts,
              p->sf >= 5 && p->sf <= 12 ? p->sf - 5 : 0, 90, onRadioDropdown, R_SF);
  o = 0;
  for (int i = 0; i < LORA_BW_OPT_COUNT; i++)
    o += snprintf(s_opts + o, OPTS_LEN - o, "%s%g kHz", i ? "\n" : "", (double)LORA_BW_OPTS[i]);
  rowDropdown(settingRow(body, "Bandwidth", nullptr), s_opts, nearestBwIndex(p->bw), 130, onRadioDropdown, R_BW);
  o = 0;
  for (int v = 5; v <= 8; v++) o += snprintf(s_opts + o, OPTS_LEN - o, v > 5 ? "\n4/%d" : "4/%d", v);
  rowDropdown(settingRow(body, "Coding rate", nullptr), s_opts, p->cr >= 5 && p->cr <= 8 ? p->cr - 5 : 0, 90,
              onRadioDropdown, R_CR);

  sectionTitle(body, "TRANSMIT");
  o = 0;
  for (int v = TX_MIN; v <= TX_MAX; v++) o += snprintf(s_opts + o, OPTS_LEN - o, v > TX_MIN ? "\n%d dBm" : "%d dBm", v);
  int tx = p->tx_power_dbm < TX_MIN ? TX_MIN : p->tx_power_dbm > TX_MAX ? TX_MAX : p->tx_power_dbm;
  rowDropdown(settingRow(body, "TX power", p->tx_apc ? "Ceiling for auto power" : nullptr), s_opts, tx - TX_MIN, 110,
              onRadioDropdown, R_TX);
  row = settingRow(body, "Auto power", p->client_repeat ? "Off while repeating" : "Lowers power on good links");
  lv_obj_t* sw = lv_switch_create(row);
  lv_obj_set_size(sw, 46, 24);
  lv_obj_align(sw, LV_ALIGN_RIGHT_MID, -theme::PAD, 0);
  if (p->tx_apc) lv_obj_add_state(sw, LV_STATE_CHECKED);
  if (p->client_repeat) lv_obj_add_state(sw, LV_STATE_DISABLED);
  lv_obj_add_event_cb(sw, onRadioSwitch, LV_EVENT_VALUE_CHANGED, (void*)(uintptr_t)R_APC);

  lv_obj_t* note = label(body, "Everyone you talk to needs the same settings.",
                         THEME_FONT_SMALL, theme::TEXT_MUTED);
  lv_label_set_long_mode(note, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(note, LV_PCT(100));

  buildRadioExtras(body);   // my presets, scopes (RadioExtras.h)
}

void UITask::radioSet(int which, int v) {
  using namespace radioview;
  NodePrefs* p = _prefs;
  if (!p) return;
  switch (which) {
    case R_PRESET:
      if (v == 0) return;   // "Custom": nothing to take
      radioctl::choosePreset(p, v - 1);
      break;
    case R_SF: p->sf = (uint8_t)(5 + v); radioctl::applyParams(); break;
    case R_BW: if (v >= 0 && v < LORA_BW_OPT_COUNT) { p->bw = LORA_BW_OPTS[v]; radioctl::applyParams(); } break;
    case R_CR: p->cr = (uint8_t)(5 + v); radioctl::applyParams(); break;
    case R_TX: p->tx_power_dbm = (int8_t)(TX_MIN + v); radioctl::applyTxPower(p); break;
    case R_APC: p->tx_apc = (uint8_t)v; radioctl::applyApc(); break;
  }
  the_mesh.savePrefs();
  rebuildRadio();   // preset name / hints follow
}

// Rebuilds the screen where it was scrolled to.
void UITask::rebuildRadio() {
  int32_t y = _body ? lv_obj_get_scroll_y(_body) : 0;
  buildRadio();
  if (_body) { lv_obj_update_layout(_body); lv_obj_scroll_to_y(_body, y, LV_ANIM_OFF); }
}

void UITask::radioFreqPopup(bool repeater) {
  using namespace radioview;
  if (s_overlay) return;
  s_freq_rpt = repeater;
  s_overlay = lv_obj_create(screen());
  lv_obj_remove_style_all(s_overlay);
  lv_obj_set_size(s_overlay, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_bg_color(s_overlay, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(s_overlay, LV_OPA_60, 0);
  lv_obj_add_flag(s_overlay, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_t* panel = lv_obj_create(s_overlay);
  anim::popup(s_overlay);
  lv_obj_set_size(panel, lv_display_get_horizontal_resolution(NULL) - 16, LV_SIZE_CONTENT);
  lv_obj_align(panel, LV_ALIGN_TOP_MID, 0, theme::STATUS_H + 4);
  lv_obj_set_style_bg_color(panel, lv_color_hex(theme::BG), 0);
  lv_obj_set_style_border_color(panel, lv_color_hex(theme::ACCENT), 0);
  lv_obj_set_style_border_width(panel, 1, 0);
  lv_obj_set_style_radius(panel, theme::RADIUS, 0);
  lv_obj_set_style_pad_all(panel, theme::PAD, 0);
  lv_obj_set_style_pad_row(panel, 6, 0);
  lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
  lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
  float lo, hi;
  radio_driver.getFreqBounds(lo, hi);
  char t[48];
  snprintf(t, sizeof(t), "Frequency, MHz (%.0f - %.0f)", lo, hi);
  label(panel, t, THEME_FONT_BODY, theme::TEXT);
  s_ta = textField(panel);
  lv_textarea_set_accepted_chars(s_ta, "0123456789.");
  lv_textarea_set_max_length(s_ta, 10);
  char fs[16];
  snprintf(fs, sizeof(fs), "%.3f", repeater ? _prefs->repeater_freq : _prefs->freq);
  lv_textarea_set_text(s_ta, fs);
  lv_obj_add_state(s_ta, LV_STATE_FOCUSED);
  lv_obj_t* kbd = kb::create(s_overlay, _prefs);
  lv_obj_set_size(kbd, LV_PCT(100), 124);
  lv_obj_align(kbd, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_keyboard_set_textarea(kbd, s_ta);
  kb::apply(kbd, kb::L_SYM);
  lv_obj_add_event_cb(kbd, onRadioFreqKb, LV_EVENT_READY, NULL);
  lv_obj_add_event_cb(kbd, onRadioFreqKb, LV_EVENT_CANCEL, NULL);
}

void UITask::radioFreqDone(bool ok) {
  using namespace radioview;
  if (ok && s_ta) {
    float lo, hi;
    radio_driver.getFreqBounds(lo, hi);
    float f = strtof(lv_textarea_get_text(s_ta), nullptr);
    if (f < lo || f > hi) { showToast("Out of the radio's range"); return; }
    if (s_freq_rpt) { _prefs->repeater_freq = f; rptctl::applyProfile(); }
    else            { _prefs->freq = f; radioctl::applyParams(); }
    the_mesh.savePrefs();
  }
  radioCloseFreq();
  if (ok) { if (s_freq_rpt) rebuildRepeater(); else rebuildRadio(); }
}

void UITask::radioCloseFreq() {
  if (radioview::s_overlay) lv_obj_delete_async(radioview::s_overlay);
  radioview::s_overlay = radioview::s_ta = nullptr;
}

bool UITask::radioPopupOpen() const { return radioview::s_overlay != nullptr; }

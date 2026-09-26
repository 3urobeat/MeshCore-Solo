#pragma once
// Home > Repeater -- ui-new's Tools > Repeater: relay other people's
// messages from this device. The switch, the network it relays on (the
// companion's own frequency, or a dedicated profile with its preset / freq /
// SF / BW / CR), and the flood filters (adverts, hop limit, yield, min SNR,
// duplicate suppression, scope only, extra scopes). Every change applies at
// once and is saved; the logic is ui-core/RepeaterControl.h, the relaying MyMesh.
//
// Single-TU fragment: included at the end of ui-lvgl/UITask.cpp.

namespace rptview {

enum : uint8_t { RP_ON, RP_NETWORK, RP_PRESET, RP_SF, RP_BW, RP_CR, RP_HOPS, RP_YIELD, RP_SNR };
static const int OPTS_LEN = 768;
static char* s_opts = psramBuf<char>(OPTS_LEN);   // dropdown options (LVGL copies them)
static lv_obj_t* s_scopes_sub = nullptr;   // "Extra scopes" row's count, updated from the popup

}  // namespace rptview

static void onOpenRepeater(lv_event_t* e) { (void)e; s_ui->showRepeater(); }
static void onRptDropdown(lv_event_t* e) {
  s_ui->repeaterSet((int)(uintptr_t)lv_event_get_user_data(e), (int)lv_dropdown_get_selected((lv_obj_t*)lv_event_get_target(e)));
}
static void onRptSwitch(lv_event_t* e) {
  s_ui->repeaterSet((int)(uintptr_t)lv_event_get_user_data(e),
                    lv_obj_has_state((lv_obj_t*)lv_event_get_target(e), LV_STATE_CHECKED) ? 1 : 0);
}
static void onRptNetwork(lv_event_t* e) {
  s_ui->repeaterSet(rptview::RP_NETWORK, (int)lv_buttonmatrix_get_selected_button((lv_obj_t*)lv_event_get_target(e)));
}
static void onRptFreq(lv_event_t* e)   { (void)e; s_ui->radioFreqPopup(true); }
static void onRptScopes(lv_event_t* e) { (void)e; s_ui->repeaterScopesPopup(); }
static void onRptScopeSwitch(lv_event_t* e) {
  s_ui->repeaterScopeSet((uint8_t)(uintptr_t)lv_event_get_user_data(e),
                         lv_obj_has_state((lv_obj_t*)lv_event_get_target(e), LV_STATE_CHECKED));
}

static void extraScopesSummary(const NodePrefs* p, char* b, int n) {
  const ScopeList& sl = the_mesh.scopeList();
  if (sl.count == 0) snprintf(b, n, "No scopes set up (Settings > Radio)");
  else snprintf(b, n, "%d of %u relayed besides the default", rptctl::extraScopesPicked(p), (unsigned)sl.count);
}

void UITask::showRepeater() {
  _screen = SCR_REPEATER;
  buildRepeater();
}

void UITask::buildRepeater() {
  using namespace rptview;
  using radioview::rowDropdown;
  lv_obj_t* body = newScreen("Repeater", true);
  NodePrefs* p = _prefs;

  lv_obj_t* row = settingRow(body, "Repeater", p->client_repeat ? "Relaying for others" : "Relay others' messages");
  lv_obj_t* sw = lv_switch_create(row);
  lv_obj_set_size(sw, 46, 24);
  lv_obj_align(sw, LV_ALIGN_RIGHT_MID, -theme::PAD, 0);
  if (p->client_repeat) lv_obj_add_state(sw, LV_STATE_CHECKED);
  lv_obj_add_event_cb(sw, onRptSwitch, LV_EVENT_VALUE_CHANGED, (void*)(uintptr_t)RP_ON);
  lv_obj_t* t = label(body, "Uses more battery; auto power pauses.",
                      THEME_FONT_SMALL, theme::TEXT_MUTED);
  lv_label_set_long_mode(t, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(t, LV_PCT(100));

  sectionTitle(body, "NETWORK");
  row = settingRow(body, "Relay on", p->repeater_use_profile ? "Its own frequency" : "Your chat frequency");
  static const char* NET[] = { "Current", "Custom", "" };
  lv_obj_t* seg = segmented(row, NET, p->repeater_use_profile ? 1 : 0, 150, 34);
  lv_obj_align(seg, LV_ALIGN_RIGHT_MID, -4, 0);
  lv_obj_add_event_cb(seg, onRptNetwork, LV_EVENT_VALUE_CHANGED, NULL);
  if (p->repeater_use_profile) {
    int o = snprintf(s_opts, OPTS_LEN, "Custom");
    const char* name; float f, b; uint8_t sf, cr;
    for (int i = 0; radioctl::presetAt(p, i, name, f, b, sf, cr) && o < OPTS_LEN - 24; i++)
      o += snprintf(s_opts + o, OPTS_LEN - o, "\n%s", name);
    lv_obj_t* dd = rowDropdown(settingRow(body, "Preset", nullptr), s_opts, rptctl::currentPreset(p) + 1, 200,
                               onRptDropdown, RP_PRESET);
    lv_dropdown_set_dir(dd, LV_DIR_BOTTOM);

    row = settingRow(body, "Frequency", "MHz");
    lv_obj_t* fb = lv_button_create(row);
    lv_obj_set_size(fb, 120, 34);
    lv_obj_align(fb, LV_ALIGN_RIGHT_MID, -4, 0);
    lv_obj_set_style_shadow_width(fb, 0, 0);
    lv_obj_set_style_radius(fb, theme::RADIUS, 0);
    lv_obj_set_style_bg_color(fb, lv_color_hex(theme::SURFACE_2), 0);
    lv_obj_add_event_cb(fb, onRptFreq, LV_EVENT_CLICKED, NULL);
    char fs[16];
    snprintf(fs, sizeof(fs), "%.3f", p->repeater_freq);
    lv_obj_center(label(fb, fs, THEME_FONT_BODY, theme::TEXT));

    o = 0;
    for (int v = 5; v <= 12; v++) o += snprintf(s_opts + o, OPTS_LEN - o, v > 5 ? "\n%d" : "%d", v);
    rowDropdown(settingRow(body, "Spreading factor", nullptr), s_opts,
                p->repeater_sf >= 5 && p->repeater_sf <= 12 ? p->repeater_sf - 5 : 0, 90, onRptDropdown, RP_SF);
    o = 0;
    for (int i = 0; i < LORA_BW_OPT_COUNT; i++)
      o += snprintf(s_opts + o, OPTS_LEN - o, "%s%g kHz", i ? "\n" : "", (double)LORA_BW_OPTS[i]);
    rowDropdown(settingRow(body, "Bandwidth", nullptr), s_opts, nearestBwIndex(p->repeater_bw), 130, onRptDropdown, RP_BW);
    o = 0;
    for (int v = 5; v <= 8; v++) o += snprintf(s_opts + o, OPTS_LEN - o, v > 5 ? "\n4/%d" : "4/%d", v);
    rowDropdown(settingRow(body, "Coding rate", nullptr), s_opts,
                p->repeater_cr >= 5 && p->repeater_cr <= 8 ? p->repeater_cr - 5 : 0, 90, onRptDropdown, RP_CR);
  }

  sectionTitle(body, "WHAT IT RELAYS");
  switchRow(body, "Skip adverts", "Relay messages, not adverts", &p->repeat_skip_adverts);
  char v[12];
  int o = 0;
  for (int i = 0; i <= rptctl::MAX_HOPS; i++) {
    rptctl::fmtHops(v, sizeof(v), (uint8_t)i);
    o += snprintf(s_opts + o, OPTS_LEN - o, "%s%s", i ? "\n" : "", v);
  }
  rowDropdown(settingRow(body, "Max hops", "Hop limit for floods"), s_opts, p->repeat_max_hops, 90,
              onRptDropdown, RP_HOPS);
  o = 0;
  for (int i = 0; i <= rptctl::MAX_YIELD; i++) {
    rptctl::fmtYield(v, sizeof(v), (uint8_t)i);
    o += snprintf(s_opts + o, OPTS_LEN - o, "%s%s", i ? "\n" : "", v);
  }
  rowDropdown(settingRow(body, "Yield", "Let fixed repeaters go first"), s_opts, p->repeat_delay_boost, 90,
              onRptDropdown, RP_YIELD);
  o = 0;
  for (int i = 0; i < rptctl::snrChoiceCount(); i++) {
    rptctl::fmtSnr(v, sizeof(v), rptctl::snrFromChoice(i));
    o += snprintf(s_opts + o, OPTS_LEN - o, "%s%s", i ? "\n" : "", v);
  }
  rowDropdown(settingRow(body, "Min SNR", "Ignore weaker packets"), s_opts, rptctl::snrToChoice(p->repeat_min_snr), 100,
              onRptDropdown, RP_SNR);
  switchRow(body, "Skip duplicates", "Skip if already relayed", &p->repeat_suppress_dup);
  switchRow(body, "Scope only", "Only floods in your scopes", &p->repeat_scope_only);
  char sub[48];
  extraScopesSummary(p, sub, sizeof(sub));
  s_scopes_sub = lv_obj_get_child(listRow(body, LV_SYMBOL_LIST "  Extra scopes", sub, onRptScopes, NULL), 1);
}

// Rebuilds the screen where it was scrolled to.
void UITask::rebuildRepeater() {
  int32_t y = _body ? lv_obj_get_scroll_y(_body) : 0;
  buildRepeater();
  if (_body) { lv_obj_update_layout(_body); lv_obj_scroll_to_y(_body, y, LV_ANIM_OFF); }
}

void UITask::repeaterSet(int which, int v) {
  using namespace rptview;
  NodePrefs* p = _prefs;
  if (!p) return;
  switch (which) {
    case RP_ON:
      rptctl::setEnabled(p, v != 0);
      showToast(v ? "Repeater on" : "Repeater off");
      break;
    case RP_NETWORK: rptctl::setUseProfile(p, v != 0); break;
    case RP_PRESET:  if (v == 0) return; rptctl::choosePreset(p, v - 1); break;
    case RP_SF: p->repeater_sf = (uint8_t)(5 + v); rptctl::applyProfile(); break;
    case RP_BW: if (v >= 0 && v < LORA_BW_OPT_COUNT) { p->repeater_bw = LORA_BW_OPTS[v]; rptctl::applyProfile(); } break;
    case RP_CR: p->repeater_cr = (uint8_t)(5 + v); rptctl::applyProfile(); break;
    case RP_HOPS:  p->repeat_max_hops = (uint8_t)v; break;
    case RP_YIELD: p->repeat_delay_boost = (uint8_t)v; break;
    case RP_SNR:   p->repeat_min_snr = rptctl::snrFromChoice(v); break;
  }
  the_mesh.savePrefs();
  if (which <= RP_CR) rebuildRepeater();   // hints, the profile rows and the preset name follow
  refreshStatusBar();
}

// A switch per named scope: relayed besides the default one.
void UITask::repeaterScopesPopup() {
  const ScopeList& sl = the_mesh.scopeList();
  if (sl.count == 0) { showToast("Set up scopes in Settings > Radio first"); return; }
  lv_obj_t* panel = navPopupPanel("Extra scopes", sl.count > 2);
  if (sl.count > 2) lv_obj_add_flag(panel, LV_OBJ_FLAG_SCROLLABLE);   // up to 8 rows
  lv_obj_t* t = label(panel, "Relayed besides the default scope.",
                      THEME_FONT_SMALL, theme::TEXT_MUTED);
  lv_label_set_long_mode(t, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(t, LV_PCT(100));
  for (uint8_t i = 0; i < sl.count; i++) {
    lv_obj_t* sw = switchRow(panel, sl.name((uint8_t)(i + 1)), (i + 1) == sl.default_idx ? "Default - always relayed" : nullptr,
                             nullptr);
    if (rptctl::extraScope(_prefs, i)) lv_obj_add_state(sw, LV_STATE_CHECKED);
    lv_obj_add_event_cb(sw, onRptScopeSwitch, LV_EVENT_VALUE_CHANGED, (void*)(uintptr_t)i);
  }
}

void UITask::repeaterScopeSet(uint8_t i, bool on) {
  rptctl::setExtraScope(_prefs, i, on);
  the_mesh.savePrefs();
  char sub[48];
  extraScopesSummary(_prefs, sub, sizeof(sub));
  if (rptview::s_scopes_sub) lv_label_set_text(rptview::s_scopes_sub, sub);
}

#pragma once
// Radio settings actions shared by the frontends: push the companion's
// NodePrefs radio fields (freq / bw / sf / cr, TX power, Adaptive Power
// Control) to the radio, and the preset list (../RadioPresets.h built-ins,
// then the user's saved presets in NodePrefs) the Settings > Radio pickers show.
// ui-new's Settings / Repeater screens and ui-lvgl's Radio screen both go
// through here.

#include "../RadioPresets.h"

namespace radioctl {

// freq / bw / sf / cr -> the radio (or the repeater profile, while relaying with one).
static void applyParams() { the_mesh.applyRepeaterRadio(); }

static void applyTxPower(const NodePrefs* p) {
  if (!p) return;
  // With APC on, tx_power_dbm is the ceiling -- re-baseline the controller to
  // it (which also sets the radio) so the live power tracks the new ceiling.
  if (p->tx_apc) { the_mesh.applyApc(); return; }
  radio_driver.setTxPower(p->tx_power_dbm);
}

static void applyApc() { the_mesh.applyApc(); }   // (re)initialise Adaptive Power Control from prefs

// ── Presets ─────────────────────────────────────────────────────────────────
// One list: built-ins 0..RADIO_PRESET_COUNT-1, then the non-empty user slots.

static int presetCount(const NodePrefs* p) {
  int n = RADIO_PRESET_COUNT;
  for (int i = 0; p && i < NodePrefs::USER_RADIO_PRESET_MAX; i++) if (p->user_radio_presets[i].name[0]) n++;
  return n;
}

// Preset `idx` of the list; false past the end.
static bool presetAt(const NodePrefs* p, int idx, const char*& name, float& freq, float& bw, uint8_t& sf, uint8_t& cr) {
  if (idx < 0) return false;
  if (idx < RADIO_PRESET_COUNT) {
    const RadioPreset& r = RADIO_PRESETS[idx];
    name = r.name; freq = r.freq; bw = r.bw; sf = r.sf; cr = r.cr;
    return true;
  }
  int k = idx - RADIO_PRESET_COUNT;
  for (int i = 0; p && i < NodePrefs::USER_RADIO_PRESET_MAX; i++) {
    const NodePrefs::UserRadioPreset& u = p->user_radio_presets[i];
    if (!u.name[0]) continue;
    if (k-- == 0) { name = u.name; freq = u.freq; bw = u.bw; sf = u.sf; cr = u.cr; return true; }
  }
  return false;
}

// List index of the preset the current companion params match, else -1 ("Custom").
static int currentPreset(const NodePrefs* p) {
  if (!p) return -1;
  const char* name; float f, b; uint8_t s, c;
  for (int i = 0; presetAt(p, i, name, f, b, s, c); i++)
    if (radioParamsMatchPreset(p->freq, p->bw, p->sf, p->cr, f, b, s, c)) return i;
  return -1;
}

// Take preset `idx` into the companion params and apply it.
static bool choosePreset(NodePrefs* p, int idx) {
  const char* name; float f, b; uint8_t s, c;
  if (!p || !presetAt(p, idx, name, f, b, s, c)) return false;
  p->freq = f; p->bw = b; p->sf = s; p->cr = c;
  applyParams();
  return true;
}

}  // namespace radioctl

#pragma once
// Radio settings actions shared by the frontends: push the companion's
// NodePrefs radio fields (freq / bw / sf / cr, TX power, Adaptive Power
// Control) to the radio, the preset list (../RadioPresets.h built-ins, then
// the user's saved presets in NodePrefs) the Settings > Radio pickers show,
// and saving / deleting those user presets.
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

// ── The user's saved presets (NodePrefs::user_radio_presets) ────────────────

// Whether saving one more under a new name would replace the oldest slot.
static bool userPresetsFull(const NodePrefs* p) {
  for (int i = 0; p && i < NodePrefs::USER_RADIO_PRESET_MAX; i++) if (!p->user_radio_presets[i].name[0]) return false;
  return p != nullptr;
}

// Saves freq / bw / sf / cr under `name`: over a slot with the same name if
// there is one, else the first empty slot, else slot 0 (the oldest). The
// caller saves the prefs. False for an empty name.
static bool saveUserPreset(NodePrefs* p, const char* name, float freq, float bw, uint8_t sf, uint8_t cr) {
  if (!p || !name || !name[0]) return false;
  int slot = -1;
  for (int i = 0; i < NodePrefs::USER_RADIO_PRESET_MAX; i++)
    if (strcmp(p->user_radio_presets[i].name, name) == 0) { slot = i; break; }
  if (slot < 0)
    for (int i = 0; i < NodePrefs::USER_RADIO_PRESET_MAX; i++)
      if (!p->user_radio_presets[i].name[0]) { slot = i; break; }
  if (slot < 0) slot = 0;
  NodePrefs::UserRadioPreset& u = p->user_radio_presets[slot];
  strncpy(u.name, name, sizeof(u.name) - 1);
  u.name[sizeof(u.name) - 1] = '\0';
  u.freq = freq; u.bw = bw; u.sf = sf; u.cr = cr;
  return true;
}

static void deleteUserPreset(NodePrefs* p, int slot) {
  if (p && slot >= 0 && slot < NodePrefs::USER_RADIO_PRESET_MAX) p->user_radio_presets[slot].name[0] = '\0';
}

}  // namespace radioctl

#pragma once
// Battery level shared by the frontends' status bars (and ui-new's dashboard
// Batt% field), so both report the same number for the same voltage, plus the
// Settings > Battery display choice (NodePrefs::batt_display_mode).

#include <stdint.h>

#ifndef BATT_MIN_MILLIVOLTS
  #define BATT_MIN_MILLIVOLTS 3200
#endif

namespace battery {

enum Mode : uint8_t { ICON, PERCENT, VOLTAGE, MODE_COUNT };   // NodePrefs::batt_display_mode

// LiPo discharge curve: voltage (mV) -> raw capacity (%). low_mv (typically
// NodePrefs.low_batt_mv, the user-configurable auto-shutdown threshold in
// Settings) is rescaled to 0% so the bar empties at the cutoff the user
// actually cares about.
inline int percent(int mv, int low_mv) {
  static const struct { uint16_t mv; uint8_t pct; } CURVE[] = {
    {3200,  0}, {3300,  3}, {3400,  8}, {3500, 15},
    {3600, 25}, {3650, 33}, {3700, 45}, {3750, 58},
    {3800, 68}, {3900, 77}, {4000, 86}, {4100, 93}, {4170, 100}
  };
  static const int CURVE_LEN = sizeof(CURVE) / sizeof(CURVE[0]);
  auto curveAt = [&](int v) -> int {
    if (v <= (int)CURVE[0].mv) return CURVE[0].pct;
    if (v >= (int)CURVE[CURVE_LEN-1].mv) return CURVE[CURVE_LEN-1].pct;
    for (int i = 1; i < CURVE_LEN; i++) {
      if (v <= (int)CURVE[i].mv) {
        int span_mv  = CURVE[i].mv  - CURVE[i-1].mv;
        int span_pct = CURVE[i].pct - CURVE[i-1].pct;
        return CURVE[i-1].pct + (v - (int)CURVE[i-1].mv) * span_pct / span_mv;
      }
    }
    return 100;
  };
  if (low_mv <= 0) low_mv = BATT_MIN_MILLIVOLTS;
  int raw_pct = curveAt(mv);
  int low_pct = curveAt(low_mv);
  int pct = (low_pct >= 100) ? 0 : (raw_pct - low_pct) * 100 / (100 - low_pct);
  if (pct < 0)   pct = 0;
  if (pct > 100) pct = 100;
  return pct;
}

inline Mode mode(uint8_t pref) { return pref < MODE_COUNT ? (Mode)pref : ICON; }

}  // namespace battery

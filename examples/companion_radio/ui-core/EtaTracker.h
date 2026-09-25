#pragma once
// Closing-speed / ETA estimator for the "navigate to a point" views. The caller
// keeps one instance per navigation session and feeds it the live target
// distance. Speed is the rate the gap closes (smoothed), so it works for both a
// fixed waypoint and a moving live contact; ETA = remaining distance / closing
// speed, meaningful only while actually approaching.

#include <stdint.h>
#include <stdio.h>

namespace navview {

struct EtaTracker {
  float    prev_km     = -1.0f;
  uint32_t prev_ms     = 0;
  float    closing_kmh = 0.0f;   // > 0 = gap shrinking (approaching)
  bool     have        = false;
  void reset() { prev_km = -1.0f; have = false; closing_kmh = 0.0f; }
  void update(float dist_km, uint32_t now_ms) {
    if (prev_km >= 0.0f && now_ms > prev_ms) {
      float dt_h = (now_ms - prev_ms) / 3600000.0f;
      if (dt_h > 0.0f) {
        float inst = (prev_km - dist_km) / dt_h;            // +approaching, -receding
        closing_kmh = have ? (closing_kmh * 0.6f + inst * 0.4f) : inst;  // light EMA
        have = true;
      }
    }
    prev_km = dist_km;
    prev_ms = now_ms;
  }
  // "12m" / "1h05m" into out, or false while not approaching.
  bool eta(float dist_km, char* out, size_t n) const {
    if (!have || closing_kmh <= 0.3f) return false;
    int secs = (int)(dist_km / closing_kmh * 3600.0f);
    if (secs < 3600) snprintf(out, n, "%dm", (secs + 59) / 60);
    else             snprintf(out, n, "%dh%02dm", secs / 3600, (secs % 3600) / 60);
    return true;
  }
};

}  // namespace navview

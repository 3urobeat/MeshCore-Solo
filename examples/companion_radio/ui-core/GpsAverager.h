#pragma once
// GPS averaging for marking a waypoint (NodePrefs::gps_avg_idx): collect one
// fix a second for N seconds and mark their mean -- a steadier position than a
// single fix. The caller feeds the current fix on every poll; sampling is on
// its own 1 s gate, independent of how often the screen redraws. int64 sums:
// 30 samples x ~180e6 would overflow int32.

#include <stdint.h>

class GpsAverager {
public:
  enum Step : uint8_t { RUNNING, DONE, NO_FIX };

  void start(uint16_t secs, int32_t lat, int32_t lon) {
    _sum_lat = lat; _sum_lon = lon; _n = 1;
    uint32_t now = millis();
    _end_ms = now + (uint32_t)secs * 1000UL;
    _next_ms = now + 1000UL;
    _total_s = secs;
    _active = true;
  }
  void cancel() { _active = false; }
  bool active() const { return _active; }

  // DONE: lat/lon hold the mean (averaging is over). NO_FIX: the window closed
  // without a single fix. RUNNING otherwise.
  Step poll(bool have_fix, int32_t fix_lat, int32_t fix_lon, int32_t& lat, int32_t& lon) {
    if (!_active) return NO_FIX;
    uint32_t now = millis();
    if ((int32_t)(now - _next_ms) >= 0) {
      if (have_fix) { _sum_lat += fix_lat; _sum_lon += fix_lon; _n++; }
      _next_ms = now + 1000UL;
    }
    if ((int32_t)(now - _end_ms) < 0) return RUNNING;
    _active = false;
    if (_n == 0) return NO_FIX;
    lat = (int32_t)(_sum_lat / (long long)_n);
    lon = (int32_t)(_sum_lon / (long long)_n);
    return DONE;
  }

  int      remainingSecs() const { int r = (int)((int32_t)(_end_ms - millis()) / 1000); return r < 0 ? 0 : r; }
  uint32_t samples() const { return _n; }
  uint16_t totalSecs() const { return _total_s; }

private:
  long long _sum_lat = 0, _sum_lon = 0;
  uint32_t  _n = 0;
  uint32_t  _end_ms = 0, _next_ms = 0;
  uint16_t  _total_s = 0;
  bool      _active = false;
};

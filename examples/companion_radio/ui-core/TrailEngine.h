#pragma once
// GPS trail: the RAM TrailStore plus its background sampling (runs while the
// trail is active, independent of which screen is shown), auto-pause, and the
// pre-shutdown auto-save. Recording control (start/stop/clear/save/load) is
// still done by TrailScreen directly on the store.

#include "../Trail.h"
#include "../GeoUtils.h"
#include "CourseEngine.h"

class TrailEngine {
public:
  void begin(NodePrefs* prefs, const CourseEngine* course) { _prefs = prefs; _course = course; }

  // Skips silently if no GPS fix; the min-delta gate inside addPoint() avoids
  // near-stationary spam.
  void loop() {
    if (!_trail.isActive()) _pause_has_ref = false;   // fresh ref on next start
    if (!_trail.isActive() || _prefs == NULL
        || (int32_t)(millis() - _next_sample_ms) < 0) return;
    _next_sample_ms = millis() + (uint32_t)TrailStore::SAMPLING_SECS * 1000UL;
    int32_t la, lo;
    if (!_course->currentLocation(la, lo)) return;
    uint16_t md = TrailStore::minDeltaMeters(_prefs->trail_min_delta_idx, _prefs->units_imperial);
    // Auto-pause: freeze the trail once the device has stayed within
    // TRAIL_AUTOPAUSE_MOVE_M of one spot for the configured delay; resume on
    // the next real move. Its own coarse gate (not the trail min-delta) so
    // GPS jitter while parked doesn't keep the idle timer alive.
    uint16_t ap = NodePrefs::trailAutoPauseSecs(_prefs->trail_autopause_idx);
    if (ap > 0) {
      uint32_t now = millis();
      float moved = _pause_has_ref
          ? geo::haversineKm(_pause_ref_lat, _pause_ref_lon, la, lo) * 1000.0f
          : 1e9f;
      if (!_pause_has_ref || moved >= (float)NodePrefs::TRAIL_AUTOPAUSE_MOVE_M) {
        _pause_ref_lat = la; _pause_ref_lon = lo;
        _pause_has_ref = true;
        _last_move_ms  = now;
        if (_trail.isPaused()) _trail.setPaused(false);
      } else if (!_trail.isPaused() && (now - _last_move_ms) >= (uint32_t)ap * 1000UL) {
        _trail.setPaused(true);
      }
    } else if (_trail.isPaused()) {
      _trail.setPaused(false);   // feature turned off → resume
    }
    if (!_trail.isPaused())
      _trail.addPoint(la, lo, (uint32_t)rtc_clock.getCurrentTime(), md);
  }

  // True while a trail is recording and not auto-paused (needs live GPS).
  bool isRecording() const { return _trail.isActive() && !_trail.isPaused(); }

  // Auto-save the live trail before power-off when the user enabled it
  // (Tools › Trail › Settings › Auto-save). This covers the low-battery
  // auto-shutdown, which otherwise loses the whole route. Overwrites /trail
  // (same file as the manual Trail › Save); guarded on count()>0 so an empty
  // trail can't wipe a previously saved one.
  void onShutdown() {
    if (!_prefs || !_prefs->trail_autosave_lowbatt || _trail.count() == 0) return;
    DataStore* ds = the_mesh.getDataStore();
    if (!ds) return;
    File f = ds->openWrite("/trail");
    if (f) { _trail.writeTo(f); f.close(); }
  }

  TrailStore& store() { return _trail; }

private:
  NodePrefs*          _prefs  = nullptr;
  const CourseEngine* _course = nullptr;
  TrailStore _trail;
  uint32_t _next_sample_ms = 0;
  // Auto-pause: _pause_ref is the last position the device was considered
  // "at"; if it doesn't move beyond TRAIL_AUTOPAUSE_MOVE_M for the configured
  // delay, the trail is auto-paused.
  int32_t  _pause_ref_lat = 0, _pause_ref_lon = 0;
  bool     _pause_has_ref = false;
  uint32_t _last_move_ms = 0;
};

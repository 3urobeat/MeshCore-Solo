#pragma once
// Clock tools engine: the alarm (wall-clock, persisted in NodePrefs), the
// countdown timer and the ring window that follows either firing. Runs from
// UiCore::loop() every frontend loop so it fires regardless of the current
// screen. The stopwatch is pure view state and stays in the frontend.
//
// The alarm is scheduled as an ABSOLUTE wall instant, recomputed from the stored
// time-of-day, so it survives RTC re-syncs (mesh/app/GPS/CLI all jump the
// clock) -- small corrections still fire on time, a jump over the target still
// fires (late). Timer + ring are millis-based.
//
// Output: UiEventType::ClockAlert when something fires, ClockRingEnded when the
// ring window lapses undismissed. While isRinging() the frontend keeps the
// melody going (it owns the buzzer).

#include "UiEvents.h"

class ClockEngine {
public:
  static const uint32_t RING_MS            = 60000;
  static const uint32_t ALARM_CATCHUP_SECS = 6 * 3600;  // fire late up to 6 h, else reschedule

  void begin(NodePrefs* prefs, UiEventQueue* events) { _prefs = prefs; _events = events; }

  void loop() {
    uint32_t now_ms = millis();
    // Ring window. Signed-difference compares (like the trail/loc-share timers)
    // so deadlines landing past the millis() rollover don't read as elapsed.
    if (_ringing && (int32_t)(now_ms - _ring_until_ms) >= 0) {
      _ringing = false;
      _events->push(UiEventType::ClockRingEnded);
    }
    // Countdown timer (millis -- sync-immune).
    if (_timer_running && (int32_t)(now_ms - _timer_deadline_ms) >= 0) {
      _timer_running = false;
      fire("Timer done");
    }
    // Alarm (wall clock -- absolute schedule for sync robustness).
    evaluateAlarm();
  }

  // ── Alarm ──────────────────────────────────────────────────────────────────
  void onAlarmChanged() { _alarm_next_fire = 0; }   // re-schedule after an alarm edit

  // ── Countdown ──────────────────────────────────────────────────────────────
  void startTimer(uint32_t duration_ms) { _timer_running = true; _timer_deadline_ms = millis() + duration_ms; }
  void stopTimer() { _timer_running = false; }
  bool isTimerRunning() const { return _timer_running; }
  uint32_t timerRemainingMs() const {
    if (!_timer_running) return 0;
    uint32_t now = millis();
    if ((int32_t)(now - _timer_deadline_ms) >= 0) return 0;
    return _timer_deadline_ms - now;
  }

  // ── Ring ───────────────────────────────────────────────────────────────────
  bool isRinging() const { return _ringing; }
  void dismissRing() { _ringing = false; }

private:
  void fire(const char* label) {
    _ringing = true;
    _ring_until_ms = millis() + RING_MS;
    _events->push(UiEventType::ClockAlert, label);
  }

  // Next absolute wall instant matching alarm_hour:alarm_min in local time,
  // strictly after now_wall (an alarm set to the current minute waits a day).
  // With alarm_repeat_mask == 0 that's just tomorrow's occurrence (one-shot).
  // With a repeat mask set, scan today..+6 days for the next weekday whose bit
  // is set (struct tm's tm_wday convention, same as the mask) -- today counts
  // only if its time hasn't already passed.
  uint32_t computeAlarmNextFire(uint32_t now_wall) const {
    int tz = _prefs->tz_offset_hours;
    int64_t now_local = (int64_t)now_wall + (int64_t)tz * 3600;
    time_t t = (time_t)now_local;
    struct tm* ti = gmtime(&t);
    int64_t sod = ti->tm_hour * 3600 + ti->tm_min * 60 + ti->tm_sec;  // secs since local midnight
    int64_t midnight = now_local - sod;
    int64_t time_of_day = (int64_t)_prefs->alarm_hour * 3600 + (int64_t)_prefs->alarm_min * 60;
    uint8_t mask = _prefs->alarm_repeat_mask;
    if (mask != 0) {
      for (int d = 0; d < 7; d++) {
        if (mask & (1 << ((ti->tm_wday + d) % 7))) {
          int64_t target = midnight + (int64_t)d * 86400 + time_of_day;
          if (target > now_local) return (uint32_t)(target - (int64_t)tz * 3600);
        }
      }
      // Mask had no bit set (shouldn't happen -- the UI only offers non-empty
      // presets) -- fall through to the one-shot calculation so it still fires.
    }
    int64_t target = midnight + time_of_day;
    if (target <= now_local) target += 86400;
    return (uint32_t)(target - (int64_t)tz * 3600);
  }

  void evaluateAlarm() {
    if (!_prefs || !_prefs->alarm_on) return;
    uint32_t now_ms = millis();
    if (now_ms - _alarm_check_ms < 500) return;   // ~2 Hz is plenty for a minute alarm
    _alarm_check_ms = now_ms;
    uint32_t now_wall = rtc_clock.getCurrentTime();
    if (now_wall < 1000000000UL) return;           // need a real time sync first
    if (_alarm_next_fire == 0) _alarm_next_fire = computeAlarmNextFire(now_wall);
    if (now_wall < _alarm_next_fire) return;
    if (now_wall - _alarm_next_fire < ALARM_CATCHUP_SECS) {
      char lbl[20];
      snprintf(lbl, sizeof(lbl), "Alarm %02d:%02d", _prefs->alarm_hour, _prefs->alarm_min);
      if (_prefs->alarm_repeat_mask == 0) {
        _prefs->alarm_on = 0;                     // one-shot
        the_mesh.savePrefs();
      }
      // Repeating: alarm_on stays set: computeAlarmNextFire() re-arms it for the
      // next matching weekday.
      _alarm_next_fire = 0;
      fire(lbl);
    } else {
      // Clock jumped implausibly far past the target -- reschedule rather than
      // ringing absurdly late.
      _alarm_next_fire = computeAlarmNextFire(now_wall);
    }
  }

  NodePrefs*    _prefs  = nullptr;
  UiEventQueue* _events = nullptr;
  uint32_t _alarm_next_fire = 0;   // unix; 0 = (re)compute lazily once time is valid
  uint32_t _alarm_check_ms  = 0;   // throttle the wall-clock read to ~2 Hz
  bool     _timer_running = false;
  uint32_t _timer_deadline_ms = 0;
  bool     _ringing = false;
  uint32_t _ring_until_ms = 0;
};

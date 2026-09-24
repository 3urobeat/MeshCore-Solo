#pragma once
// Core → frontend events. Engines never call into the frontend (no drawing, no
// sound, no display power); they push a tagged event here and the frontend
// drains the queue from its own loop() and reacts in its own way (ui-new:
// alert overlay + buzzer; ui-lvgl: a dialog). See docs/development/ui-core.md.
//
// Fixed-size ring, no heap. When full the oldest event is dropped -- events are
// hints for the view; the authoritative state stays queryable on the engines.

enum class UiEventType : uint8_t {
  None = 0,
  ClockAlert,     // alarm / countdown fired: wake, show `text`, start the ring melody
  ClockRingEnded, // ring window elapsed with no dismiss: stop melody, clear alert
};

struct UiEvent {
  UiEventType type;
  char        text[20];
};

class UiEventQueue {
public:
  static const int SIZE = 8;

  UiEventQueue() : _head(0), _count(0) {}

  void push(UiEventType type, const char* text = nullptr) {
    int pos;
    if (_count < SIZE) { pos = (_head + _count) % SIZE; _count++; }
    else               { pos = _head; _head = (_head + 1) % SIZE; }   // drop oldest
    _q[pos].type = type;
    if (text) {
      strncpy(_q[pos].text, text, sizeof(_q[pos].text) - 1);
      _q[pos].text[sizeof(_q[pos].text) - 1] = '\0';
    } else {
      _q[pos].text[0] = '\0';
    }
  }

  bool pop(UiEvent& out) {
    if (_count == 0) return false;
    out = _q[_head];
    _head = (_head + 1) % SIZE;
    _count--;
    return true;
  }

private:
  UiEvent _q[SIZE];
  int _head, _count;
};

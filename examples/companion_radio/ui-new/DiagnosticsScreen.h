#pragma once
#include <helpers/ui/DisplayDriver.h>
#include <helpers/ui/UIScreen.h>
#include <Arduino.h>
#include "icons.h"
#include "PopupMenu.h"
#include "TabBar.h"
#include "../MyMesh.h"
#include "../ui-core/Diagnostics.h"

extern MyMesh the_mesh;

// Device diagnostics, organised as a circular tab carousel (shared TabBar.h,
// same idiom as BotScreen/NearbyScreen). LEFT/RIGHT switches tabs; UP/DOWN
// scrolls within the active tab:
//   Live   — live counters: uptime, packet counts by category (RX/TX),
//            forwarded count, heap/stack headroom, radio signal, pool/queue
//            depth, error flags. Hold Enter resets the counters.
//   System — static device identity: firmware version + build date, device
//            model, node name, and the active radio parameters.
//   Font   — a rendering test card: one sample line per script the UI font
//            claims to cover (Latin, diacritics, Greek, Cyrillic, digits,
//            symbols), so the on-device font can be eyeballed for coverage.
// The rows themselves come from the UI Core (ui-core/Diagnostics.h). Every tab
// falls back to scrolling on screens too small to show its rows at once.
class DiagnosticsScreen : public UIScreen {
  UITask* _task;
  int _scroll = 0;
  uint8_t _tab = 0;        // persists across visits (like BotScreen's _tab)
  PopupMenu _reset_menu;   // Live tab, Hold Enter → Reset/Cancel confirm (defaults to Cancel)

  enum Tab : uint8_t { TAB_LIVE, TAB_SYSTEM, TAB_FONT, TAB_COUNT };
  static const char* const TAB_LABELS[TAB_COUNT];

  // Rows / lines built by the Core (ui-core/Diagnostics.h).
  diag::Row _rows[diag::MAX_ROWS];
  int _row_count = 0;
  diag::Line _lines[diag::MAX_LINES];
  int _line_count = 0;

  // Shared scrollable renderer for the label/value Live tab. Neither tab has a
  // row cursor -- UP/DOWN move _scroll directly -- so this passes _scroll as
  // drawList()'s `sel` too: its internal clamp-toward-sel is then a no-op
  // (sel == scroll always), leaving clampScroll() below as the only thing
  // that actually bounds _scroll, same as before.
  void renderRows(DisplayDriver& display) {
    const int item_h = display.lineStep();
    int visible = display.listVisible(item_h);
    if (visible < 1) visible = 1;
    clampScroll(_row_count, visible);
    drawList(display, _row_count, _scroll, _scroll,
      [&](int idx, int y, bool, int reserve) {
        const diag::Row& r = _rows[idx];
        display.setCursor(2, y);
        display.print(r.label);
        display.drawTextRightAlign(display.width() - reserve - 2, y, r.value);
      });
  }

  // Shared scrollable renderer for the full-width System / Font tabs.
  void renderLines(DisplayDriver& display) {
    const int item_h = display.lineStep();
    int visible = display.listVisible(item_h);
    if (visible < 1) visible = 1;
    clampScroll(_line_count, visible);
    drawList(display, _line_count, _scroll, _scroll,
      [&](int idx, int y, bool, int reserve) {
        display.drawTextEllipsized(2, y, display.width() - reserve - 4, _lines[idx]);
      });
  }

  void clampScroll(int total, int visible) {
    int max_scroll = total - visible;
    if (max_scroll < 0) max_scroll = 0;
    if (_scroll > max_scroll) _scroll = max_scroll;
    if (_scroll < 0) _scroll = 0;
  }

public:
  DiagnosticsScreen(UITask* task) : _task(task) {}

  int render(DisplayDriver& display) override {
    display.setTextSize(1);
    display.setColor(DisplayDriver::LIGHT);
    tabbar::draw(display, TAB_LABELS, TAB_COUNT, _tab);

    switch (_tab) {
      case TAB_SYSTEM: _line_count = diag::systemLines(_lines); renderLines(display); break;
      case TAB_FONT:   _line_count = diag::fontLines(_lines);   renderLines(display); break;
      default:         _row_count = diag::liveRows(_rows);   renderRows(display);  break;
    }

    display.setColor(DisplayDriver::LIGHT);
    if (_reset_menu.active) _reset_menu.render(display);
    // Live counters refresh once a second; the static System/Font cards don't
    // change, so they can idle. The reset popup wants a snappier redraw.
    if (_reset_menu.active) return 50;
    return _tab == TAB_LIVE ? 1000 : 2000;
  }

  bool handleInput(char c) override {
    if (_reset_menu.active) {
      auto res = _reset_menu.handleInput(c);
      if (res == PopupMenu::SELECTED && _reset_menu.selectedIndex() == 0) {
        diag::resetCounters();
        _task->showAlert("Counters reset", 800);
      }
      return true;
    }
    if (keyIsPrev(c)) { _tab = (_tab + TAB_COUNT - 1) % TAB_COUNT; _scroll = 0; return true; }
    if (keyIsNext(c)) { _tab = (_tab + 1) % TAB_COUNT;            _scroll = 0; return true; }
    if (c == KEY_UP)   { if (_scroll > 0) _scroll--; return true; }
    if (c == KEY_DOWN) { _scroll++; return true; }   // clamped in render()
    if (c == KEY_CONTEXT_MENU && _tab == TAB_LIVE) {   // Hold Enter — reset the live counters
      _reset_menu.beginConfirm("Reset counters?", "Reset");
      return true;
    }
    if (c == KEY_CANCEL) { _task->gotoToolsScreen(); return true; }
    return false;
  }
};

const char* const DiagnosticsScreen::TAB_LABELS[DiagnosticsScreen::TAB_COUNT] = { "Live", "System", "Font" };

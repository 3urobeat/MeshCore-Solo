#pragma once
// Shared circular tab-bar header, extracted after NearbyScreen and BotScreen
// had each independently grown byte-identical rendering code for the same
// "active tab centred as a pill, neighbours fan out either side and wrap
// around" layout (see NearbyScreen::drawFilterTabs / BotScreen::drawTabBar,
// pre-extraction). A third screen (AdminScreen) needing the same thing was
// the rule-of-three trigger to finally share it.
//
// Tab-switching itself (LEFT/RIGHT cycling the active index) is NOT part of
// this helper -- each screen's cycle has different side effects (refresh a
// list, reset a row selection, ...), so that one-liner stays inline per screen.
// A screen that switches calls turn() too, for the animation.

namespace tabbar {

// A tab turn (OLED): the strip glides from the old tab centred to the new one,
// and the view below slides across as Home's page turn does, where the driver
// can read its frame back (else it just cuts over). One tab bar is on screen
// at a time, so one state.
static constexpr unsigned long TURN_MS = 220;   // the bar: Home's SLIDE_MS
static constexpr unsigned long VIEW_MS = 140;   // the view: it covers the screen's width
                                                // to the bar's few px, so sooner
inline float easeOut(unsigned long el, unsigned long ms) {   // quick start, soft landing
  if (el >= ms) return 1.0f;
  const float u = 1.0f - (float)el / ms;
  return 1.0f - u * u * u;
}
struct Turn {
  const char* const* labels = nullptr;   // the bar it belongs to
  int from = 0, dir = 0;                 // dir 0: none running
  unsigned long t0 = 0;
  bool content = false;                  // slideBegin() took the view below
  float p = 0;                           // the bar's eased progress this frame, set by draw()
  bool drawn = false;                    // draw() painted a frame of it
};
inline Turn& turnState() { static Turn t; return t; }

// Call on switching from tab `from` one step in `dir` (+1 next, -1 previous),
// before the next frame is drawn.
inline void turn(DisplayDriver* d, const char* const* labels, int from, int dir) {
  Turn& t = turnState();
  t = Turn();
  if (!d || d->isEink()) return;
  t.labels = labels; t.from = from; t.dir = dir; t.t0 = millis();
  t.content = d->slideBegin(d->headerH());
}

// After the screen's render(): slides the view in behind the old one. True
// while a turn runs, for the caller to refresh at animation pace.
inline bool endFrame(DisplayDriver& d) {
  Turn& t = turnState();
  if (!t.dir) return false;
  if (!t.drawn) { t = Turn(); return false; }   // the bar is gone (a popup, another view)
  t.drawn = false;
  if (t.content) d.slideCompose(t.dir * (int)(d.width() * easeOut(millis() - t.t0, VIEW_MS) + 0.5f));
  return true;
}

// One tab: short label; the active one is an inverted pill (same look as a
// selected row) so it reads as "you are here" in the tab strip.
inline void drawPill(DisplayDriver& display, int x, int w, const char* label, bool active) {
  int lh = display.getLineHeight();
  if (active) {
    display.setColor(DisplayDriver::LIGHT);
    display.fillSoftRect(x, 0, w, lh + 1);
    display.setColor(DisplayDriver::DARK);
  } else {
    display.setColor(DisplayDriver::LIGHT);
  }
  display.drawTextCentered(x + w / 2, 0, label);
  display.setColor(DisplayDriver::LIGHT);
}

// Small solid triangle pointing left (dir < 0) or right, 3 px wide and 5
// tall (times sepH()), centred on row y_mid: "more tabs this way".
inline void drawEdgeArrow(DisplayDriver& display, int x, int y_mid, int dir) {
  const int t = display.sepH();
  display.setColor(DisplayDriver::LIGHT);
  for (int c = 0; c < 3; c++) {
    int h = (dir < 0 ? c * 2 + 1 : 5 - c * 2) * t;
    display.fillRect(x + c * t, y_mid - h / 2, t, h);
  }
}

// Header as a circular tab bar: the active tab sits centred as a soft pill and
// the neighbours that fit whole fan out either side. When some tabs don't fit,
// a small arrow at each edge says there are more (the bar wraps around, so
// both ways lead to them) instead of squeezing in cut-off "Sn..." labels.
// `right_reserve` keeps any trailing decoration the caller draws afterwards
// (a context-menu hint, a counter) clear. Also draws the header separator.
inline void draw(DisplayDriver& display, const char* const* labels, int count, int active, int right_reserve = 0) {
  const int pad = 3, gap = 2;
  const int aw = display.getTextWidth(labels[active]) + pad * 2;
  const int ax = display.width() / 2 - aw / 2;
  const int rx_limit = display.width() - right_reserve;

  // Lays the neighbours out (and draws them when `paint`), keeping `edge` px
  // free at both ends; returns how many tabs are on screen and the free space
  // left at each end.
  struct Fit { int shown, left_free, right_free; };
  auto layout = [&](int edge, bool paint) {
    int lx = ax - gap, rx = ax + aw + gap, shown = 1;
    bool lfit = true, rfit = true;
    for (int k = 1; k <= count / 2 && (lfit || rfit); k++) {
      int li = (active - k + count) % count;
      int ri = (active + k) % count;
      if (lfit) {
        int w = display.getTextWidth(labels[li]) + pad * 2;
        if (lx - w >= edge) { if (paint) drawPill(display, lx - w, w, labels[li], false); lx -= w + gap; shown++; }
        else lfit = false;
      }
      // Skip the right side when it lands on the same tab as the left (the
      // single opposite tab on an even count) so it isn't drawn twice.
      if (rfit && ri != li) {
        int w = display.getTextWidth(labels[ri]) + pad * 2;
        if (rx + w <= rx_limit - edge) { if (paint) drawPill(display, rx, w, labels[ri], false); rx += w + gap; shown++; }
        else rfit = false;
      }
    }
    return Fit{ shown, lx + gap, rx_limit - rx + gap };
  };

  const int arr = 3 * display.sepH();   // the edge arrow's width

  Turn& t = turnState();
  if (t.dir && t.labels == labels) {
    const unsigned long el = millis() - t.t0;
    if (el >= TURN_MS) t.dir = 0;
    else {
      // The strip laid out round the new tab, shifted to start with the old
      // one centred (it's the new one's neighbour) and easing over to it.
      t.p = easeOut(el, TURN_MS);
      t.drawn = true;
      const int wf = display.getTextWidth(labels[t.from]) + pad * 2;
      const int sh = (int)(t.dir * (aw / 2 + gap + wf / 2) * (1.0f - t.p) + 0.5f * t.dir);
      drawPill(display, ax + sh, aw, labels[active], true);
      int lx = ax - gap + sh, rx = ax + aw + gap + sh;
      for (int k = 1; k <= count / 2; k++) {
        const int li = (active - k + count) % count, ri = (active + k) % count;
        const int wl = display.getTextWidth(labels[li]) + pad * 2;
        if (lx > 0) drawPill(display, lx - wl, wl, labels[li], false);
        lx -= wl + gap;
        if (ri == li) break;
        const int wr = display.getTextWidth(labels[ri]) + pad * 2;
        if (rx < rx_limit) drawPill(display, rx, wr, labels[ri], false);
        rx += wr + gap;
      }
      // Seen through the window the settled bar spans, with its arrows: the
      // tabs slide in and out at its ends instead of popping at the finish,
      // and nothing slides under the caller's decoration.
      Fit f = layout(0, false);
      if (f.shown < count && f.left_free < arr + 1 && f.right_free < arr + 1) f = layout(arr + 2, false);
      const int bh = display.headerH() - display.sepH(), y_mid = display.getLineHeight() / 2;
      display.setColor(DisplayDriver::DARK);
      display.fillRect(0, 0, f.left_free, bh);
      display.fillRect(rx_limit - f.right_free, 0, f.right_free + right_reserve, bh);
      if (f.shown < count) {
        if (f.left_free >= arr + 1)  drawEdgeArrow(display, 0, y_mid, -1);
        if (f.right_free >= arr + 1) drawEdgeArrow(display, rx_limit - arr, y_mid, +1);
      }
      display.setColor(DisplayDriver::LIGHT);
      display.fillRect(0, display.headerH() - display.sepH(), display.width(), display.sepH());
      return;
    }
  }

  drawPill(display, ax, aw, labels[active], true);
  Fit f = layout(0, false);
  if (f.shown < count && f.left_free < arr + 1 && f.right_free < arr + 1)
    f = layout(arr + 2, true);       // no room for an arrow anywhere: give up a tab for them
  else
    layout(0, true);
  if (f.shown < count) {             // more tabs off screen: an arrow wherever there's room
    int y_mid = display.getLineHeight() / 2;
    if (f.left_free >= arr + 1)  drawEdgeArrow(display, 0, y_mid, -1);
    if (f.right_free >= arr + 1) drawEdgeArrow(display, rx_limit - arr, y_mid, +1);
  }
  display.setColor(DisplayDriver::LIGHT);
  display.fillRect(0, display.headerH() - display.sepH(), display.width(), display.sepH());
}

}  // namespace tabbar

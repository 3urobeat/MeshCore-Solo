#pragma once
// Included into UITask.cpp only (see the note on screen fragments there).


// Boot splash, as on the L2 (ui-lvgl/Splash.h, same lettering from
// ui-core/Lettering.h): the MeshCore wordmark rising into place, "solo" in its
// lettering once it has landed, the Solo version, upstream version + build date, and three dots
// rising in turn while the device starts. E-ink gets the still final frame.
class SplashScreen : public UIScreen {
  UITask* _task;
  unsigned long _start, dismiss_after;
  char _solo_ver[24];
  char _line2[32];

  // A word in the wordmark's lettering at `sc` px a pixel, top-left at (x, y).
  static void drawLettering(DisplayDriver& d, const char* text, int x, int y, int gap, int sc) {
    for (const char* p = text; *p; p++) {
      char c = (char)tolower((unsigned char)*p);
      int cw = lettering::charW(c);
      if (cw < 0) continue;
      for (int r = 0; r < lettering::LOGO_H; r++)
        for (int col = 0; col < cw; ) {   // runs of inked pixels as one rect
          if (!lettering::inked(c, col, r)) { col++; continue; }
          int e = col;
          while (e + 1 < cw && lettering::inked(c, e + 1, r)) e++;
          d.fillRect(x + col * sc, y + r * sc, (e - col + 1) * sc, sc);
          col = e + 1;
        }
      x += cw * sc + gap;
    }
  }

public:
  SplashScreen(UITask* task) : _task(task) {
    lettering::soloVersion(_solo_ver, sizeof(_solo_ver));
    char date[16];
    lettering::buildDate(date, sizeof(date));
#ifdef MESHCORE_VERSION
    snprintf(_line2, sizeof(_line2), "%s  %s", MESHCORE_VERSION, date);
#else
    snprintf(_line2, sizeof(_line2), "%s", date);
#endif
    _start = millis();
    dismiss_after = _start + BOOT_SCREEN_MILLIS;
  }

  int render(DisplayDriver& display) override {
    const bool anim = !display.isEink();
    const unsigned long t = anim ? millis() - _start : 100000UL;   // e-ink: the settled frame
    display.setTextSize(1);
    display.setColor(UIColor::primary_txt);
    const int lh = display.getLineHeight();
    const int cx = display.width() / 2;

    // The wordmark and "solo" grow in whole pixels with the panel (1x on 128 px,
    // 3x on the 4.2"), as far as the block still fits over the dots.
    const int dots_h = 8;
    int sc = display.width() / lettering::LOGO_W;
    while (sc > 1 && (lettering::LOGO_H * 2 + 5) * sc + lh * 2 + 2 > display.height() - dots_h) sc--;
    if (sc < 1) sc = 1;

    // Block from the wordmark down to the second text line, centred above the dots.
    const int logo_h = lettering::LOGO_H * sc;
    const int block_h = logo_h * 2 + 5 * sc + lh * 2 + 2;
    int top = (display.height() - dots_h - block_h) / 2;
    if (top < 0) top = 0;

    // Wordmark: rises 6 px into place over the first 400 ms (ease-out).
    int rise = 0;
    if (t < 400) { int k = 400 - (int)t; rise = (6 * sc * k * k) / (400 * 400); }
    const int lx = (display.width() - lettering::LOGO_W * sc) / 2;
    for (int r = 0; r < lettering::LOGO_H; r++)
      for (int col = 0; col < lettering::LOGO_W; ) {   // runs of inked pixels as one rect
        if (!lettering::logoBit(col, r)) { col++; continue; }
        int e = col;
        while (e + 1 < lettering::LOGO_W && lettering::logoBit(e + 1, r)) e++;
        display.fillRect(lx + col * sc, top + rise + r * sc, (e - col + 1) * sc, sc);
        col = e + 1;
      }

    int y = top + logo_h + 3 * sc;
    const int solo_w = lettering::textW("solo", 0) * sc + 2 * sc * 3;
    if (t >= 400) drawLettering(display, "solo", cx - solo_w / 2, y, 2 * sc, sc);   // once the wordmark has landed
    y += logo_h + 2 * sc;
    display.drawTextCentered(cx, y, _solo_ver[0] ? _solo_ver : "dev");
    y += lh + 2;
    display.drawTextCentered(cx, y, _line2);

    drawLoadingDots(display, cx, display.height() - 2);   // as the L2's
    return anim ? 40 : 1000;
  }

  void poll() override {
    if ((int32_t)(millis() - dismiss_after) >= 0) {
      _task->gotoHomeScreen();
    }
  }
};

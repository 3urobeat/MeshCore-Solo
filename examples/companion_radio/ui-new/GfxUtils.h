#pragma once
// Small 1-px drawing primitives the DisplayDriver abstraction doesn't provide
// (no line / circle in the GFX wrapper used here). Shared by the map and the
// compass so the Bresenham / midpoint routines aren't copy-pasted per screen.
// Everything is plotted as 1x1 fillRect to stay within DisplayDriver's API.

#include <helpers/ui/DisplayDriver.h>
#include <stdlib.h>   // abs
#include "../Trail.h"

namespace gfx {

// Bresenham line between two points.
static inline void drawLine(DisplayDriver& d, int x0, int y0, int x1, int y1) {
  int dx =  abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
  int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
  int err = dx + dy;
  const int t = d.sepH();   // 2px on a doubled panel
  while (true) {
    d.fillRect(x0 - t / 2, y0 - t / 2, t, t);
    if (x0 == x1 && y0 == y1) break;
    int e2 = err * 2;
    if (e2 >= dy) { err += dy; x0 += sx; }
    if (e2 <= dx) { err += dx; y0 += sy; }
  }
}

// The part of a line inside a box (x, y, w, h), Liang-Barsky; nothing when
// it misses the box. For lines whose ends may lie far off the screen.
static inline void drawLineClipped(DisplayDriver& d, int x0, int y0, int x1, int y1, int bx, int by, int bw, int bh) {
  float t0 = 0, t1 = 1;
  const float dx = (float)(x1 - x0), dy = (float)(y1 - y0);
  const float p[4] = { -dx, dx, -dy, dy };
  const float q[4] = { (float)(x0 - bx), (float)(bx + bw - 1 - x0), (float)(y0 - by), (float)(by + bh - 1 - y0) };
  for (int i = 0; i < 4; i++) {
    if (p[i] == 0) { if (q[i] < 0) return; continue; }
    float r = q[i] / p[i];
    if (p[i] < 0) { if (r > t1) return; if (r > t0) t0 = r; }
    else          { if (r < t0) return; if (r < t1) t1 = r; }
  }
  drawLine(d, x0 + (int)(t0 * dx), y0 + (int)(t0 * dy), x0 + (int)(t1 * dx), y0 + (int)(t1 * dy));
}

// Midpoint-circle outline.
static inline void drawCircle(DisplayDriver& d, int cx, int cy, int r) {
  int x = r, y = 0, err = 1 - r;
  while (x >= y) {
    d.fillRect(cx + x, cy + y, 1, 1); d.fillRect(cx + y, cy + x, 1, 1);
    d.fillRect(cx - y, cy + x, 1, 1); d.fillRect(cx - x, cy + y, 1, 1);
    d.fillRect(cx - x, cy - y, 1, 1); d.fillRect(cx - y, cy - x, 1, 1);
    d.fillRect(cx + y, cy - x, 1, 1); d.fillRect(cx + x, cy - y, 1, 1);
    y++;
    if (err < 0) err += 2 * y + 1;
    else { x--; err += 2 * (y - x) + 1; }
  }
}

// Walks a TrailStore and renders it as a connected polyline via drawLine(),
// breaking the line at each TRAIL_FLAG_SEG_START point (trail paused/
// restarted) instead of bridging the dead-time gap. Shared by the full Trail
// map and the Home screen's mini-map preview, which only differ in how they
// project lat/lon to screen coords and what (if anything) they draw at a
// break. `project(lat, lon, x, y)` fills the screen coords for one point;
// `on_break(x_prev_end, y_prev_end, x_new_start, y_new_start)` runs at each
// break instead of drawLine() — pass a no-op lambda for a silent gap.
template <typename Project, typename OnBreak>
static void drawTrail(DisplayDriver& d, TrailStore& tr, Project project, OnBreak on_break) {
  if (tr.count() == 0) return;
  int x0, y0; project(tr.at(0).lat_1e6, tr.at(0).lon_1e6, x0, y0);
  for (int i = 1; i < tr.count(); i++) {
    const TrailPoint& pt = tr.at(i);
    int x1, y1; project(pt.lat_1e6, pt.lon_1e6, x1, y1);
    if (pt.flags & TRAIL_FLAG_SEG_START) on_break(x0, y0, x1, y1);
    else                                  drawLine(d, x0, y0, x1, y1);
    x0 = x1; y0 = y1;
  }
  // The simplifier's pending point: the straight stretch walked since the
  // last vertex, so the line reaches where you are instead of lagging behind.
  TrailPoint pend;
  if (tr.pendingPoint(pend)) {
    int x1, y1; project(pend.lat_1e6, pend.lon_1e6, x1, y1);
    drawLine(d, x0, y0, x1, y1);
  }
}

}  // namespace gfx

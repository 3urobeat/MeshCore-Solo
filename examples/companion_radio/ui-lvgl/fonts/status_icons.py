#!/usr/bin/env python3
"""Builds the status bar's icon font source: FontAwesome glyphs refitted so
every icon reads the same size.

FontAwesome draws each icon on its own canvas -- a battery is wide and flat,
a map pin narrow and tall, a mute sign short -- so at one font size they come
out anywhere from 9 to 16 px wide and 9 to 13 px tall. Here every glyph is
scaled to fit one box (BOX_H tall, at most BOX_W wide; WIDE icons get more
width), centred on the middle of the Noto digits, in a cell of ADVANCE (wide
icons a little more).
Line metrics match ui_font_12 (line height 18, baseline 4), so the digits of
"85%" can share a label with the icons (the font falls back to ui_font_12).

Usage: status_icons.py <FontAwesome .woff/.ttf> <out.ttf>   (needs fonttools)
Called by generate.sh, which turns the result into ui_icons_14.c.
"""
import math
import sys
from fontTools.ttLib import TTFont
from fontTools.fontBuilder import FontBuilder
from fontTools.pens.recordingPen import DecomposingRecordingPen
from fontTools.pens.transformPen import TransformPen
from fontTools.pens.ttGlyphPen import TTGlyphPen
from fontTools.pens.boundsPen import BoundsPen

SIZE = 14.0            # px per em, as generate.sh renders it
UPM = 1024
PX = UPM / SIZE        # font units per pixel

BOX_H = 11.0           # every icon this tall...
BOX_W = 13.0           # ...unless that makes it wider than this
WIDE_W = 17.0          # battery-shaped icons may be this wide
ADVANCE = 16.0         # one cell per icon
MID_Y = 4.5            # icon centre, px above the baseline (middle of the digits)
ASCENT, DESCENT = 14.0, 4.0   # line height 18, baseline 4 = ui_font_12

ICONS = [   # codepoint, wide (True: wider box, None: tight cell)
    (0xF293, False),   # bluetooth
    (0xF124, False),   # location arrow (GPS)
    (0xF0F3, False),   # bell (alarm)
    (0xF6A9, False),   # volume mute
    (0xF519, False),   # broadcast tower (auto-advert)
    (0xF4D7, False),   # route (trail)
    (0xF3C5, False),   # map marker (live share)
    (0xF079, True),    # retweet (repeater)
    (0xF024, False),   # flag (arrival alert)
    (0xF0E7, None),    # bolt (charging): a tight cell, it leads the battery
    (0xF1EB, True),    # wifi
    (0xF240, True), (0xF241, True), (0xF242, True), (0xF243, True), (0xF244, True),   # battery
]


def main(src, out):
    fa = TTFont(src)
    gs = fa.getGlyphSet()
    cmap = fa.getBestCmap()

    order = [".notdef"]
    glyphs, metrics, cmap_out = {}, {}, {}
    pen = TTGlyphPen(None)
    glyphs[".notdef"] = pen.glyph()
    metrics[".notdef"] = (int(ADVANCE * PX), 0)

    for cp, wide in ICONS:
        name = cmap[cp]
        rec = DecomposingRecordingPen(gs)
        gs[name].draw(rec)
        bp = BoundsPen(gs)
        rec.replay(bp)
        x0, y0, x1, y1 = bp.bounds
        w, h = x1 - x0, y1 - y0
        s = min(BOX_H * PX / h, (WIDE_W if wide else BOX_W) * PX / w)
        adv = math.ceil(w * s / PX) + 2   # 1 px clear each side
        if wide is not None: adv = max(ADVANCE, adv)   # one cell for all; wide icons a wider one
        dx = (adv * PX - w * s) / 2 - x0 * s
        dy = MID_Y * PX - (y0 + h / 2) * s
        pen = TTGlyphPen(None)
        rec.replay(TransformPen(pen, (s, 0, 0, s, dx, dy)))
        g = pen.glyph()
        g.recalcBounds(None)
        glyphs[name] = g
        metrics[name] = (int(adv * PX), g.xMin)   # left side bearing = xMin, or the centring is lost
        order.append(name)
        cmap_out[cp] = name

    fb = FontBuilder(UPM, isTTF=True)
    fb.setupGlyphOrder(order)
    fb.setupCharacterMap(cmap_out)
    fb.setupGlyf(glyphs)
    fb.setupHorizontalMetrics(metrics)
    asc, desc = round(ASCENT * PX), round(DESCENT * PX)
    fb.setupHorizontalHeader(ascent=asc, descent=-desc)
    fb.setupOS2(sTypoAscender=asc, sTypoDescender=-desc, sTypoLineGap=0,
                usWinAscent=asc, usWinDescent=desc)
    fb.setupNameTable({"familyName": "UiIcons", "styleName": "Regular"})
    fb.setupPost()
    fb.save(out)


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])

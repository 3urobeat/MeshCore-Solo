#!/usr/bin/env python3
"""OSM data -> vector map tiles for the Wio Tracker L2 (spike).

Reads an Overpass JSON export (`out geom;`) and writes small binary tiles the
device rasterises itself (ui-lvgl/map/VectorTileProvider.h): roads, paths,
marked hiking routes in their waymark colours, water, forest, meadows, rock,
buildings. No labels yet.

  tools/maps/osm_vector.py area.json --out vmap/

Three data zooms: 10 (drawn at z10-11), 12 (z12-13), 14 (z14-18); the device picks the
data tile covering the tile it draws and scales it. Copy the output folder to
the card as /sdcard/vmap.

Tile format 'VT2' (little-endian):
  'V' 'T' '2' dz:u8  count:u16
  count x feature, in drawing order:
    cls:u8 nparts:u8 colour:u16 (RGB565, 0 = the class's own)
    bbox: x0 y0 x1 y1 (i16) -- the device skips what's off the tile unread
    nparts x (npts:u16, npts x (x:i16, y:i16))
  Coordinates: 0..4096 across the data tile, a little past its edges.
  Polygons: rings, even-odd. Lines: polylines.

Get the data for a box (south, west, north, east) from Overpass, e.g.:
  [out:json][timeout:120];
  ( way["highway"](S,W,N,E); way["waterway"~"river|stream|canal"](S,W,N,E);
    way["natural"~"water|wood|scrub|grassland|heath|scree|bare_rock"](S,W,N,E);
    way["landuse"~"forest|meadow|grass|residential|farmland"](S,W,N,E);
    way["building"](S,W,N,E);
    relation["natural"~"water|wood|scrub"](S,W,N,E);
    relation["landuse"~"forest|meadow"](S,W,N,E);
    relation["route"="hiking"](S,W,N,E); );
  out geom;
Map data (c) OpenStreetMap contributors, ODbL.
"""
import argparse, json, math, os, struct, sys
from collections import defaultdict

EXTENT = 4096
BUFFER = 128
DATA_ZOOMS = (10, 12, 14)   # drawn at z10-11, z12-13, z14-18

# Classes: the draw order is the number's order (the device styles them).
P_RESIDENTIAL, P_MEADOW, P_SCRUB, P_FOREST, P_ROCK, P_WATER, P_BUILDING = 1, 2, 3, 4, 5, 6, 7
L_STREAM, L_RIVER = 20, 21
L_PATH, L_TRACK, L_SERVICE, L_MINOR, L_TERTIARY, L_SECONDARY, L_PRIMARY, L_TRUNK = 30, 31, 32, 33, 34, 35, 36, 37
L_ROUTE = 50

HIGHWAY = {
    'path': L_PATH, 'footway': L_PATH, 'steps': L_PATH, 'bridleway': L_PATH, 'cycleway': L_PATH,
    'track': L_TRACK,
    'service': L_SERVICE, 'living_street': L_SERVICE, 'pedestrian': L_SERVICE,
    'residential': L_MINOR, 'unclassified': L_MINOR, 'road': L_MINOR,
    'tertiary': L_TERTIARY, 'tertiary_link': L_TERTIARY,
    'secondary': L_SECONDARY, 'secondary_link': L_SECONDARY,
    'primary': L_PRIMARY, 'primary_link': L_PRIMARY,
    'trunk': L_TRUNK, 'trunk_link': L_TRUNK, 'motorway': L_TRUNK, 'motorway_link': L_TRUNK,
}
# Lowest data zoom a class goes in (smaller ones would be clutter / weight).
MIN_DZ = {P_BUILDING: 14, L_SERVICE: 14, L_PATH: 12, L_TRACK: 12, L_STREAM: 12, L_MINOR: 12}

WAYMARK = {   # osmc:symbol / colour -> RGB888
    'red': 0xE0302A, 'blue': 0x2A5FE0, 'green': 0x2EA043, 'yellow': 0xE8C20E, 'black': 0x202020,
    'orange': 0xF08A1C, 'purple': 0x9040C0, 'white': 0xF0F0F0, 'brown': 0x8B5A2B,
}


def rgb565(c):
    return ((c >> 8) & 0xF800) | ((c >> 5) & 0x07E0) | ((c >> 3) & 0x001F)


def poly_class(t):
    n, lu = t.get('natural'), t.get('landuse')
    if t.get('building'):
        return P_BUILDING
    if n == 'water' or t.get('water'):
        return P_WATER
    if n == 'wood' or lu == 'forest':
        return P_FOREST
    if n == 'scrub':
        return P_SCRUB
    if n in ('scree', 'bare_rock'):
        return P_ROCK
    if n in ('grassland', 'heath') or lu in ('meadow', 'grass', 'farmland'):
        return P_MEADOW
    if lu == 'residential':
        return P_RESIDENTIAL
    return None


def line_class(t):
    if 'highway' in t:
        return HIGHWAY.get(t['highway'])
    w = t.get('waterway')
    if w in ('river', 'canal'):
        return L_RIVER
    if w == 'stream':
        return L_STREAM
    return None


def route_colour(t):
    c = t.get('colour', '').lower()
    if c in WAYMARK:
        return WAYMARK[c]
    sym = t.get('osmc:symbol', '')
    if sym:
        first = sym.split(':')[0].lower()
        if first in WAYMARK:
            return WAYMARK[first]
        # "white:red_bar" style: the bar's colour
        for part in sym.split(':')[1:]:
            k = part.split('_')[0].lower()
            if k in WAYMARK and k != 'white':
                return WAYMARK[k]
    return 0xD04040


def world(lon, lat):
    """Web Mercator 0..1."""
    s = math.sin(math.radians(lat))
    return (lon + 180.0) / 360.0, 0.5 - math.log((1 + s) / (1 - s)) / (4 * math.pi)


def geom_pts(g):
    return [world(p['lon'], p['lat']) for p in g if p]


def join_rings(segments):
    """Member ways of a multipolygon -> closed rings (by shared end points)."""
    segs = [list(s) for s in segments if len(s) >= 2]
    rings = []
    while segs:
        ring = segs.pop()
        changed = True
        while ring[0] != ring[-1] and changed:
            changed = False
            for i, s in enumerate(segs):
                if s[0] == ring[-1]:
                    ring += s[1:]
                elif s[-1] == ring[-1]:
                    ring += s[-2::-1]
                elif s[-1] == ring[0]:
                    ring = s[:-1] + ring
                elif s[0] == ring[0]:
                    ring = s[:0:-1] + ring
                else:
                    continue
                segs.pop(i)
                changed = True
                break
        if len(ring) >= 4 and ring[0] == ring[-1]:
            rings.append(ring)
    return rings


def simplify(pts, tol):
    """Douglas-Peucker on tile units."""
    if len(pts) < 3 or tol <= 0:
        return pts
    keep = [False] * len(pts)
    keep[0] = keep[-1] = True
    stack = [(0, len(pts) - 1)]
    t2 = tol * tol
    while stack:
        a, b = stack.pop()
        ax, ay = pts[a]
        bx, by = pts[b]
        dx, dy = bx - ax, by - ay
        L = dx * dx + dy * dy
        best, bi = -1, -1
        for i in range(a + 1, b):
            px, py = pts[i]
            if L == 0:
                d = (px - ax) ** 2 + (py - ay) ** 2
            else:
                t = max(0, min(1, ((px - ax) * dx + (py - ay) * dy) / L))
                d = (ax + t * dx - px) ** 2 + (ay + t * dy - py) ** 2
            if d > best:
                best, bi = d, i
        if best > t2:
            keep[bi] = True
            stack += [(a, bi), (bi, b)]
    return [p for p, k in zip(pts, keep) if k]


def clip_line(pts, lo, hi):
    """Polyline -> pieces inside the box (Liang-Barsky per segment)."""
    out, cur = [], []
    for (x0, y0), (x1, y1) in zip(pts, pts[1:]):
        t0, t1, dx, dy = 0.0, 1.0, x1 - x0, y1 - y0
        ok = True
        for p, q in ((-dx, x0 - lo), (dx, hi - x0), (-dy, y0 - lo), (dy, hi - y0)):
            if p == 0:
                if q < 0:
                    ok = False
                    break
            else:
                r = q / p
                if p < 0:
                    t0 = max(t0, r)
                else:
                    t1 = min(t1, r)
        if not ok or t0 > t1:
            if len(cur) >= 2:
                out.append(cur)
            cur = []
            continue
        a = (x0 + t0 * dx, y0 + t0 * dy)
        b = (x0 + t1 * dx, y0 + t1 * dy)
        if not cur or cur[-1] != a:
            if len(cur) >= 2:
                out.append(cur)
            cur = [a]
        cur.append(b)
        if t1 < 1.0:
            out.append(cur)
            cur = []
    if len(cur) >= 2:
        out.append(cur)
    return out


def clip_ring(pts, lo, hi):
    """Sutherland-Hodgman against the box."""
    def edge(pts, inside, cross):
        out = []
        for i in range(len(pts)):
            cur, prev = pts[i], pts[i - 1]
            if inside(cur):
                if not inside(prev):
                    out.append(cross(prev, cur))
                out.append(cur)
            elif inside(prev):
                out.append(cross(prev, cur))
        return out

    def cx(v):
        return lambda a, b: (v, a[1] + (b[1] - a[1]) * (v - a[0]) / (b[0] - a[0]))

    def cy(v):
        return lambda a, b: (a[0] + (b[0] - a[0]) * (v - a[1]) / (b[1] - a[1]), v)

    for inside, cross in ((lambda p: p[0] >= lo, cx(lo)), (lambda p: p[0] <= hi, cx(hi)),
                          (lambda p: p[1] >= lo, cy(lo)), (lambda p: p[1] <= hi, cy(hi))):
        pts = edge(pts, inside, cross)
        if not pts:
            return []
    return pts


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('json')
    ap.add_argument('--out', default='vmap')
    a = ap.parse_args()
    data = json.load(open(a.json))

    feats = []   # (cls, colour, 'poly'|'line', [parts in world coords])
    for e in data['elements']:
        t = e.get('tags', {})
        if e['type'] == 'way' and 'geometry' in e:
            pts = geom_pts(e['geometry'])
            closed = len(pts) >= 4 and pts[0] == pts[-1]
            pc = poly_class(t) if closed and t.get('area') != 'no' and 'highway' not in t else None
            if pc:
                feats.append((pc, 0, 'poly', [pts]))
            lc = line_class(t)
            if lc and not pc:
                feats.append((lc, 0, 'line', [pts]))
        elif e['type'] == 'relation':
            members = e.get('members', [])
            if t.get('route') == 'hiking':
                col = rgb565(route_colour(t))
                parts = [geom_pts(m['geometry']) for m in members if m.get('type') == 'way' and 'geometry' in m]
                feats.append((L_ROUTE, col, 'line', [p for p in parts if len(p) >= 2]))
            else:
                pc = poly_class(t)
                if not pc:
                    continue
                outer = [geom_pts(m['geometry']) for m in members if m.get('role') in ('outer', '') and 'geometry' in m]
                inner = [geom_pts(m['geometry']) for m in members if m.get('role') == 'inner' and 'geometry' in m]
                rings = join_rings(outer) + join_rings(inner)
                if rings:
                    feats.append((pc, 0, 'poly', rings))
    print(f'{len(feats)} features', file=sys.stderr)

    total_bytes = total_tiles = 0
    for dz in DATA_ZOOMS:
        n = 1 << dz
        tol = 1.0 if dz == DATA_ZOOMS[-1] else 4.0   # half a pixel at the deepest zoom drawn from it
        tiles = defaultdict(list)
        for cls, col, kind, parts in feats:
            if MIN_DZ.get(cls, 0) > dz:
                continue
            allp = [p for part in parts for p in part]
            if not allp:
                continue
            x0 = int(min(p[0] for p in allp) * n); x1 = int(max(p[0] for p in allp) * n)
            y0 = int(min(p[1] for p in allp) * n); y1 = int(max(p[1] for p in allp) * n)
            for tx in range(x0, x1 + 1):
                for ty in range(y0, y1 + 1):
                    out = []
                    for part in parts:
                        loc = [((p[0] * n - tx) * EXTENT, (p[1] * n - ty) * EXTENT) for p in part]
                        if kind == 'poly':
                            r = clip_ring(loc, -BUFFER, EXTENT + BUFFER)
                            r = simplify(r, tol)
                            if len(r) >= 3 and (max(p[0] for p in r) - min(p[0] for p in r) > 4 * tol or
                                                max(p[1] for p in r) - min(p[1] for p in r) > 4 * tol):   # not a speck
                                out.append(r)
                        else:
                            for piece in clip_line(loc, -BUFFER, EXTENT + BUFFER):
                                piece = simplify(piece, tol)
                                if len(piece) >= 2:
                                    out.append(piece)
                    if out:
                        tiles[(tx, ty)].append((cls, col, out))
        for (tx, ty), fl in tiles.items():
            fl.sort(key=lambda f: f[0])
            buf = bytearray(b'VT2' + bytes([dz]) + struct.pack('<H', 0))
            count = 0
            for cls, col, parts in fl:
                for i in range(0, len(parts), 255):   # nparts is a byte
                    chunk = parts[i:i + 255]
                    qs = []
                    for part in chunk:
                        q = []
                        for x, y in part:
                            p = (int(round(x)), int(round(y)))
                            if not q or q[-1] != p:
                                q.append(p)
                        qs.append(q)
                    allq = [p for q in qs for p in q]
                    buf += struct.pack('<BBHhhhh', cls, len(chunk), col, min(p[0] for p in allq), min(p[1] for p in allq),
                                       max(p[0] for p in allq), max(p[1] for p in allq))
                    for q in qs:
                        buf += struct.pack('<H', len(q))
                        for x, y in q:
                            buf += struct.pack('<hh', x, y)
                    count += 1
            struct.pack_into('<H', buf, 4, min(count, 65535))
            d = os.path.join(a.out, str(dz), str(tx))
            os.makedirs(d, exist_ok=True)
            with open(os.path.join(d, f'{ty}.vt'), 'wb') as f:
                f.write(buf)
            total_bytes += len(buf)
            total_tiles += 1
    with open(os.path.join(a.out, 'attribution.txt'), 'w') as f:
        f.write('© OpenStreetMap contributors (ODbL)\n')
    print(f'{total_tiles} tiles, {total_bytes / 1024:.0f} KB', file=sys.stderr)


if __name__ == '__main__':
    main()

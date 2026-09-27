#!/usr/bin/env python3
"""OSM data -> vector map tiles for the Wio Tracker L2 (spike).

Reads an Overpass JSON export (`out geom;`) and writes small binary tiles the
device rasterises itself (ui-lvgl/map/VectorTileProvider.h): roads, paths,
marked hiking routes in their waymark colours, water, forest, meadows, rock,
buildings; and named points for the labels (places, peaks, huts, springs...).
No street names (the raster map has them).

  tools/maps/osm_vector.py area.json [points.json] --out vmap/ [--dem dem-cache/]
  tools/maps/osm_vector.py area.json ... --pack tatry.vpk --name "Tatry"
  tools/maps/osm_vector.py --pbf malopolskie-latest.osm.pbf --bbox 49.15,19.75,49.35,20.25 \
      --dem dem-cache/ --pack tatry.vpk --name "Tatry"

--pbf reads a Geofabrik extract (tools/maps/pbf.py, needs pyosmium: pip
install osmium) for the --bbox (south,west,north,east) -- no Overpass limits;
give it more than once for a box across a border.

With --dem, contour lines too (tools/maps/dem.py: Terrain Tiles fetched into
that folder): every 20 m (from zoom 15) with a darker one every 100 m.

Three data zooms: 10 (drawn at z10-11), 12 (z12-13), 14 (z14-18); the device picks the
data tile covering the tile it draws and scales it. Copy the output folder to
the card as /sdcard/vmap.

A pack (--pack) holds a whole region in one file, which the device reads in
place from /sdcard/vmap/*.vpk (a card writes one big file much faster than
thousands of small ones, and a download is one file):
  header, 64 bytes: 'VPK1' count:u32 index_off:u32 data_off:u32
    lon0 lat0 lon1 lat1: i32 (degrees x 1e6)  zmin zmax flags(1 contours) 0: u8
    name: 28 bytes UTF-8, 0-padded
  count x index entry, 16 bytes, sorted: kind:u8 (0 tile, 1 points) dz:u8
    x:u16 y:u16 0:u16 off:u32 (from data_off) len:u32
  the files, one after another.
Without --pack the files go to --out as {dz}/{x}/{y}.vt / .vp.

Tile format 'VT3' (little-endian):
  'V' 'T' '3' dz:u8  count:u16
  count x feature, in drawing order:
    cls:u8 nparts:u8 colour:u16 (RGB565, 0 = the class's own)
    bbox: x0 y0 x1 y1 (i16) -- the device skips what's off the tile unread
    len: varint -- bytes of the parts that follow (to skip them)
    nparts x (npts: varint, npts x (dx, dy: zigzag varint))
  Points are deltas from the previous one; a part's first from (x0, y0).
  Coordinates: 0..4096 across the data tile, a little past its edges.
  Polygons: rings, even-odd. Lines: polylines.
  Hiking routes (51) are per stretch of path, not per route: `colour` packs
  the waymark colours of every route along it (4 bits each, PALETTE index,
  lowest nibble first), drawn side by side.

Points 'VP1' beside each tile ({y}.vp, little-endian), every point whose class
starts at or below that data zoom (so a zoom's file is complete on its own):
  'V' 'P' '1' dz:u8  count:u16
  count x (cls:u8 x:u16 y:u16 ele:i16 (-32768 none) namelen:u8 name (UTF-8))
  cls: 60 town, 61 village, 62 hut, 63 peak, 64 lake, 65 hamlet, 66 pass,
  67 shelter, 68 spring, 69 viewpoint, 70 cave.

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
and the points (a second file):
  [out:json][timeout:90];
  ( node["natural"~"^(peak|saddle|spring|cave_entrance)$"](S,W,N,E);
    node["tourism"~"^(alpine_hut|wilderness_hut|viewpoint)$"](S,W,N,E);
    node["amenity"="shelter"](S,W,N,E);
    node["place"~"^(city|town|village|hamlet)$"](S,W,N,E);
    way["tourism"~"^(alpine_hut|wilderness_hut)$"](S,W,N,E);
    nwr["natural"="water"]["name"](S,W,N,E); );
  out center tags;
Map data (c) OpenStreetMap contributors, ODbL.
"""
import argparse, json, math, os, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dem
from collections import defaultdict

EXTENT = 4096
BUFFER = 128
DATA_ZOOMS = (10, 12, 14)   # drawn at z10-11, z12-13, z14-18

# Classes: the draw order is the number's order (the device styles them).
P_RESIDENTIAL, P_MEADOW, P_SCRUB, P_FOREST, P_ROCK, P_WATER, P_BUILDING = 1, 2, 3, 4, 5, 6, 7
L_STREAM, L_RIVER = 20, 21
L_PATH, L_TRACK, L_SERVICE, L_MINOR, L_TERTIARY, L_SECONDARY, L_PRIMARY, L_TRUNK = 30, 31, 32, 33, 34, 35, 36, 37
L_PATH_HARD = 29   # a path of demanding / alpine difficulty (sac_scale)
L_ROUTE = 50       # (old: one route, its RGB565 colour)
L_ROUTES = 51      # the routes along a stretch
L_CONTOUR, L_CONTOUR_IDX = 15, 16   # contour lines, every 20 m / 100 m

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
MIN_DZ = {P_BUILDING: 14, L_SERVICE: 14, L_PATH: 12, L_PATH_HARD: 12, L_TRACK: 12, L_STREAM: 12, L_MINOR: 12,
          L_CONTOUR: 14, L_CONTOUR_IDX: 12}
HARD_SAC = ('demanding_mountain_hiking', 'alpine_hiking', 'demanding_alpine_hiking', 'difficult_alpine_hiking')

# Points (labels): class, lowest data zoom.
T_TOWN, T_VILLAGE, T_HUT, T_PEAK, T_LAKE, T_HAMLET, T_PASS, T_SHELTER, T_SPRING, T_VIEW, T_CAVE = range(60, 71)
POINT_DZ = {T_TOWN: 10, T_VILLAGE: 10, T_HUT: 10, T_PEAK: 10, T_LAKE: 12, T_HAMLET: 12, T_PASS: 12,
            T_SHELTER: 12, T_SPRING: 14, T_VIEW: 14, T_CAVE: 14}
NAME_MAX = 40   # bytes
# Long words the labels shorten (map convention; the rest of the name stays).
ABBREV = (('Schronisko', 'Schr.'), ('Przełęcz', 'Przeł.'), ('Schutzhütte', 'Sch.'), ('Chata', 'Ch.'))


def point_class(t):
    n, tour, pl = t.get('natural'), t.get('tourism'), t.get('place')
    if pl in ('city', 'town'):
        return T_TOWN
    if pl == 'village':
        return T_VILLAGE
    if pl == 'hamlet':
        return T_HAMLET
    if tour in ('alpine_hut', 'wilderness_hut'):
        return T_HUT
    if n == 'peak':
        return T_PEAK
    if n == 'saddle':
        return T_PASS
    if n == 'water':
        return T_LAKE
    if t.get('amenity') == 'shelter':
        return T_SHELTER
    if n == 'spring':
        return T_SPRING
    if tour == 'viewpoint':
        return T_VIEW
    if n == 'cave_entrance':
        return T_CAVE
    return None


def elevation(t):
    v = t.get('ele', '').replace(',', '.').split(' ')[0].rstrip('m')
    try:
        return max(-32767, min(32767, int(round(float(v)))))
    except ValueError:
        return None


def short_name(name):
    if ' / ' in name:   # "Świnica / Svinica" on a border: the first one
        name = name.split(' / ')[0]
    for a, b in ABBREV:
        if name.startswith(a + ' ') and len(name) > 16:
            name = b + name[len(a):]
    b = name.encode('utf-8')
    if len(b) <= NAME_MAX:
        return b
    b = b[:NAME_MAX]
    while b and (b[-1] & 0xC0) == 0x80:   # a cut multi-byte character
        b = b[:-1]
    if b and b[-1] >= 0xC0:
        b = b[:-1]
    return b


# Natural land cover (not roads, buildings, water): its edges are vague
# anyway, so simplified harder -- most of the points are here.
NATURAL = (P_MEADOW, P_SCRUB, P_FOREST, P_ROCK, L_CONTOUR, L_CONTOUR_IDX)   # (and the contours: from a 25 m grid)
NATURAL_TOL = 3

WAYMARK = {   # osmc:symbol / colour -> RGB888
    'red': 0xE0302A, 'blue': 0x2A5FE0, 'green': 0x2EA043, 'yellow': 0xE8C20E, 'black': 0x202020,
    'orange': 0xF08A1C, 'purple': 0x9040C0, 'white': 0xF0F0F0, 'brown': 0x8B5A2B,
}
# Route colours by index (1..), the device has the same table; also the
# order the stripes go side by side.
PALETTE = ['red', 'blue', 'green', 'yellow', 'black', 'orange', 'purple', 'white', 'brown', 'other']


def varint(v):
    out = bytearray()
    while v >= 0x80:
        out.append((v & 0x7F) | 0x80)
        v >>= 7
    out.append(v)
    return out


def zigzag(v):
    return (v << 1) if v >= 0 else ((-v << 1) - 1)


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
    """The waymark colour's PALETTE index (1..)."""
    c = t.get('colour', '').lower()
    if c in WAYMARK:
        return PALETTE.index(c) + 1
    sym = t.get('osmc:symbol', '')
    if sym:
        first = sym.split(':')[0].lower()
        if first in WAYMARK:
            return PALETTE.index(first) + 1
        # "white:red_bar" style: the bar's colour
        for part in sym.split(':')[1:]:
            k = part.split('_')[0].lower()
            if k in WAYMARK and k != 'white':
                return PALETTE.index(k) + 1
    return len(PALETTE)


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


def join_lines(ways):
    """Polylines -> as few as possible, joined at shared ends."""
    segs = [list(w) for w in ways if len(w) >= 2]
    out = []
    while segs:
        line = segs.pop()
        changed = True
        while changed:
            changed = False
            for i, s in enumerate(segs):
                if s[0] == line[-1]:
                    line += s[1:]
                elif s[-1] == line[-1]:
                    line += s[-2::-1]
                elif s[-1] == line[0]:
                    line = s[:-1] + line
                elif s[0] == line[0]:
                    line = s[:0:-1] + line
                else:
                    continue
                segs.pop(i)
                changed = True
                break
        if line[0][0] > line[-1][0]:   # west to east: the stripes keep their sides between stretches
            line.reverse()
        out.append(line)
    return out


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
    ap.add_argument('json', nargs='*', help='Overpass JSON file(s)')
    ap.add_argument('--pbf', action='append', default=[], help='an OSM PBF extract (with --bbox); again for more')
    ap.add_argument('--bbox', help='south,west,north,east: only this box (and the pack\'s box)')
    ap.add_argument('--out', default='vmap')
    ap.add_argument('--pack', metavar='FILE.vpk', help='write one pack file instead of a folder')
    ap.add_argument('--name', default='', help="the region's name in the pack")
    ap.add_argument('--lang', help='names in this language where OSM has them (name:pl ...)')
    ap.add_argument('--dem', metavar='CACHE', help='add contour lines; DEM tiles are kept in this folder')
    ap.add_argument('--contour-step', type=int, default=20)
    ap.add_argument('--index-step', type=int, default=100)
    a = ap.parse_args()
    box = None
    if a.bbox:
        s_, w_, n_, e_ = (float(v) for v in a.bbox.split(','))
        box = (w_, s_, e_, n_)
    elements = []
    for fn in a.json:
        elements += json.load(open(fn))['elements']
    if a.pbf:
        if not box:
            ap.error('--pbf needs --bbox')
        import pbf
        for fn in a.pbf:   # neighbouring extracts: what both have is drawn twice, harmless
            elements += pbf.read_pbf(fn, box)
    if not elements:
        ap.error('no input')
    if not box:
        lons = [p['lon'] for e in elements for p in (e.get('geometry') or []) if p] or [0]
        lats = [p['lat'] for e in elements for p in (e.get('geometry') or []) if p] or [0]
        box = (min(lons), min(lats), max(lons), max(lats))

    feats = []   # (cls, colour, 'poly'|'line', [parts in world coords])
    way_routes = defaultdict(set)   # way id -> route colours along it
    way_geom = {}
    points = {}  # (cls, name, rounded pos) -> (cls, x, y, ele, name bytes): a node and a way can both be there
    for e in elements:
        t = e.get('tags', {})
        pt = point_class(t)
        if pt is not None:
            pos = (e['lon'], e['lat']) if 'lat' in e else (e['center']['lon'], e['center']['lat']) if 'center' in e else None
            if not pos and e.get('geometry'):   # a hut drawn as a building: its middle
                g = [p for p in e['geometry'] if p]
                pos = (sum(p['lon'] for p in g) / len(g), sum(p['lat'] for p in g) / len(g))
            if pos and not (box[0] <= pos[0] <= box[2] and box[1] <= pos[1] <= box[3]):
                pos = None
            name = (a.lang and t.get('name:' + a.lang)) or t.get('name', '')
            ele = elevation(t)
            if pos and (name or (pt == T_PEAK and ele is not None)):   # unnamed: just peaks, by their height
                x, y = world(*pos)
                points[(pt, name, round(x * 2e5), round(y * 2e5))] = (pt, x, y, ele, short_name(name))
            if 'geometry' not in e and 'members' not in e:
                continue
        if e['type'] == 'way' and 'geometry' in e:
            pts = geom_pts(e['geometry'])
            closed = len(pts) >= 4 and pts[0] == pts[-1]
            pc = poly_class(t) if closed and t.get('area') != 'no' and 'highway' not in t else None
            if pc:
                feats.append((pc, 0, 'poly', [pts]))
            lc = line_class(t)
            if lc == L_PATH and t.get('sac_scale') in HARD_SAC:
                lc = L_PATH_HARD
            if lc and not pc:
                feats.append((lc, 0, 'line', [pts]))
        elif e['type'] == 'relation':
            members = e.get('members', [])
            if t.get('route') == 'hiking':
                col = route_colour(t)
                for m in members:
                    if m.get('type') == 'way' and 'geometry' in m:
                        way_routes[m['ref']].add(col)
                        way_geom[m['ref']] = geom_pts(m['geometry'])
            else:
                pc = poly_class(t)
                if not pc:
                    continue
                outer = [geom_pts(m['geometry']) for m in members if m.get('role') in ('outer', '') and 'geometry' in m]
                inner = [geom_pts(m['geometry']) for m in members if m.get('role') == 'inner' and 'geometry' in m]
                rings = join_rings(outer) + join_rings(inner)
                if rings:
                    feats.append((pc, 0, 'poly', rings))
    bundles = defaultdict(list)   # the same routes along adjacent ways: one line
    for wid, cols in way_routes.items():
        if len(way_geom[wid]) >= 2:
            bundles[tuple(sorted(cols))[:4]].append(way_geom[wid])
    for cols, ways in bundles.items():
        packed = sum(c << (4 * i) for i, c in enumerate(cols))
        feats.append((L_ROUTES, packed, 'line', join_lines(ways)))
    if a.dem:
        nc = 0
        for v, line in dem.contour_lines(a.dem, *box, a.contour_step):
            feats.append((L_CONTOUR_IDX if round(v) % a.index_step == 0 else L_CONTOUR, 0, 'line', [line]))
            nc += 1
        print(f'{nc} contour lines', file=sys.stderr)
    print(f'{len(feats)} features, {len(way_routes)} route ways', file=sys.stderr)

    files = {}   # (kind 0 tile / 1 points, dz, x, y) -> bytes
    for dz in DATA_ZOOMS:
        n = 1 << dz
        base_tol = 1.0 if dz == DATA_ZOOMS[-1] else 4.0   # half a pixel at the deepest zoom drawn from it
        (wx0, wy0), (wx1, wy1) = world(box[0], box[3]), world(box[2], box[1])
        tx_lo, ty_lo, tx_hi, ty_hi = int(wx0 * n), int(wy0 * n), int(wx1 * n), int(wy1 * n)   # tiles of the box only
        tiles = defaultdict(list)
        for cls, col, kind, parts in feats:
            if MIN_DZ.get(cls, 0) > dz:
                continue
            tol = base_tol * (NATURAL_TOL if cls in NATURAL else 1)
            if cls == L_ROUTES:   # routes: close to the ground at every zoom
                tol = min(tol, 1.0 if dz >= 12 else 2.0)
            allp = [p for part in parts for p in part]
            if not allp:
                continue
            x0 = max(int(min(p[0] for p in allp) * n), tx_lo); x1 = min(int(max(p[0] for p in allp) * n), tx_hi)
            y0 = max(int(min(p[1] for p in allp) * n), ty_lo); y1 = min(int(max(p[1] for p in allp) * n), ty_hi)
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
            buf = bytearray(b'VT3' + bytes([dz]) + struct.pack('<H', 0))
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
                    bx0, by0 = min(p[0] for p in allq), min(p[1] for p in allq)
                    body = bytearray()
                    for q in qs:
                        body += varint(len(q))
                        px, py = bx0, by0
                        for x, y in q:
                            body += varint(zigzag(x - px)) + varint(zigzag(y - py))
                            px, py = x, y
                    buf += struct.pack('<BBHhhhh', cls, len(chunk), col, bx0, by0,
                                       max(p[0] for p in allq), max(p[1] for p in allq))
                    buf += varint(len(body)) + body
                    count += 1
            struct.pack_into('<H', buf, 4, min(count, 65535))
            files[(0, dz, tx, ty)] = bytes(buf)
    for dz in DATA_ZOOMS:
        n = 1 << dz
        tiles = defaultdict(list)
        for pt, x, y, ele, name in points.values():
            if POINT_DZ[pt] > dz or (not name and dz < 14):   # unnamed peaks only close up
                continue
            tx, ty = int(x * n), int(y * n)
            tiles[(tx, ty)].append((pt, -(ele or 0), min(EXTENT - 1, int((x * n - tx) * EXTENT)),
                                    min(EXTENT - 1, int((y * n - ty) * EXTENT)), ele, name))
        for (tx, ty), pl in tiles.items():
            pl.sort()
            buf = bytearray(b'VP1' + bytes([dz]) + struct.pack('<H', len(pl)))
            for pt, _, px, py, ele, name in pl:
                buf += struct.pack('<BHHhB', pt, px, py, -32768 if ele is None else ele, len(name)) + name
            files[(1, dz, tx, ty)] = bytes(buf)
    print(f'{len(points)} points', file=sys.stderr)
    total = sum(len(b) for b in files.values())
    if a.pack:
        name = a.name or os.path.splitext(os.path.basename(a.pack))[0]
        write_pack(a.pack, files, box, name, bool(a.dem))
    else:
        for (kind, dz, x, y), b in files.items():
            d = os.path.join(a.out, str(dz), str(x))
            os.makedirs(d, exist_ok=True)
            with open(os.path.join(d, f'{y}.{"vp" if kind else "vt"}'), 'wb') as f:
                f.write(b)
        with open(os.path.join(a.out, 'attribution.txt'), 'w') as f:
            f.write('© OpenStreetMap contributors (ODbL)\n')
            if a.dem:
                f.write(dem.ATTRIBUTION + '\n')
    print(f'{sum(1 for k in files if k[0] == 0)} tiles, {total / 1024:.0f} KB', file=sys.stderr)


def write_pack(path, files, box, name, contours):
    keys = sorted(files)
    index = bytearray()
    off = 0
    for k in keys:
        index += struct.pack('<BBHHHII', k[0], k[1], k[2], k[3], 0, off, len(files[k]))
        off += len(files[k])
    nb = name.encode('utf-8')[:27]
    while nb and (nb[-1] & 0xC0) == 0x80:
        nb = nb[:-1]
    zs = [k[1] for k in keys if k[0] == 0] or [0]
    head = struct.pack('<4sIII4iBBBB28s', b'VPK1', len(keys), 64, 64 + len(index),
                       *(int(round(v * 1e6)) for v in box), min(zs), max(zs), 1 if contours else 0, 0, nb)
    assert len(head) == 64
    with open(path + '.tmp', 'wb') as f:
        f.write(head)
        f.write(index)
        for k in keys:
            f.write(files[k])
    os.replace(path + '.tmp', path)


if __name__ == '__main__':
    main()

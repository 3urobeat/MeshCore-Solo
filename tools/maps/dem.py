"""Contour lines from a DEM, for osm_vector.py (standard library only).

Elevation comes from the Terrain Tiles on AWS Open Data ("terrarium" PNGs:
height = R * 256 + G + B / 256 - 32768 m; sources SRTM, GMTED, ETOPO1 and
others, see https://github.com/tilezen/joerd/blob/master/docs/attribution.md),
fetched once into a cache folder. The grid is smoothed a little, cut by
marching squares at every `step` metres, and the pieces joined into lines in
world coordinates (Web Mercator 0..1), each with its height.
"""
import math, os, struct, sys, urllib.request, zlib

DEM_ZOOM = 12   # ~25 m a pixel at 49 N: what SRTM has
URL = 'https://s3.amazonaws.com/elevation-tiles-prod/terrarium/{z}/{x}/{y}.png'
ATTRIBUTION = 'elevation: Terrain Tiles (Mapzen / AWS Open Data; SRTM and others)'


def read_png(data):
    """8-bit RGB / RGBA, not interlaced -> (w, h, channels, bytes)."""
    assert data[:8] == b'\x89PNG\r\n\x1a\n', 'not a PNG'
    pos, idat, w = 8, bytearray(), 0
    while pos < len(data):
        n, kind = struct.unpack('>I4s', data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + n]
        if kind == b'IHDR':
            w, h, depth, ctype, _, _, interlace = struct.unpack('>IIBBBBB', body)
            assert depth == 8 and ctype in (2, 6) and not interlace, 'unsupported PNG'
            ch = 3 if ctype == 2 else 4
        elif kind == b'IDAT':
            idat += body
        pos += 12 + n
    raw = zlib.decompress(bytes(idat))
    stride = w * ch
    out = bytearray(stride * h)
    prev = bytearray(stride)
    for y in range(h):
        f = raw[y * (stride + 1)]
        line = bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        if f == 1:
            for i in range(ch, stride):
                line[i] = (line[i] + line[i - ch]) & 255
        elif f == 2:
            for i in range(stride):
                line[i] = (line[i] + prev[i]) & 255
        elif f == 3:
            for i in range(stride):
                line[i] = (line[i] + ((line[i - ch] if i >= ch else 0) + prev[i]) // 2) & 255
        elif f == 4:
            for i in range(stride):
                a = line[i - ch] if i >= ch else 0
                b = prev[i]
                c = prev[i - ch] if i >= ch else 0
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        out[y * stride:(y + 1) * stride] = line
        prev = line
    return w, h, ch, out


def tile(cache, x, y):
    path = os.path.join(cache, str(DEM_ZOOM), str(x), f'{y}.png')
    if not os.path.exists(path):
        os.makedirs(os.path.dirname(path), exist_ok=True)
        req = urllib.request.Request(URL.format(z=DEM_ZOOM, x=x, y=y), headers={'User-Agent': 'meshcore-maps/1'})
        with urllib.request.urlopen(req, timeout=60) as r, open(path + '.tmp', 'wb') as f:
            f.write(r.read())
        os.replace(path + '.tmp', path)
    w, h, ch, px = read_png(open(path, 'rb').read())
    return [[px[(j * w + i) * ch] * 256 + px[(j * w + i) * ch + 1] + px[(j * w + i) * ch + 2] / 256 - 32768
             for i in range(w)] for j in range(h)]


def grid(cache, x0, y0, x1, y1):
    """Heights of the tiles x0..x1, y0..y1 stitched: rows of floats."""
    rows = []
    for ty in range(y0, y1 + 1):
        band = [tile(cache, tx, ty) for tx in range(x0, x1 + 1)]
        for j in range(256):
            rows.append([v for t in band for v in t[j]])
    return rows


def smooth(g):
    """3x3 box blur: the 1 px noise of the DEM makes wiggly lines."""
    h, w = len(g), len(g[0])
    out = [row[:] for row in g]
    for j in range(1, h - 1):
        a, b, c = g[j - 1], g[j], g[j + 1]
        o = out[j]
        for i in range(1, w - 1):
            o[i] = (a[i - 1] + a[i] + a[i + 1] + b[i - 1] + b[i] + b[i + 1] + c[i - 1] + c[i] + c[i + 1]) / 9
    return out


def contours(g, step):
    """Marching squares -> {height: [polyline of (col, row) grid points]}."""
    h, w = len(g), len(g[0])
    segs = {}   # height -> list of (edge key, point, edge key, point)
    for j in range(h - 1):
        r0, r1 = g[j], g[j + 1]
        for i in range(w - 1):
            a, b, c, d = r0[i], r0[i + 1], r1[i + 1], r1[i]   # corners clockwise from top-left
            lo, hi = min(a, b, c, d), max(a, b, c, d)
            k0, k1 = math.floor(lo / step) + 1, math.floor(hi / step)
            if k0 > k1:
                continue
            for k in range(k0, k1 + 1):
                v = k * step
                # Crossings on the cell's edges: top, right, bottom, left.
                cr = []
                if (a < v) != (b < v):
                    cr.append((('h', i, j), (i + (v - a) / (b - a), j)))
                if (b < v) != (c < v):
                    cr.append((('v', i + 1, j), (i + 1, j + (v - b) / (c - b))))
                if (d < v) != (c < v):
                    cr.append((('h', i, j + 1), (i + (v - d) / (c - d), j + 1)))
                if (a < v) != (d < v):
                    cr.append((('v', i, j), (i, j + (v - a) / (d - a))))
                s = segs.setdefault(v, [])
                if len(cr) == 2:
                    s.append((cr[0], cr[1]))
                elif len(cr) == 4:   # a saddle: pair by the centre's side
                    centre = (a + b + c + d) / 4
                    if (centre < v) == (a < v):
                        s.append((cr[0], cr[1])); s.append((cr[2], cr[3]))
                    else:
                        s.append((cr[0], cr[3])); s.append((cr[1], cr[2]))
    out = {}
    for v, sl in segs.items():
        at = {}   # edge key -> segments touching it
        for n, (p, q) in enumerate(sl):
            at.setdefault(p[0], []).append(n)
            at.setdefault(q[0], []).append(n)
        used = [False] * len(sl)
        lines = []
        for n in range(len(sl)):
            if used[n]:
                continue
            used[n] = True
            p, q = sl[n]
            line = [p, q]
            for grow_end in (True, False):   # extend from the end, then from the start
                while True:
                    key = line[-1][0] if grow_end else line[0][0]
                    nxt = next((m for m in at[key] if not used[m]), None)
                    if nxt is None:
                        break
                    used[nxt] = True
                    a2, b2 = sl[nxt]
                    far = b2 if a2[0] == key else a2
                    if grow_end:
                        line.append(far)
                    else:
                        line.insert(0, far)
            lines.append([pt for _, pt in line])
        out[v] = lines
    return out


def chaikin(pts):
    """One round of corner cutting: softer lines than the grid's."""
    if len(pts) < 3:
        return pts
    closed = pts[0] == pts[-1]
    out = [] if closed else [pts[0]]
    for (x0, y0), (x1, y1) in zip(pts, pts[1:]):
        out.append((0.75 * x0 + 0.25 * x1, 0.75 * y0 + 0.25 * y1))
        out.append((0.25 * x0 + 0.75 * x1, 0.25 * y0 + 0.75 * y1))
    if closed:
        out.append(out[0])
    else:
        out.append(pts[-1])
    return out


def contour_lines(cache, lon0, lat0, lon1, lat1, step):
    """[(height, polyline in world coords)] for a box (degrees)."""
    n = 1 << DEM_ZOOM

    def wxy(lon, lat):
        s = math.sin(math.radians(lat))
        return (lon + 180.0) / 360.0 * n, (0.5 - math.log((1 + s) / (1 - s)) / (4 * math.pi)) * n

    ax, ay = wxy(lon0, lat1)
    bx, by = wxy(lon1, lat0)
    x0, y0, x1, y1 = int(ax), int(ay), int(bx), int(by)
    print(f'DEM: {(x1 - x0 + 1) * (y1 - y0 + 1)} tiles at z{DEM_ZOOM}', file=sys.stderr)
    g = smooth(grid(cache, x0, y0, x1, y1))
    # Only the box (the tiles reach past it): its pixels, plus one.
    c0, r0 = max(0, int((ax - x0) * 256) - 1), max(0, int((ay - y0) * 256) - 1)
    c1, r1 = min(len(g[0]), int((bx - x0) * 256) + 2), min(len(g), int((by - y0) * 256) + 2)
    g = [row[c0:c1] for row in g[r0:r1]]
    size = 256 * n
    out = []
    for v, lines in contours(g, step).items():
        for line in lines:
            if len(line) < 4:
                continue
            line = chaikin(line)
            out.append((v, [((x0 * 256 + c0 + c + 0.5) / size, (y0 * 256 + r0 + r + 0.5) / size) for c, r in line]))
    return out

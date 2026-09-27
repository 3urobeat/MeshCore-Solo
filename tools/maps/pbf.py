"""OSM PBF extract -> the elements osm_vector.py works on (as Overpass gives
them with `out geom;`), for a box. Needs pyosmium (pip install osmium).

Two passes over the file: relations first (hiking routes, multipolygons: which
ways they need), then nodes and ways with their node locations. A way is kept
if it touches the box; a relation if one of its ways was kept.
"""
import sys

WAY_KEYS = ('highway', 'waterway', 'natural', 'landuse', 'building', 'tourism')
AREA_KEYS = ('natural', 'landuse', 'building', 'water')
NODE_KEYS = ('natural', 'tourism', 'amenity', 'place')


def read_pbf(path, box):
    """box: (lon0, lat0, lon1, lat1). Returns a list of element dicts."""
    import osmium
    lon0, lat0, lon1, lat1 = box

    rels, want = [], {}   # relations kept; way id -> [(relation index, role)]
    for r in osmium.FileProcessor(path, osmium.osm.RELATION):
        t = dict(r.tags)
        if not (t.get('route') == 'hiking' or (t.get('type') == 'multipolygon' and any(k in t for k in AREA_KEYS))):
            continue
        i = len(rels)
        rels.append({'type': 'relation', 'id': r.id, 'tags': t, 'members': []})
        for m in r.members:
            if m.type == 'w':
                want.setdefault(m.ref, []).append((i, m.role))
    print(f'PBF: {len(rels)} relations', file=sys.stderr)

    elements, geoms = [], {}
    # Untagged nodes (most of them) never reach Python; ways do, untagged ones
    # can be a multipolygon's outline.
    fp = osmium.FileProcessor(path, osmium.osm.NODE | osmium.osm.WAY).with_locations() \
        .with_filter(osmium.filter.EmptyTagFilter().enable_for(osmium.osm.NODE))
    for o in fp:
        if o.is_node():
            if not any(k in o.tags for k in NODE_KEYS):
                continue
            loc = o.location
            if loc.valid() and lon0 <= loc.lon <= lon1 and lat0 <= loc.lat <= lat1:
                elements.append({'type': 'node', 'id': o.id, 'lat': loc.lat, 'lon': loc.lon, 'tags': dict(o.tags)})
            continue
        in_rel = o.id in want
        tagged = any(k in o.tags for k in WAY_KEYS)
        if not in_rel and not tagged:
            continue
        g = [{'lat': n.lat, 'lon': n.lon} for n in o.nodes if n.location.valid()]
        if len(g) < 2:
            continue
        if (max(p['lon'] for p in g) < lon0 or min(p['lon'] for p in g) > lon1 or
                max(p['lat'] for p in g) < lat0 or min(p['lat'] for p in g) > lat1):
            continue
        if in_rel:
            geoms[o.id] = g
        if tagged:
            elements.append({'type': 'way', 'id': o.id, 'tags': dict(o.tags), 'geometry': g})
    for wid, uses in want.items():
        if wid in geoms:
            for i, role in uses:
                rels[i]['members'].append({'type': 'way', 'ref': wid, 'role': role, 'geometry': geoms[wid]})
    kept = [r for r in rels if r['members']]
    print(f'PBF: {len(elements)} nodes / ways, {len(kept)} relations in the box', file=sys.stderr)
    return elements + kept

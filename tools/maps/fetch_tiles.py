#!/usr/bin/env python3
"""Offline raster map tiles for the ui-lvgl map screen (Wio Tracker L2 SD card).

Writes <out>/maps/{z}/{x}/{y}.png -- the same layout Meshtastic MUI uses, so a
card prepared for either works for both. Copy the `maps` folder to the root of
the SD card. The device can also download an area itself over WiFi; this is
the offline / bulk option.

  tools/maps/fetch_tiles.py --preset krakow --url URL --out /Volumes/SDCARD
  tools/maps/fetch_tiles.py --bbox 19.90,50.04,19.98,50.08 --zoom 10-16 --url URL --out ./card
  tools/maps/fetch_tiles.py --preset krakow --dry-run          # count + size estimate

Tile source: --url (or TILE_URL) with {z}/{x}/{y}. There is deliberately no
default. Check the provider's terms before downloading an area:
  - tile.openstreetmap.org forbids offline use and answers with an
    "access blocked" image;
  - OpenTopoMap (https://a.tile.opentopomap.org/{z}/{x}/{y}.png) tolerates
    moderate use -- a town or a trip, not a country;
  - keyed providers (MapTiler, Thunderforest, Stadia, ...) put the key in the
    URL; their plans say whether offline caching is allowed;
  - your own render server (e.g. from an OSM extract) has no limits.
--attribution is written to maps/attribution.txt; the device shows it on the
map, as the data licences require.

The tool stops if the server answers every tile with the same image (a
"blocked" or "API key required" placeholder).

Resumable (existing files are skipped), single connection, throttled.
--manifest also writes maps/manifest.json (the list of tiles), which the
browser simulator (variants/sim/web/lvgl.html) uses to preload them.
"""

import argparse
import json
import math
import os
import sys
import time
import urllib.error
import urllib.request

PRESETS = {
    # name: (lon_min, lat_min, lon_max, lat_max, z_min, z_max)
    "krakow":      (19.90, 50.04, 19.98, 50.08, 10, 16),   # Old Town + Kazimierz; test set
    "krakow-wide": (19.79, 49.97, 20.22, 50.13, 10, 15),
    "tatry":       (19.75, 49.15, 20.25, 49.32, 10, 15),
    "warszawa":    (20.85, 52.10, 21.20, 52.35, 10, 15),
    "poland":      (14.07, 49.00, 24.15, 54.84,  5,  9),   # overview zooms only
}

USER_AGENT = "MeshCore-Solo-map-prep/1.0 (offline tiles for a LoRa handheld; throttled)"


def tile_xy(lon, lat, z):
    n = 1 << z
    x = int((lon + 180.0) / 360.0 * n)
    lat_r = math.radians(max(min(lat, 85.0511), -85.0511))
    y = int((1.0 - math.asinh(math.tan(lat_r)) / math.pi) / 2.0 * n)
    return min(max(x, 0), n - 1), min(max(y, 0), n - 1)


def tiles_for(bbox, z_min, z_max):
    lon_min, lat_min, lon_max, lat_max = bbox
    for z in range(z_min, z_max + 1):
        x0, y0 = tile_xy(lon_min, lat_max, z)   # north-west corner
        x1, y1 = tile_xy(lon_max, lat_min, z)   # south-east corner
        for x in range(x0, x1 + 1):
            for y in range(y0, y1 + 1):
                yield z, x, y


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--preset", choices=sorted(PRESETS))
    ap.add_argument("--bbox", help="lon_min,lat_min,lon_max,lat_max")
    ap.add_argument("--zoom", help="z_min-z_max (default from preset, else 10-15)")
    ap.add_argument("--out", default=".", help="folder that gets maps/ (e.g. the SD card root)")
    ap.add_argument("--url", default=os.environ.get("TILE_URL"), help="tile URL template with {z} {x} {y}")
    ap.add_argument("--attribution", default="\u00a9 OpenStreetMap contributors",
                    help="credit line shown on the device's map")
    ap.add_argument("--delay", type=float, default=1.0, help="seconds between requests (default 1)")
    ap.add_argument("--manifest", action="store_true", help="also write maps/manifest.json")
    ap.add_argument("--dry-run", action="store_true")
    a = ap.parse_args()

    if a.preset:
        *bbox, z_min, z_max = PRESETS[a.preset]
    elif a.bbox:
        bbox = [float(v) for v in a.bbox.split(",")]
        z_min, z_max = 10, 15
    else:
        ap.error("give --preset or --bbox")
    if a.zoom:
        z_min, z_max = (int(v) for v in a.zoom.split("-"))

    tiles = list(tiles_for(bbox, z_min, z_max))
    print(f"{len(tiles)} tiles, z{z_min}-{z_max}, ~{len(tiles) * 25 // 1024} MB (at ~25 KB/tile)")
    if a.dry_run:
        return
    if not a.url:
        ap.error("give --url (see the provider notes in --help)")

    root = os.path.join(a.out, "maps")
    os.makedirs(root, exist_ok=True)
    with open(os.path.join(root, "attribution.txt"), "w", encoding="utf-8") as f:
        f.write(a.attribution + "\n")
    got = skipped = failed = 0
    seen = {}   # payload -> count, over the first downloads: a placeholder repeats
    for i, (z, x, y) in enumerate(tiles, 1):
        path = os.path.join(root, str(z), str(x), f"{y}.png")
        if os.path.exists(path) and os.path.getsize(path) > 0:
            skipped += 1
            continue
        os.makedirs(os.path.dirname(path), exist_ok=True)
        url = a.url.format(z=z, x=x, y=y)
        for attempt in range(3):
            try:
                req = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
                with urllib.request.urlopen(req, timeout=20) as r:
                    data = r.read()
                if got < 8:
                    seen[data] = seen.get(data, 0) + 1
                    if seen[data] >= 4:
                        sys.exit("stopped: the server returns the same image for different tiles "
                                 "(blocked or API key required?) -- check --url; delete what was written")
                with open(path + ".tmp", "wb") as f:
                    f.write(data)
                os.replace(path + ".tmp", path)
                got += 1
                break
            except (urllib.error.URLError, TimeoutError) as e:
                if attempt == 2:
                    failed += 1
                    print(f"  failed {z}/{x}/{y}: {e}", file=sys.stderr)
                time.sleep(2 * (attempt + 1))
        if i % 50 == 0:
            print(f"  {i}/{len(tiles)}")
        time.sleep(a.delay)

    if a.manifest:
        listed = []
        for dirpath, _, files in os.walk(root):
            for fn in files:
                if fn.endswith(".png") or fn == "attribution.txt":
                    listed.append(os.path.relpath(os.path.join(dirpath, fn), root).replace(os.sep, "/"))
        with open(os.path.join(root, "manifest.json"), "w") as f:
            json.dump(sorted(listed), f)
    print(f"done: {got} downloaded, {skipped} already there, {failed} failed -> {root}")


if __name__ == "__main__":
    main()

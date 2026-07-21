#!/usr/bin/env python3
"""Inspect the offline tile cache (maps/area.mbtiles).

  python3 tiles_info.py                 # per-zoom counts + covered lat/lon box
  python3 tiles_info.py --lat 43.14 --lon 27.93   # is this point covered at z11/13/15?

Helps explain "empty box" tiles on the offline map: a blank tile means that exact
z/x/y is not in the cache (never downloaded, download failed, or you panned outside the
downloaded area). Re-running "Download visible area" in preflight fills missing tiles.
"""

import argparse
import glob
import math
import os
import sqlite3
from configparser import ConfigParser

HERE = os.path.dirname(os.path.abspath(__file__))
ZOOMS = [11, 13, 15]


def maps_dir():
    cfg = ConfigParser()
    cfg.read(os.path.join(HERE, "config.ini"))
    p = cfg.get("server", "mbtiles", fallback="./maps/area.mbtiles")
    return os.path.dirname(os.path.normpath(os.path.join(HERE, p)))


def num2deg(x, y, z):
    n = 1 << z
    lon = x / n * 360.0 - 180.0
    lat = math.degrees(math.atan(math.sinh(math.pi * (1 - 2 * y / n))))
    return lat, lon


def deg2num(lat, lon, z):
    n = 1 << z
    x = int((lon + 180.0) / 360.0 * n)
    y = int((1 - math.asinh(math.tan(math.radians(lat))) / math.pi) / 2 * n)
    return x, y


def report(path, lat, lon):
    name = os.path.splitext(os.path.basename(path))[0]
    conn = sqlite3.connect(f"file:{path}?mode=ro", uri=True)
    rows = conn.execute(
        "SELECT zoom_level, COUNT(*), MIN(tile_column), MAX(tile_column), "
        "MIN(tile_row), MAX(tile_row) FROM tiles GROUP BY zoom_level ORDER BY zoom_level"
    ).fetchall()
    print(f"=== {name} ({path}) ===")
    if not rows:
        print("  (empty)\n"); return
    total = 0
    print(f"  {'zoom':>4} {'tiles':>7}   covered area (lat,lon NW -> SE)")
    for z, cnt, minc, maxc, minr, maxr in rows:
        total += cnt
        ymax = (1 << z) - 1                 # MBTiles rows are TMS; convert to XYZ y
        nlat, wlon = num2deg(minc, ymax - maxr, z)
        slat, elon = num2deg(maxc + 1, (ymax - minr) + 1, z)
        print(f"  {z:>4} {cnt:>7}   ({nlat:.4f},{wlon:.4f}) -> ({slat:.4f},{elon:.4f})")
    print(f"  total: {total} tiles")
    if lat is not None and lon is not None:
        print(f"  coverage at ({lat},{lon}):")
        for z in ZOOMS:
            x, y = deg2num(lat, lon, z)
            hit = conn.execute(
                "SELECT 1 FROM tiles WHERE zoom_level=? AND tile_column=? AND tile_row=?",
                (z, x, (1 << z) - 1 - y),
            ).fetchone()
            print(f"    z{z:>2}  tile {x}/{y}  {'PRESENT' if hit else 'MISSING'}")
    print()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--lat", type=float)
    ap.add_argument("--lon", type=float)
    ap.add_argument("--basemap", help="inspect only this basemap's cache")
    args = ap.parse_args()

    md = maps_dir()
    files = ([os.path.join(md, args.basemap + ".mbtiles")] if args.basemap
             else sorted(glob.glob(os.path.join(md, "*.mbtiles"))))
    files = [f for f in files if os.path.exists(f)]
    if not files:
        print(f"No tile caches in {md}\nDownload an area in preflight first.")
        return
    for f in files:
        report(f, args.lat, args.lon)


if __name__ == "__main__":
    main()

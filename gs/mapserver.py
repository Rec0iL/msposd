#!/usr/bin/env python3
"""Offline moving-map bridge for the Ground Station (x86 PoC).

Single stdlib-only process that:
  * listens for MSP forwarded by `msposd --out 127.0.0.1:14560`,
    extracts aircraft lat/lon/heading/course,
  * serves the Leaflet viewer and a hybrid tile endpoint: offline MBTiles when a tile is
    cached, else a live proxy to OSM when online (so the user can browse to find a place),
  * downloads the currently-viewed area into the MBTiles for offline use,
  * an SSE position feed and a small settings endpoint.

See documentation/offline-map-overlay-spec.md.
"""

import json
import math
import os
import re
import signal
import socket
import sqlite3
import sys
import struct
import tempfile
import threading
import time
import urllib.request
import zipfile
from configparser import ConfigParser
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlparse, parse_qs

HERE = os.path.dirname(os.path.abspath(__file__))

# Standalone (PyInstaller) awareness. When frozen into a single binary, bundled
# read-only assets (web/) are unpacked to sys._MEIPASS, while writable data
# (config.ini, maps/, state.ini, landmarks.db) must live next to the executable
# so it survives restarts. In normal script mode both are just HERE, so the dev
# workflow (mapserver.py + mapwin) is unchanged.
_FROZEN = getattr(sys, "frozen", False)
APP_DIR = os.path.dirname(sys.executable) if _FROZEN else HERE  # writable data root
RES_DIR = getattr(sys, "_MEIPASS", HERE)                        # bundled read-only assets

CONFIG_PATH = os.path.join(APP_DIR, "config.ini")
ZOOMS = [11, 13, 15]          # zoom levels stored for offline / flight use
BROWSE_MIN, BROWSE_MAX = 2, 18  # live-proxy browse range
MAX_TILES = 12000              # refuse offline downloads larger than this

# Selectable basemaps — all keyless and OK for app/proxy use (unlike OSM's volunteer
# servers, which 403 bulk/proxy traffic). ESRI uses {z}/{y}/{x} ordering; the OSM-style
# sources use {z}/{x}/{y} and may include {s} for a/b/c subdomain rotation (see
# _fmt_tile). NOTE: the Esri imagery layer serves JPEG — it previews in the browser but
# the native OSD map decodes PNG only, so the PNG sources below also render in the OSD.
# You are responsible for each provider's usage terms and attribution.
BASEMAPS = {
    "Satellite": "https://server.arcgisonline.com/ArcGIS/rest/services/World_Imagery/MapServer/tile/{z}/{y}/{x}",
    "Streets": "https://server.arcgisonline.com/ArcGIS/rest/services/World_Street_Map/MapServer/tile/{z}/{y}/{x}",
    "Topo": "https://server.arcgisonline.com/ArcGIS/rest/services/World_Topo_Map/MapServer/tile/{z}/{y}/{x}",
    "OpenTopoMap": "https://a.tile.opentopomap.org/{z}/{x}/{y}.png",
    # keyed provider: {key} is filled from [server] tile_key in the gitignored
    # config.ini, so no API key is committed to source.
    "Thunderforest": "https://api.thunderforest.com/outdoors/{z}/{x}/{y}.png?apikey={key}",
}
# NB: volunteer OSM servers (CyclOSM, OSM-Humanitarian, tile.openstreetmap.org) are
# deliberately NOT listed — they throttle/block app/proxy/bulk traffic and make the
# preview go blank mid-browse. Use custom sources via [server] tile_url at your own risk.

DEFAULTS = {
    "server": {
        "port": "8088",
        "udp_listen": "127.0.0.1:14560",
        "mbtiles": "./maps/area.mbtiles",
        "web_root": "./web",
        # used only when [map] basemap is not one of BASEMAPS (custom source)
        "tile_url": BASEMAPS["Satellite"],
        "tile_delay_ms": "60",
        # API key for keyed basemaps (fills {key} in the tile URL, e.g. Thunderforest)
        "tile_key": "",
    },
    "map": {
        "zoom": "15",
        "basemap": "Satellite",
        "center_lat": "", "center_lon": "",
    },
}


def current_tile_url():
    """Return the {z}/{x}/{y} tile URL template for the active basemap.

    Returns: the URL string from BASEMAPS, or the custom [server] tile_url
    when the configured basemap is not a known BASEMAPS entry.
    """
    with config_lock:
        bm = config["map"].get("basemap", "Satellite")
        custom = config["server"]["tile_url"]
    return BASEMAPS.get(bm, custom)

SUBDOMAINS = "abc"          # hosts to rotate through for tile URLs that use {s}

def _fmt_tile(template, z, x, y):
    """Fill a {z}/{x}/{y} tile-URL template. Also substitutes {s} (a/b/c subdomain
    rotation) and {key} (the provider API key from [server] tile_key, kept in the
    gitignored config.ini). Templates without those fields simply ignore them."""
    s = SUBDOMAINS[(x + y) % len(SUBDOMAINS)]
    with config_lock:
        key = config["server"].get("tile_key", "")
    return template.format(s=s, z=z, x=x, y=y, key=key)

USER_AGENT = "msposd-gs-map/0.1 (+https://github.com/OpenIPC/msposd)"


def tile_ctype(data):
    """Guess a tile's MIME type from its magic bytes.

    data: raw tile bytes.
    Returns: "image/jpeg" for a JPEG SOI marker, else "image/png".
    """
    return "image/jpeg" if data[:2] == b"\xff\xd8" else "image/png"


def load_config():
    """Load config.ini layered over DEFAULTS.

    Returns: a ConfigParser seeded with DEFAULTS then overlaid with any
    values found in CONFIG_PATH.
    """
    cfg = ConfigParser()
    cfg.read_dict(DEFAULTS)
    cfg.read(CONFIG_PATH)
    return cfg


def save_config():
    """Write the in-memory config back to CONFIG_PATH, holding config_lock."""
    with config_lock:
        with open(CONFIG_PATH, "w") as fh:
            config.write(fh)


config = load_config()
config_lock = threading.Lock()
db_lock = threading.Lock()   # serialize all SQLite access (one writer + many readers, same file)

# web/ is a bundled read-only asset (RES_DIR); tile caches are writable (APP_DIR).
WEB_ROOT = os.path.normpath(os.path.join(RES_DIR, config["server"]["web_root"]))
# Per-basemap caches live in the maps/ folder as <basemap>.mbtiles, so switching
# basemaps offline serves the right tiles instead of a mix.
MAPS_DIR = os.path.dirname(os.path.normpath(os.path.join(APP_DIR, config["server"]["mbtiles"])))
LANDMARKS_DB = os.path.join(MAPS_DIR, "landmarks.db")
landmarks_db_lock = threading.Lock()


def current_basemap():
    """Return the active basemap name from config (default "Satellite")."""
    with config_lock:
        return config["map"].get("basemap", "Satellite")


def mbtiles_for(basemap):
    """Build the MBTiles cache path for a basemap.

    basemap: basemap name (non-alphanumeric chars are sanitised to '_').
    Returns: absolute path to <MAPS_DIR>/<safe-name>.mbtiles.
    """
    safe = re.sub(r"[^A-Za-z0-9_-]", "_", basemap or "Satellite")
    return os.path.join(MAPS_DIR, safe + ".mbtiles")

STATIC_WHITELIST = {
    "viewer.html": "text/html; charset=utf-8",
    "leaflet.js": "application/javascript",
    "leaflet.css": "text/css",
    "icons/plane.svg": "image/svg+xml",
}

# ---------------------------------------------------------------------------
# Telemetry state
# ---------------------------------------------------------------------------

state_lock = threading.Lock()
latest = {"lat": None, "lon": None, "heading": 0, "course": 0, "fix": 0, "sats": 0}
state_seq = 0


def valid_coord(lat, lon):
    """Validate a GPS fix.

    lat, lon: decimal degrees (may be None).
    Returns: True if both are in range and not the 0,0 null-island fix.
    """
    return (
        lat is not None and lon is not None
        and -90.0 <= lat <= 90.0 and -180.0 <= lon <= 180.0
        and not (abs(lat) < 1e-7 and abs(lon) < 1e-7)
    )


def update_state(**kw):
    """Merge telemetry fields into `latest` and bump state_seq for SSE.

    kw: telemetry keys to update (lat, lon, heading, course, fix, sats).
    """
    global state_seq
    with state_lock:
        latest.update(kw)
        state_seq += 1


MSP_RAW_GPS = 106
MSP_ATTITUDE = 108
MSP_CMD_STATUS = 101
MSP_CMD_STATUS_EX = 150

# ---------------------------------------------------------------------------
# Shared home/target state.
#   home   : captured live from GPS on the OSD station at arm; station-local
#            runtime, kept in state.ini (also read by msposd.c via ini_parser).
#   target : preflight-authored; stored in landmarks.db `waypoints` so it travels
#            with the POIs as part of the map pack (see osd/util/poi_osd.c).
# ---------------------------------------------------------------------------

STATE_PATH = os.path.join(APP_DIR, "state.ini")
state_io_lock = threading.Lock()
geo = {"target": None, "home": None}   # each None or (lat, lon)
armed_state = False
seen_disarmed = False                  # gates home capture (see on_status)
home_pending = False


def load_target_from_db():
    """Return the preflight target (lat, lon) from landmarks.db, or None."""
    try:
        with landmarks_db_lock:
            conn = open_landmarks_db()
            row = conn.execute(
                "SELECT lat, lon FROM waypoints WHERE kind='target'").fetchone()
            conn.close()
        return (row[0], row[1]) if row else None
    except sqlite3.Error:
        return None


def save_target_to_db(pt):
    """Persist (or clear, when pt is None) the target in landmarks.db `waypoints`."""
    with landmarks_db_lock:
        conn = open_landmarks_db()
        if pt:
            conn.execute("INSERT OR REPLACE INTO waypoints(kind, lat, lon) "
                         "VALUES('target', ?, ?)", (float(pt[0]), float(pt[1])))
        else:
            conn.execute("DELETE FROM waypoints WHERE kind='target'")
        conn.commit()
        conn.close()


def load_geo_state():
    """Load home from state.ini and the target from landmarks.db into `geo`.

    Home is read only when its `set=1`; malformed values become None. The target
    lives in the DB now, but a legacy [target] in state.ini is honoured as a
    one-time migration fallback and copied into the DB.
    """
    cfg = ConfigParser()
    cfg.read(STATE_PATH)

    def pt(sec):
        """Read a [sec] lat/lon point; (lat, lon) when set=1 and parseable, else None."""
        if cfg.has_section(sec) and cfg.get(sec, "set", fallback="0") == "1":
            try:
                return (cfg.getfloat(sec, "lat"), cfg.getfloat(sec, "lon"))
            except ValueError:
                return None
        return None

    geo["home"] = pt("home")

    geo["target"] = load_target_from_db()
    if geo["target"] is None:                       # migration: adopt legacy state.ini target
        legacy = pt("target")
        if legacy:
            geo["target"] = legacy
            save_target_to_db(legacy)


def save_geo_state():
    """Atomically write the home point to state.ini (target lives in the DB).

    Uses a temp file + os.replace so a concurrent C-side read (msposd.c)
    never observes a half-written file.
    """
    cfg = ConfigParser()
    cfg.add_section("home")
    v = geo["home"]
    cfg.set("home", "set", "1" if v else "0")
    cfg.set("home", "lat", f"{v[0]:.7f}" if v else "")
    cfg.set("home", "lon", f"{v[1]:.7f}" if v else "")
    tmp = STATE_PATH + ".tmp"
    with state_io_lock:                 # atomic write so a C read never sees half a file
        with open(tmp, "w") as fh:
            cfg.write(fh)
        os.replace(tmp, STATE_PATH)


def on_status(now_armed):
    """Track armed state and arm a one-shot home capture on rising edge.

    now_armed: armed flag from the latest MSP status frame.
    Home is captured only on a witnessed disarmed->armed transition, so a
    mid-flight restart keeps the persisted home instead of recapturing.
    """
    # Capture home only on a real disarmed->armed transition we actually witnessed.
    # If the server restarts mid-flight the first frame is already armed (no witnessed
    # disarm), so we keep the persisted home instead of recapturing at the wrong spot.
    global armed_state, seen_disarmed, home_pending
    armed_edge = disarmed_edge = False
    with state_lock:
        if not now_armed and armed_state:
            disarmed_edge = True
        if not now_armed:
            seen_disarmed = True
        rising = now_armed and not armed_state
        armed_state = now_armed
        if rising and seen_disarmed:
            home_pending = True
            armed_edge = True
    if disarmed_edge:
        print("[mapserver] DISARMED")
    if armed_edge:
        print("[mapserver] ARMED — waiting for GPS fix to capture home")
    maybe_capture_home()


def maybe_capture_home():
    """Capture home at the current position once a GPS fix arrives.

    No-op unless a home capture is pending (armed edge seen) and a valid fix
    exists. On success, stores home in `geo` and persists state.ini.
    """
    global home_pending
    pos = None
    with state_lock:
        if home_pending and latest["fix"] and latest["lat"] is not None:
            pos = (latest["lat"], latest["lon"])
            home_pending = False
    if pos:
        geo["home"] = pos
        save_geo_state()
        print(f"[mapserver] HOME captured: lat={pos[0]:.6f} lon={pos[1]:.6f}")


def parse_msp_frames(buf):
    """Yield (cmd, payload) for each CRC-valid MSP v1 frame in a datagram.

    buf: raw bytes possibly containing several '$M' framed messages.
    Yields: (command_id, payload_bytes) tuples; bad frames are skipped.
    """
    i, n = 0, len(buf)
    while i + 6 <= n:
        if buf[i] != 0x24 or buf[i + 1] != 0x4D:
            i += 1
            continue
        length, cmd = buf[i + 3], buf[i + 4]
        end = i + 5 + length
        if end >= n:
            break
        payload = buf[i + 5:end]
        crc = length ^ cmd
        for b in payload:
            crc ^= b
        if crc == buf[end]:
            yield cmd, payload
            i = end + 1
        else:
            i += 1


def handle_msp(cmd, p):
    """Decode one MSP frame and update telemetry/armed state.

    cmd: MSP command id. p: payload bytes.
    Handles RAW_GPS (position/course), ATTITUDE (heading) and STATUS (armed).
    """
    if cmd == MSP_RAW_GPS and len(p) >= 16:
        lat = struct.unpack_from("<i", p, 2)[0] / 1e7
        lon = struct.unpack_from("<i", p, 6)[0] / 1e7
        if not valid_coord(lat, lon):
            lat, lon = None, None
        update_state(
            fix=p[0], sats=p[1],
            lat=lat, lon=lon,
            course=struct.unpack_from("<h", p, 14)[0] / 10.0,
        )
        maybe_capture_home()
    elif cmd == MSP_ATTITUDE and len(p) >= 6:
        update_state(heading=struct.unpack_from("<h", p, 4)[0])
    elif cmd in (MSP_CMD_STATUS, MSP_CMD_STATUS_EX) and len(p) >= 7:
        on_status(bool(p[6] & 0x01))


def udp_listener():
    """Bind the configured UDP socket and forward MSP frames forever.

    Runs in a daemon thread; each datagram is parsed and dispatched to
    handle_msp. Blocks indefinitely on recvfrom.
    """
    host, port = config["server"]["udp_listen"].split(":")
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind((host, int(port)))
    print(f"[mapserver] listening for MSP on udp://{host}:{port}")
    while True:
        data, _ = sock.recvfrom(4096)
        for cmd, payload in parse_msp_frames(data):
            handle_msp(cmd, payload)


# ---------------------------------------------------------------------------
# Connectivity probe (so the UI can show online/offline and we avoid slow
# proxy timeouts when offline)
# ---------------------------------------------------------------------------

online_lock = threading.Lock()
online_ok = False


def online_check_loop():
    """Probe the active tile server every 15s and update `online_ok`.

    Runs in a daemon thread so the UI can show online/offline and the tile
    handler can skip slow proxy timeouts when offline.
    """
    global online_ok
    while True:
        probe = _fmt_tile(current_tile_url(), 0, 0, 0)
        ok = False
        try:
            req = urllib.request.Request(probe, headers={"User-Agent": USER_AGENT})
            with urllib.request.urlopen(req, timeout=5) as r:
                r.read(1)
            ok = True
        except Exception:
            ok = False
        with online_lock:
            online_ok = ok
        time.sleep(15)


def is_online():
    """Return the last connectivity-probe result (True if online)."""
    with online_lock:
        return online_ok


# ---------------------------------------------------------------------------
# Tile math + MBTiles
# ---------------------------------------------------------------------------


def deg2num(lat, lon, z):
    """Convert lat/lon to a Slippy-map tile X/Y at zoom z.

    lat, lon: decimal degrees. z: zoom level.
    Returns: (x, y) tile indices clamped to the valid 0..2^z-1 range.
    """
    n = 1 << z
    x = int((lon + 180.0) / 360.0 * n)
    y = int((1 - math.asinh(math.tan(math.radians(lat))) / math.pi) / 2 * n)
    return max(0, min(n - 1, x)), max(0, min(n - 1, y))


def num2deg(x, y, z):
    """Convert a Slippy-map tile X/Y at zoom z to its NW-corner lat/lon.

    x, y: tile indices. z: zoom level.
    Returns: (lat, lon) of the tile's north-west corner.
    """
    n = 1 << z
    lat = math.degrees(math.atan(math.sinh(math.pi * (1 - 2 * y / n))))
    return lat, x / n * 360.0 - 180.0


def bbox_tile_ranges(north, south, east, west, z):
    """Compute the tile X and Y index ranges covering a bbox at zoom z.

    north/south/east/west: bbox edges in degrees. z: zoom level.
    Returns: (xrange, yrange) of XYZ tile indices spanning the box.
    """
    x0, _ = deg2num(north, west, z)
    x1, _ = deg2num(north, east, z)
    _, y0 = deg2num(north, west, z)  # north -> smaller y
    _, y1 = deg2num(south, west, z)
    return range(min(x0, x1), max(x0, x1) + 1), range(min(y0, y1), max(y0, y1) + 1)


def plan_total(north, south, east, west):
    """Count tiles a bbox download would cover across all ZOOMS.

    north/south/east/west: bbox edges in degrees.
    Returns: total tile count summed over every zoom in ZOOMS.
    """
    total = 0
    for z in ZOOMS:
        xs, ys = bbox_tile_ranges(north, south, east, west, z)
        total += len(xs) * len(ys)
    return total


def open_mbtiles(basemap, write=False):
    """Open (and, when writing, create) a basemap's MBTiles database.

    basemap: basemap name. write: True to create schema and open read-write,
    False to open read-only.
    Returns: an sqlite3 connection with a 10s busy timeout.
    """
    path = mbtiles_for(basemap)
    if write:
        os.makedirs(MAPS_DIR, exist_ok=True)
        conn = sqlite3.connect(path, timeout=10)
        conn.execute("PRAGMA busy_timeout=10000")
        conn.execute(
            "CREATE TABLE IF NOT EXISTS tiles("
            "zoom_level INTEGER, tile_column INTEGER, tile_row INTEGER, tile_data BLOB)"
        )
        conn.execute(
            "CREATE UNIQUE INDEX IF NOT EXISTS tile_index "
            "ON tiles(zoom_level, tile_column, tile_row)"
        )
        conn.execute("CREATE TABLE IF NOT EXISTS metadata(name TEXT, value TEXT)")
        return conn
    conn = sqlite3.connect(f"file:{path}?mode=ro", uri=True, timeout=10)
    conn.execute("PRAGMA busy_timeout=10000")
    return conn


def read_tile(basemap, z, x, y):
    """Read one tile from a basemap's offline MBTiles cache.

    basemap: basemap name. z/x/y: XYZ tile coordinates (converted to TMS).
    Returns: tile bytes, or None if the cache or tile is absent.
    """
    if not os.path.exists(mbtiles_for(basemap)):
        return None
    ymbt = (1 << z) - 1 - y
    with db_lock:
        conn = open_mbtiles(basemap, write=False)
        try:
            row = conn.execute(
                "SELECT tile_data FROM tiles WHERE zoom_level=? AND tile_column=? AND tile_row=?",
                (z, x, ymbt),
            ).fetchone()
            return row[0] if row else None
        finally:
            conn.close()


def fetch_tile(basemap, z, x, y):
    """Fetch one tile live from a basemap's remote tile server.

    basemap: basemap name. z/x/y: XYZ tile coordinates.
    Returns: tile bytes. Raises on network/HTTP error.
    """
    url = _fmt_tile(BASEMAPS.get(basemap, config["server"]["tile_url"]), z, x, y)
    req = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
    with urllib.request.urlopen(req, timeout=15) as r:
        return r.read()


def coverage_in_bbox(basemap, z, north, south, east, west):
    """Return present XYZ (x, y) tiles for a basemap within a bbox at zoom z."""
    path = mbtiles_for(basemap)
    if not os.path.exists(path):
        return []
    xs, ys = bbox_tile_ranges(north, south, east, west, z)
    ymax = (1 << z) - 1
    rmin, rmax = ymax - (ys.stop - 1), ymax - ys.start   # XYZ y range -> TMS row range
    with db_lock:
        conn = open_mbtiles(basemap, write=False)
        try:
            rows = conn.execute(
                "SELECT tile_column, tile_row FROM tiles WHERE zoom_level=? "
                "AND tile_column BETWEEN ? AND ? AND tile_row BETWEEN ? AND ?",
                (z, xs.start, xs.stop - 1, rmin, rmax),
            ).fetchall()
        finally:
            conn.close()
    return [[c, ymax - r] for c, r in rows]    # back to XYZ y


def cache_summary(basemap):
    """Per-zoom tile counts and covered box for a basemap's offline cache."""
    path = mbtiles_for(basemap)
    if not os.path.exists(path):
        return {"basemap": basemap, "total": 0, "zooms": []}
    with db_lock:
        conn = open_mbtiles(basemap, write=False)
        try:
            rows = conn.execute(
                "SELECT zoom_level, COUNT(*), MIN(tile_column), MAX(tile_column), "
                "MIN(tile_row), MAX(tile_row) FROM tiles GROUP BY zoom_level ORDER BY zoom_level"
            ).fetchall()
        finally:
            conn.close()
    zooms, total = [], 0
    for z, cnt, minc, maxc, minr, maxr in rows:
        total += cnt
        ymax = (1 << z) - 1
        nlat, wlon = num2deg(minc, ymax - maxr, z)
        slat, elon = num2deg(maxc + 1, (ymax - minr) + 1, z)
        zooms.append({"z": z, "count": cnt,
                      "n": round(nlat, 4), "w": round(wlon, 4),
                      "s": round(slat, 4), "e": round(elon, 4)})
    return {"basemap": basemap, "total": total, "zooms": zooms}


# ---------------------------------------------------------------------------
# Offline download of a viewed area
# ---------------------------------------------------------------------------

dl_lock = threading.Lock()
dl_status = {"state": "idle", "done": 0, "total": 0, "failed": 0, "msg": "", "lm_count": None}


def set_dl(**kw):
    """Update the shared download-status dict under dl_lock.

    kw: fields to merge into dl_status (state, done, total, failed, msg, ...).
    """
    with dl_lock:
        dl_status.update(kw)


def download_worker(basemap, north, south, east, west):
    """Download a bbox's tiles (all ZOOMS) into the basemap cache, then POIs.

    basemap: basemap name. north/south/east/west: bbox edges in degrees.
    Runs in a thread; reports progress via set_dl and refuses areas over
    MAX_TILES. Also stores center in config and fetches Overpass landmarks.
    """
    total = plan_total(north, south, east, west)
    if total > MAX_TILES:
        set_dl(state="error", msg=f"{total} tiles > limit {MAX_TILES}; zoom in")
        return
    set_dl(state="running", done=0, total=total, failed=0, msg=f"downloading {basemap}")
    delay = int(config["server"]["tile_delay_ms"]) / 1000.0
    done = failed = 0
    try:
        with db_lock:
            conn = open_mbtiles(basemap, write=True)
        try:
            for z in ZOOMS:
                xs, ys = bbox_tile_ranges(north, south, east, west, z)
                ymax = (1 << z) - 1
                for x in xs:
                    for y in ys:
                        ymbt = ymax - y
                        with db_lock:
                            have = conn.execute(
                                "SELECT 1 FROM tiles WHERE zoom_level=? AND tile_column=? AND tile_row=?",
                                (z, x, ymbt),
                            ).fetchone()
                        if not have:
                            data = None
                            for _ in range(3):          # retry transient fetch errors
                                try:
                                    data = fetch_tile(basemap, z, x, y)
                                    break
                                except Exception:
                                    time.sleep(0.3)
                            if data is not None:
                                with db_lock:
                                    conn.execute(
                                        "INSERT OR REPLACE INTO tiles VALUES(?,?,?,?)",
                                        (z, x, ymbt, data),
                                    )
                                time.sleep(delay)
                            else:
                                failed += 1
                        done += 1
                        if done % 10 == 0:
                            with db_lock:
                                conn.commit()
                            set_dl(done=done, failed=failed)
            with db_lock:
                conn.commit()
        finally:
            with db_lock:
                conn.close()
    except Exception as e:                              # never let the thread die silently
        set_dl(state="error", failed=failed, msg=f"download error: {e}")
        print(f"[mapserver] download error: {e}")
        return
    with config_lock:
        config["map"]["center_lat"] = str((north + south) / 2)
        config["map"]["center_lon"] = str((east + west) / 2)
    save_config()
    log_cache_summary(basemap)

    # Download POI landmarks for the same area (non-fatal if Overpass is unreachable)
    set_dl(done=done, failed=failed, msg="tiles saved; fetching POIs…")
    n_lm, lm_note = 0, "POIs unavailable"
    try:
        raw = fetch_landmarks(north, south, east, west)
        features = parse_landmarks(raw)
        n_lm = store_landmarks(features)
        lm_note = f"{n_lm} POIs"
        print(f"[mapserver] {n_lm} landmarks stored")
    except Exception as e:
        print(f"[mapserver] POI download failed: {e}")

    set_dl(state="done", done=done, failed=failed,
           msg=f"saved ({failed} failed) · {lm_note}", lm_count=n_lm)


def log_cache_summary(basemap):
    """Print the per-zoom cache summary (same view as tiles_info.py) after a download."""
    try:
        import tiles_info
        tiles_info.report(mbtiles_for(basemap), None, None)
    except Exception as e:
        print(f"[mapserver] cache summary failed: {e}")


# ---------------------------------------------------------------------------
# POI landmarks (Overpass API → local SQLite)
# ---------------------------------------------------------------------------

OVERPASS_URL = "https://overpass-api.de/api/interpreter"


def open_landmarks_db():
    """Open the landmarks SQLite DB, creating/migrating its schema.

    Drops and rebuilds the old kind/subtype/ele-only schema, ensures the
    landmarks and poi_selection tables exist.
    Returns: an sqlite3 connection with a 10s busy timeout.
    """
    os.makedirs(MAPS_DIR, exist_ok=True)
    conn = sqlite3.connect(LANDMARKS_DB, timeout=10)
    conn.execute("PRAGMA busy_timeout=10000")
    # Migrate old schema (kind/subtype/ele columns) → new schema (tags JSON blob)
    try:
        conn.execute("SELECT tags, kind, name_en FROM landmarks LIMIT 0")
    except sqlite3.OperationalError:
        conn.execute("DROP TABLE IF EXISTS landmarks")
        conn.execute("DROP INDEX IF EXISTS lm_osm")
    conn.execute(
        "CREATE TABLE IF NOT EXISTS landmarks("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "osm_type TEXT, osm_id INTEGER,"
        "name TEXT, name_en TEXT, lat REAL, lon REAL,"
        "kind TEXT, subtype TEXT, ele REAL,"   # indexed columns for SQL filtering
        "tags TEXT)"                           # full OSM tag JSON for everything else
    )
    conn.execute(
        "CREATE UNIQUE INDEX IF NOT EXISTS lm_osm ON landmarks(osm_type, osm_id)"
    )
    # Which kind/subtype pairs the OSD should draw (subtype '' = none). Persists the
    # preflight tree selection; read by both the server and osd/util/poi_osd.c.
    conn.execute(
        "CREATE TABLE IF NOT EXISTS poi_selection("
        "kind TEXT NOT NULL, subtype TEXT NOT NULL, enabled INTEGER NOT NULL,"
        "PRIMARY KEY(kind, subtype))"
    )
    # User-authored points (the preflight target, and named waypoints later). Kept
    # separate from the Overpass-populated `landmarks` table so a POI re-download
    # never clobbers them. Read by osd/util/poi_osd.c. A missing 'target' row means
    # no target is set. This is the preflight->flight target handoff (was state.ini).
    conn.execute(
        "CREATE TABLE IF NOT EXISTS waypoints("
        "kind TEXT PRIMARY KEY, lat REAL NOT NULL, lon REAL NOT NULL, name TEXT)"
    )
    conn.commit()
    return conn


def fetch_landmarks(north, south, east, west):
    """Query Overpass for all named features in the bbox; returns parsed JSON dict."""
    bbox = f"{south},{west},{north},{east}"
    # All named nodes + named non-highway ways/relations (roads would add thousands of
    # duplicate segments per road name and are not useful for landmark navigation).
    query = (
        "[out:json][timeout:60];\n(\n"
        f'  node["name"]({bbox});\n'
        f'  way["name"][!"highway"]({bbox});\n'
        f'  relation["name"][!"highway"]({bbox});\n'
        ");\nout center tags;\n"
    )
    req = urllib.request.Request(
        OVERPASS_URL, data=query.encode(),
        headers={"User-Agent": USER_AGENT,
                 "Content-Type": "application/x-www-form-urlencoded"},
    )
    with urllib.request.urlopen(req, timeout=90) as r:
        return json.loads(r.read())


def _classify(tags):
    """Return (kind, subtype) from OSM tags using a priority order."""
    for key in ("place", "natural", "amenity", "tourism", "historic",
                "aeroway", "waterway", "leisure", "landuse", "man_made",
                "military", "boundary", "shop", "office", "emergency"):
        val = tags.get(key)
        if val:
            return key, val
    for k, v in tags.items():
        if k not in ("name", "name:en", "source", "created_by", "note", "wikidata", "wikipedia"):
            return k, v
    return "other", "unknown"


def _parse_ele(tags):
    """Parse an elevation value from OSM tags.

    tags: OSM tag dict.
    Returns: the 'ele' tag as a float (first value if ';'-separated), or None.
    """
    try:
        return float(str(tags.get("ele", "")).split(";")[0].strip())
    except ValueError:
        return None


def parse_landmarks(resp):
    """Store every named feature with kind/subtype/ele columns + full tags JSON."""
    features = []
    for el in resp.get("elements", []):
        tags = el.get("tags", {})
        name = tags.get("name") or tags.get("name:en")
        if not name:
            continue
        osm_type, osm_id = el["type"], el["id"]
        if osm_type == "node":
            lat, lon = el.get("lat"), el.get("lon")
        else:
            c = el.get("center", {})
            lat, lon = c.get("lat"), c.get("lon")
        if lat is None or lon is None:
            continue
        kind, subtype = _classify(tags)
        features.append({
            "osm_type": osm_type, "osm_id": osm_id,
            "name": name, "name_en": tags.get("name:en") or None,
            "lat": lat, "lon": lon,
            "kind": kind, "subtype": subtype, "ele": _parse_ele(tags),
            "tags": json.dumps(tags, ensure_ascii=False),
        })
    return features


def store_landmarks(features):
    """Upsert landmark list into the local DB; returns count stored."""
    with landmarks_db_lock:
        conn = open_landmarks_db()
        try:
            conn.executemany(
                "INSERT OR REPLACE INTO landmarks"
                "(osm_type,osm_id,name,name_en,lat,lon,kind,subtype,ele,tags)"
                " VALUES(:osm_type,:osm_id,:name,:name_en,:lat,:lon,:kind,:subtype,:ele,:tags)",
                features,
            )
            conn.commit()
        finally:
            conn.close()
    return len(features)


def query_landmarks(north, south, east, west):
    """Return cached landmarks within a bbox with all columns."""
    if not os.path.exists(LANDMARKS_DB):
        return []
    with landmarks_db_lock:
        conn = open_landmarks_db()
        try:
            rows = conn.execute(
                "SELECT name,name_en,lat,lon,kind,subtype,ele,tags FROM landmarks"
                " WHERE lat BETWEEN ? AND ? AND lon BETWEEN ? AND ?",
                (south, north, west, east),
            ).fetchall()
        finally:
            conn.close()
    return [{"name": r[0], "name_en": r[1], "lat": r[2], "lon": r[3],
             "kind": r[4], "subtype": r[5], "ele": r[6],
             "tags": json.loads(r[7])} for r in rows]


def landmarks_count():
    """Return the number of stored landmarks (0 if the DB is absent)."""
    if not os.path.exists(LANDMARKS_DB):
        return 0
    with landmarks_db_lock:
        conn = open_landmarks_db()
        try:
            return conn.execute("SELECT COUNT(*) FROM landmarks").fetchone()[0]
        finally:
            conn.close()


def poi_types():
    """Grouped kind/subtype counts joined with the saved enable state.
    Seeds place-only on first use (empty selection table)."""
    if not os.path.exists(LANDMARKS_DB):
        return []
    with landmarks_db_lock:
        conn = open_landmarks_db()
        try:
            # First run: seed enabled=1 for place, 0 for everything else.
            if conn.execute("SELECT COUNT(*) FROM poi_selection").fetchone()[0] == 0:
                conn.executemany(
                    "INSERT OR IGNORE INTO poi_selection(kind, subtype, enabled)"
                    " VALUES(?,?,?)",
                    [(k, st, 1 if k == "place" else 0) for k, st in conn.execute(
                        "SELECT DISTINCT kind, COALESCE(NULLIF(subtype,''),'')"
                        " FROM landmarks")],
                )
                conn.commit()
            rows = conn.execute(
                "SELECT l.kind, COALESCE(NULLIF(l.subtype,''),'') AS st,"
                "       COUNT(*) AS cnt, COALESCE(s.enabled,0) AS en"
                " FROM landmarks l"
                " LEFT JOIN poi_selection s"
                "   ON s.kind=l.kind AND s.subtype=COALESCE(NULLIF(l.subtype,''),'')"
                " GROUP BY l.kind, st ORDER BY l.kind, st"
            ).fetchall()
        finally:
            conn.close()
    return [{"kind": r[0], "subtype": r[1], "count": r[2], "enabled": bool(r[3])}
            for r in rows]


def save_poi_selection(items):
    """Upsert [{kind, subtype, enabled}] rows into poi_selection."""
    with landmarks_db_lock:
        conn = open_landmarks_db()
        try:
            conn.executemany(
                "INSERT INTO poi_selection(kind, subtype, enabled) VALUES(?,?,?)"
                " ON CONFLICT(kind, subtype) DO UPDATE SET enabled=excluded.enabled",
                [(str(it["kind"]), str(it.get("subtype", "")), 1 if it["enabled"] else 0)
                 for it in items],
            )
            conn.commit()
        finally:
            conn.close()


def start_download(basemap, north, south, east, west):
    """Spawn a download_worker thread unless one is already running.

    basemap: basemap name. north/south/east/west: bbox edges in degrees.
    Returns: True if a download was started, False if one is in progress.
    """
    with dl_lock:
        if dl_status["state"] == "running":
            return False
    threading.Thread(
        target=download_worker, args=(basemap, north, south, east, west), daemon=True
    ).start()
    return True


# ---------------------------------------------------------------------------
# HTTP
# ---------------------------------------------------------------------------


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, *a):
        """Silence the default per-request stderr logging."""
        pass

    def handle(self):
        """Run the request loop, swallowing client-abort socket errors.

        The browser cancels in-flight tile loads while panning/zooming; this
        absorbs the resulting broken-pipe/reset instead of logging a traceback.
        """
        try:
            super().handle()
        except (BrokenPipeError, ConnectionResetError):
            pass

    def _send(self, code, body=b"", ctype="text/plain", extra=None):
        """Write an HTTP response with body, content type and headers.

        code: status code. body: bytes or str. ctype: Content-Type.
        extra: optional header dict (defaults Cache-Control to no-store).
        """
        if isinstance(body, str):
            body = body.encode()
        extra = dict(extra or {})
        # Default to no-store so WebKitGTK never serves stale JSON (e.g. /poi-types
        # checkbox states). Tiles/static pass an explicit Cache-Control to override.
        extra.setdefault("Cache-Control", "no-store")
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        for k, v in extra.items():
            self.send_header(k, v)
        self.end_headers()
        if body:
            self.wfile.write(body)

    def _json_body(self):
        """Read and parse the request body as JSON.

        Returns: the decoded object ({} when the body is empty).
        Raises: ValueError on malformed JSON.
        """
        length = int(self.headers.get("Content-Length", 0))
        return json.loads(self.rfile.read(length) or b"{}")

    def do_GET(self):
        """Route GET requests to the matching handler (static, SSE, JSON, tiles)."""
        path = urlparse(self.path).path
        if path in ("/", "/viewer.html"):
            return self.serve_static("viewer.html")
        if path == "/pos":
            return self.serve_sse()
        if path == "/status":
            return self.serve_status()
        if path == "/settings":
            return self.serve_settings_get()
        if path == "/download":
            with dl_lock:
                return self._send(200, json.dumps(dl_status), "application/json")
        if path == "/coverage":
            return self.serve_coverage()
        if path == "/cache":
            return self.serve_cache()
        if path == "/landmarks":
            return self.serve_landmarks()
        if path == "/poi-types":
            return self._send(200, json.dumps(poi_types()), "application/json")
        if path == "/export":
            return self.serve_export()
        if path.startswith("/tiles/"):
            return self.serve_tile(path)
        return self.serve_static(path.lstrip("/"))

    def do_POST(self):
        """Route POST requests (settings, target, download, poi-selection)."""
        path = urlparse(self.path).path
        if path == "/settings":
            return self.serve_settings_post()
        if path == "/target":
            return self.serve_target_post()
        if path == "/download":
            return self.serve_download_post()
        if path == "/poi-selection":
            return self.serve_poi_selection_post()
        self._send(404, b"not found")

    def serve_static(self, name):
        """Serve a whitelisted static file from WEB_ROOT (no-store cached).

        name: requested file name; must be in STATIC_WHITELIST and path-safe.
        Sends 404 for unknown or traversal names.
        """
        ctype = STATIC_WHITELIST.get(name)
        if ctype is None or ".." in name:
            return self._send(404, b"not found")
        try:
            with open(os.path.join(WEB_ROOT, name), "rb") as fh:
                body = fh.read()
        except OSError:
            return self._send(404, b"not found")
        # Never cache the app shell/JS — otherwise WebKitGTK serves a stale
        # viewer.html across mapwin restarts and code changes don't take effect.
        self._send(200, body, ctype, {"Cache-Control": "no-store"})

    def serve_tile(self, path):
        """Serve a /tiles/{z}/{x}/{y} tile: offline cache first, else live proxy.

        path: the request path. The optional ?src= picks a basemap and
        ?offline= forces cache-only. Sends 204 when no tile is available.
        """
        parts = path.split("/")
        try:
            z, x = int(parts[2]), int(parts[3])
            y = int(parts[4].split(".")[0])
        except (IndexError, ValueError):
            return self._send(400, b"bad tile")
        q = parse_qs(urlparse(self.path).query)
        src = q.get("src", [None])[0]
        offline = q.get("offline", [None])[0]      # "test offline" -> cache only, no proxy
        basemap = src if src in BASEMAPS else current_basemap()
        # 1) offline cache for this basemap
        try:
            data = read_tile(basemap, z, x, y)
        except sqlite3.Error:
            data = None
        # 2) live proxy when online (unless the user is testing offline coverage)
        if data is None and not offline and BROWSE_MIN <= z <= BROWSE_MAX and is_online():
            try:
                data = fetch_tile(basemap, z, x, y)
            except Exception:
                data = None
        if data is None:
            return self._send(204)
        self._send(200, data, tile_ctype(data), {"Cache-Control": "max-age=86400"})

    def serve_cache(self):
        """Send the per-zoom cache summary (plus landmark count) as JSON.

        Basemap is chosen by the ?src= query param, else the active basemap.
        """
        src = parse_qs(urlparse(self.path).query).get("src", [None])[0]
        basemap = src if src in BASEMAPS else current_basemap()
        summary = cache_summary(basemap)
        summary["lm_count"] = landmarks_count()
        self._send(200, json.dumps(summary), "application/json")

    def serve_export(self):
        """Stream a zip of the map pack (the basemap's .mbtiles + landmarks.db).

        This is the preflight->flight handoff: the user saves it wherever they
        want (the browser's download picks the location) and copies it to the OSD
        station's gs/maps/. Basemap comes from ?src=, else the active one.
        """
        src = parse_qs(urlparse(self.path).query).get("src", [None])[0]
        basemap = src if src in BASEMAPS else current_basemap()
        mb = mbtiles_for(basemap)
        if not os.path.exists(mb):
            return self._send(404, b"nothing downloaded for this basemap yet")

        # Build the zip in a temp file (ZIP_STORED: tiles/db are already compact,
        # so skip the CPU of deflating ~100 MB), then stream it out.
        safe = os.path.splitext(os.path.basename(mb))[0]
        tmp = tempfile.NamedTemporaryFile(prefix="mappack_", suffix=".zip", delete=False)
        tmp.close()
        try:
            with zipfile.ZipFile(tmp.name, "w", zipfile.ZIP_STORED, allowZip64=True) as zf:
                zf.write(mb, os.path.basename(mb))
                with landmarks_db_lock:                # keep landmarks.db read-consistent
                    if os.path.exists(LANDMARKS_DB):
                        zf.write(LANDMARKS_DB, "landmarks.db")

            size = os.path.getsize(tmp.name)
            self.send_response(200)
            self.send_header("Content-Type", "application/zip")
            self.send_header("Content-Length", str(size))
            self.send_header("Content-Disposition",
                             f'attachment; filename="{safe}-mappack.zip"')
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            with open(tmp.name, "rb") as fh:
                while True:
                    chunk = fh.read(256 * 1024)
                    if not chunk:
                        break
                    self.wfile.write(chunk)
        finally:
            try:
                os.remove(tmp.name)
            except OSError:
                pass

    def serve_landmarks(self):
        """Send cached landmarks within the ?n/s/e/w bbox as JSON.

        Sends 400 if any bbox query parameter is missing or non-numeric.
        """
        q = parse_qs(urlparse(self.path).query)
        try:
            n, s = float(q["n"][0]), float(q["s"][0])
            e, w = float(q["e"][0]), float(q["w"][0])
        except (KeyError, ValueError):
            return self._send(400, b"need n, s, e, w")
        self._send(200, json.dumps(query_landmarks(n, s, e, w)), "application/json")

    def serve_coverage(self):
        """Send which tiles are cached within the ?z/n/s/e/w bbox as JSON.

        Returns a truncated empty result when the requested area exceeds 4000
        tiles. Sends 400 on missing/invalid query parameters.
        """
        q = parse_qs(urlparse(self.path).query)
        try:
            z = int(q["z"][0])
            n, s = float(q["n"][0]), float(q["s"][0])
            e, w = float(q["e"][0]), float(q["w"][0])
        except (KeyError, ValueError):
            return self._send(400, b"need z, n, s, e, w")
        src = q.get("src", [None])[0]
        basemap = src if src in BASEMAPS else current_basemap()
        xs, ys = bbox_tile_ranges(n, s, e, w, z)
        want = len(xs) * len(ys)
        if want > 4000:
            return self._send(200, json.dumps({"z": z, "truncated": True, "present": []}),
                              "application/json")
        present = coverage_in_bbox(basemap, z, n, s, e, w)
        self._send(200, json.dumps({"z": z, "want": want, "present": present}),
                   "application/json")

    def serve_status(self):
        """Send overall server status as JSON.

        Includes online state, cache presence, saved center/zoom, download
        progress, basemap list, armed flag and target/home points.
        """
        with config_lock:
            m = config["map"]
            center = ([float(m["center_lat"]), float(m["center_lon"])]
                      if m["center_lat"] and m["center_lon"] else None)
            zoom = int(m["zoom"])
            basemap = m.get("basemap", "Satellite")
            key = config["server"].get("tile_key", "")
        # basemaps that need an API key ({key} in the URL) but have none configured:
        # the UI greys these out so they can't be selected until a key is set.
        disabled = [n for n, u in BASEMAPS.items() if "{key}" in u and not key]
        with dl_lock:
            dl = dict(dl_status)
        with state_lock:
            ar = armed_state
        tgt, hm = geo["target"], geo["home"]
        body = json.dumps({
            "online": is_online(),
            "mbtiles": os.path.exists(mbtiles_for(basemap)),
            "center": center, "zoom": zoom, "max_tiles": MAX_TILES, "download": dl,
            "basemaps": list(BASEMAPS.keys()), "basemaps_disabled": disabled,
            "basemap": basemap,
            "armed": ar,
            "target": {"lat": tgt[0], "lon": tgt[1]} if tgt else None,
            "home": {"lat": hm[0], "lon": hm[1]} if hm else None,
        })
        self._send(200, body, "application/json")

    def serve_settings_get(self):
        """Send the saved zoom and center settings as JSON."""
        with config_lock:
            m = config["map"]
            body = json.dumps({
                "zoom": int(m["zoom"]),
                "center_lat": m["center_lat"] or None,
                "center_lon": m["center_lon"] or None,
            })
        self._send(200, body, "application/json")

    def serve_settings_post(self):
        """Update zoom and/or basemap from a JSON body, then persist config.

        Sends 400 on malformed JSON; unknown basemaps are ignored.
        """
        try:
            data = self._json_body()
        except ValueError:
            return self._send(400, b"bad json")
        with config_lock:
            m = config["map"]
            if "zoom" in data:
                m["zoom"] = str(int(data["zoom"]))
            if data.get("basemap") in BASEMAPS:
                m["basemap"] = data["basemap"]
        save_config()
        self._send(200, b"{}", "application/json")

    def serve_target_post(self):
        """Set or clear the target point from a JSON {lat, lon} body.

        Null lat/lon clears the target. Sends 400 on bad JSON or coordinates;
        persists the target to landmarks.db (waypoints) on success.
        """
        try:
            d = self._json_body()
        except ValueError:
            return self._send(400, b"bad json")
        if d.get("lat") is None or d.get("lon") is None:
            geo["target"] = None
        else:
            try:
                geo["target"] = (float(d["lat"]), float(d["lon"]))
            except (ValueError, TypeError):
                return self._send(400, b"bad lat/lon")
        save_target_to_db(geo["target"])
        self._send(200, b"{}", "application/json")

    def serve_poi_selection_post(self):
        """Persist the POI enable selection from a JSON {selection: [...]} body.

        Sends 400 if the body is malformed or selection is not a list of
        valid {kind, subtype, enabled} items.
        """
        try:
            d = self._json_body()
        except ValueError:
            return self._send(400, b"bad json")
        sel = d.get("selection")
        if not isinstance(sel, list):
            return self._send(400, b"need selection list")
        try:
            save_poi_selection(sel)
        except (KeyError, TypeError):
            return self._send(400, b"bad selection item")
        self._send(200, b"{}", "application/json")

    def serve_download_post(self):
        """Start an offline download for the bbox in a JSON body.

        Body needs north/south/east/west. Sends 409 if the area exceeds
        MAX_TILES or a download is already running, else 200 with the count.
        """
        try:
            d = self._json_body()
            north, south = float(d["north"]), float(d["south"])
            east, west = float(d["east"]), float(d["west"])
        except (ValueError, KeyError):
            return self._send(400, b"need north, south, east, west")
        total = plan_total(north, south, east, west)
        if total > MAX_TILES:
            return self._send(409, json.dumps({"error": f"area too large ({total} tiles) — zoom in", "total": total}), "application/json")
        if not start_download(current_basemap(), north, south, east, west):
            return self._send(409, json.dumps({"error": "busy"}), "application/json")
        self._send(200, json.dumps({"started": True, "total": total}), "application/json")

    def serve_sse(self):
        """Stream live position updates to the client as Server-Sent Events.

        Pushes a new event whenever telemetry changes and a keep-alive comment
        every 2s. Returns when the client disconnects.
        """
        self.send_response(200)
        self.send_header("Content-Type", "text/event-stream")
        self.send_header("Cache-Control", "no-cache")
        self.send_header("Connection", "keep-alive")
        self.end_headers()
        last_seq, last_beat = -1, 0.0
        try:
            while True:
                with state_lock:
                    seq, snap = state_seq, dict(latest)
                now = time.time()
                if seq != last_seq and snap["lat"] is not None:
                    self.wfile.write(f"data: {json.dumps(snap)}\n\n".encode())
                    self.wfile.flush()
                    last_seq, last_beat = seq, now
                elif now - last_beat > 2.0:
                    self.wfile.write(b": keep-alive\n\n")
                    self.wfile.flush()
                    last_beat = now
                time.sleep(0.2)
        except (BrokenPipeError, ConnectionResetError):
            pass


def server_responds(port, timeout=1.5):
    """True if a mapserver instance is actually answering HTTP on the port.

    Distinguishes a live instance (reuse it) from a process that merely holds the
    port but is not serving — e.g. one suspended with Ctrl+Z, or hung.
    """
    import urllib.error
    try:
        urllib.request.urlopen(f"http://127.0.0.1:{port}/viewer.html", timeout=timeout)
        return True
    except urllib.error.HTTPError:
        return True    # answered with an HTTP status -> it is alive
    except Exception:
        return False   # refused / timed out / not answering


def main():
    """Start the server: restore state, launch UDP/online threads, serve HTTP.

    Installs a SIGTERM handler for clean shutdown and blocks in
    serve_forever until interrupted or terminated.

    With --open-browser (the default when packaged as a standalone binary) it
    opens the preflight page in the user's default browser instead of relying on
    the WebKit `mapwin` host — so the standalone app needs no bundled browser.
    """
    import argparse
    import errno
    import webbrowser

    ap = argparse.ArgumentParser(description="Offline preflight map server")
    ap.add_argument("--port", type=int, default=int(config["server"]["port"]),
                    help="HTTP port (default from config.ini)")
    ap.add_argument("--open-browser", dest="open_browser", action="store_true",
                    default=_FROZEN,
                    help="open the preflight page in the system browser "
                         "(default: on when packaged, off in dev)")
    ap.add_argument("--no-browser", dest="open_browser", action="store_false",
                    help="do not open a browser (dev default; used with mapwin)")
    args = ap.parse_args()

    port = args.port
    url = f"http://127.0.0.1:{port}/viewer.html?mode=preflight"

    # Single-instance: bind the HTTP port first. If it is already taken, another
    # copy of the app is running (closing the browser does not stop the detached
    # server) — so instead of crashing with "address already in use", check
    # whether that instance is actually serving:
    #   * responding  -> reuse it: just reopen the browser and exit.
    #   * not responding -> it is stuck (suspended with Ctrl+Z, or hung). We can't
    #     take the port from it, so tell the user how to clear it.
    # Bind before starting the UDP/online threads so a re-launch never fights over
    # the MSP socket either.
    try:
        httpd = ThreadingHTTPServer(("127.0.0.1", port), Handler)
    except OSError as e:
        if e.errno != errno.EADDRINUSE:
            raise
        if server_responds(port):
            print(f"[mapserver] already running on 127.0.0.1:{port}; reopening browser")
            if args.open_browser:
                webbrowser.open(url)
            return
        print(f"[mapserver] ERROR: port {port} is in use but no server is responding.")
        print("[mapserver] A previous instance is probably suspended (Ctrl+Z) or hung.")
        print("[mapserver] Clear it, then relaunch:")
        print("[mapserver]   - if you background/suspended it: run 'fg' then press Ctrl+C, or 'kill %1'")
        print(f"[mapserver]   - otherwise free the port, e.g.: fuser -k {port}/tcp   (Linux)")
        print("[mapserver] Tip: stop this server with Ctrl+C, not Ctrl+Z (Ctrl+Z only freezes it).")
        sys.exit(1)

    # Mark request-handler threads as daemon so they never block shutdown.
    httpd.daemon_threads = True

    load_geo_state()   # restore target + home (home survives a mid-flight restart)
    if geo["home"]:
        print(f"[mapserver] home loaded: {geo['home'][0]:.6f},{geo['home'][1]:.6f}")
    threading.Thread(target=udp_listener, daemon=True).start()
    threading.Thread(target=online_check_loop, daemon=True).start()
    # Handle SIGTERM (sent by map.sh / systemd / kill) the same as Ctrl+C:
    # call httpd.shutdown() from a side thread so serve_forever() exits cleanly.
    signal.signal(signal.SIGTERM,
                  lambda *_: threading.Thread(target=httpd.shutdown, daemon=True).start())
    print(f"[mapserver] http://127.0.0.1:{port}  (web_root={WEB_ROOT})")
    print(f"[mapserver] per-basemap tile caches in {MAPS_DIR}")

    if args.open_browser:
        # open once the socket is accepting, so the first load succeeds
        threading.Timer(0.6, lambda: webbrowser.open(url)).start()
        print(f"[mapserver] opening {url} in the system browser")
        print("[mapserver] leave this window open; press Ctrl+C to stop (not Ctrl+Z).")

    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        httpd.shutdown()
    finally:
        httpd.server_close()
        print("[mapserver] stopped")


if __name__ == "__main__":
    main()

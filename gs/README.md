# Offline Moving-Map Overlay — Ground Station (x86 PoC)

A standalone offline moving map for the GS. Reads telemetry from the MSP stream that
`msposd` already forwards, renders aircraft position / course vector / target over
offline OSM tiles in a Leaflet page shown by a WPE/WebKit window.

`msposd` is **not modified** — `mapserver.py` is a separate MSP consumer.

Spec: [../documentation/offline-map-overlay-spec.md](../documentation/offline-map-overlay-spec.md)

## One-time setup (needs internet)

```bash
./fetch-leaflet.sh                 # vendor leaflet.js/css into web/
```

Tiles are downloaded **from inside the app** (see Pre-flight below), so MOBAC is
optional. Runtime tools: `python3` (stdlib only), and `cog` (WPE) or a WebKitGTK browser.

## Pre-flight (browse / download / settings)

1. `./run-map.sh` (no `msposd` needed). When online, the map shows live OSM tiles so you
   can browse; the panel shows an online/offline dot.
2. **Scroll and drag** to your area. The **Download visible area** button shows a live
   tile estimate (z11/13/15 of the current view) — click it to save those tiles into
   `maps/area.mbtiles` for offline use. Re-downloading only fetches missing tiles; if the
   view is too large the button asks you to zoom in.
3. Review offline anytime: once cached, the area is served from the MBTiles even with no
   internet. The map opens centered on the last downloaded area.
4. Set a **target** lat/lon if desired — persisted and restored next launch.

### Basemap

Pick the basemap from the **Basemap** dropdown in the panel (persisted). Options:

| Basemap | Content | Format |
| ------- | ------- | ------ |
| Satellite | ESRI World Imagery — aerial, no labels (default) | JPEG |
| Streets | ESRI World Street Map — roads + city names | JPEG |
| Topo | ESRI World Topo — terrain + roads + names | JPEG |
| OpenTopoMap | topographic with contours | PNG |
| Thunderforest | Outdoors — topo/trails, great for FPV (needs API key) | PNG |

The first four are keyless and permit app/proxy use.

**Keyed providers (Thunderforest):** get a free key at thunderforest.com, then put it in
the **gitignored** `config.ini` under `[server] tile_key = <your-key>` — the `{key}` field
in the tile URL is filled from there, so no secret is committed. A keyed basemap with **no
key configured is greyed out** in the dropdown (shown as "… (needs key)") and can't be
selected until you set one. Other keyed styles work too: point `[server] tile_url` at e.g.
`.../landscape/{z}/{x}/{y}.png?apikey={key}`.

**Do not** use volunteer OSM tile servers (`tile.openstreetmap.org`, and the community
`openstreetmap.fr` servers behind CyclOSM / Humanitarian) — they throttle or 403
app/proxy/bulk traffic ([policy](https://operations.osmfoundation.org/policies/tiles/)),
which makes the preview go blank mid-browse and stalls downloads. You are responsible for
complying with the chosen provider's terms and attribution.

**Format note:** the browser preview renders any format, but the **native OSD map
(`[map]` in `msposd.ini`) decodes PNG only** — so the PNG basemaps above also work as the
in-flight OSD overlay, while the ESRI **JPEG** imagery layers are preview/WebKit-only until
a JPEG decoder is added to the native side. Sources whose URL uses `{s}` rotate over
subdomains `a`/`b`/`c` automatically.

Each basemap has its **own offline cache** (`maps/<basemap>.mbtiles`), so download and
coverage are per basemap — switching basemaps offline serves the right tiles, not a mix.
(An older single `maps/area.mbtiles` from before this change is no longer read; re-download
the area for the basemap you want.)

A custom source can be set via `tile_url` in `config.ini` (used when `[map] basemap` is
not one of the built-ins). Template fields: `{z} {x} {y}`, plus `{s}` (rotates over
subdomains `a`/`b`/`c`) and `{key}` (filled from `[server] tile_key`). `tile_delay_ms`
throttles the offline download.

> **Hybrid imagery + labels** (Esri/Google style) is not a single tile source — it is the
> aerial base composited with a transparent labels/roads overlay. The pipeline stores one
> tile per `{z}/{x}/{y}`, so a hybrid needs either a second preview-only overlay layer, or
> tiles composited at download time into one PNG. Not built in yet.

## Modes (`map.sh`)

`map.sh` starts `mapserver.py` (if not already up) and opens the viewer in one of three
modes. All modes use the tiny WebKitGTK host (`mapwin`) so window reuse, toggle, and
`--kill` behave consistently.

```bash
./map.sh preflight                 # fullscreen config: browse, download, click-to-target
./map.sh preview --follow plane    # 1/4-screen borderless preview window, plane centered
./map.sh preview --follow fit      # 1/4-screen borderless preview window, fit plane+home+target
./map.sh full    --follow plane    # fullscreen borderless overlay (Phase B: OSD over it)
./map.sh --kill                    # close mapserver + map window/WebKit helpers
```

- **preflight** — toolbox visible. Browse, **Download visible area**, pick basemap, and
  **click the map to set a target**. Free pan/zoom.
- **preview / full** — no toolbox, locked interaction, driven by telemetry:
  - `--follow center`: plane stays centered, map moves, fixed zoom.
  - `--follow fit`: keeps **plane, home and target** all in view (auto scale/center).
- `--kill` closes the existing map server/window; with a mode it kills first, then relaunches.

Overlay modes accept extra window flags, passed through to `mapwin`:
`--topmost` (keep above other windows) and `--transparent` (transparent background, for
compositing over video). Always-on-top is **off** unless `--topmost` is given.

`run-map.sh` is kept as an alias for `./map.sh preflight`.

## Realtime / moving-map test

The viewer shows the aircraft (rotating marker), course vector, target, and a **house icon
for home** as soon as MSP arrives on `udp://127.0.0.1:14560`.

**Desk demo (no aircraft)** — orbit a fake plane; `--arm` triggers home capture:

```bash
./map.sh preview --follow plane        # terminal 1 (server + overlay)
python3 sim_msp.py --arm               # terminal 2 (fake telemetry; arms after 3 s)
```

**Real use** — feed it from the ground-side msposd instead of the simulator:

```bash
msposd --master <gs-msp-source> --osd -r 50 --ahi 3 --matrix 11 --out 127.0.0.1:14560
```

## Checking offline tiles / empty boxes

In **preflight**, tick **Show downloaded coverage** — cached tiles for the current
basemap are shaded green over the map (for the stored zoom nearest your current view),
with a `cached/total` count. Use it to see exactly what you have and spot gaps before a
flight.

From the command line:

```bash
python3 tiles_info.py                          # all basemaps: per-zoom counts + covered box
python3 tiles_info.py --basemap Satellite      # just one basemap
python3 tiles_info.py --lat 43.14 --lon 27.93  # PRESENT/MISSING at z11/13/15 for a point
```

A blank ("empty box") tile means that exact `z/x/y` is not cached for the current
basemap. Common causes and fixes:
- **Outside the downloaded area** — the plane/view moved beyond the box you saved. Open
  preflight and Download the wider area.
- **Failed tiles during download** — transient errors. Downloads now retry 3× and are
  resumable, so just re-run "Download visible area" over the same spot; only missing
  tiles are fetched (watch the `failed` count when it finishes).
- **Zoom not downloaded** — only z11/13/15 are stored. `--follow fit` can pick an
  in-between zoom; at those zooms offline tiles will be blank (online they proxy fine).

## Target & home

- **Target**: click the map (preflight) — a preflight-authored point, so it is stored
  in `maps/landmarks.db` (`waypoints` table, `kind='target'`) and travels with the POIs
  as part of the map pack. `msposd` reads it there (`osd/util/poi_osd.c`). A legacy
  `state.ini [target]` is still honoured once and migrated into the DB.
- **Home**: captured automatically on the **disarmed→armed** transition (from MSP STATUS),
  saved to `state.ini [home]`, and reloaded on startup — so restarting the server
  mid-flight keeps the correct home instead of recapturing at the plane's current spot.
  Home is live, station-local runtime state, so it stays in `state.ini` (read by
  `msposd.c` via its `ini_parser`).
- This split means the preflight→flight handoff of authored data is DB-only: the tiles
  (`<basemap>.mbtiles`) and points (`landmarks.db`, incl. POI selection + target). You
  can prepare them on any PC and copy them to the OSD station; only `[home]` is local.

## Standalone installer (packaged preflight)

For users who don't have Python, the preflight map can be packaged into a **single
self-contained binary per OS** with PyInstaller. It bundles `mapserver.py` + `web/`,
starts the local server, and opens the map in the user's **default system browser** — no
Python, WebKit, or Chromium to install. Writable data (`config.ini`, `maps/`, `state.ini`,
`landmarks.db`) is created **next to the executable** on first run, so the resulting
`maps/<basemap>.mbtiles` + `landmarks.db` is the pack you copy to the OSD/flight station.

Build (needs Python 3.9+ and `pip`; `pyinstaller` is installed by the scripts — build on
each target OS, e.g. a Windows `.exe` needs a Windows machine):

```bash
./gs/pack/build.sh        # Linux / macOS
gs\pack\build.bat         # Windows
# output: dist/msposd-preflight[.exe]   (one file)
```

Or build all three OSes at once via CI: `.github/workflows/preflight-pack.yml` runs a
Linux/macOS/Windows matrix on `workflow_dispatch` or a pushed `preflight-v*` tag —
download the binaries from the run's artifacts.

Run:

```bash
./dist/msposd-preflight               # starts server, opens your browser
./dist/msposd-preflight --port 9000
./dist/msposd-preflight --no-browser  # server only
```

Notes: unsigned binaries trip macOS Gatekeeper / Windows SmartScreen (code-sign +
notarize for public release); some Windows AV engines false-positive on PyInstaller
one-file builds. The dev workflow is unaffected — `./map.sh preflight` still runs
`mapserver.py` + the WebKit `mapwin` window. Full details: [`pack/README.md`](pack/README.md).

## Files

| File | Purpose |
| ---- | ------- |
| `mapserver.py` | MSP parse (pos/heading/armed) + HTTP (SSE, tiles, download, status, target) |
| `mapwin` | minimal WebKitGTK window host (borderless/fullscreen/geometry) |
| `map.sh` | mode launcher (preflight / preview / full) |
| `web/viewer.html` | Leaflet viewer (modes via `?mode=&follow=`) |
| `web/leaflet.js/.css` | vendored by `fetch-leaflet.sh` |
| `sim_msp.py` | fake MSP telemetry for desk testing (`--arm` for home) |
| `tiles_info.py` | inspect offline cache (counts, coverage, point check) |
| `maps/<basemap>.mbtiles` | offline tiles, one cache per basemap (downloaded in-app) |
| `maps/landmarks.db` | POIs + `poi_selection` + `waypoints` (target) — the points pack |
| `config.ini` | port / udp / paths / last zoom / basemap / center |
| `state.ini` | station-local **home** only (read by msposd.c) |
| `run-map.sh` | alias for `map.sh preflight` |
| `fetch-leaflet.sh` | one-time Leaflet download |
| `pack/` | PyInstaller build for the standalone preflight binary (`build.sh` / `build.bat` / `mapserver.spec`) |

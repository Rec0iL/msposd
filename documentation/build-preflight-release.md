# Build a release package — preflight map

How to package the preflight map (`map.sh preflight` / `gs/mapserver.py`) into a **single
self-contained binary per OS** that end users run without installing Python. It starts a
local server and opens the map in the user's **default browser** (no Python/WebKit to
install). Writable data (`config.ini`, `maps/`, `state.ini`, `landmarks.db`) is created
next to the executable on first run — that folder is the pack you copy to the OSD station.

## Prerequisites
- Python **3.9+** and `pip` on the build machine (`pyinstaller` is installed automatically).
- Build **on each target OS** — a Windows `.exe` needs Windows, a macOS build needs a Mac.

## Build (current OS)
```bash
./gs/pack/build.sh      # Linux / macOS
gs\pack\build.bat       # Windows
```
Output: **`dist/msposd-preflight`** (`.exe` on Windows) — one file.

## Build all three OSes at once (CI)
The `.github/workflows/preflight-pack.yml` matrix builds Linux/macOS/Windows. Trigger it:
- **Manually:** GitHub → Actions → *preflight-pack* → *Run workflow*, or
- **By tag:** push a tag matching `preflight-v*`, e.g.
  ```bash
  git tag preflight-v1.0 && git push origin preflight-v1.0
  ```
Download the three binaries from the run's **Artifacts**.

## Run / verify
```bash
./dist/msposd-preflight               # starts server, opens the browser
./dist/msposd-preflight --port 9000   # custom port
./dist/msposd-preflight --no-browser  # server only
```
Ctrl+C (or closing the console) stops it.

## Ship it
Give users the single binary. On first run it creates its data folder alongside itself;
after they download an area, hand the resulting `maps/<basemap>.mbtiles` + `landmarks.db`
to the OSD/flight station.

## Notes
- **Unsigned binaries** trip macOS Gatekeeper / Windows SmartScreen — code-sign (and
  notarize on macOS) for public release; fine as-is for internal use.
- Some Windows AV engines false-positive on PyInstaller one-file builds.
- The dev workflow is unaffected: `./map.sh preflight` still runs `mapserver.py` + the
  WebKit `mapwin` window.

See [`../gs/pack/README.md`](../gs/pack/README.md) for how the packaging works internally.

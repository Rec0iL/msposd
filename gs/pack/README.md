# Standalone preflight map app (Option A)

Package the preflight map (`gs/mapserver.py` + `web/`) into a **single downloadable
binary per OS** that end users run without installing Python. It starts a local
server and opens the map in the user's **default system browser** — no WebKit or
Chromium is bundled.

This is an *extra* packaging path. The normal dev workflow is unchanged:
`./map.sh preflight` still runs `mapserver.py` + the WebKit `mapwin` window, and
`mapwin` is still used for the in-flight WebKit map. Only the packaged build uses
the system browser instead of `mapwin`.

## How it works
- `mapserver.py` is pure Python standard library, so it bundles cleanly.
- When frozen (PyInstaller), it auto-enables `--open-browser`: after the server
  binds `127.0.0.1`, it opens `…/viewer.html?mode=preflight` in the default browser.
- Read-only assets (`web/`) are bundled inside the binary; writable data
  (`config.ini`, `maps/`, `state.ini`, `landmarks.db`) is created **next to the
  executable** on first run. Point the OSD/flight station at the resulting
  `maps/<basemap>.mbtiles` + `landmarks.db` pack.

## Build locally
- **Linux / macOS:** `./gs/pack/build.sh`
- **Windows:** `gs\pack\build.bat`

Output: `dist/msposd-preflight` (`.exe` on Windows) — one self-contained file.

Requirements: Python 3.9+ and `pip` on the build machine. `pyinstaller` is
installed automatically by the scripts. You must build **on each target OS**
(a macOS app needs a Mac, etc.) — or use CI below.

## Build all three via CI
`.github/workflows/preflight-pack.yml` builds Linux/macOS/Windows binaries on a
matrix runner. Trigger it manually (workflow_dispatch) or by pushing a
`preflight-v*` tag; download the binaries from the run's artifacts.

## Run
```
./dist/msposd-preflight          # starts server, opens your browser
./dist/msposd-preflight --port 9000
./dist/msposd-preflight --no-browser   # server only
```
Close the console window or press Ctrl+C to stop.

## Notes
- **Unsigned binaries** trigger macOS Gatekeeper / Windows SmartScreen prompts.
  For public distribution, code-sign (and notarize on macOS). Fine for internal use.
- Some Windows antivirus engines flag PyInstaller one-file binaries (false positive).
- Telemetry still works: the packaged process listens for MSP over UDP exactly as
  the script does — the browser choice doesn't affect it.

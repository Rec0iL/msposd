# Getting the fancy OSD running

This fork draws the flight controller's OSD as widgets, on the ground station.
There are two ground stations it works with: **PixelPilot_rk** on an SBC, and
**Aviateur** on a desktop. The pieces are the same either way.

> **Work in progress.** Two of us fly it. It breaks, the theme format still
> changes, and the PixelPilot build below has never been compiled by us — see
> [What we have not verified](#what-we-have-not-verified).

## How the parts fit

Three programs and two files, and nothing between them but the files:

| | |
|---|---|
| **The flight controller** | decides *where*. You place elements in Betaflight or INAV as you always have; msposd recognises them on the MSP DisplayPort grid and redraws them as widgets in the same place. There is no separate layout to maintain. |
| **msposd** | reads the MSP stream, draws the widgets. Runs on the ground station, not on the camera. |
| **A settings front end** | PixelPilot's on-screen `gsmenu`, or Aviateur's OSD tab. Writes the theme file. |
| **The theme file** | one ini. msposd re-reads it whenever its mtime changes, so a change lands on the next frame with nothing restarted. |
| **The link stats file** | a small ini the ground station writes with its own signal numbers, for the one widget the flight controller knows nothing about. Written and found automatically. |

Nothing here is an IPC protocol. If a front end is not running, msposd carries
on with the file it has; if msposd is not running, the front end still writes.

## Before either path

**On the flight controller:** MSP DisplayPort OSD enabled, elements placed
where you want them. Betaflight and INAV are both recognised. Nothing to change
for this fork.

**On the air unit:** it has to *forward* MSP to the ground rather than render
it. That is your existing OpenIPC setup, not something this fork changes —
msposd's own `--output <host:port>` is the usual way it leaves the camera.

**On the ground station:** msposd binds the UDP port you give it and listens:

```
msposd --master 127.0.0.1:14551 --osd ...
```

A `--master` beginning with a digit is an address to listen on; anything else
is a serial port. **Only one process can bind a port.** If your ground station
already has something reading the MSP stream, either point that one somewhere
else or let msposd bind it — with the widget OSD you no longer need a second
program drawing the same glyphs.

## Path A — PixelPilot_rk on an SBC

On Rockchip, msposd does not draw to the screen at all. It renders into a POSIX
shared-memory region called `msposd`, and PixelPilot composites that over the
video as one more OSD widget. That is why the order matters: **PixelPilot
first** — it creates the region — then msposd, which waits for it to appear.

### 1. Build PixelPilot on the ground station

The Rockchip build needs the vendor stack: `rockchip_mpp`, `rga`, GStreamer
(`gstreamer-1.0`, `gstreamer-app-1.0`, `gstreamer-net-1.0`), `libdrm`,
`nlohmann_json`, `yaml-cpp`, `libgpiod`, `spdlog`, `fmt`, `libpng`.

```sh
cmake -S . -B build
cmake --build build
```

Build it on the SBC, or cross-build with the vendor SDK. A desktop cannot do
this without the Rockchip headers — which is exactly why this step is the one we
have not run.

### 2. Check the OSD config has the msposd surface

`config_osd.json` needs this widget, which is where msposd's output lands. It
is in the shipped config:

```json
{ "name": "msposd", "type": "ExternalSurfaceWidget", "shm_name": "msposd",
  "x": 0, "y": 0, "width": 0, "height": 0, "facts": [] }
```

If a `MspDisplayPortWidget` is also in there, it is a second renderer of the
same stream, and the two cannot share: only one process can bind the MSP port,
and the starved one paints a "NO MSP DATA / UDP PORT n" box over the video after
a few seconds. Feeding both — msposd binds the port and forwards with
`--output` — does not help either: you would then have two glyph layers on top
of each other whenever msposd draws glyphs.

**Taking it out does not cost you the classic OSD.** `Style: classic` in the
menu is msposd's own glyph renderer, not PixelPilot's widget: in classic mode
msposd draws the flight controller's OSD exactly as it always did, and in fancy
mode it hides that layer and draws widgets instead. Switching back and forth at
the field keeps working with PixelPilot's widget gone, as long as msposd is
running.

What it does cost is the OSD you get when msposd is *not* running. So keep the
original file rather than editing the widget out of it:

```sh
cp /etc/pixelpilot/config_osd.json /etc/pixelpilot/config_osd.pixelpilot.json
# then remove the MspDisplayPortWidget from config_osd.json
```

`OSD_PATH` in `/etc/default/pixelpilot` chooses between them, so going back to
PixelPilot's own OSD is one line and a restart — worth having if msposd ever
fails to start on a flying day.

### 3. Build msposd for the SBC

```sh
make rockchip
```

`build_rockchip.sh` cross-builds the same thing in a Debian arm64 chroot if you
would rather not build on the target. The Rockchip target needs `cairo`, `x11`,
`xext`, `libevent` — the X libraries are linked but unused on this path; the
output goes to shared memory.

### 4. Install the themes

```sh
install -d /etc/msposd/themes
cp -r themes/* /etc/msposd/themes/
cp themes/tactical/theme.ini /etc/msposd/theme.ini
```

`/etc/msposd/theme.ini` is the file both sides use, and `/etc/msposd/themes` is
where the theme picker looks. Both are the defaults, so nothing needs setting.

### 5. Start the two, in order

By hand, to try it:

```sh
pixelpilot --osd --osd-config /etc/pixelpilot/config_osd.json &
msposd --master 127.0.0.1:14551 --osd -r 50 --theme /etc/msposd/theme.ini
```

A packaged station already runs PixelPilot from `pixelpilot.service`, which
sources `/etc/default/pixelpilot`. Two things go in that file:

```sh
OSD_THEME=/etc/msposd/theme.ini
OSD_THEMES=/etc/msposd/themes
```

They have to be in **PixelPilot's** environment, not msposd's, because gsmenu.sh
inherits them from the process that runs it. `gsmenu.sh` also has to be on
`PATH` — PixelPilot calls it by bare name — so `/usr/local/bin/gsmenu.sh` or
similar.

msposd then wants its own unit, ordered after PixelPilot so the shared memory
exists (it waits for it anyway, and says so once a second):

```ini
[Unit]
After=pixelpilot.service
Requires=pixelpilot.service

[Service]
ExecStart=/usr/bin/msposd --master 127.0.0.1:14551 --osd -r 50 --theme /etc/msposd/theme.ini
Restart=always
```

### 6. Use it

**GS Settings → OSD.** The first row is **Theme** — the list of folders in the
themes directory. Picking one replaces `/etc/msposd/theme.ini` with a copy of
that theme and keeps the old file as `theme.ini.bak`. Below that are Appearance,
Elements, Compass, Map and Link stats.

Link statistics need no setting up: PixelPilot publishes its own numbers, in
both wfb-ng and APFPV modes, and msposd looks in the same place.

## Path B — Aviateur on a desktop

Aviateur draws the video in its own window; msposd draws a transparent
fullscreen X11 overlay on top of it. Two windows, no compositing agreement
between them.

### 1. Build both

Aviateur builds as upstream documents. On Fedora it needs a few extra packages
and three CMake flags — see `NOTES.md` in the workspace for the working line.

msposd, native:

```sh
CFLAGS="-fpermissive -std=gnu17" make -B DRV=$PWD OUTPUT=msposd native
```

(`-fpermissive` is for GCC 14+, which rejects what this codebase was written
against. Invoke `make` directly rather than `build.sh native`, which clones the
whole OpenIPC firmware repo for no reason on this target.)

The native build needs `CSFML-devel cairo-devel libX11-devel libXext-devel
libevent-devel`. Fonts are loaded from **the binary's own directory** — `font.png`
and `font_hd.png`, or `font_inav*.png` once an INAV flight controller is
detected. They are in the repo already; keep them beside the binary.

### 2. Point msposd at Aviateur's theme

Aviateur writes `~/.aviateur/osd-theme.ini` (or whatever `[osd] theme_path` in
its `config.ini` says). Start msposd with the same file:

```sh
./msposd --master 127.0.0.1:14550 --osd -r 50 --theme ~/.aviateur/osd-theme.ini
```

`-v` is worth knowing about: it does not only add logging, it makes the overlay
a normal managed window instead of a borderless fullscreen one. Useful while
setting up, wrong for flying.

### 3. Give the theme picker somewhere to look

Aviateur looks for theme folders in `$OSD_THEMES`, then beside its own theme
file. The simplest arrangement:

```sh
ln -s /path/to/msposd/themes ~/.aviateur/themes
```

Then **OSD tab → Theme** lists them. Picking one replaces the theme file and
keeps the old one as `.bak`, the same as on PixelPilot.

### 4. What works and what does not, on this path

Link statistics work: Aviateur publishes its wfb-ng numbers and msposd finds
them with nothing configured.

**Telemetry does not, yet.** Aviateur de-multiplexes the MAVLink stream off the
link but its handler is still an empty stub (`src/wifi/wfbng_link.cpp`, the
`MatchesChannelID(mavlink_channel_id_be8)` branch), so nothing arrives on
`127.0.0.1:14550` from Aviateur itself. Until that is wired up, msposd on this
path needs its MSP from somewhere else — an existing wfb-ng ground setup that
forwards it, or the SITL replay loop described in `NOTES.md`. The OSD tab, the
themes and the link widget are all usable meanwhile.

## Checking it works

| What you see | What it usually is |
|---|---|
| No widgets at all, raw glyphs instead | `[osd] mode` is `classic`, or msposd was started without `--osd` |
| No widgets and no glyphs | No MSP arriving — check what is bound to the port |
| Widgets, but one element stays a glyph | That element is not recognised yet. It is not a placement problem |
| Widgets in odd places | They follow the flight controller's layout by design. Move them there |
| Theme edits do nothing | The front end and msposd have different files. Compare the two paths |
| No link panel | The ground station is not writing stats, or `[link] enabled` is off. An empty `[link] source` is fine — it means the default place |
| Compass drawn twice | The flight controller's own heading bar is still on. Turn its element off on the FC, or the widget off in the theme |
| A "NO MSP DATA" box over the video | PixelPilot's own `MspDisplayPortWidget` is still configured on a port msposd has bound. See step 2 of Path A |
| Glyph OSD drawn twice | Both renderers have the stream. Only one of them should |

Run msposd with `-v` while setting up. It says what it recognised.

## What we have not verified

- **The PixelPilot arm64 build.** The changes for it — the tuned channel out of
  `rf_info`, the link-stats writer, the gsmenu OSD pages — were compiled and
  tested in isolation on a desktop and exercised in the LVGL simulator, but the
  full Rockchip binary has never been built here. It needs the vendor headers.
- **The two paths side by side.** Each has been used on its own.

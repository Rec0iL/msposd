# Widget rendering

`osd_paint.c` is a small software rasteriser for the 32bpp overlay buffer
(memory order `[B,G,R,A]`, see `ConvertI4ToRGBA` in `bmp/bitmap.c`). It has no
msposd or platform dependencies so it can be built and checked on its own.

## Visual check

```sh
gcc -I. -I bmp/lib -o preview osd/widgets/preview.c osd/widgets/osd_paint.c \
    osd/widgets/osd_text.c bmp/lib/schrift.c libpng/lodepng.c -lm
./preview        # writes widget-preview.png
```

Run it from the repo root: it loads `fonts/UbuntuMono-Regular.ttf` by relative path.

Look at the PNG *and* sample pixel values. An earlier gradient bug rendered a
plausible-looking coloured bar while the green and blue channels were silently
zero, so 20A of a 67A scale drew red - the opposite of the intended warning.
Appearance alone would not have caught it.

## Element recognition tests

```sh
gcc -Wall -o test_elements osd/elements/test_elements.c osd/elements/osd_elements.c
./test_elements
```

## Theme tests

```sh
gcc -Wall -D_GNU_SOURCE -o test_theme osd/widgets/test_theme.c osd/widgets/osd_theme.c \
    osd/elements/osd_elements.c -I osd/widgets
./test_theme     # run from the repo root, it loads themes/tactical/theme.ini
```

Covers the parts that matter operationally: a malformed theme must keep its
defaults rather than disable the OSD, values are clamped, and `mode = classic`
turns every widget off in one key.

## Panel placement tests

```sh
gcc -Wall -I. -I bmp/lib -I osd/widgets -D_GNU_SOURCE -o test_layout \
    osd/widgets/test_layout.c osd/elements/osd_elements.c osd/widgets/osd_paint.c \
    osd/widgets/osd_text.c osd/widgets/osd_theme.c osd/widgets/osd_widgets.c \
    osd/widgets/osd_map.c osd/widgets/osd_tiles.c osd/widgets/osd_heading.c osd/widgets/osd_link.c osd/widgets/osd_link_stats.c \
    bmp/lib/schrift.c \
    libpng/lodepng.c -lm -lpthread
./test_layout    # run from the repo root, it loads the theme font
```

A panel must not move when its reading gains a digit. Flight controllers
right-align their fields, so `col` slides one cell left at 99 -> 100 and again
at 999 -> 1000; the widget is placed against `anchor_col` - the symbol cell,
which does not move - and the layout signature is keyed on it too, so a digit
crossing no longer forces a full relayout.

## Two of the same kind on screen

Nothing stops a pilot placing two elements of one type - Betaflight draws core
and ESC temperature with the same symbol, and several fields have variants meant
to be shown side by side. The cache and the resolved layout are therefore keyed
on `(type, row, anchor_col)`, one slot per element *instance*, not per type.

Keyed on type alone the two shared a slot: each frame overwrote the other, so
both flickered and neither held its position. `anchor_col` is the edge the flight
controller keeps still, so the key survives a reading going from 99 to 100.

Callers ask `osd_widgets_placement()` where something ended up rather than
indexing by type. Where only one instance can mean anything - the map's corner
coordinates, the fix the home bearing is measured from - the first live one wins.

## The one widget the flight controller knows nothing about

wfb-ng and APFPV run on the ground station. msposd only ever sees MSP coming
*down* from the air unit, so there is no element on the glyph grid to anchor a
link-stats widget to, and no position the pilot chose on the flight controller
to inherit. Everything about placement therefore comes from the theme's
`[link]` block, in percent of the screen so a layout survives moving between a
720p and a 1080p ground station.

The numbers arrive through a small ini the ground station writes — the same seam
the theme itself uses, front end writes and we poll. That keeps msposd from
having to know anything about wfb-ng, APFPV, or whatever comes next.

Nothing has to be configured for the two to find each other. An empty `source`
means "wherever ground stations put it", and every writer resolves that the same
way msposd does: `$MSPOSD_LINK_STATS` if set, otherwise `/tmp/msposd-link.ini`.

Deliberately *not* `$XDG_RUNTIME_DIR`, tempting as it is: msposd is often started
from a service where it is unset while the ground station runs in a user session
where it is not, and the two would then quietly disagree about the path. A rule
that can resolve differently in two processes is worse than a plain one.

That rule now exists in three repositories with nothing at build time connecting
them, which is exactly the sort of thing that drifts — and if it drifts the
widget goes blank with no error anywhere. Aviateur's
`src/gui/test_osd_link_writer.cpp` links all three and asserts they agree, with
and without the override.

Three styles, because where it goes decides what shape it can be: `vertical`
stacks the aerials for a screen edge, `horizontal` puts them side by side for
the top or bottom, and `ultrawide` is a shallow strip that moves the channel,
the loss and the throughput onto its header line so the whole width below is
free for the link quality bar.

The per-aerial rows are optional in all three. Off leaves the headline - who is
reporting, which channel, the quality bar - and the panel drops to roughly a
third of its height. Six aerials is a lot of screen for something you study
after landing rather than in the air.

What appears is whatever the ground station reports, and the panel measures
itself to that: wfb-ng gives per-aerial dBm and SNR, packet counts and the tuned
channel, while APFPV's WiFi driver gives RSSI and a channel and nothing else, so
that panel has no SNR row and no quality bar.

Two things the parser has to survive, both of which a reader polling five times
a second will hit sooner or later:

- **A file caught mid-write.** It fills a local copy and commits only if the
  file held something usable, so a truncated read keeps the last good numbers
  instead of blanking the widget for a frame.
- **A writer that stops.** Past `hold_ms` the widget says so rather than showing
  a dead link's final reading for the rest of the flight.

## End-to-end check

`preview_live.c` builds a glyph grid exactly as a flight controller would send
it, runs the real recogniser over it, and draws the result with the real widget
renderer - so it exercises recognition, theme, text and painting together.

```sh
gcc -I. -I bmp/lib -D_GNU_SOURCE -o preview_live osd/widgets/preview_live.c \
    osd/elements/osd_elements.c osd/widgets/osd_paint.c osd/widgets/osd_text.c \
    osd/widgets/osd_theme.c osd/widgets/osd_widgets.c osd/widgets/osd_map.c \
    osd/widgets/osd_tiles.c bmp/lib/schrift.c libpng/lodepng.c -lm -lpthread
./preview_live   # writes widget-live.png
```

`osd_widgets.c` calls into the map, so the map and tile sources have to be on
the link line even for a preview that draws no map.

It composites the overlay over a stand-in video frame, which is what the
hardware does. Without that step the transparent cut-outs look like white
blocks rather than the scene showing through.

## Map tile tests

```sh
gcc -Wall -o test_map osd/widgets/test_map.c osd/widgets/osd_map.c -I osd/widgets -lm
./test_map
```

Expected tile numbers come from the canonical slippy-map formula, not from hand
arithmetic - an earlier version of this test asserted values computed with
interpolated trig and was wrong by two tiles while the code was correct.

Watch the tile URL ordering: OpenStreetMap serves `{z}/{x}/{y}`, Esri serves
`{z}/{row}/{col}` i.e. y before x. Swapping them returns valid-looking imagery
of the wrong place, which is not obvious on screen.

## Map tiles

`osd_tiles.c` keeps a memory LRU plus a disk cache and fetches misses on a
background thread. Every lookup returns immediately - a missing tile draws a gap
for a moment, whereas blocking the render loop on the network would make the OSD
unusable.

```sh
gcc -I. -I osd/widgets -D_GNU_SOURCE -DOSD_MAP_HTTP -DOSD_MAP_JPEG -o tiletest \
    osd/widgets/tiletest.c osd/widgets/osd_tiles.c osd/widgets/osd_map.c \
    osd/widgets/osd_paint.c libpng/lodepng.c -lm -lcurl -lpthread -ljpeg
./tiletest roads     # writes map-roads.png
./tiletest sat       # writes map-sat.png
```

Two things to know:

- **Tile servers do not all serve PNG.** OpenStreetMap does; Esri's World
  Imagery serves JPEG. The format is decided by sniffing magic bytes, not by the
  URL or the Content-Type header. Without JPEG support satellite tiles download
  fine and then fail to decode, which looks like a network problem but is not.
- **OpenStreetMap's tile usage policy** requires an identifying User-Agent and
  forbids bulk downloading, hence the UA string and the aggressive caching.
  Esri's World Imagery has its own terms. Check both before relying on either.

## Where the theme comes from

`--theme <path>` overrides the built-in `themes/tactical/theme.ini`, which is
relative to msposd's working directory. A camera-side install wants the relative
default; a ground station wants an absolute path, because something else owns the
file. `osd_theme_reload_if_changed` re-reads it whenever the mtime moves, so a
front-end can retune widgets while the video is running - which is exactly what
Aviateur's OSD tab does, writing `~/.aviateur/osd-theme.ini`.

Keep the two in step: the keys, ranges and defaults are duplicated in
`aviateur/src/gui/osd_theme_model.cpp`. msposd clamps whatever it reads, so a
drift there degrades the look rather than breaking anything, but it is still
drift.

## Speed-driven map view

`osd_map_view_update` picks the zoom from ground speed and pushes the view
centre ahead along the ground track, so what is in front of the aircraft gets
the screen. It is pure maths over a small state struct, so `test_map` covers it
without tiles or a network.

Ground *course*, not heading: in wind an aeroplane's nose and its track differ
by enough to point the map at the wrong piece of ground.

Two things are deliberate and easy to get wrong if you touch it:

- **Zoom changes are expensive.** Each one discards every tile on screen and
  fetches a new set, so a new zoom has to be what the speed has been asking for
  continuously (`zoom_settle_ms`) before it is taken. Without that, ordinary
  speed variation flaps the map between two zoom levels and thrashes the cache.
- **The lead is eased as a vector, not as a bearing.** Easing the bearing sends
  the view the long way round through south whenever the track crosses north.
- **The track guard uses reported speed, not smoothed speed.** If the receiver
  says the aircraft is barely moving *now*, its course is noise *now*, whatever
  it was doing three seconds ago. Smoothing that guard lets a sudden stop spin
  the map.

`orientation = track` turns the map so the ground track is up. The renderer then
walks *destination* pixels and asks `osd_map_screen_to_world` where each samples
from; walking source pixels instead tears the image into gaps as the rotation
stretches them apart. A turned viewport reaches into the corners of its bounding
square, so the tiles fetched cover the diagonal, not the rectangle. A compass
needle is drawn whenever it is on - a turning map is easy to fly to and
impossible to orient by without one.

Note the aircraft marker is drawn at `heading - track`, so in track-up what is
left on screen is the crab angle.

## When lat/lon are not a map

The map's rectangle is whatever the two coordinate elements span, so two
readouts on one row - or stacked one above the other - describe no usable
rectangle. `map_rect` rejects both and they fall back to ordinary value panels.
Getting this wrong is worse than it sounds: the caller used to mark the
coordinates as handled before `draw_map` discovered the rectangle was unusable,
so they vanished off the screen entirely.

```sh
gcc -I. -I bmp/lib -I osd/widgets -D_GNU_SOURCE -DOSD_MAP_HTTP -DOSD_MAP_JPEG \
    -o preview_map osd/widgets/preview_map.c osd/elements/osd_elements.c \
    osd/widgets/osd_paint.c osd/widgets/osd_text.c osd/widgets/osd_theme.c \
    osd/widgets/osd_widgets.c osd/widgets/osd_map.c osd/widgets/osd_tiles.c \
    bmp/lib/schrift.c libpng/lodepng.c -lm -lpthread -lcurl -ljpeg
./preview_map    # writes map-speed.png: the same track at 0, 12 and 28 m/s
```

## Screenshots

`preview_shots.c` renders whole scenes - a full flight-controller layout, three
flight situations - over a still frame standing in for live video. Same link
line as `preview_map`, with `preview_shots.c` in place of `preview_map.c`:

```sh
./preview_shots ../design/backdrop-drone.png ../design/shots
```

It is worth running after any change to the widget layer: three realistic
screens at once catch things the single-element previews do not. The two bugs
below were both found this way, not by the unit tests.

The backdrop is any PNG the size of the OSD - swap in a real frame grabbed from
your own video and the same command re-renders against it. `design/backdrop-drone.png`
is a CC0 drone photograph; `design/backdrop-drone.txt` records where it came from.

Point the map at ground that matches the backdrop (`LAT_TXT` / `LON_TXT` in
`preview_shots.c`). A minimap showing somewhere else entirely is the first thing
that gives a screenshot away.

Two placement rules that only show up on a full screen:

- **Clamp to the viewport before resolving collisions.** An element in one of
  the last columns has its panel pulled left to fit; doing that after the
  overlap test has passed drops it straight onto the panel beside it.
- **OSD messages have no numeric value.** They have to be exempted from the
  `value_valid` filter along with the flight time and mode, or every failsafe is
  recognised and then silently dropped.
- **Map labels go down before map markers.** The scale note sits in the map's
  bottom-left corner, which is where an off-map home arrow gets pinned once you
  are south-west of the launch point. Drawing the note last hides the way back
  behind a zoom readout.

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

## End-to-end check

`preview_live.c` builds a glyph grid exactly as a flight controller would send
it, runs the real recogniser over it, and draws the result with the real widget
renderer - so it exercises recognition, theme, text and painting together.

```sh
gcc -I. -I bmp/lib -D_GNU_SOURCE -o preview_live osd/widgets/preview_live.c \
    osd/elements/osd_elements.c osd/widgets/osd_paint.c osd/widgets/osd_text.c \
    osd/widgets/osd_theme.c osd/widgets/osd_widgets.c bmp/lib/schrift.c \
    libpng/lodepng.c -lm
./preview_live   # writes widget-live.png
```

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

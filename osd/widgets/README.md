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

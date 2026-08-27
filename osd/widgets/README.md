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

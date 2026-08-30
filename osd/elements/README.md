# Element recognition

MSP DisplayPort carries no semantics — only glyph codes at grid cells. Everything
here is about recovering meaning from that grid, so the ground station can draw
widgets instead of characters and still put them where the pilot chose on the
flight controller.

Three ways an element gets found, in decreasing order of confidence:

1. **A symbol glyph.** Both firmwares mark most fields with a fixed code — a volt
   symbol after the number, a latitude symbol before it. Unambiguous, and the
   bulk of the table.
2. **A run of graphic glyphs.** The compass bar is built from a six-glyph
   alphabet nothing else uses, so a run of them can only be the bar.
3. **The literal text around the number.** Betaflight draws a couple of dozen
   fields as plain characters. Where it wraps the number in something fixed — a
   trailing `G`, `WH`, an `RF:` prefix — that literal anchors the field as well
   as a symbol would.

## What cannot be reached, and why

Some Betaflight elements are a bare number and nothing else: ESC RPM is `"%d"`,
the RC channel readout `"%5d"`, the ETA `"%02u:%02u"`, the PID/rate profile
`"%d-%d"`. Nothing distinguishes them from each other, or from a number belonging
to something else on the screen.

These are left alone on purpose. Matching on shape would turn arbitrary digits
into widgets, and a widget confidently showing the wrong quantity is worse than
one that is missing — the pilot cannot tell the difference in the air. Reaching
them needs the layout itself, which Betaflight will hand over through
`MSP_OSD_CONFIG`. That is a different mechanism, not a better pattern match.

## Fields that borrow another element's symbol

Three Betaflight fields are not what their symbol says, and each was being
reported as the element whose symbol it carries — with a plausible number, which
is the dangerous kind of wrong:

| On screen | Symbol | Was read as | Actually |
|---|---|---|---|
| `-94` | `SYM_RSSI` | 0% signal, or worse a healthy one | RSSI in dBm |
| `25MW` | `SYM_RSSI` | 25% signal | uplink transmit power |
| `180<mAh>/<km>` | `SYM_MAH` | 180 mAh consumed | 180 mAh per km |

RSSI percent, RSSI in dBm and link SNR all carry `SYM_RSSI`, so only magnitude
separates them: at or below −20 it is dBm, otherwise negative it is SNR, and
anything else is a percentage. The overlap — a small positive SNR against a low
RSSI percentage — is genuinely undecidable, and percentage wins because the other
mistake is the harmful one.

## The two firmwares disagree about almost everything

Both are covered from their own source — Betaflight's `src/main/osd/osd_elements.c`
and INAV's `src/main/io/osd.c`, with the symbol codes from each firmware's
`osd_symbols.h`. They arrange the same readings very differently:

| | Betaflight | INAV |
|---|---|---|
| unit | separate glyph at the end of the field | baked into the symbol (`SYM_ALT_M` vs `SYM_ALT_FT`) |
| speed | `SYM_SPEED` then digits then unit | no symbol at all — `"%3d%c"`, the unit glyph *is* the anchor |
| airspeed | an `a` after the symbol | `SYM_AIR` in front of the field |
| dBm | shares `SYM_RSSI`, told apart by magnitude | own `SYM_DBM` closing the field |
| direction arrows | `0x60`–`0x6F` | `0x13C`–`0x14B` |
| numerical heading | an arrow followed by three digits | `SYM_HEADING` then digits then `SYM_DEGREES` |

The last row is why the arrow pass tests for digits: on Betaflight a lone arrow
is the home marker and an arrow with digits is the heading, while INAV gives
heading its own symbol so the digit check simply never fires there.

## Firmware differences that bite

- **`SYM_ALTITUDE` leads in Betaflight.** `osdFormatAltitudeString` passes it to
  `osdPrintFloat` as the leading symbol with the unit trailing. Treated as a
  trailing symbol it never matched anything, so Betaflight altitude was not
  recognised at all.
- **Units are separate glyphs in Betaflight, baked into the symbol in INAV.**
  INAV has `SYM_ALT_M` and `SYM_ALT_FT`; Betaflight has one altitude symbol and
  puts `SYM_M` or `SYM_FT` at the end of the field. So the unit is read off the
  screen — the firmware has already converted, and relabelling feet as metres
  would be wrong by a factor of three.
- **Letters sit on both sides of the symbol.** Temperature is `C<sym>42<unit>`,
  ESC temperature `E<sym>68<unit>`, airspeed `<sym>a92<unit>`.
- **Vario prints an unsigned number.** The sign is in the arrow glyph and nowhere
  else, so a dive and a climb of the same rate arrive identical.
- **The same sixteen arrow glyphs serve two elements.** A lone arrow is the home
  direction; an arrow followed by three digits is the numerical heading.
- **INAV's ground speed and wind speed end identically.** Both are digits
  followed by the same unit glyph; only what sits in front separates them. Rows
  anchored on nothing but a unit glyph therefore demand a clear run of blanks to
  their left, checked across the field's padding rather than one cell over — a
  right-aligned `"%3d"` holding 4 leaves a blank where the marker would be.
- **A marker is not adjacent to its digits.** `"%c%3d%c"` holding 92 puts a blank
  between the symbol and the number, so both the row lookup and the absorb have
  to look across the padding.

## Tests

```sh
gcc -Wall -I. -I bmp/lib -D_GNU_SOURCE -o test_elements \
    osd/elements/test_elements.c osd/elements/osd_elements.c -lm
./test_elements
```

Every Betaflight layout in there is the exact byte sequence the firmware writes,
taken from the `tfp_sprintf` and `osdPrintFloat` calls in
`src/main/osd/osd_elements.c`, so a format change upstream fails here rather than
silently on the video.

One thing to watch when adding cases: the grid is `uint16_t`, so
`memset(grid, 0x20, ...)` fills it with glyph `0x2020`, not blanks. Use
`clear_grid()`. Every test in this file used to clear the grid the wrong way,
which meant nothing in it had a genuinely blank cell — and anything that looks at
the cells *around* a field was untestable.

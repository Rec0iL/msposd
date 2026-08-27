// Renders the widget panels straight through osd_paint and writes a PNG, so the
// visual design can be iterated without running the whole OSD.
#include "osd/widgets/osd_paint.h"
#include "osd/widgets/osd_text.h"
#include "libpng/lodepng.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define W 1020
#define H 420

static osd_surface_t surf;
static uint8_t *buf;
static osd_font_t *font;

#define C_LABEL    OSD_ARGB(0xFF,0x4F,0xA8,0xC4)
#define C_LABEL_W  OSD_ARGB(0xFF,0xB9,0x8A,0x2A)
#define C_PEAK     OSD_ARGB(0xFF,0x8F,0xD8,0xEA)
#define C_DIM      OSD_ARGB(0xFF,0x2F,0x7F,0x96)

// Panel width follows the measured value text: tab must contain it with padding.
#define PAD_R 18.0f
#define TAB_MIN_X 128.0f

static float panel_width_for(const char *value, float vsize, float min_w) {
    osd_text_metrics_t m;
    if (!font || !osd_text_measure(font, vsize, value, &m))
        return min_w;
    float need = TAB_MIN_X + (float)m.width + PAD_R + 12.0f;
    return need > min_w ? need : min_w;
}

// theme palette
#define C_ACCENT   OSD_ARGB(0xFF,0x00,0xE5,0xFF)
#define C_WARN     OSD_ARGB(0xFF,0xFF,0xB3,0x00)
#define C_PANEL    OSD_ARGB(0xD6,0x0A,0x1A,0x26)
#define C_PANEL_W  OSD_ARGB(0xD6,0x1C,0x14,0x05)
#define C_EDGE     OSD_ARGB(0xFF,0x0E,0x3D,0x52)
#define C_EDGE_W   OSD_ARGB(0xFF,0x5C,0x42,0x06)
#define C_TRACK    OSD_ARGB(0xCC,0x06,0x22,0x2E)
#define C_TRACK_W  OSD_ARGB(0xCC,0x1A,0x13,0x05)

// panel outline: tab step top-left, square top-right (value sits there),
// chamfer bottom-right. Sized from the measured value text in the real widget.
static void panel_path(osd_pointf_t *p, float x, float y, float w, float h, float tab) {
    p[0] = (osd_pointf_t){x,           y + tab};
    p[1] = (osd_pointf_t){x + 100,     y + tab};
    p[2] = (osd_pointf_t){x + 128,     y};
    p[3] = (osd_pointf_t){x + w,       y};
    p[4] = (osd_pointf_t){x + w,       y + h - 20};
    p[5] = (osd_pointf_t){x + w - 20,  y + h};
    p[6] = (osd_pointf_t){x,           y + h};
}

static void accent_corners(float x, float y, float w, float h, osd_color_t c) {
    osd_draw_line(&surf, x, y + h - 18, x, y + h, 2.5f, c);
    osd_draw_line(&surf, x, y + h, x + 26, y + h, 2.5f, c);
    osd_draw_line(&surf, x + w, y + h - 32, x + w, y + h - 20, 2.5f, c);
    osd_draw_line(&surf, x + w, y + h - 20, x + w - 20, y + h, 2.5f, c);
}

int main(void) {
    font = osd_font_load("fonts/UbuntuMono-Regular.ttf");
    if (!font) { printf("font load failed\n"); return 1; }
    // demonstrate that panel width follows the value string
    printf("panel width for \"4.14V\"   = %.0f\n", panel_width_for("4.14V", 32, 340));
    printf("panel width for \"20A/67A\" = %.0f\n", panel_width_for("20A/67A", 32, 340));
    printf("panel width for \"92\"      = %.0f\n", panel_width_for("92", 32, 340));
    buf = calloc((size_t)W * H * 4, 1);
    osd_surface_init(&surf, buf, W, H, W * 4);

    // dark "video" backdrop so translucency is visible
    osd_fill_rect(&surf, 0, 0, W, H, OSD_ARGB(0xFF,0x08,0x0B,0x10));
    for (int i = 0; i < 5; i++)
        osd_fill_rect(&surf, 0, 60 + i*80, W, 1, OSD_ARGB(0x30,0x1D,0x4F,0x63));

    osd_pointf_t p[7];
    const float PW = 340, PH = 150, TAB = 48;

    // --- 1. nominal voltage: full hatched bar
    panel_path(p, 40, 60, PW, PH, TAB);
    osd_fill_poly(&surf, p, 7, C_PANEL);
    osd_stroke_poly(&surf, p, 7, 1.5f, C_EDGE);
    accent_corners(40, 60, PW, PH, C_ACCENT);
    osd_fill_rect(&surf, 60, 154, 300, 28, C_TRACK);
    osd_fill_rect_hatched(&surf, 60, 154, 300, 28, C_ACCENT,
                          OSD_ARGB(0x8C,0x0B,0x1A,0x24), 9.0f, 0.64f, -0.45f);
    osd_text_draw_right(&surf, font, 32, 40+PW-18, 60+36, "4.14V", C_ACCENT);
    osd_text_draw_tracked(&surf, font, 13, 60, 60+80, "CELL VOLTAGE", 2.5f, C_LABEL);

    // --- 2. warn voltage: partial bar
    panel_path(p, 440, 60, PW, PH, TAB);
    osd_fill_poly(&surf, p, 7, C_PANEL_W);
    osd_stroke_poly(&surf, p, 7, 1.5f, C_EDGE_W);
    accent_corners(440, 60, PW, PH, C_WARN);
    osd_fill_rect(&surf, 460, 154, 300, 28, C_TRACK_W);
    osd_fill_rect_hatched(&surf, 460, 154, 141, 28, C_WARN,
                          OSD_ARGB(0x8C,0x23,0x17,0x03), 9.0f, 0.64f, -0.45f);
    osd_text_draw_right(&surf, font, 32, 440+PW-18, 60+36, "3.59V", C_WARN);
    osd_text_draw_tracked(&surf, font, 13, 460, 60+80, "CELL VOLTAGE", 2.5f, C_LABEL_W);

    // --- 3. current: gradient hatch, auto-scaled
    static const osd_color_t stops[4] = {
        OSD_ARGB(0xFF,0x00,0xFF,0x9C), OSD_ARGB(0xFF,0xC8,0xF0,0x00),
        OSD_ARGB(0xFF,0xFF,0xB3,0x00), OSD_ARGB(0xFF,0xFF,0x3B,0x30)};
    static const float offs[4] = {0.0f, 0.45f, 0.72f, 1.0f};
    panel_path(p, 40, 240, PW, PH, TAB);
    osd_fill_poly(&surf, p, 7, C_PANEL);
    osd_stroke_poly(&surf, p, 7, 1.5f, C_EDGE);
    accent_corners(40, 240, PW, PH, C_ACCENT);
    osd_fill_rect(&surf, 60, 334, 300, 28, C_TRACK);
    osd_fill_rect_hatched_gradient(&surf, 60, 334, 90, 28, 300, stops, offs, 4,
                                   OSD_ARGB(0x8C,0x04,0x14,0x0A), 9.0f, 0.64f, -0.45f);
    osd_draw_line(&surf, 360, 330, 360, 366, 2.0f, C_PEAK);
    {   // "20A" large + "/67A" smaller, right-aligned as one unit
        osd_text_metrics_t big, small;
        osd_text_measure(font, 32, "20A", &big);
        osd_text_measure(font, 22, "/67A", &small);
        int right = 40 + (int)PW - 18;
        int x0 = right - big.width - small.width;
        osd_text_draw(&surf, font, 32, x0, 240+36, "20A", C_ACCENT);
        osd_text_draw(&surf, font, 22, x0 + big.width, 240+36, "/67A", C_PEAK);
    }
    osd_text_draw_tracked(&surf, font, 13, 60, 240+80, "CURRENT DRAW", 2.5f, C_LABEL);
    {   // above the peak marker, clear of the bottom-right chamfer
        osd_text_metrics_t m;
        osd_text_measure_tracked(font, 10, "PEAK", 1.0f, &m);
        osd_text_draw_tracked(&surf, font, 10, 360 - m.width / 2, 240 + 86, "PEAK", 1.0f, C_PEAK);
    }

    // --- 4. bracket variant
    float bx = 440, by = 240;
    osd_fill_rect(&surf, (int)bx+12, (int)by+10, 316, 130, OSD_ARGB(0x8C,0x08,0x16,0x1F));
    osd_draw_line(&surf, bx, by, bx, by+150, 2.0f, C_ACCENT);
    osd_draw_line(&surf, bx, by, bx+36, by, 2.0f, C_ACCENT);
    osd_draw_line(&surf, bx, by+150, bx+36, by+150, 2.0f, C_ACCENT);
    osd_draw_line(&surf, bx+340, by, bx+340, by+150, 2.0f, C_ACCENT);
    osd_draw_line(&surf, bx+340, by, bx+304, by, 2.0f, C_ACCENT);
    osd_draw_line(&surf, bx+340, by+150, bx+304, by+150, 2.0f, C_ACCENT);
    for (int i = 0; i < 7; i++)
        osd_draw_line(&surf, bx+34+i*38, by+106, bx+34+i*38+(i%2?20:32), by+106, 1.5f,
                      OSD_ARGB(0x80,0x00,0xE5,0xFF));
    {   // centred value + label
        osd_text_metrics_t m;
        osd_text_measure(font, 34, "530", &m);
        osd_text_draw(&surf, font, 34, (int)bx+170-m.width/2, (int)by+60, "530", C_ACCENT);
        osd_text_measure_tracked(font, 12, "ALT", 4.0f, &m);
        osd_text_draw_tracked(&surf, font, 12, (int)bx+170-m.width/2, (int)by+82, "ALT", 4.0f, C_LABEL);
    }

    // BGRA -> RGBA for lodepng
    uint8_t *rgba = malloc((size_t)W * H * 4);
    for (size_t i = 0; i < (size_t)W * H; i++) {
        rgba[i*4+0] = buf[i*4+2];
        rgba[i*4+1] = buf[i*4+1];
        rgba[i*4+2] = buf[i*4+0];
        rgba[i*4+3] = buf[i*4+3];
    }
    unsigned err = lodepng_encode32_file("widget-preview.png", rgba, W, H);
    printf(err ? "png error %u\n" : "wrote preview\n", err);
    return 0;
}

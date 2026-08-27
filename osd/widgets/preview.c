// Renders the widget panels straight through osd_paint and writes a PNG, so the
// visual design can be iterated without running the whole OSD.
#include "osd/widgets/osd_paint.h"
#include "libpng/lodepng.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define W 1020
#define H 420

static osd_surface_t surf;
static uint8_t *buf;

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

    // --- 2. warn voltage: partial bar
    panel_path(p, 440, 60, PW, PH, TAB);
    osd_fill_poly(&surf, p, 7, C_PANEL_W);
    osd_stroke_poly(&surf, p, 7, 1.5f, C_EDGE_W);
    accent_corners(440, 60, PW, PH, C_WARN);
    osd_fill_rect(&surf, 460, 154, 300, 28, C_TRACK_W);
    osd_fill_rect_hatched(&surf, 460, 154, 141, 28, C_WARN,
                          OSD_ARGB(0x8C,0x23,0x17,0x03), 9.0f, 0.64f, -0.45f);

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
    osd_draw_line(&surf, 360, 330, 360, 366, 2.0f, OSD_ARGB(0xFF,0x8F,0xD8,0xEA));

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

// Software rasteriser for OSD widgets.
//
// Draws into msposd's 32bpp overlay buffer. Memory layout is [B,G,R,A] per
// pixel (see ConvertI4ToRGBA in bmp/bitmap.c), i.e. 0xAARRGGBB read as a
// little-endian uint32_t.
//
// Free of platform and msposd dependencies so it can be unit tested on its own
// and reused wherever msposd runs, PixelPilot included.
#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
	uint8_t *pixels; // BGRA, 4 bytes per pixel
	int width;
	int height;
	int stride; // bytes per row
	// Clip rectangle; all drawing is confined to it.
	int clip_x, clip_y, clip_w, clip_h;
} osd_surface_t;

/// 0xAARRGGBB
typedef uint32_t osd_color_t;

#define OSD_ARGB(a, r, g, b)                                                                       \
	(((osd_color_t)(a) << 24) | ((osd_color_t)(r) << 16) | ((osd_color_t)(g) << 8) |               \
		(osd_color_t)(b))
#define OSD_A(c) (((c) >> 24) & 0xFF)
#define OSD_R(c) (((c) >> 16) & 0xFF)
#define OSD_G(c) (((c) >> 8) & 0xFF)
#define OSD_B(c) ((c) & 0xFF)

typedef struct {
	float x, y;
} osd_pointf_t;

void osd_surface_init(osd_surface_t *s, uint8_t *pixels, int width, int height, int stride);
void osd_surface_set_clip(osd_surface_t *s, int x, int y, int w, int h);

/// Source-over blend of one pixel. `coverage` is 0..1 for antialiasing.
void osd_blend_px(osd_surface_t *s, int x, int y, osd_color_t c, float coverage);

void osd_fill_rect(osd_surface_t *s, int x, int y, int w, int h, osd_color_t c);

/// Clears a rectangle to fully transparent. Used to blank the FC glyphs a
/// widget covers, so they do not show through the backdrop.
void osd_clear_rect(osd_surface_t *s, int x, int y, int w, int h);

/// Fills a convex polygon with 4x vertical supersampling on the edges, which is
/// what makes the chamfered corners look clean rather than stepped.
void osd_fill_poly(osd_surface_t *s, const osd_pointf_t *pts, int count, osd_color_t c);

/// Strokes a closed polygon outline of the given width.
void osd_stroke_poly(osd_surface_t *s, const osd_pointf_t *pts, int count, float width, osd_color_t c);

void osd_draw_line(osd_surface_t *s, float x0, float y0, float x1, float y1, float width, osd_color_t c);

/// Horizontal gradient fill, used for the current-draw bar.
void osd_fill_rect_gradient(osd_surface_t *s, int x, int y, int w, int h,
	const osd_color_t *stops, const float *offsets, int stop_count);

/// Diagonal hatching, clipped to a rect. `slant` is the x-shift per unit of y;
/// negative leans the stripes to the right like the mockup.
void osd_fill_rect_hatched(osd_surface_t *s, int x, int y, int w, int h,
	osd_color_t stripe, osd_color_t gap, float period, float duty, float slant);

/// Same, but the stripe colour is sampled from a gradient across [x, x+total_w).
void osd_fill_rect_hatched_gradient(osd_surface_t *s, int x, int y, int w, int h, int total_w,
	const osd_color_t *stops, const float *offsets, int stop_count,
	osd_color_t gap, float period, float duty, float slant);

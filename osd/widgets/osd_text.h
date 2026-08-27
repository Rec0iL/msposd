// TrueType text for widgets, on top of libschrift.
//
// Widgets size themselves from measured text, so measurement and rendering must
// agree exactly - a panel laid out with guessed extents clips its own value.
#pragma once

#include "osd_paint.h"

typedef struct osd_font osd_font_t;

/// Loads a TTF. Returns NULL on failure; callers fall back to glyph text.
osd_font_t *osd_font_load(const char *path);
void osd_font_free(osd_font_t *f);

typedef struct {
	int width;    // advance width of the whole string, in pixels
	int ascent;   // pixels above the baseline
	int descent;  // pixels below the baseline (positive)
	int height;   // ascent + descent
} osd_text_metrics_t;

/// Measures `text` at `size` px. Uses the same advances and kerning as
/// osd_text_draw(), so layout computed from this is exact.
bool osd_text_measure(osd_font_t *f, float size, const char *text, osd_text_metrics_t *out);

/// Draws `text` with its left edge at `x` and its baseline at `y_baseline`.
/// Returns the x position just past the last glyph.
int osd_text_draw(osd_surface_t *s, osd_font_t *f, float size, int x, int y_baseline,
	const char *text, osd_color_t color);

/// Convenience: draw right-aligned so the text *ends* at `x_right`.
int osd_text_draw_right(osd_surface_t *s, osd_font_t *f, float size, int x_right, int y_baseline,
	const char *text, osd_color_t color);

/// Letter-spaced small caps style label, as used for "CELL VOLTAGE".
int osd_text_draw_tracked(osd_surface_t *s, osd_font_t *f, float size, int x, int y_baseline,
	const char *text, float tracking, osd_color_t color);

bool osd_text_measure_tracked(osd_font_t *f, float size, const char *text, float tracking,
	osd_text_metrics_t *out);

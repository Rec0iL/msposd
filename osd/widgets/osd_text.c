#include "osd_text.h"

#include "../../bmp/lib/schrift.h"

#include <stdlib.h>
#include <string.h>

struct osd_font {
	SFT_Font *font;
};

osd_font_t *osd_font_load(const char *path) {
	if (!path)
		return NULL;
	SFT_Font *f = sft_loadfile(path);
	if (!f)
		return NULL;
	osd_font_t *out = (osd_font_t *)calloc(1, sizeof(osd_font_t));
	if (!out) {
		sft_freefont(f);
		return NULL;
	}
	out->font = f;
	return out;
}

void osd_font_free(osd_font_t *f) {
	if (!f)
		return;
	if (f->font)
		sft_freefont(f->font);
	free(f);
}

static void make_sft(SFT *sft, osd_font_t *f, float size) {
	memset(sft, 0, sizeof(*sft));
	sft->font = f->font;
	sft->xScale = size;
	sft->yScale = size;
	sft->xOffset = 0.0;
	sft->yOffset = 0.0;
	// Matches bmp/text.c: y grows downward, as our surface does.
	sft->flags = SFT_DOWNWARD_Y;
}

// Walks the string accumulating advances and kerning. Rendering uses the same
// walk, so measured and drawn extents cannot drift apart.
// Outline state, set once per frame by the widget layer. Global rather than a
// parameter because it is a look applied to *all* text - threading it through
// every call site would be noise at each one.
static bool g_outline_on = false;
static osd_color_t g_outline_color = 0;
static int g_outline_px = 1;

void osd_text_set_outline(bool on, osd_color_t color, int width_px) {
	g_outline_on = on;
	g_outline_color = color;
	g_outline_px = width_px < 1 ? 1 : (width_px > 3 ? 3 : width_px);
}

void osd_text_get_outline(bool *on, osd_color_t *color, int *width_px) {
	if (on)
		*on = g_outline_on;
	if (color)
		*color = g_outline_color;
	if (width_px)
		*width_px = g_outline_px;
}

static bool walk(osd_font_t *f, float size, const char *text, float tracking,
	osd_surface_t *s, int pen_x, int baseline, osd_color_t color, int *out_width,
	int *out_ascent, int *out_descent) {
	if (!f || !f->font || !text)
		return false;

	SFT sft;
	make_sft(&sft, f, size);

	SFT_LMetrics lm;
	if (sft_lmetrics(&sft, &lm) < 0)
		return false;

	float x = (float)pen_x;
	int max_up = 0, max_down = 0;
	SFT_Glyph prev = 0;
	bool have_prev = false;

	for (const unsigned char *p = (const unsigned char *)text; *p; p++) {
		SFT_Glyph gid;
		if (sft_lookup(&sft, *p, &gid) < 0)
			continue;

		SFT_GMetrics gm;
		if (sft_gmetrics(&sft, gid, &gm) < 0)
			continue;

		if (have_prev) {
			SFT_Kerning k;
			if (sft_kerning(&sft, prev, gid, &k) >= 0)
				x += (float)k.xShift;
		}

		// yOffset is the top of the glyph relative to the baseline, negative up.
		int up = -gm.yOffset;
		int down = gm.minHeight + gm.yOffset;
		if (up > max_up)
			max_up = up;
		if (down > max_down)
			max_down = down;

		if (s && gm.minWidth > 0 && gm.minHeight > 0) {
			SFT_Image img;
			img.width = gm.minWidth;
			img.height = gm.minHeight;
			img.pixels = calloc((size_t)img.width * img.height, 1);
			if (img.pixels) {
				if (sft_render(&sft, gid, img) >= 0) {
					const uint8_t *cov = (const uint8_t *)img.pixels;
					int gx = (int)(x + gm.leftSideBearing + 0.5f);
					int gy = baseline + gm.yOffset;
					// Outline first, then the glyph over it. Eight offsets rather
					// than four: at four, diagonal strokes show gaps where the
					// outline should close around them.
					if (g_outline_on) {
						const int d = g_outline_px;
						const int ox[8] = {-d, 0, d, -d, d, -d, 0, d};
						const int oy[8] = {-d, -d, -d, 0, 0, d, d, d};
						for (int k = 0; k < 8; k++) {
							for (int row = 0; row < img.height; row++) {
								for (int colx = 0; colx < img.width; colx++) {
									uint8_t a = cov[row * img.width + colx];
									if (a)
										osd_blend_px(s, gx + colx + ox[k], gy + row + oy[k],
											g_outline_color, (float)a / 255.0f);
								}
							}
						}
					}
					for (int row = 0; row < img.height; row++) {
						for (int colx = 0; colx < img.width; colx++) {
							uint8_t a = cov[row * img.width + colx];
							if (a)
								osd_blend_px(s, gx + colx, gy + row, color, (float)a / 255.0f);
						}
					}
				}
				free(img.pixels);
			}
		}

		x += (float)gm.advanceWidth + tracking;
		prev = gid;
		have_prev = true;
	}

	// Trailing tracking is spacing after the last glyph, not part of the extent.
	if (have_prev && tracking != 0.0f)
		x -= tracking;

	if (out_width)
		*out_width = (int)(x + 0.5f) - pen_x;
	if (out_ascent)
		*out_ascent = max_up;
	if (out_descent)
		*out_descent = max_down;
	return true;
}

bool osd_text_measure_tracked(osd_font_t *f, float size, const char *text, float tracking,
	osd_text_metrics_t *out) {
	int w = 0, a = 0, d = 0;
	if (!walk(f, size, text, tracking, NULL, 0, 0, 0, &w, &a, &d))
		return false;
	if (out) {
		out->width = w;
		out->ascent = a;
		out->descent = d;
		out->height = a + d;
	}
	return true;
}

bool osd_text_measure(osd_font_t *f, float size, const char *text, osd_text_metrics_t *out) {
	return osd_text_measure_tracked(f, size, text, 0.0f, out);
}

int osd_text_draw_tracked(osd_surface_t *s, osd_font_t *f, float size, int x, int y_baseline,
	const char *text, float tracking, osd_color_t color) {
	int w = 0;
	walk(f, size, text, tracking, s, x, y_baseline, color, &w, NULL, NULL);
	return x + w;
}

int osd_text_draw(osd_surface_t *s, osd_font_t *f, float size, int x, int y_baseline,
	const char *text, osd_color_t color) {
	return osd_text_draw_tracked(s, f, size, x, y_baseline, text, 0.0f, color);
}

int osd_text_draw_right(osd_surface_t *s, osd_font_t *f, float size, int x_right, int y_baseline,
	const char *text, osd_color_t color) {
	osd_text_metrics_t m;
	if (!osd_text_measure(f, size, text, &m))
		return x_right;
	return osd_text_draw(s, f, size, x_right - m.width, y_baseline, text, color);
}

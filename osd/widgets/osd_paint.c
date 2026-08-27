#include "osd_paint.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

void osd_surface_init(osd_surface_t *s, uint8_t *pixels, int width, int height, int stride) {
	s->pixels = pixels;
	s->width = width;
	s->height = height;
	s->stride = stride;
	s->clip_x = 0;
	s->clip_y = 0;
	s->clip_w = width;
	s->clip_h = height;
}

void osd_surface_set_clip(osd_surface_t *s, int x, int y, int w, int h) {
	if (x < 0) {
		w += x;
		x = 0;
	}
	if (y < 0) {
		h += y;
		y = 0;
	}
	if (x + w > s->width)
		w = s->width - x;
	if (y + h > s->height)
		h = s->height - y;
	s->clip_x = x;
	s->clip_y = y;
	s->clip_w = w < 0 ? 0 : w;
	s->clip_h = h < 0 ? 0 : h;
}

static inline bool in_clip(const osd_surface_t *s, int x, int y) {
	return x >= s->clip_x && x < s->clip_x + s->clip_w && y >= s->clip_y && y < s->clip_y + s->clip_h;
}

void osd_blend_px(osd_surface_t *s, int x, int y, osd_color_t c, float coverage) {
	if (!s->pixels || !in_clip(s, x, y) || coverage <= 0.0f)
		return;
	if (coverage > 1.0f)
		coverage = 1.0f;

	int sa = (int)(OSD_A(c) * coverage + 0.5f);
	if (sa <= 0)
		return;

	uint8_t *px = s->pixels + (size_t)y * s->stride + (size_t)x * 4;
	// Memory order is [B,G,R,A].
	int db = px[0], dg = px[1], dr = px[2], da = px[3];
	int sb = OSD_B(c), sg = OSD_G(c), sr = OSD_R(c);

	// Straight (non-premultiplied) source-over.
	int outa = sa + da * (255 - sa) / 255;
	if (outa <= 0) {
		px[0] = px[1] = px[2] = px[3] = 0;
		return;
	}
	px[0] = (uint8_t)((sb * sa + db * da * (255 - sa) / 255) / outa);
	px[1] = (uint8_t)((sg * sa + dg * da * (255 - sa) / 255) / outa);
	px[2] = (uint8_t)((sr * sa + dr * da * (255 - sa) / 255) / outa);
	px[3] = (uint8_t)outa;
}

void osd_fill_rect(osd_surface_t *s, int x, int y, int w, int h, osd_color_t c) {
	for (int yy = y; yy < y + h; yy++)
		for (int xx = x; xx < x + w; xx++)
			osd_blend_px(s, xx, yy, c, 1.0f);
}

void osd_clear_rect(osd_surface_t *s, int x, int y, int w, int h) {
	if (!s->pixels)
		return;
	for (int yy = y; yy < y + h; yy++) {
		for (int xx = x; xx < x + w; xx++) {
			if (!in_clip(s, xx, yy))
				continue;
			uint8_t *px = s->pixels + (size_t)yy * s->stride + (size_t)xx * 4;
			px[0] = px[1] = px[2] = px[3] = 0;
		}
	}
}

// Scanline fill with 4x vertical supersampling. Horizontal coverage comes from
// the fractional span ends, which together give clean chamfer diagonals without
// needing a full analytic rasteriser.
#define SUBSAMPLES 4

void osd_fill_poly(osd_surface_t *s, const osd_pointf_t *pts, int count, osd_color_t c) {
	if (count < 3)
		return;

	float miny = pts[0].y, maxy = pts[0].y;
	for (int i = 1; i < count; i++) {
		if (pts[i].y < miny)
			miny = pts[i].y;
		if (pts[i].y > maxy)
			maxy = pts[i].y;
	}
	int y0 = (int)floorf(miny), y1 = (int)ceilf(maxy);
	if (y0 < s->clip_y)
		y0 = s->clip_y;
	if (y1 > s->clip_y + s->clip_h)
		y1 = s->clip_y + s->clip_h;

	int cw = s->clip_w;
	if (cw <= 0)
		return;

	float *cov = (float *)calloc((size_t)cw, sizeof(float));
	if (!cov)
		return;

	for (int y = y0; y < y1; y++) {
		memset(cov, 0, (size_t)cw * sizeof(float));

		for (int sub = 0; sub < SUBSAMPLES; sub++) {
			float sy = (float)y + ((float)sub + 0.5f) / SUBSAMPLES;

			// Gather crossings of this scanline with every edge.
			float xs[16];
			int n = 0;
			for (int i = 0; i < count && n < 16; i++) {
				const osd_pointf_t *a = &pts[i];
				const osd_pointf_t *b = &pts[(i + 1) % count];
				if ((a->y <= sy && b->y > sy) || (b->y <= sy && a->y > sy)) {
					float t = (sy - a->y) / (b->y - a->y);
					xs[n++] = a->x + t * (b->x - a->x);
				}
			}
			// Sort crossings, then fill between pairs.
			for (int i = 1; i < n; i++) {
				float v = xs[i];
				int j = i - 1;
				while (j >= 0 && xs[j] > v) {
					xs[j + 1] = xs[j];
					j--;
				}
				xs[j + 1] = v;
			}

			for (int i = 0; i + 1 < n; i += 2) {
				float xa = xs[i], xb = xs[i + 1];
				if (xb <= xa)
					continue;
				int ixa = (int)floorf(xa), ixb = (int)ceilf(xb);
				for (int x = ixa; x < ixb; x++) {
					int idx = x - s->clip_x;
					if (idx < 0 || idx >= cw)
						continue;
					// Horizontal coverage of this pixel by the span.
					float l = (float)x, r = (float)x + 1.0f;
					float ov = (xb < r ? xb : r) - (xa > l ? xa : l);
					if (ov <= 0.0f)
						continue;
					if (ov > 1.0f)
						ov = 1.0f;
					cov[idx] += ov / SUBSAMPLES;
				}
			}
		}

		for (int i = 0; i < cw; i++)
			if (cov[i] > 0.0f)
				osd_blend_px(s, s->clip_x + i, y, c, cov[i]);
	}

	free(cov);
}

void osd_draw_line(osd_surface_t *s, float x0, float y0, float x1, float y1, float width, osd_color_t c) {
	float dx = x1 - x0, dy = y1 - y0;
	float len = sqrtf(dx * dx + dy * dy);
	if (len < 0.0001f)
		return;
	float nx = -dy / len * width * 0.5f;
	float ny = dx / len * width * 0.5f;
	osd_pointf_t quad[4] = {
		{x0 + nx, y0 + ny}, {x1 + nx, y1 + ny}, {x1 - nx, y1 - ny}, {x0 - nx, y0 - ny}};
	osd_fill_poly(s, quad, 4, c);
}

void osd_stroke_poly(osd_surface_t *s, const osd_pointf_t *pts, int count, float width, osd_color_t c) {
	for (int i = 0; i < count; i++) {
		const osd_pointf_t *a = &pts[i];
		const osd_pointf_t *b = &pts[(i + 1) % count];
		osd_draw_line(s, a->x, a->y, b->x, b->y, width, c);
	}
}

static osd_color_t sample_gradient(const osd_color_t *stops, const float *offsets, int n, float t) {
	if (n <= 0)
		return 0;
	if (t <= offsets[0])
		return stops[0];
	if (t >= offsets[n - 1])
		return stops[n - 1];
	for (int i = 0; i + 1 < n; i++) {
		if (t >= offsets[i] && t <= offsets[i + 1]) {
			float span = offsets[i + 1] - offsets[i];
			float f = span > 0.0f ? (t - offsets[i]) / span : 0.0f;
			osd_color_t a = stops[i], b = stops[i + 1];
			// Pull every channel into signed ints first. The OSD_x() macros
			// yield unsigned expressions, so a descending channel (e.g. green
			// 255 -> 240) would underflow to ~4.29e9 and the float->int cast
			// back is undefined, collapsing the channel to 0.
			int aa = (int)OSD_A(a), ar = (int)OSD_R(a), ag = (int)OSD_G(a), ab = (int)OSD_B(a);
			int ba = (int)OSD_A(b), br = (int)OSD_R(b), bg = (int)OSD_G(b), bb = (int)OSD_B(b);
			return OSD_ARGB((int)(aa + (ba - aa) * f + 0.5f),
				(int)(ar + (br - ar) * f + 0.5f),
				(int)(ag + (bg - ag) * f + 0.5f),
				(int)(ab + (bb - ab) * f + 0.5f));
		}
	}
	return stops[n - 1];
}

void osd_fill_rect_gradient(osd_surface_t *s, int x, int y, int w, int h,
	const osd_color_t *stops, const float *offsets, int stop_count) {
	if (w <= 0)
		return;
	for (int xx = x; xx < x + w; xx++) {
		float t = (float)(xx - x) / (float)w;
		osd_color_t c = sample_gradient(stops, offsets, stop_count, t);
		for (int yy = y; yy < y + h; yy++)
			osd_blend_px(s, xx, yy, c, 1.0f);
	}
}

// Diagonal stripes. `period` is the stripe pitch in pixels, `duty` the fraction
// of it that is stripe rather than gap.
static void hatch_impl(osd_surface_t *s, int x, int y, int w, int h, int total_w,
	const osd_color_t *stops, const float *offsets, int stop_count, osd_color_t flat_stripe,
	bool use_gradient, osd_color_t gap, float period, float duty, float slant) {
	if (period <= 0.5f)
		period = 8.0f;
	if (total_w <= 0)
		total_w = w;

	for (int yy = y; yy < y + h; yy++) {
		for (int xx = x; xx < x + w; xx++) {
			float shifted = (float)xx + slant * (float)(yy - y);
			float phase = fmodf(shifted, period);
			if (phase < 0.0f)
				phase += period;
			bool is_stripe = phase < period * duty;

			osd_color_t c;
			if (is_stripe) {
				if (use_gradient) {
					float t = (float)(xx - x) / (float)total_w;
					c = sample_gradient(stops, offsets, stop_count, t);
				} else {
					c = flat_stripe;
				}
			} else {
				c = gap;
			}
			if (OSD_A(c) > 0)
				osd_blend_px(s, xx, yy, c, 1.0f);
		}
	}
}

void osd_fill_rect_hatched(osd_surface_t *s, int x, int y, int w, int h,
	osd_color_t stripe, osd_color_t gap, float period, float duty, float slant) {
	hatch_impl(s, x, y, w, h, w, NULL, NULL, 0, stripe, false, gap, period, duty, slant);
}

void osd_fill_rect_hatched_gradient(osd_surface_t *s, int x, int y, int w, int h, int total_w,
	const osd_color_t *stops, const float *offsets, int stop_count,
	osd_color_t gap, float period, float duty, float slant) {
	hatch_impl(s, x, y, w, h, total_w, stops, offsets, stop_count, 0, true, gap, period, duty, slant);
}

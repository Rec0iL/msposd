#include "osd_heading.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#ifndef M_PI
	#define M_PI 3.14159265358979323846
#endif

// ── smoothing ───────────────────────────────────────────────────────────────

void osd_heading_smooth_init(osd_heading_smooth_t *s) {
	if (s)
		memset(s, 0, sizeof(*s));
}

// Frame-rate independent easing: the display settles in the same wall-clock
// time at 60fps as at 10.
static float ease_alpha(float dt_ms, float tau_ms) {
	if (dt_ms <= 0.0f)
		return 0.0f;
	if (tau_ms <= 1.0f)
		return 1.0f;
	float a = 1.0f - expf(-dt_ms / tau_ms);
	return a < 0.0f ? 0.0f : (a > 1.0f ? 1.0f : a);
}

static void ease_bearing(double *e, double *n, float deg, float a) {
	const double r = (double)deg * M_PI / 180.0;
	const double te = sin(r), tn = cos(r);
	if (*e == 0.0 && *n == 0.0) {
		*e = te;
		*n = tn;
		return;
	}
	*e += (te - *e) * a;
	*n += (tn - *n) * a;
}

static float bearing_of(double e, double n) {
	if (e == 0.0 && n == 0.0)
		return 0.0f;
	float deg = (float)(atan2(e, n) * 180.0 / M_PI);
	return deg < 0.0f ? deg + 360.0f : deg;
}

void osd_heading_smooth_update(osd_heading_smooth_t *s, float heading_deg, float track_deg,
	bool track_valid, uint64_t now_ms, float tau_ms) {
	if (!s)
		return;
	if (!s->valid) {
		s->valid = true;
		s->last_ms = now_ms;
	}

	float dt_ms = (float)(now_ms - s->last_ms);
	// A stall - a lost link, a paused replay - must not arrive as one huge step
	// that whips the display round.
	if (dt_ms < 0.0f || dt_ms > 2000.0f)
		dt_ms = 0.0f;
	s->last_ms = now_ms;

	const float a = ease_alpha(dt_ms, tau_ms);
	ease_bearing(&s->hdg_e, &s->hdg_n, heading_deg, a);
	if (track_valid)
		ease_bearing(&s->trk_e, &s->trk_n, track_deg, a);
}

float osd_heading_smooth_heading(const osd_heading_smooth_t *s) {
	return s ? bearing_of(s->hdg_e, s->hdg_n) : 0.0f;
}

float osd_heading_smooth_track(const osd_heading_smooth_t *s) {
	return s ? bearing_of(s->trk_e, s->trk_n) : 0.0f;
}

// ── shared helpers ──────────────────────────────────────────────────────────

/// Dims a colour without touching its alpha - used to fade the far side of a
/// ring into the distance. Scaling alpha instead would let the video through
/// and read as transparency rather than depth.
// Outline, applied to every shape this file draws. Held in file statics and set
// once at the top of osd_heading_draw rather than threaded through: it is a
// property of the whole display, and passing it to each of the ~30 call sites
// below would bury the geometry in bookkeeping.
static bool g_out_on = false;
static osd_color_t g_out_col = 0;
static float g_out_px = 2.0f;

// The outline has to fade with whatever it sits behind. A ring tick 90 degrees
// off the nose is drawn at 30% alpha; an outline held at full strength turns it
// into a black mark with a faint core, which is worse than no outline at all.
static osd_color_t outline_for(osd_color_t c) {
	const float f = (float)OSD_A(c) / 255.0f;
	return OSD_ARGB((uint8_t)((float)OSD_A(g_out_col) * f), OSD_R(g_out_col), OSD_G(g_out_col),
		OSD_B(g_out_col));
}

static void hline(osd_surface_t *s, float x0, float y0, float x1, float y1, float w,
	osd_color_t c) {
	if (g_out_on) {
		// Grown in proportion to the line, not by a fixed amount: a hairline
		// tick given a 2px halo on each side is 80% halo, which reads as a black
		// dash with a coloured seam rather than as an outlined tick.
		float grow = w * 0.5f;
		if (grow < 0.8f)
			grow = 0.8f;
		if (grow > g_out_px)
			grow = g_out_px;
		osd_draw_line(s, x0, y0, x1, y1, w + 2.0f * grow, outline_for(c));
	}
	osd_draw_line(s, x0, y0, x1, y1, w, c);
}

static void hpoly(osd_surface_t *s, const osd_pointf_t *pts, int count, osd_color_t c) {
	if (g_out_on && count > 0 && count <= 8) {
		// Grown about the centroid, so a marker keeps its shape and gets an even
		// border on every side. Offsetting each edge properly would need a
		// mitred inset; for triangles this size the difference is sub-pixel.
		float mx = 0.0f, my = 0.0f;
		for (int i = 0; i < count; i++) {
			mx += pts[i].x;
			my += pts[i].y;
		}
		mx /= (float)count;
		my /= (float)count;
		osd_pointf_t big[8];
		for (int i = 0; i < count; i++) {
			const float dx = pts[i].x - mx, dy = pts[i].y - my;
			const float len = sqrtf(dx * dx + dy * dy);
			const float k = len > 0.001f ? (len + g_out_px) / len : 1.0f;
			big[i].x = mx + dx * k;
			big[i].y = my + dy * k;
		}
		osd_fill_poly(s, big, count, outline_for(c));
	}
	osd_fill_poly(s, pts, count, c);
}

static void hrect(osd_surface_t *s, int x, int y, int w, int h, osd_color_t c) {
	if (g_out_on) {
		const int o = (int)(g_out_px + 0.5f);
		osd_fill_rect(s, x - o, y - o, w + 2 * o, h + 2 * o, outline_for(c));
	}
	osd_fill_rect(s, x, y, w, h, c);
}

static osd_color_t dim(osd_color_t c, float f) {
	if (f < 0.0f)
		f = 0.0f;
	if (f > 1.0f)
		f = 1.0f;
	return OSD_ARGB(OSD_A(c), (uint8_t)(OSD_R(c) * f), (uint8_t)(OSD_G(c) * f),
		(uint8_t)(OSD_B(c) * f));
}

/// Signed difference from the nose, in degrees, wrapped to (-180, 180].
static float rel(float bearing, float heading) {
	float d = fmodf(bearing - heading + 540.0f, 360.0f) - 180.0f;
	return d;
}

static void label_for(int deg, char *out, size_t n) {
	switch (((deg % 360) + 360) % 360) {
	case 0: snprintf(out, n, "N"); return;
	case 90: snprintf(out, n, "E"); return;
	case 180: snprintf(out, n, "S"); return;
	case 270: snprintf(out, n, "W"); return;
	default: snprintf(out, n, "%02d", (((deg % 360) + 360) % 360) / 10); return;
	}
}

static void centred_text(osd_surface_t *s, osd_font_t *font, float size, float cx, float baseline,
	const char *txt, osd_color_t c) {
	if (!font)
		return;
	osd_text_metrics_t m;
	if (!osd_text_measure(font, size, txt, &m))
		return;
	osd_text_draw(s, font, size, (int)(cx - m.width * 0.5f), (int)baseline, txt, c);
}

/// A marker pointing at the display: used for the nose, the track and home.
static void pointer(osd_surface_t *s, float x, float y, float w, float h, osd_color_t c) {
	const osd_pointf_t tri[3] = {{x, y}, {x - w, y - h}, {x + w, y - h}};
	hpoly(s, tri, 3, c);
}

// ── band ────────────────────────────────────────────────────────────────────

static void draw_band(osd_surface_t *s, osd_font_t *font, float cx, float cy,
	const osd_heading_params_t *p) {
	const float half = p->size * 0.5f;
	const float h = 46.0f;
	const float top = cy - h * 0.5f;
	const float span = (p->span_deg > 1.0f) ? p->span_deg : 90.0f;
	const float px_per_deg = p->size / span;

	hrect(s, (int)(cx - half), (int)top, (int)p->size, (int)h, p->fill);
	hline(s, cx - half, top, cx + half, top, 1.5f, p->edge);
	hline(s, cx - half, top + h, cx + half, top + h, 1.5f, p->edge);

	for (int deg = 0; deg < 360; deg += 5) {
		const float d = rel((float)deg, p->heading_deg);
		const float x = cx + d * px_per_deg;
		if (x < cx - half || x > cx + half)
			continue;
		if (deg % 30 == 0) {
			hline(s, x, top + h * 0.5f, x, top + h - 4.0f, 2.0f, p->accent);
			char t[8];
			label_for(deg, t, sizeof(t));
			centred_text(s, font, 15.0f, x, top + h * 0.42f, t, p->accent);
		} else {
			hline(s, x, top + h * 0.72f, x, top + h - 4.0f, 1.0f, p->label);
		}
	}

	// Track and home hang below the tape at their true bearings - which is what
	// turns a heading readout into something you can navigate by.
	if (p->track_valid && p->show_track) {
		const float d = rel(p->track_deg, p->heading_deg);
		if (fabsf(d) * px_per_deg <= half)
			pointer(s, cx + d * px_per_deg, top + h + 12.0f, 6.0f, 12.0f, p->track);
	}
	if (p->home_valid) {
		const float d = rel(p->home_deg, p->heading_deg);
		if (fabsf(d) * px_per_deg <= half) {
			pointer(s, cx + d * px_per_deg, top + h + 12.0f, 6.0f, 12.0f, p->home);
		} else {
			// Home is off the tape - which is exactly when you want it. Pin it to
			// the edge it lies beyond, with a chevron pointing the way round,
			// the same trick the map uses for an off-map launch point.
			const float ex = (d > 0.0f) ? (cx + half - 10.0f) : (cx - half + 10.0f);
			const float dir = (d > 0.0f) ? 1.0f : -1.0f;
			pointer(s, ex, top + h + 12.0f, 6.0f, 12.0f, p->home);
			hline(s, ex + dir * 9.0f, top + h + 18.0f, ex + dir * 17.0f, top + h + 12.0f,
				2.0f, p->home);
			hline(s, ex + dir * 9.0f, top + h + 6.0f, ex + dir * 17.0f, top + h + 12.0f,
				2.0f, p->home);
		}
	}

	// Above the tape, not inside it - in the tape it lands on whatever label
	// happens to be at the nose.
	pointer(s, cx, top - 2.0f, 9.0f, -14.0f, p->accent);
	char num[8];
	snprintf(num, sizeof(num), "%03d", (int)(p->heading_deg + 0.5f) % 360);
	centred_text(s, font, 22.0f, cx, top - 20.0f, num, p->accent);
}

// ── rose ────────────────────────────────────────────────────────────────────

static void draw_rose(osd_surface_t *s, osd_font_t *font, float cx, float cy,
	const osd_heading_params_t *p) {
	const float r = p->size * 0.5f;

	// Ticks hang inward from a common outer edge, with the major ones reaching
	// further in. Keeping the *outer* edge flush is what makes a dial read as a
	// dial; varying both ends made it look ragged, and varying neither threw
	// the hierarchy away.
	for (int deg = 0; deg < 360; deg += 15) {
		const float a = rel((float)deg, p->heading_deg) * (float)M_PI / 180.0f;
		const bool major = (deg % 90 == 0);
		const bool mid = (deg % 30 == 0);
		if (!mid && !major && (deg % 15 != 0))
			continue;
		const float inner = major ? 0.72f : (mid ? 0.84f : 0.90f);
		hline(s, cx + sinf(a) * r * inner, cy - cosf(a) * r * inner,
			cx + sinf(a) * r * 0.97f, cy - cosf(a) * r * 0.97f, major ? 2.5f : 1.6f,
			major ? p->accent : p->label);
		if (major) {
			char t[8];
			label_for(deg, t, sizeof(t));
			centred_text(s, font, 15.0f, cx + sinf(a) * r * 0.58f,
				cy - cosf(a) * r * 0.58f + 5.0f, t, p->accent);
		}
	}

	// Track and home ride the rim as markers, like every other style. Drawing
	// the track as a spoke from the centre made it the loudest thing on the
	// dial, when it is a detail beside the nose.
	if (p->track_valid && p->show_track) {
		const float a = rel(p->track_deg, p->heading_deg) * (float)M_PI / 180.0f;
		const osd_pointf_t tri[3] = {{cx + sinf(a) * r * 0.97f, cy - cosf(a) * r * 0.97f},
			{cx + sinf(a + 0.10f) * r * 0.82f, cy - cosf(a + 0.10f) * r * 0.82f},
			{cx + sinf(a - 0.10f) * r * 0.82f, cy - cosf(a - 0.10f) * r * 0.82f}};
		hpoly(s, tri, 3, p->track);
	}
	if (p->home_valid) {
		const float a = rel(p->home_deg, p->heading_deg) * (float)M_PI / 180.0f;
		const osd_pointf_t tri[3] = {{cx + sinf(a) * r * 0.97f, cy - cosf(a) * r * 0.97f},
			{cx + sinf(a + 0.10f) * r * 0.82f, cy - cosf(a + 0.10f) * r * 0.82f},
			{cx + sinf(a - 0.10f) * r * 0.82f, cy - cosf(a - 0.10f) * r * 0.82f}};
		hpoly(s, tri, 3, p->home);
	}

	// The aircraft is fixed and the card turns under it, matching the view out
	// of the front. Small: it is the reference, not the reading.
	const osd_pointf_t ac[3] = {{cx, cy - r * 0.13f}, {cx - r * 0.07f, cy + r * 0.09f},
		{cx + r * 0.07f, cy + r * 0.09f}};
	hpoly(s, ac, 3, p->accent);

	char num[8];
	snprintf(num, sizeof(num), "%03d", (int)(p->heading_deg + 0.5f) % 360);
	centred_text(s, font, 22.0f, cx, cy + r + 22.0f, num, p->accent);
}

// ── ring ────────────────────────────────────────────────────────────────────

// The lens. sin() alone already compresses toward the edges, which reads as
// perspective; raising it to a power below 1 pushes the marks near the nose
// further apart on top of that. Without it the useful part of the ring - what
// is in front of you - is the most cramped part of it.
static float lens(float u, float power) {
	const float m = powf(fabsf(u), power);
	return u < 0.0f ? -m : m;
}

static void ring_place(const osd_heading_params_t *p, float cx, float cy, float rx, float ry,
	float bearing, float *out_x, float *out_y, float *out_depth) {
	const float a = rel(bearing, p->heading_deg) * (float)M_PI / 180.0f;
	const float sign = p->flip ? -1.0f : 1.0f;
	*out_x = cx + lens(sinf(a), p->lens) * rx;
	*out_y = cy - sign * cosf(a) * ry;
	*out_depth = cosf(a);
}

static void draw_ring(osd_surface_t *s, osd_font_t *font, float cx, float cy,
	const osd_heading_params_t *p) {
	const float rx = p->size * 0.5f;
	const float ry = p->size * 0.09f;
	const float sign = p->flip ? -1.0f : 1.0f;

	for (int deg = 0; deg < 360; deg += 5) {
		float x, y, depth;
		ring_place(p, cx, cy, rx, ry, (float)deg, &x, &y, &depth);
		if (depth < -0.12f)
			continue; // behind the aircraft
		// depth is cos(angle off the nose): +1 straight ahead, -1 behind. Both
		// this and the cull above were inverted, so the ring showed its far
		// side and faded the part you are flying at.
		const float near = (depth + 1.0f) * 0.5f;
		const float h = (5.0f + 11.0f * near) * ((deg % 15 == 0) ? 1.0f : 0.45f);
		// Fading with depth is what sells it as a ring rather than a wavy line.
		const osd_color_t c = dim((deg % 45 == 0) ? p->accent : p->label, 0.30f + 0.70f * near);
		hline(s, x, y, x, y + sign * h, (deg % 45 == 0) ? 2.0f : 1.0f, c);
		if (deg % 45 == 0 && near > 0.25f) {
			char t[8];
			label_for(deg, t, sizeof(t));
			centred_text(s, font, 15.0f, x, y + sign * (h + 14.0f) + (p->flip ? 0.0f : 0.0f), t, c);
		}
	}

	if (p->track_valid && p->show_track) {
		float x, y, depth;
		ring_place(p, cx, cy, rx, ry, p->track_deg, &x, &y, &depth);
		if (depth >= -0.12f)
			pointer(s, x, y, 7.0f, sign * 11.0f, p->track);
	}
	if (p->home_valid) {
		float x, y, depth;
		ring_place(p, cx, cy, rx, ry, p->home_deg, &x, &y, &depth);
		if (depth >= -0.12f)
			pointer(s, x, y, 7.0f, sign * 11.0f, p->home);
	}

	// The ring's nose sits at (cx, cy - sign*ry), not at the centre. pointer()
	// takes the apex and puts the base at y - h, so the marker hangs outward
	// from the arc with its point on it, the same way track and home do.
	pointer(s, cx, cy - sign * (ry + 4.0f), 10.0f, sign * 17.0f, p->accent);
	char num[8];
	snprintf(num, sizeof(num), "%03d", (int)(p->heading_deg + 0.5f) % 360);
	centred_text(s, font, 22.0f, cx, cy - sign * ry + sign * 42.0f, num, p->accent);
}

// ── numeric ─────────────────────────────────────────────────────────────────

static void draw_numeric(osd_surface_t *s, osd_font_t *font, float cx, float cy,
	const osd_heading_params_t *p) {
	const float w = p->size, h = 56.0f;
	hrect(s, (int)(cx - w * 0.5f), (int)(cy - h * 0.5f), (int)w, (int)h, p->fill);
	hline(s, cx - w * 0.5f, cy - h * 0.5f, cx + w * 0.5f, cy - h * 0.5f, 1.5f, p->edge);
	hline(s, cx - w * 0.5f, cy + h * 0.5f, cx + w * 0.5f, cy + h * 0.5f, 1.5f, p->edge);

	char num[16];
	const int hdg = (int)(p->heading_deg + 0.5f) % 360;
	snprintf(num, sizeof(num), "%03d", hdg);
	centred_text(s, font, 30.0f, cx - w * 0.12f, cy + 10.0f, num, p->accent);

	char card[8];
	label_for(((hdg + 22) / 45) * 45, card, sizeof(card));
	centred_text(s, font, 18.0f, cx + w * 0.28f, cy + 8.0f, card, p->label);
}

// ── navball ─────────────────────────────────────────────────────────────────

static void draw_navball(osd_surface_t *s, osd_font_t *font, float cx, float cy,
	const osd_heading_params_t *p) {
	const float r = p->size * 0.5f;
	const float roll = p->roll_deg * (float)M_PI / 180.0f;
	const float ca = cosf(roll), sa = sinf(roll);
	// Degrees of pitch that fit between the centre and the rim.
	const float px_per_deg = r / 45.0f;
	// Where the horizon sits in the ball's own frame. Nose up means you see more
	// sky, so the horizon drops down the instrument - hence +pitch. This was
	// -pitch, which put the horizon above centre in a climb: the ball read as
	// diving exactly when the aircraft was going up.
	const float horizon_y = p->pitch_deg * px_per_deg;

	// Ball frame -> screen. Every feature below is positioned in the ball's
	// frame and mapped through this, so they all roll together by construction.
	#define BALL_X(bx, by) (cx + (bx) * ca - (by) * sa)
	#define BALL_Y(bx, by) (cy + (bx) * sa + (by) * ca)

	// A blue that reads as sky and a brown that reads as ground, so which way is
	// up is answered before you have looked at anything else.
	const float op = p->opacity > 0.0f ? (p->opacity > 1.0f ? 1.0f : p->opacity) : 1.0f;
	const osd_color_t sky = OSD_ARGB((uint8_t)(200.0f * op), 38, 92, 148);
	const osd_color_t ground = OSD_ARGB((uint8_t)(200.0f * op), 98, 68, 38);

	// Walked per pixel: the sphere is a filled circle split by a rolled horizon,
	// which is cheaper and steadier than trying to clip polygons to a curve.
	const int ir = (int)r;
	for (int dy = -ir; dy <= ir; dy++) {
		const int span = (int)sqrtf(fmaxf(0.0f, r * r - (float)dy * dy));
		for (int dx = -span; dx <= span; dx++) {
			// Into the ball's own frame: undo roll, then compare against pitch.
			const float by = -(float)dx * sa + (float)dy * ca;
			osd_blend_px(s, (int)(cx + dx), (int)(cy + dy), (by < horizon_y) ? sky : ground, 1.0f);
		}
	}

	// Pitch ladder. Without it the ball only says level or not level; the rungs
	// are what turn it into a reading.
	for (int v = -30; v <= 30; v += 10) {
		if (v == 0)
			continue;
		const float by = horizon_y - (float)v * px_per_deg;
		if (fabsf(by) > r * 0.86f)
			continue; // off the ball
		const float half = (v % 20 == 0) ? r * 0.30f : r * 0.19f;
		const osd_color_t c = dim(p->label, 0.85f);
		hline(s, BALL_X(-half, by), BALL_Y(-half, by), BALL_X(-half * 0.45f, by),
			BALL_Y(-half * 0.45f, by), 1.6f, c);
		hline(s, BALL_X(half * 0.45f, by), BALL_Y(half * 0.45f, by), BALL_X(half, by),
			BALL_Y(half, by), 1.6f, c);
		char t[8];
		snprintf(t, sizeof(t), "%d", v < 0 ? -v : v);
		centred_text(s, font, 12.0f, BALL_X(-half - 12.0f, by), BALL_Y(-half - 12.0f, by) + 4.0f,
			t, c);
	}

	// Horizon line, in the label colour rather than the accent: the aircraft
	// symbol is drawn in the accent on top of it, and two bright cyan lines
	// crossing at the centre merge into one shape you cannot read.
	hline(s, BALL_X(-r, horizon_y), BALL_Y(-r, horizon_y), BALL_X(r, horizon_y),
		BALL_Y(r, horizon_y), 2.5f, p->label);

	// Heading strip: short ticks hanging *below* the horizon with the cardinal
	// under them, which is where an ADI puts them. They used to run a third of
	// the way down the ball, which read as streaks across the ground.
	for (int deg = 0; deg < 360; deg += 30) {
		const float a = rel((float)deg, p->heading_deg) * (float)M_PI / 180.0f;
		if (cosf(a) <= 0.05f)
			continue; // the far side of the ball
		const float bx = sinf(a) * r * 0.92f;
		const osd_color_t c = dim(p->accent, 0.35f + 0.65f * cosf(a));
		const float len = (deg % 90 == 0) ? r * 0.13f : r * 0.07f;
		hline(s, BALL_X(bx, horizon_y), BALL_Y(bx, horizon_y), BALL_X(bx, horizon_y + len),
			BALL_Y(bx, horizon_y + len), (deg % 90 == 0) ? 2.0f : 1.4f, c);
		if (deg % 90 == 0) {
			char t[8];
			label_for(deg, t, sizeof(t));
			const float ty = horizon_y + len + 12.0f;
			centred_text(s, font, 14.0f, BALL_X(bx, ty), BALL_Y(bx, ty) + 4.0f, t, c);
		}
	}

	// A circle, walked as a polygon. Four points made a diamond.
	{
		osd_pointf_t rim[48];
		for (int k = 0; k < 48; k++) {
			const float a = (float)k * 2.0f * (float)M_PI / 48.0f;
			rim[k].x = cx + cosf(a) * r;
			rim[k].y = cy + sinf(a) * r;
		}
		osd_stroke_poly(s, rim, 48, 1.5f, p->edge);
	}

	// Roll scale: ticks fixed to the airframe outside the rim, and a pointer at
	// the ball's zenith. Past about 20 degrees the horizon's slope alone is
	// guesswork, and knowing the bank angle is most of what an ADI is for.
	{
		static const int stops[] = {-60, -45, -30, -20, -10, 0, 10, 20, 30, 45, 60};
		for (size_t k = 0; k < sizeof(stops) / sizeof(stops[0]); k++) {
			const float a = (float)stops[k] * (float)M_PI / 180.0f;
			const float ux = sinf(a), uy = -cosf(a);
			const bool major = (stops[k] % 30 == 0);
			const float len = major ? 0.11f : 0.06f;
			hline(s, cx + ux * r * 1.02f, cy + uy * r * 1.02f, cx + ux * r * (1.02f + len),
				cy + uy * r * (1.02f + len), major ? 2.0f : 1.4f,
				stops[k] == 0 ? p->accent : p->label);
		}
		// The ball's own "up" - (0,-1) through the same rotation as everything
		// else - so the pointer cannot disagree with the horizon it reads off.
		const float ux = BALL_X(0.0f, -1.0f) - cx, uy = BALL_Y(0.0f, -1.0f) - cy;
		const osd_pointf_t tri[3] = {{cx + ux * r * 0.99f, cy + uy * r * 0.99f},
			{cx + ux * r * 0.84f - uy * r * 0.06f, cy + uy * r * 0.84f + ux * r * 0.06f},
			{cx + ux * r * 0.84f + uy * r * 0.06f, cy + uy * r * 0.84f - ux * r * 0.06f}};
		hpoly(s, tri, 3, p->accent);
	}

	// Track and home ride the rim, where the ball's own surface is not competing.
	if (p->track_valid && p->show_track) {
		const float a = rel(p->track_deg, p->heading_deg) * (float)M_PI / 180.0f;
		if (cosf(a) > 0.0f)
			pointer(s, cx + sinf(a) * r * 0.92f, cy + r + 4.0f, 6.0f, 11.0f, p->track);
	}
	if (p->home_valid) {
		const float a = rel(p->home_deg, p->heading_deg) * (float)M_PI / 180.0f;
		if (cosf(a) > 0.0f)
			pointer(s, cx + sinf(a) * r * 0.92f, cy + r + 4.0f, 6.0f, 11.0f, p->home);
	}

	// Fixed aircraft, so the ball moves and the reference does not. Wings with
	// turned-down tips and a centre dot: a pair of plain bars was
	// indistinguishable from the horizon line whenever the two crossed.
	hline(s, cx - r * 0.52f, cy, cx - r * 0.16f, cy, 3.5f, p->accent);
	hline(s, cx + r * 0.16f, cy, cx + r * 0.52f, cy, 3.5f, p->accent);
	hline(s, cx - r * 0.52f, cy, cx - r * 0.52f, cy + r * 0.09f, 3.5f, p->accent);
	hline(s, cx + r * 0.52f, cy, cx + r * 0.52f, cy + r * 0.09f, 3.5f, p->accent);
	{
		const osd_pointf_t dot[4] = {{cx - 3.5f, cy}, {cx, cy - 3.5f}, {cx + 3.5f, cy},
			{cx, cy + 3.5f}};
		hpoly(s, dot, 4, p->accent);
	}

	#undef BALL_X
	#undef BALL_Y

	char num[8];
	snprintf(num, sizeof(num), "%03d", (int)(p->heading_deg + 0.5f) % 360);
	centred_text(s, font, 20.0f, cx, cy + r + 40.0f, num, p->accent);
}

// ── entry point ─────────────────────────────────────────────────────────────

void osd_heading_draw(
	osd_surface_t *s, osd_font_t *font, float cx, float cy, const osd_heading_params_t *p) {
	if (!s || !p)
		return;

	g_out_on = p->outline;
	g_out_col = p->outline_color;
	g_out_px = p->outline_px > 0.0f ? p->outline_px : 2.0f;

	// The numbers have to be outlined along with the ticks, or the display ends
	// up half legible. The caller's own text-outline setting is put back
	// afterwards: this is the heading's look, not a global one.
	bool prev_on;
	osd_color_t prev_col;
	int prev_px;
	osd_text_get_outline(&prev_on, &prev_col, &prev_px);
	if (p->outline)
		osd_text_set_outline(true, p->outline_color, (int)(g_out_px + 0.5f));

	switch (p->style) {
	case OSD_HEADING_ROSE: draw_rose(s, font, cx, cy, p); break;
	case OSD_HEADING_RING: draw_ring(s, font, cx, cy, p); break;
	case OSD_HEADING_NAVBALL: draw_navball(s, font, cx, cy, p); break;
	case OSD_HEADING_NUMERIC: draw_numeric(s, font, cx, cy, p); break;
	case OSD_HEADING_BAND:
	default: draw_band(s, font, cx, cy, p); break;
	}

	osd_text_set_outline(prev_on, prev_col, prev_px);
	g_out_on = false;
}

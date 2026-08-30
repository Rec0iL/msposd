#include "osd_link.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------

// Signal strength as a fraction, for the bars. -90dBm is where a wfb-ng link
// starts losing packets and -35 is as good as it gets sitting next to the
// aircraft, so those are the ends of the scale rather than the radio's range.
static float rssi_fraction(int dbm) {
	const float f = ((float)dbm + 90.0f) / 55.0f;
	return f < 0.0f ? 0.0f : (f > 1.0f ? 1.0f : f);
}

static osd_color_t rssi_colour(const osd_link_params_t *p, int dbm) {
	if (dbm >= -65)
		return p->good;
	if (dbm >= -80)
		return p->warn;
	return p->crit;
}

static osd_color_t snr_colour(const osd_link_params_t *p, int db) {
	if (db >= 10)
		return p->good;
	if (db >= 5)
		return p->warn;
	return p->crit;
}

// Same idea as the panels: a plate with the top-left corner stepped and the
// bottom-right cut away.
static void plate(osd_surface_t *s, float x, float y, float w, float h, float tab, float ch,
	osd_color_t fill, osd_color_t edge, osd_color_t accent) {
	const float step = w * 0.34f;
	const osd_pointf_t poly[7] = {{x, y + tab}, {x + step - 28.0f, y + tab}, {x + step, y},
		{x + w, y}, {x + w, y + h - ch}, {x + w - ch, y + h}, {x, y + h}};
	osd_fill_poly(s, poly, 7, fill);
	osd_stroke_poly(s, poly, 7, 1.5f, edge);
	osd_draw_line(s, x, y + h - 18.0f, x, y + h, 2.5f, accent);
	osd_draw_line(s, x, y + h, x + 26.0f, y + h, 2.5f, accent);
	osd_draw_line(s, x + w, y + h - ch - 12.0f, x + w, y + h - ch, 2.5f, accent);
	osd_draw_line(s, x + w, y + h - ch, x + w - ch, y + h, 2.5f, accent);
}

// Row heights, in unscaled units. Kept here so measure and draw cannot drift -
// the horizontal style got this wrong first time round and stacked the antenna
// captions on top of the footer.
#define LINK_HEAD_H 42.0f
#define LINK_ROW_H 30.0f
#define LINK_FOOT_H 34.0f
#define LINK_VERT_W 250.0f

// Horizontal: one column per antenna, four bands down each.
#define LINK_COL_W 96.0f
#define LINK_H_CAP 18.0f  // "A1"
#define LINK_H_VAL 26.0f  // the dBm number
#define LINK_H_BAR 14.0f  // its bar
#define LINK_H_SNR 20.0f

static int antenna_count(const osd_link_stats_t *s) {
	if (!s || !s->valid)
		return 0;
	int n = s->antennas;
	if (n > OSD_LINK_MAX_ANTENNAS)
		n = OSD_LINK_MAX_ANTENNAS;
	return n < 0 ? 0 : n;
}

// The WiFi driver behind APFPV reports no SNR at all, so the row would be blank
// space on every frame. Reserved only when something actually fills it.
static bool has_snr(const osd_link_stats_t *s) {
	if (!s)
		return false;
	for (int i = 0; i < OSD_LINK_MAX_ANTENNAS; i++)
		if (s->snr_valid[i])
			return true;
	return false;
}

static bool has_footer(const osd_link_stats_t *s) {
	return s && (s->quality_pct >= 0.0f || s->loss_pct >= 0.0f || s->bitrate_mbps >= 0.0f);
}

void osd_link_measure(
	const osd_link_params_t *p, const osd_link_stats_t *s, float *out_w, float *out_h) {
	if (!p || !out_w || !out_h)
		return;
	const float k = p->scale > 0.0f ? p->scale : 1.0f;
	const int n = antenna_count(s);
	const float foot = has_footer(s) ? LINK_FOOT_H : 0.0f;

	if (p->style == OSD_LINK_HORIZONTAL) {
		// One column per antenna, with the header stacked above them so a wide
		// widget does not also have to be a tall one.
		const int cols = n > 0 ? n : 1;
		*out_w = ((float)cols * LINK_COL_W + 24.0f) * k;
		const float snr = has_snr(s) ? LINK_H_SNR : 0.0f;
		*out_h = (LINK_HEAD_H + LINK_H_CAP + LINK_H_VAL + LINK_H_BAR + snr + foot) * k;
	} else {
		*out_w = LINK_VERT_W * k;
		*out_h = (LINK_HEAD_H + (float)n * LINK_ROW_H + foot + 8.0f) * k;
	}
}

// The footer line, shared by both styles: whichever of the three numbers the
// ground station actually reports, in a fixed order so the eye can find them.
static void footer_text(const osd_link_stats_t *s, char *out, size_t n) {
	out[0] = '\0';
	size_t used = 0;
	if (s->quality_pct >= 0.0f)
		used += (size_t)snprintf(out + used, n - used, "%.0f%%", s->quality_pct);
	if (s->loss_pct >= 0.0f)
		used += (size_t)snprintf(out + used, n - used, "%s%.1f%% lost", used ? "  " : "",
			s->loss_pct);
	if (s->bitrate_mbps >= 0.0f && used < n)
		snprintf(out + used, n - used, "%s%.1f Mbps", used ? "  " : "", s->bitrate_mbps);
}

void osd_link_draw(osd_surface_t *s, osd_font_t *font, float x, float y,
	const osd_link_params_t *p, const osd_link_stats_t *st, bool stale) {
	if (!s || !p || !st)
		return;
	const float k = p->scale > 0.0f ? p->scale : 1.0f;
	const int n = antenna_count(st);

	float w, h;
	osd_link_measure(p, st, &w, &h);

	bool prev_on;
	osd_color_t prev_col;
	int prev_px;
	osd_text_get_outline(&prev_on, &prev_col, &prev_px);
	if (p->outline)
		osd_text_set_outline(true, p->outline_color, (int)(p->outline_px + 0.5f));

	const float tab = LINK_HEAD_H * k * 0.82f;
	const float ch = p->chamfer * k;
	plate(s, x, y, w, h, tab, ch, p->fill, p->edge, p->accent);

	const float px = p->pad_x * k;
	const float py = p->pad_y * k;
	const float label_sz = p->label_size * k;
	const float value_sz = p->value_size * k;

	// Header: who is reporting, and a stale marker when they have stopped.
	osd_text_draw_tracked(s, font, label_sz, (int)(x + px), (int)(y + label_sz + 8.0f * k),
		stale ? "LINK  NO DATA" : st->source, p->label_tracking * k, stale ? p->crit : p->label);

	const float bar_h = p->bar_height * k * 0.62f;

	if (p->style == OSD_LINK_HORIZONTAL) {
		const float col_w = LINK_COL_W * k;
		// Four bands per column, each with its own row so nothing lands on the
		// footer: caption, the dBm number, its bar, SNR. Reading down a column
		// gives one antenna; reading across compares them.
		const float cap_y = y + (LINK_HEAD_H + LINK_H_CAP) * k;
		const float val_y = cap_y + LINK_H_VAL * k;
		const float bar_y = val_y + (LINK_H_BAR - 10.0f) * k;
		const float snr_y = val_y + (LINK_H_BAR + LINK_H_SNR) * k;
		for (int i = 0; i < n; i++) {
			const float cx = x + px + (float)i * col_w;
			char t[16];

			snprintf(t, sizeof(t), "A%d", i + 1);
			osd_text_draw_tracked(s, font, label_sz, (int)cx, (int)cap_y, t, p->label_tracking * k,
				p->label);

			if (st->rssi_valid[i]) {
				snprintf(t, sizeof(t), "%d", st->rssi_dbm[i]);
				osd_text_draw(s, font, value_sz * 0.78f, (int)cx, (int)val_y, t,
					rssi_colour(p, st->rssi_dbm[i]));
				const float bw = col_w - 18.0f * k;
				osd_fill_rect(s, (int)cx, (int)bar_y, (int)bw, (int)bar_h, p->track);
				osd_fill_rect(s, (int)cx, (int)bar_y, (int)(bw * rssi_fraction(st->rssi_dbm[i])),
					(int)bar_h, rssi_colour(p, st->rssi_dbm[i]));
			}
			if (st->snr_valid[i]) {
				snprintf(t, sizeof(t), "%ddB", st->snr_db[i]);
				osd_text_draw(s, font, label_sz * 1.15f, (int)cx, (int)snr_y, t,
					snr_colour(p, st->snr_db[i]));
			}
		}
	} else {
		const float row_h = LINK_ROW_H * k;
		for (int i = 0; i < n; i++) {
			const float ry = y + LINK_HEAD_H * k + (float)i * row_h;
			char t[16];

			snprintf(t, sizeof(t), "A%d", i + 1);
			osd_text_draw_tracked(s, font, label_sz, (int)(x + px), (int)(ry + label_sz),
				t, p->label_tracking * k, p->label);

			if (st->rssi_valid[i]) {
				snprintf(t, sizeof(t), "%d", st->rssi_dbm[i]);
				osd_text_metrics_t m = {0};
				osd_text_measure(font, value_sz * 0.72f, t, &m);
				// Right-aligned against the bar's left edge, so the numbers form
				// a column however many digits each has.
				const float bar_x = x + w * 0.46f;
				osd_text_draw(s, font, value_sz * 0.72f, (int)(bar_x - 8.0f * k - m.width),
					(int)(ry + label_sz + 2.0f * k), t, rssi_colour(p, st->rssi_dbm[i]));
				const float bw = w - (bar_x - x) - px;
				const float by = ry + row_h * 0.32f;
				osd_fill_rect(s, (int)bar_x, (int)by, (int)bw, (int)bar_h, p->track);
				osd_fill_rect(s, (int)bar_x, (int)by, (int)(bw * rssi_fraction(st->rssi_dbm[i])),
					(int)bar_h, rssi_colour(p, st->rssi_dbm[i]));
			}
			if (st->snr_valid[i]) {
				// With a unit: a bare number next to a dBm reading is just
				// another number, and the two are not on the same scale.
				snprintf(t, sizeof(t), "%ddB", st->snr_db[i]);
				osd_text_draw(s, font, label_sz * 1.1f, (int)(x + px + 32.0f * k),
					(int)(ry + label_sz), t, snr_colour(p, st->snr_db[i]));
			}
		}
	}

	if (has_footer(st)) {
		char t[48];
		footer_text(st, t, sizeof(t));
		osd_text_draw(s, font, label_sz * 1.25f, (int)(x + px), (int)(y + h - py - 2.0f * k), t,
			p->accent);
	}

	osd_text_set_outline(prev_on, prev_col, prev_px);
}

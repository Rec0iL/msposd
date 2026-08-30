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
	bool square, osd_color_t fill, osd_color_t edge, osd_color_t accent) {
	if (square) {
		const osd_pointf_t poly[4] = {{x, y}, {x + w, y}, {x + w, y + h}, {x, y + h}};
		osd_fill_poly(s, poly, 4, fill);
		osd_stroke_poly(s, poly, 4, 1.5f, edge);
		// Right-angled brackets: a diagonal across a square corner is the thing
		// a square theme is trying not to have.
		// Inset by half the stroke: on a square edge an overhang is plainly
		// visible where a cut corner hid it.
		const float arm = 20.0f;
		const float l = x + 1.25f, r = x + w - 1.25f, b = y + h - 1.25f;
		osd_draw_line(s, l, b - arm, l, b, 2.5f, accent);
		osd_draw_line(s, l, b, l + arm, b, 2.5f, accent);
		osd_draw_line(s, r - arm, b, r, b, 2.5f, accent);
		osd_draw_line(s, r, b - arm, r, b, 2.5f, accent);
		return;
	}
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
// The floor for the ultrawide style, which otherwise sizes itself to its header
// line. Below this the quality bar is too short to read as a bar.
#define LINK_ULTRA_MIN_W 340.0f

// Horizontal: one column per antenna, four bands down each.
#define LINK_COL_W 96.0f
#define LINK_H_CAP 18.0f  // "A1"
#define LINK_H_VAL 26.0f  // the dBm number
#define LINK_H_BAR 14.0f  // its bar
#define LINK_H_SNR 20.0f

// Breathing room under the last row when no footer follows it. The footer's own
// band used to supply this by accident; with APFPV, which reports neither SNR
// nor packet counts, the bars ended up sitting on the plate's bottom edge.
#define LINK_BOTTOM_PAD 12.0f

// Link quality gets a row of its own, above the aerials in both styles: it is
// the headline number, and a bar is read at a glance where a percentage has to
// be looked at.
#define LINK_LQ_H 32.0f

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

static bool has_lq(const osd_link_stats_t *s) {
	return s && s->quality_pct >= 0.0f;
}

// The footer carries whatever is left once link quality has its own row - and
// nothing at all in the ultrawide style, where it has moved up to the header.
static bool has_footer(const osd_link_stats_t *s, osd_link_style_t style) {
	if (style == OSD_LINK_ULTRAWIDE)
		return false;
	return s && (s->loss_pct >= 0.0f || s->bitrate_mbps >= 0.0f);
}

// Link quality on the same scale the aerials use: green is a link you can fly,
// amber is one to watch, red is one that is dropping frames.
static osd_color_t quality_colour(const osd_link_params_t *p, float pct) {
	if (pct >= 80.0f)
		return p->good;
	if (pct >= 50.0f)
		return p->warn;
	return p->crit;
}

// The header's pieces: what the radio is tuned to, and - in the ultrawide style,
// which is the point of it - the loss and the throughput alongside.
static void header_pieces(const osd_link_stats_t *st, osd_link_style_t style, char *tune,
	size_t tune_n, char *tail, size_t tail_n) {
	tune[0] = '\0';
	tail[0] = '\0';

	if (style == OSD_LINK_ULTRAWIDE) {
		size_t used = 0;
		if (st->loss_pct >= 0.0f)
			used += (size_t)snprintf(tail + used, tail_n - used, "%.1f%% lost", st->loss_pct);
		if (st->bitrate_mbps >= 0.0f)
			snprintf(tail + used, tail_n - used, "%s%.1f Mbps", used ? "   " : "",
				st->bitrate_mbps);
	}

	if (st->channel > 0 && st->freq_mhz > 0 && st->bandwidth_mhz > 0)
		snprintf(tune, tune_n, "CH%d  %d  %dMHz", st->channel, st->freq_mhz, st->bandwidth_mhz);
	else if (st->channel > 0 && st->freq_mhz > 0)
		snprintf(tune, tune_n, "CH%d  %d", st->channel, st->freq_mhz);
	else if (st->channel > 0)
		snprintf(tune, tune_n, "CH%d", st->channel);
	else if (st->freq_mhz > 0)
		snprintf(tune, tune_n, "%dMHz", st->freq_mhz);
}

// Everything the header would carry if it had room. What the ultrawide style
// sizes itself to.
static void header_full_text(
	const osd_link_stats_t *st, osd_link_style_t style, char *out, size_t n) {
	char tune[40], tail[48];
	header_pieces(st, style, tune, sizeof(tune), tail, sizeof(tail));
	if (tune[0] && tail[0])
		snprintf(out, n, "%s   %s", tune, tail);
	else
		snprintf(out, n, "%s%s", tune, tail);
}

void osd_link_measure(const osd_link_params_t *p, const osd_link_stats_t *s, osd_font_t *font,
	float *out_w, float *out_h) {
	if (!p || !out_w || !out_h)
		return;
	const float k = p->scale > 0.0f ? p->scale : 1.0f;
	const int n = p->show_antennas ? antenna_count(s) : 0;
	const float foot = has_footer(s, p->style) ? LINK_FOOT_H : 0.0f;
	const float lq = has_lq(s) ? LINK_LQ_H : 0.0f;

	if (p->style == OSD_LINK_ULTRAWIDE) {
		const float snr = (n > 0 && has_snr(s)) ? LINK_H_SNR : 0.0f;
		const float body = n > 0 ? (LINK_H_CAP + LINK_H_VAL + LINK_H_BAR + snr) : 0.0f;
		const float px = p->pad_x * k;

		// Sized to its header line rather than to a constant. A width picked for
		// the worst case - channel, bandwidth, loss and throughput all present -
		// leaves most of the strip empty in every other case, and empty strip is
		// covered video.
		//
		// The header text sits on the raised part of the plate, which starts a
		// third of the way across, so the width has to be scaled up by that
		// fraction rather than just padded.
		float need = LINK_ULTRA_MIN_W * k;
		if (font) {
			char full[96];
			header_full_text(s, p->style, full, sizeof(full));
			if (full[0]) {
				osd_text_metrics_t m = {0};
				osd_text_measure(font, p->label_size * 1.15f * k, full, &m);
				const float notch = p->square ? 1.0f : (1.0f - 0.34f);
				const float w_for_header = ((float)m.width + px * 2.0f) / notch;
				if (w_for_header > need)
					need = w_for_header;
			}
		}
		// The aerials, if shown, may want more than the header does.
		if (n > 0) {
			const float w_for_cols = ((float)n * LINK_COL_W + 24.0f) * k;
			if (w_for_cols > need)
				need = w_for_cols;
		}
		*out_w = need;
		*out_h = (LINK_HEAD_H + lq + body + LINK_BOTTOM_PAD) * k;
	} else if (p->style == OSD_LINK_HORIZONTAL) {
		// One column per antenna, with the header stacked above them so a wide
		// widget does not also have to be a tall one.
		const int cols = n > 0 ? n : 1;
		// With the aerials off there are no columns to size to, so the width
		// comes from the one row that is left.
		*out_w = (n > 0 ? ((float)cols * LINK_COL_W + 24.0f) : LINK_VERT_W + 60.0f) * k;
		const float snr = (n > 0 && has_snr(s)) ? LINK_H_SNR : 0.0f;
		const float body = n > 0 ? (LINK_H_CAP + LINK_H_VAL + LINK_H_BAR + snr) : 0.0f;
		const float tail = foot > 0.0f ? foot : LINK_BOTTOM_PAD;
		*out_h = (LINK_HEAD_H + lq + body + tail) * k;
	} else {
		const float tail = foot > 0.0f ? foot : LINK_BOTTOM_PAD;
		*out_w = LINK_VERT_W * k;
		*out_h = (LINK_HEAD_H + lq + (float)n * LINK_ROW_H + tail) * k;
	}
}

// The footer line, shared by both styles: whichever of the three numbers the
// ground station actually reports, in a fixed order so the eye can find them.
static void footer_text(const osd_link_stats_t *s, char *out, size_t n) {
	out[0] = '\0';
	size_t used = 0;
	if (s->loss_pct >= 0.0f)
		used += (size_t)snprintf(out + used, n - used, "%s%.1f%% lost", used ? "  " : "",
			s->loss_pct);
	if (s->bitrate_mbps >= 0.0f && used < n)
		snprintf(out + used, n - used, "%s%.1f Mbps", used ? "  " : "", s->bitrate_mbps);
}

// What the receiver is tuned to, right-aligned in the header band.
//
// Built longest-first and shortened until it fits: the vertical style is barely
// wider than four numbers, while the horizontal one has most of a raised plate
// doing nothing. Rather than pick one wording for both and have it clipped in
// the narrow case, the widest form that fits wins.
static void tuning_text(const osd_link_stats_t *st, osd_link_style_t style, osd_font_t *font,
	float size, float avail, char *out, size_t n) {
	out[0] = '\0';

	char tune[40], tail[48];
	header_pieces(st, style, tune, sizeof(tune), tail, sizeof(tail));

	char candidates[6][96];
	int count = 0;
	if (st->channel > 0 && st->freq_mhz > 0 && st->bandwidth_mhz > 0)
		snprintf(tune, sizeof(tune), "CH%d  %d  %dMHz", st->channel, st->freq_mhz,
			st->bandwidth_mhz);
	else if (st->channel > 0 && st->freq_mhz > 0)
		snprintf(tune, sizeof(tune), "CH%d  %d", st->channel, st->freq_mhz);
	else if (st->channel > 0)
		snprintf(tune, sizeof(tune), "CH%d", st->channel);
	else if (st->freq_mhz > 0)
		snprintf(tune, sizeof(tune), "%dMHz", st->freq_mhz);

	// Longest first, shedding a piece at a time. The bandwidth goes before the
	// frequency, and the frequency before the channel: of the three the channel
	// is the one a pilot actually acts on.
	if (tune[0] && tail[0])
		snprintf(candidates[count++], 96, "%s   %s", tune, tail);
	if (tune[0] && tail[0] && st->bandwidth_mhz > 0 && st->channel > 0 && st->freq_mhz > 0)
		snprintf(candidates[count++], 96, "CH%d  %d   %s", st->channel, st->freq_mhz, tail);
	if (tune[0] && tail[0] && st->channel > 0)
		snprintf(candidates[count++], 96, "CH%d   %s", st->channel, tail);
	if (tail[0])
		snprintf(candidates[count++], 96, "%s", tail);
	if (tune[0])
		snprintf(candidates[count++], 96, "%s", tune);
	if (st->channel > 0)
		snprintf(candidates[count++], 96, "CH%d", st->channel);
	if (count == 0)
		return;

	for (int i = 0; i < count; i++) {
		osd_text_metrics_t m = {0};
		osd_text_measure(font, size, candidates[i], &m);
		if ((float)m.width <= avail) {
			snprintf(out, n, "%s", candidates[i]);
			return;
		}
	}
	// Nothing fits; the shortest is still better than a clipped longer one.
	snprintf(out, n, "%s", candidates[count - 1]);
}

void osd_link_draw(osd_surface_t *s, osd_font_t *font, float x, float y,
	const osd_link_params_t *p, const osd_link_stats_t *st, bool stale) {
	if (!s || !p || !st)
		return;
	const float k = p->scale > 0.0f ? p->scale : 1.0f;
	const int n = p->show_antennas ? antenna_count(st) : 0;

	float w, h;
	osd_link_measure(p, st, font, &w, &h);

	bool prev_on;
	osd_color_t prev_col;
	int prev_px;
	osd_text_get_outline(&prev_on, &prev_col, &prev_px);
	if (p->outline)
		osd_text_set_outline(true, p->outline_color, (int)(p->outline_px + 0.5f));

	const float tab = LINK_HEAD_H * k * 0.82f;
	const float ch = p->chamfer * k;
	plate(s, x, y, w, h, tab, ch, p->square, p->fill, p->edge, p->accent);

	const float px = p->pad_x * k;
	const float label_sz = p->label_size * k;
	const float value_sz = p->value_size * k;

	// Header: who is reporting, and a stale marker when they have stopped.
	const float head_baseline = y + label_sz + 8.0f * k;
	osd_text_draw_tracked(s, font, label_sz, (int)(x + px), (int)head_baseline,
		stale ? "LINK  NO DATA" : st->source, p->label_tracking * k, stale ? p->crit : p->label);

	// What the receiver is tuned to, on the raised part of the plate. The
	// source caption sits in the stepped corner to the left of it, so the space
	// available starts after the step rather than at the plate's edge.
	{
		// Without the notch there is no raised part to start after: the caption
		// and the tuning share one line across the whole plate.
		const float step = p->square ? 0.0f : w * 0.34f;
		const float tune_sz = label_sz * 1.15f;
		char tune[96];
		tuning_text(st, p->style, font, tune_sz, w - step - px * 2.0f, tune, sizeof(tune));
		if (tune[0]) {
			osd_text_metrics_t m = {0};
			osd_text_measure(font, tune_sz, tune, &m);
			osd_text_draw(s, font, tune_sz, (int)(x + w - px - (float)m.width),
				(int)(head_baseline + 2.0f * k), tune, p->accent);
		}
	}

	const float bar_h = p->bar_height * k * 0.62f;

	// Link quality, above the aerials in both styles. A wide bar, because this
	// is the number you glance at rather than read.
	float body_top = y + LINK_HEAD_H * k;
	if (has_lq(st)) {
		const float row_h = LINK_LQ_H * k;
		const osd_color_t c = quality_colour(p, st->quality_pct);
		char t[16];

		osd_text_draw_tracked(s, font, label_sz, (int)(x + px),
			(int)(body_top + row_h * 0.5f + label_sz * 0.36f), "LQ", p->label_tracking * k,
			p->label);

		snprintf(t, sizeof(t), "%.0f%%", st->quality_pct);
		osd_text_metrics_t m = {0};
		osd_text_measure(font, value_sz * 0.72f, t, &m);
		const float val_x = x + px + 34.0f * k;
		osd_text_draw(s, font, value_sz * 0.72f, (int)val_x,
			(int)(body_top + row_h * 0.5f + value_sz * 0.72f * 0.36f), t, c);

		const float bar_x = val_x + (float)m.width + 12.0f * k;
		const float bw = x + w - px - bar_x;
		const float by = body_top + row_h * 0.5f - bar_h * 0.5f;
		if (bw > 10.0f) {
			float frac = st->quality_pct / 100.0f;
			frac = frac < 0.0f ? 0.0f : (frac > 1.0f ? 1.0f : frac);
			osd_fill_rect(s, (int)bar_x, (int)by, (int)bw, (int)bar_h, p->track);
			osd_fill_rect(s, (int)bar_x, (int)by, (int)(bw * frac), (int)bar_h, c);
		}
		body_top += row_h;
	}

	if (n > 0 && p->style != OSD_LINK_VERTICAL) {
		const float col_w = LINK_COL_W * k;
		// Four bands per column: caption, the dBm number, its bar, SNR. Each
		// baseline sits inside its own band rather than on its lower edge -
		// hanging text off a band's bottom left the descenders below the plate
		// whenever nothing followed to cover for it.
		const float cap_top = body_top;
		const float val_top = cap_top + LINK_H_CAP * k;
		const float bar_top = val_top + LINK_H_VAL * k;
		const float snr_top = bar_top + LINK_H_BAR * k;
		const float cap_y = cap_top + LINK_H_CAP * k * 0.78f;
		const float val_y = val_top + LINK_H_VAL * k * 0.80f;
		const float bar_y = bar_top + (LINK_H_BAR * k - bar_h) * 0.5f;
		const float snr_y = snr_top + LINK_H_SNR * k * 0.5f + label_sz * 1.15f * 0.36f;
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
	} else if (n > 0) {
		const float row_h = LINK_ROW_H * k;
		for (int i = 0; i < n; i++) {
			const float ry = body_top + (float)i * row_h;
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

	if (has_footer(st, p->style)) {
		char t[48];
		footer_text(st, t, sizeof(t));
		// Centred in the footer band rather than measured up from the bottom
		// edge: pad_y is small next to this text, so hanging it off the edge put
		// the descenders almost on the plate's border.
		const float band_top = y + h - LINK_FOOT_H * k;
		const float baseline = band_top + LINK_FOOT_H * k * 0.5f + label_sz * 1.25f * 0.36f;
		osd_text_draw(s, font, label_sz * 1.25f, (int)(x + px), (int)baseline, t, p->accent);
	}

	osd_text_set_outline(prev_on, prev_col, prev_px);
}

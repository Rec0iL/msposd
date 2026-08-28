#include "osd_widgets.h"

#include <stdio.h>
#include <string.h>

void osd_widgets_state_init(osd_widget_state_t *st) {
	if (!st)
		return;
	memset(st, 0, sizeof(*st));
}

void osd_widgets_update_arm(osd_widget_state_t *st, bool armed) {
	if (!st)
		return;
	if (armed && !st->prev_armed) {
		st->current_peak = 0.0f; // new flight, new peak
		st->cell_count = 0;      // re-infer at arming, in case the pack changed
	}
	st->prev_armed = armed;
}

// A LiPo cell rests between ~3.0V and ~4.35V, so dividing by 3.8 and rounding
// identifies the count unambiguously for every common pack: 16.7V/3.8 = 4.4 -> 4S.
static int infer_cell_count(float pack_voltage) {
	if (pack_voltage < 2.0f)
		return 0;
	int cells = (int)((pack_voltage / 3.8f) + 0.5f);
	if (cells < 1)
		cells = 1;
	if (cells > 12)
		cells = 12;
	return cells;
}

static const char *label_for(const osd_element_t *e) {
	switch (e->type) {
	case OSD_ELEM_VOLTAGE:
		if (e->has_battery_icon)
			return "BATTERY";
		return e->is_per_cell ? "CELL VOLTAGE" : "PACK VOLTAGE";
	case OSD_ELEM_SATS: return ""; // icon carries the meaning, no word needed
	case OSD_ELEM_THROTTLE: return "THROTTLE";
	case OSD_ELEM_FLIGHT_TIME: return "FLIGHT TIME";
	case OSD_ELEM_FLIGHT_MODE: return "MODE";
	case OSD_ELEM_WARNING: return ""; // the message is the content
	case OSD_ELEM_CURRENT: return "CURRENT DRAW";
	case OSD_ELEM_MAH: return "CAPACITY USED";
	case OSD_ELEM_ALTITUDE: return "ALTITUDE";
	case OSD_ELEM_RSSI: return "SIGNAL";
	default: return "";
	}
}

// Bar fill as a fraction, or -1 when the value has no meaningful range and the
// widget should render as a plain value panel.
static float fill_fraction(const osd_element_t *e, const osd_theme_t *th, float peak, int cells) {
	switch (e->type) {
	case OSD_ELEM_THROTTLE: {
		float f = e->value / 100.0f;
		return f < 0.0f ? 0.0f : (f > 1.0f ? 1.0f : f);
	}
	case OSD_ELEM_VOLTAGE: {
		// With a battery icon we know it is pack voltage, and the inferred cell
		// count turns it into a per-cell figure and therefore a percentage.
		if (e->has_battery_icon && cells > 0) {
			float per_cell = e->value / (float)cells;
			float span = th->cell_max - th->cell_min;
			if (span <= 0.0f)
				return -1.0f;
			float f = (per_cell - th->cell_min) / span;
			return f < 0.0f ? 0.0f : (f > 1.0f ? 1.0f : f);
		}
		if (!e->is_per_cell)
			return -1.0f; // pack voltage with no cell count has no fixed range
		float span = th->cell_max - th->cell_min;
		if (span <= 0.0f)
			return -1.0f;
		float f = (e->value - th->cell_min) / span;
		return f < 0.0f ? 0.0f : (f > 1.0f ? 1.0f : f);
	}
	case OSD_ELEM_CURRENT: {
		if (peak <= 0.0f)
			return 0.0f;
		float f = e->value / peak;
		return f < 0.0f ? 0.0f : (f > 1.0f ? 1.0f : f);
	}
	case OSD_ELEM_RSSI: {
		float f = e->value / 100.0f;
		return f < 0.0f ? 0.0f : (f > 1.0f ? 1.0f : f);
	}
	default: return -1.0f;
	}
}

static osd_color_t state_color(const osd_element_t *e, const osd_theme_t *th, int cells) {
	if (e->type == OSD_ELEM_VOLTAGE) {
		float per_cell = -1.0f;
		if (e->is_per_cell)
			per_cell = e->value;
		else if (e->has_battery_icon && cells > 0)
			per_cell = e->value / (float)cells;
		if (per_cell > 0.0f) {
			if (per_cell < th->cell_crit)
				return th->crit;
			if (per_cell < th->cell_warn)
				return th->warn;
		}
	}
	if (e->type == OSD_ELEM_WARNING) {
		// Severity is decided when the message is recognised, not by the theme:
		// a theme must not be able to make a failsafe look routine.
		if (e->severity == OSD_SEV_CRIT)
			return th->crit;
		if (e->severity == OSD_SEV_WARN)
			return th->warn;
		return th->accent;
	}
	if (e->type == OSD_ELEM_SATS) {
		if (e->value < 6.0f)
			return th->crit;
		if (e->value < 10.0f)
			return th->warn;
	}
	if (e->type == OSD_ELEM_RSSI) {
		if (e->value < 30.0f)
			return th->crit;
		if (e->value < 55.0f)
			return th->warn;
	}
	return th->accent;
}

// "20A" then "/67A" for current; a single string for everything else.
static void format_value(const osd_element_t *e, float peak, int cells, char *main, size_t mn,
	char *sub, size_t sn) {
	sub[0] = '\0';
	switch (e->type) {
	case OSD_ELEM_VOLTAGE:
		snprintf(main, mn, "%.2fV", e->value);
		// Show the inferred pack size so the percentage is auditable rather
		// than a number the user has to trust blindly.
		if (e->has_battery_icon && cells > 0)
			snprintf(sub, sn, " %dS", cells);
		break;
	case OSD_ELEM_SATS: snprintf(main, mn, "%.0f", e->value); break;
	case OSD_ELEM_THROTTLE: snprintf(main, mn, "%.0f%%", e->value); break;
	case OSD_ELEM_FLIGHT_TIME:
	case OSD_ELEM_FLIGHT_MODE:
	case OSD_ELEM_WARNING: snprintf(main, mn, "%s", e->text); break;
	case OSD_ELEM_CURRENT:
		snprintf(main, mn, "%.0fA", e->value);
		snprintf(sub, sn, "/%.0fA", peak);
		break;
	case OSD_ELEM_ALTITUDE: snprintf(main, mn, "%.0fM", e->value); break;
	case OSD_ELEM_MAH: snprintf(main, mn, "%.0f", e->value); break;
	case OSD_ELEM_RSSI: snprintf(main, mn, "%.0f%%", e->value); break;
	default: snprintf(main, mn, "%s", e->text); break;
	}
}

// A satellite: dish plus signal arcs. Replaces the word "SATELLITES", which
// says nothing the icon does not.
static void draw_sat_icon(osd_surface_t *s, float x, float y, float size, osd_color_t c) {
	const float w = 2.0f;
	osd_draw_line(s, x, y + size * 0.45f, x + size * 0.5f, y + size * 0.45f, w, c);
	osd_draw_line(s, x + size * 0.25f, y + size * 0.45f, x + size * 0.25f, y + size * 0.1f, w, c);
	osd_draw_line(s, x + size * 0.05f, y + size * 0.1f, x + size * 0.45f, y + size * 0.1f, w, c);
	// signal arcs
	osd_draw_line(s, x + size * 0.6f, y + size * 0.3f, x + size * 0.78f, y + size * 0.12f, w, c);
	osd_draw_line(s, x + size * 0.62f, y + size * 0.5f, x + size * 0.92f, y + size * 0.2f, w, c);
}

// Flight modes arrive as bare words - the firmware sends no glyph for them - so
// the icon is drawn here rather than recovered from the font.
static void draw_mode_icon(osd_surface_t *s, const char *mode, float x, float y, float size,
	osd_color_t c) {
	const float h = size * 0.5f;
	const float w = 2.0f;

	if (!strcmp(mode, "RTH")) { // house: return to home
		osd_draw_line(s, x, y, x + h, y - h * 0.8f, w, c);
		osd_draw_line(s, x + h, y - h * 0.8f, x + size, y, w, c);
		osd_draw_line(s, x + size * 0.18f, y, x + size * 0.18f, y + h * 0.8f, w, c);
		osd_draw_line(s, x + size * 0.82f, y, x + size * 0.82f, y + h * 0.8f, w, c);
		osd_draw_line(s, x + size * 0.18f, y + h * 0.8f, x + size * 0.82f, y + h * 0.8f, w, c);
	} else if (!strcmp(mode, "ANGL") || !strcmp(mode, "ANGLE") || !strcmp(mode, "HOR") ||
			   !strcmp(mode, "HORIZON") || !strcmp(mode, "HORZ")) { // levelled horizon
		osd_draw_line(s, x, y, x + size * 0.38f, y, w, c);
		osd_draw_line(s, x + size * 0.62f, y, x + size, y, w, c);
		osd_draw_line(s, x + size * 0.44f, y, x + size * 0.56f, y, w, c);
	} else if (!strcmp(mode, "POSHOLD") || !strcmp(mode, "PH")) { // crosshair: hold position
		osd_draw_line(s, x + h, y - h * 0.7f, x + h, y + h * 0.7f, w, c);
		osd_draw_line(s, x, y, x + size, y, w, c);
	} else if (!strcmp(mode, "CRUZ") || !strcmp(mode, "CRUISE") || !strcmp(mode, "CRS") ||
			   !strcmp(mode, "3CRS")) { // arrow: hold a heading
		osd_draw_line(s, x, y, x + size, y, w, c);
		osd_draw_line(s, x + size * 0.62f, y - h * 0.45f, x + size, y, w, c);
		osd_draw_line(s, x + size * 0.62f, y + h * 0.45f, x + size, y, w, c);
	} else if (!strcmp(mode, "ALTHOLD") || !strcmp(mode, "AH")) { // lock an altitude
		osd_draw_line(s, x, y, x + size, y, w, c);
		osd_draw_line(s, x + h, y - h * 0.8f, x + h, y - h * 0.1f, w, c);
		osd_draw_line(s, x + h * 0.6f, y - h * 0.45f, x + h, y - h * 0.8f, w, c);
		osd_draw_line(s, x + h * 1.4f, y - h * 0.45f, x + h, y - h * 0.8f, w, c);
	} else if (!strcmp(mode, "WP")) { // waypoint flag
		osd_draw_line(s, x + size * 0.2f, y - h * 0.8f, x + size * 0.2f, y + h * 0.8f, w, c);
		osd_draw_line(s, x + size * 0.2f, y - h * 0.8f, x + size * 0.9f, y - h * 0.45f, w, c);
		osd_draw_line(s, x + size * 0.9f, y - h * 0.45f, x + size * 0.2f, y - h * 0.1f, w, c);
	} else if (!strcmp(mode, "LAUNCH")) { // climbing launch arrow
		osd_draw_line(s, x, y + h * 0.7f, x + size, y - h * 0.7f, w, c);
		osd_draw_line(s, x + size * 0.55f, y - h * 0.7f, x + size, y - h * 0.7f, w, c);
		osd_draw_line(s, x + size, y - h * 0.7f, x + size, y - h * 0.15f, w, c);
	} else if (!strcmp(mode, "MANU") || !strcmp(mode, "MANUAL")) { // a stick, moved by hand
		osd_draw_line(s, x + h, y + h * 0.8f, x + h, y - h * 0.4f, w, c);
		osd_draw_line(s, x + h * 0.55f, y - h * 0.7f, x + h * 1.45f, y - h * 0.7f, w, c);
		osd_draw_line(s, x, y + h * 0.8f, x + size, y + h * 0.8f, w, c);
	} else if (!strcmp(mode, "AIR")) { // airflow over a wing
		osd_draw_line(s, x, y - h * 0.4f, x + size * 0.75f, y - h * 0.4f, w, c);
		osd_draw_line(s, x + size * 0.25f, y + h * 0.15f, x + size, y + h * 0.15f, w, c);
		osd_draw_line(s, x, y + h * 0.7f, x + size * 0.6f, y + h * 0.7f, w, c);
	} else if (!strcmp(mode, "HOLD")) { // pause bars
		osd_draw_line(s, x + size * 0.3f, y - h * 0.7f, x + size * 0.3f, y + h * 0.7f, w * 1.6f, c);
		osd_draw_line(s, x + size * 0.7f, y - h * 0.7f, x + size * 0.7f, y + h * 0.7f, w * 1.6f, c);
	} else if (!strcmp(mode, "FS") || !strcmp(mode, "FAILSAFE")) { // warning triangle
		osd_draw_line(s, x + h, y - h * 0.8f, x, y + h * 0.7f, w, c);
		osd_draw_line(s, x, y + h * 0.7f, x + size, y + h * 0.7f, w, c);
		osd_draw_line(s, x + size, y + h * 0.7f, x + h, y - h * 0.8f, w, c);
	} else if (!strcmp(mode, "ACRO")) { // ">>" : rates, unrestricted
		osd_draw_line(s, x, y - h * 0.55f, x + size * 0.38f, y, w, c);
		osd_draw_line(s, x + size * 0.38f, y, x, y + h * 0.55f, w, c);
		osd_draw_line(s, x + size * 0.52f, y - h * 0.55f, x + size * 0.9f, y, w, c);
		osd_draw_line(s, x + size * 0.9f, y, x + size * 0.52f, y + h * 0.55f, w, c);
	} else { // any mode added to the word list without an icon yet
		osd_draw_line(s, x + size * 0.2f, y - h * 0.55f, x + size * 0.6f, y, w, c);
		osd_draw_line(s, x + size * 0.6f, y, x + size * 0.2f, y + h * 0.55f, w, c);
	}
}

// Tab step top-left, square top-right so the value can sit there, chamfer
// bottom-right. See design notes: chamfers must never cross the value's box.
static void panel_path(osd_pointf_t *p, float x, float y, float w, float h, float tab, float ch) {
	float step = w * 0.34f;
	p[0] = (osd_pointf_t){x, y + tab};
	p[1] = (osd_pointf_t){x + step - 28.0f, y + tab};
	p[2] = (osd_pointf_t){x + step, y};
	p[3] = (osd_pointf_t){x + w, y};
	p[4] = (osd_pointf_t){x + w, y + h - ch};
	p[5] = (osd_pointf_t){x + w - ch, y + h};
	p[6] = (osd_pointf_t){x, y + h};
}

// Panel size, shared by the collision pass and the drawing pass so the
// rectangle reserved for a widget is exactly the one painted.
static void measure_panel(const osd_theme_t *th, osd_font_t *font, const osd_element_t *e,
	float peak, int cells, float scale, float *out_w, float *out_h) {
	char main_txt[24], sub_txt[16];
	format_value(e, peak, cells, main_txt, sizeof(main_txt), sub_txt, sizeof(sub_txt));

	osd_text_metrics_t mm = {0}, sm = {0};
	osd_text_measure(font, th->value_size * scale, main_txt, &mm);
	if (sub_txt[0])
		osd_text_measure(font, th->value_size * 0.69f * scale, sub_txt, &sm);

	float need = (float)(mm.width + sm.width) + th->pad_x * scale * 2.0f + 140.0f * scale;
	float min_w = th->panel_min_width * scale;
	*out_w = need > min_w ? need : min_w;

	float frac = fill_fraction(e, th, peak, cells);
	bool has_label = label_for(e)[0] != '\0';
	if (e->type == OSD_ELEM_WARNING) {
		// A message is a line of text, not a reading: size the banner to it
		// rather than to a fixed panel, so long messages are never clipped.
		*out_w = (float)mm.width + th->pad_x * scale * 3.0f;
		*out_h = th->value_size * scale * 1.6f;
		return;
	}
	if (frac >= 0.0f)
		*out_h = th->panel_height * scale;
	else if (has_label)
		*out_h = th->tab_height * scale + th->label_size * scale + th->pad_y * scale * 2.0f;
	else // icon-only: just the tab, nothing below it to make room for
		*out_h = th->tab_height * scale + th->pad_y * scale;
}

static void draw_one(osd_surface_t *s, const osd_theme_t *th, osd_font_t *font,
	const osd_element_t *e, float peak, int cells, float px, float py, float opacity, float scale) {
	char main_txt[24], sub_txt[16];
	format_value(e, peak, cells, main_txt, sizeof(main_txt), sub_txt, sizeof(sub_txt));

	// Every dimension scales together, so a resized widget keeps its
	// proportions rather than just growing its text out of the panel.
	const float value_size = th->value_size * scale;
	const float label_size = th->label_size * scale;
	const float label_tracking = th->label_tracking * scale;
	const float tab_h = th->tab_height * scale;
	const float chamfer = th->chamfer * scale;
	const float pad_x = th->pad_x * scale;
	const float pad_y = th->pad_y * scale;
	const float bar_h = th->bar_height * scale;
	const float hatch_period = th->hatch_period * scale;

	osd_text_metrics_t mm = {0}, sm = {0};
	osd_text_measure(font, value_size, main_txt, &mm);
	if (sub_txt[0])
		osd_text_measure(font, value_size * 0.69f, sub_txt, &sm);

	// Panel width follows the measured value: "92" and "20A/67A" differ a lot,
	// and a fixed width would clip one or leave the other floating.
	float text_w = (float)(mm.width + sm.width);
	// Values with no meaningful range get no bar, so they must not reserve its
	// height - otherwise the panel is mostly empty space.
	float frac = fill_fraction(e, th, peak, cells);
	float w, h;
	measure_panel(th, font, e, peak, cells, scale, &w, &h);
	(void)text_w;

	osd_color_t accent = osd_theme_apply_opacity(state_color(e, th, cells), opacity);
	osd_color_t fill = osd_theme_apply_opacity(th->panel_fill, opacity);
	osd_color_t edge = osd_theme_apply_opacity(th->panel_edge, opacity);
	osd_color_t track = osd_theme_apply_opacity(th->track, opacity);
	osd_color_t label = osd_theme_apply_opacity(th->label, opacity);
	osd_color_t peakc = osd_theme_apply_opacity(th->peak, opacity);

	// The widget owns its rectangle: clear it so the glyphs it replaces do not
	// show through the translucent backdrop.
	osd_clear_rect(s, (int)px, (int)py, (int)w, (int)h);

	if (e->type == OSD_ELEM_WARNING) {
		// Banner: a solid severity bar on the left, message beside it. Deliberately
		// plainer than a reading panel so it reads instantly.
		osd_fill_rect(s, (int)px, (int)py, (int)w, (int)h, fill);
		osd_fill_rect(s, (int)px, (int)py, (int)(5.0f * scale), (int)h, accent);
		osd_draw_line(s, px, py, px + w, py, 1.0f, edge);
		osd_draw_line(s, px, py + h, px + w, py + h, 1.0f, edge);
		osd_text_draw(s, font, value_size, (int)(px + pad_x * 1.6f),
			(int)(py + h * 0.5f + value_size * 0.36f), main_txt, accent);
		return;
	}

	osd_pointf_t poly[7];
	panel_path(poly, px, py, w, h, tab_h, chamfer);
	osd_fill_poly(s, poly, 7, fill);
	osd_stroke_poly(s, poly, 7, 1.5f, edge);

	// accent corner marks
	osd_draw_line(s, px, py + h - 18.0f * scale, px, py + h, 2.5f, accent);
	osd_draw_line(s, px, py + h, px + 26.0f * scale, py + h, 2.5f, accent);
	osd_draw_line(s, px + w, py + h - chamfer - 12.0f * scale, px + w, py + h - chamfer, 2.5f, accent);
	osd_draw_line(s, px + w, py + h - chamfer, px + w - chamfer, py + h, 2.5f, accent);

	// value, right aligned inside the tab
	int right = (int)(px + w - pad_x);
	int baseline = (int)(py + tab_h * 0.75f);
	if (sub_txt[0]) {
		int x0 = right - mm.width - sm.width;
		osd_text_draw(s, font, value_size, x0, baseline, main_txt, accent);
		osd_text_draw(s, font, value_size * 0.69f, x0 + mm.width, baseline, sub_txt, peakc);
	} else {
		osd_text_draw(s, font, value_size, right - mm.width, baseline, main_txt, accent);
	}

	if (e->type == OSD_ELEM_FLIGHT_MODE)
		draw_mode_icon(s, e->text, px + pad_x, py + tab_h * 0.5f, tab_h * 0.5f, accent);
	else if (e->type == OSD_ELEM_SATS)
		draw_sat_icon(s, px + pad_x, py + tab_h * 0.25f, tab_h * 0.55f, accent);

	if (label_for(e)[0] != '\0')
		osd_text_draw_tracked(s, font, label_size, (int)(px + pad_x),
			(int)(py + tab_h + label_size + 12.0f * scale), label_for(e), label_tracking, label);

	// bar
	if (frac >= 0.0f) {
		int bx = (int)(px + pad_x);
		int by = (int)(py + h - pad_y - bar_h);
		int bw = (int)(w - pad_x * 2.0f);
		int bh = (int)bar_h;
		osd_fill_rect(s, bx, by, bw, bh, track);

		int fw = (int)((float)bw * frac);
		if (fw > 0) {
			if (e->type == OSD_ELEM_CURRENT) {
				// Gradient spans the whole scale, so a low draw reads green
				// rather than showing the red end of the ramp.
				const osd_color_t stops[4] = {
					osd_theme_apply_opacity(th->good, opacity),
					osd_theme_apply_opacity(OSD_ARGB(0xFF, 0xC8, 0xF0, 0x00), opacity),
					osd_theme_apply_opacity(th->warn, opacity),
					osd_theme_apply_opacity(th->crit, opacity)};
				const float offs[4] = {0.0f, 0.45f, 0.72f, 1.0f};
				osd_fill_rect_hatched_gradient(s, bx, by, fw, bh, bw, stops, offs, 4,
					OSD_ARGB(0x8C, 0x04, 0x14, 0x0A), hatch_period, th->hatch_duty,
					th->hatch_slant);
				osd_draw_line(s, (float)(bx + bw), (float)by - 4.0f, (float)(bx + bw),
					(float)(by + bh + 4.0f), 2.0f, peakc);
			} else {
				osd_fill_rect_hatched(s, bx, by, fw, bh, accent,
					OSD_ARGB(0x8C, 0x0B, 0x1A, 0x24), hatch_period, th->hatch_duty,
					th->hatch_slant);
			}
		}
	}
}

// Pixel rectangle of a cell run, via the host's mapping when it provides one.
static void cell_rect(const osd_grid_t *g, int col, int row, int span, int *x, int *y, int *w,
	int *h) {
	if (g->cell_rect) {
		g->cell_rect(col, row, span, g->ctx, x, y, w, h);
		return;
	}
	*x = g->off_x + col * g->cell_w;
	*y = g->off_y + row * g->cell_h;
	*w = span * g->cell_w;
	*h = g->cell_h;
}

// Axis-aligned rectangle overlap.
static bool rects_overlap(float ax, float ay, float aw, float ah, float bx, float by, float bw,
	float bh) {
	return ax < bx + bw && bx < ax + aw && ay < by + bh && by < ay + ah;
}

int osd_widgets_draw_all(osd_surface_t *s, const osd_theme_t *th, osd_font_t *font,
	osd_widget_state_t *st, const osd_element_t *els, int count, const osd_grid_t *grid,
	uint64_t now_ms) {
	if (!s || !th || !font || !st || !els || !grid)
		return 0;
	if (th->mode != OSD_MODE_FANCY)
		return 0;

	// Track the peak before drawing so the bar and the "/67A" agree this frame.
	for (int i = 0; i < count; i++) {
		if (els[i].type == OSD_ELEM_CURRENT && els[i].value_valid && els[i].value > st->current_peak)
			st->current_peak = els[i].value;
		// Infer pack size once, from the first battery reading we see.
		if (st->cell_count == 0 && els[i].type == OSD_ELEM_VOLTAGE && els[i].has_battery_icon &&
			els[i].value_valid)
			st->cell_count = infer_cell_count(els[i].value);
	}

	// Refresh the cache with everything visible this frame.
	for (int i = 0; i < count; i++) {
		const osd_element_t *e = &els[i];
		if (e->type > OSD_ELEM_NONE && e->type < OSD_ELEM_TYPE_COUNT) {
			st->last[e->type] = *e;
			st->last_seen_ms[e->type] = now_ms;
		}
	}

	// Build the draw list from the cache, so an element that blinked out is
	// still drawn until its hold expires.
	osd_element_t list[OSD_ELEM_TYPE_COUNT];
	int n = 0;
	for (int ty = 1; ty < OSD_ELEM_TYPE_COUNT; ty++) {
		if (st->last_seen_ms[ty] == 0)
			continue;
		if ((float)(now_ms - st->last_seen_ms[ty]) > th->element_hold_ms)
			continue;
		const osd_element_t *e = &st->last[ty];
		bool textual = (e->type == OSD_ELEM_FLIGHT_TIME || e->type == OSD_ELEM_FLIGHT_MODE);
		if ((!e->value_valid && !textual) || !osd_theme_element_enabled(th, e->type))
			continue;
		if (osd_theme_element_opacity(th, e->type) <= 0.01f)
			continue;
		list[n++] = *e;
	}

	// Place top-to-bottom, left-to-right, and push any panel that would collide
	// clear of the ones already placed. Flight controller layouts stack elements
	// a single row apart, while a panel is several rows tall, so overlaps are
	// the norm rather than the exception; without this the panels bury each
	// other. Order is preserved, so a vertical list of elements stays a
	// vertical list of panels.
	for (int i = 1; i < n; i++) {
		osd_element_t key = list[i];
		int j = i - 1;
		while (j >= 0 && (list[j].row > key.row ||
							 (list[j].row == key.row && list[j].col > key.col))) {
			list[j + 1] = list[j];
			j--;
		}
		list[j + 1] = key;
	}

	// Blank the glyphs of every element we are replacing, before any panel is
	// painted. This has to be keyed on the element's own grid cells, not on the
	// panel: collision avoidance can push a panel well away from its element,
	// and clearing only under the panel would leave the original text on screen
	// beside the widget that replaced it.
	for (int i = 0; i < n; i++) {
		const osd_element_t *e = &list[i];
		int cx, cy, cw, ch;
		cell_rect(grid, e->col, e->row, e->width, &cx, &cy, &cw, &ch);
		osd_clear_rect(s, cx, cy, cw, ch);
	}

	float placed_x[OSD_ELEM_TYPE_COUNT], placed_y[OSD_ELEM_TYPE_COUNT];
	float placed_w[OSD_ELEM_TYPE_COUNT], placed_h[OSD_ELEM_TYPE_COUNT];
	int placed = 0;
	int drawn = 0;

	for (int i = 0; i < n; i++) {
		const osd_element_t *e = &list[i];
		float opacity = osd_theme_element_opacity(th, e->type);
		float scale = osd_theme_element_scale(th, e->type);

		int cx, cy, cw, ch;
		cell_rect(grid, e->col, e->row, e->width, &cx, &cy, &cw, &ch);
		float px = (float)cx;
		float py = (float)cy;
		float w, h;
		measure_panel(th, font, e, st->current_peak, st->cell_count, scale, &w, &h);

		for (int attempt = 0; attempt < OSD_ELEM_TYPE_COUNT; attempt++) {
			bool clash = false;
			for (int k = 0; k < placed; k++) {
				if (rects_overlap(px, py, w, h, placed_x[k], placed_y[k], placed_w[k], placed_h[k])) {
					py = placed_y[k] + placed_h[k] + 6.0f * scale; // drop below it
					clash = true;
					st->overlap_warnings++;
					break;
				}
			}
			if (!clash)
				break;
		}
		// Keep the panel on screen. An element in column 0 or on the last row
		// would otherwise put half its widget outside the viewport - the grid
		// position is where the *value* was, not where a whole panel fits.
		if (px + w > (float)s->width)
			px = (float)s->width - w;
		if (px < 0.0f)
			px = 0.0f;
		if (py + h > (float)s->height)
			py = (float)s->height - h;
		if (py < 0.0f)
			py = 0.0f;

		draw_one(s, th, font, e, st->current_peak, st->cell_count, px, py, opacity, scale);

		placed_x[placed] = px;
		placed_y[placed] = py;
		placed_w[placed] = w;
		placed_h[placed] = h;
		placed++;
		drawn++;
	}
	return drawn;
}

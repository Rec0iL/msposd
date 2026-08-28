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
	if (armed && !st->prev_armed)
		st->current_peak = 0.0f; // new flight, new peak
	st->prev_armed = armed;
}

static const char *label_for(const osd_element_t *e) {
	switch (e->type) {
	case OSD_ELEM_VOLTAGE: return e->is_per_cell ? "CELL VOLTAGE" : "PACK VOLTAGE";
	case OSD_ELEM_CURRENT: return "CURRENT DRAW";
	case OSD_ELEM_MAH: return "CAPACITY USED";
	case OSD_ELEM_ALTITUDE: return "ALTITUDE";
	case OSD_ELEM_RSSI: return "SIGNAL";
	default: return "";
	}
}

// Bar fill as a fraction, or -1 when the value has no meaningful range and the
// widget should render as a plain value panel.
static float fill_fraction(const osd_element_t *e, const osd_theme_t *th, float peak) {
	switch (e->type) {
	case OSD_ELEM_VOLTAGE: {
		if (!e->is_per_cell)
			return -1.0f; // pack voltage has no fixed range without cell count
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

static osd_color_t state_color(const osd_element_t *e, const osd_theme_t *th) {
	if (e->type == OSD_ELEM_VOLTAGE && e->is_per_cell) {
		if (e->value < th->cell_crit)
			return th->crit;
		if (e->value < th->cell_warn)
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
static void format_value(const osd_element_t *e, float peak, char *main, size_t mn, char *sub, size_t sn) {
	sub[0] = '\0';
	switch (e->type) {
	case OSD_ELEM_VOLTAGE: snprintf(main, mn, "%.2fV", e->value); break;
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

static void draw_one(osd_surface_t *s, const osd_theme_t *th, osd_font_t *font,
	const osd_element_t *e, float peak, float px, float py, float opacity) {
	char main_txt[24], sub_txt[16];
	format_value(e, peak, main_txt, sizeof(main_txt), sub_txt, sizeof(sub_txt));

	osd_text_metrics_t mm = {0}, sm = {0};
	osd_text_measure(font, th->value_size, main_txt, &mm);
	if (sub_txt[0])
		osd_text_measure(font, th->value_size * 0.69f, sub_txt, &sm);

	// Panel width follows the measured value: "92" and "20A/67A" differ a lot,
	// and a fixed width would clip one or leave the other floating.
	float text_w = (float)(mm.width + sm.width);
	float need = text_w + th->pad_x * 2.0f + 140.0f;
	float w = need > th->panel_min_width ? need : th->panel_min_width;

	// Values with no meaningful range get no bar, so they must not reserve its
	// height - otherwise the panel is mostly empty space.
	float frac = fill_fraction(e, th, peak);
	float h = th->panel_height;
	if (frac < 0.0f)
		h = th->tab_height + th->label_size + th->pad_y * 2.0f;

	osd_color_t accent = osd_theme_apply_opacity(state_color(e, th), opacity);
	osd_color_t fill = osd_theme_apply_opacity(th->panel_fill, opacity);
	osd_color_t edge = osd_theme_apply_opacity(th->panel_edge, opacity);
	osd_color_t track = osd_theme_apply_opacity(th->track, opacity);
	osd_color_t label = osd_theme_apply_opacity(th->label, opacity);
	osd_color_t peakc = osd_theme_apply_opacity(th->peak, opacity);

	// The widget owns its rectangle: clear it so the glyphs it replaces do not
	// show through the translucent backdrop.
	osd_clear_rect(s, (int)px, (int)py, (int)w, (int)h);

	osd_pointf_t poly[7];
	panel_path(poly, px, py, w, h, th->tab_height, th->chamfer);
	osd_fill_poly(s, poly, 7, fill);
	osd_stroke_poly(s, poly, 7, 1.5f, edge);

	// accent corner marks
	osd_draw_line(s, px, py + h - 18.0f, px, py + h, 2.5f, accent);
	osd_draw_line(s, px, py + h, px + 26.0f, py + h, 2.5f, accent);
	osd_draw_line(s, px + w, py + h - th->chamfer - 12.0f, px + w, py + h - th->chamfer, 2.5f, accent);
	osd_draw_line(s, px + w, py + h - th->chamfer, px + w - th->chamfer, py + h, 2.5f, accent);

	// value, right aligned inside the tab
	int right = (int)(px + w - th->pad_x);
	int baseline = (int)(py + th->tab_height * 0.75f);
	if (sub_txt[0]) {
		int x0 = right - mm.width - sm.width;
		osd_text_draw(s, font, th->value_size, x0, baseline, main_txt, accent);
		osd_text_draw(s, font, th->value_size * 0.69f, x0 + mm.width, baseline, sub_txt, peakc);
	} else {
		osd_text_draw(s, font, th->value_size, right - mm.width, baseline, main_txt, accent);
	}

	// label
	osd_text_draw_tracked(s, font, th->label_size, (int)(px + th->pad_x),
		(int)(py + th->tab_height + th->label_size + 12.0f), label_for(e), th->label_tracking, label);

	// bar
	if (frac >= 0.0f) {
		int bx = (int)(px + th->pad_x);
		int by = (int)(py + h - th->pad_y - th->bar_height);
		int bw = (int)(w - th->pad_x * 2.0f);
		int bh = (int)th->bar_height;
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
					OSD_ARGB(0x8C, 0x04, 0x14, 0x0A), th->hatch_period, th->hatch_duty,
					th->hatch_slant);
				osd_draw_line(s, (float)(bx + bw), (float)by - 4.0f, (float)(bx + bw),
					(float)(by + bh + 4.0f), 2.0f, peakc);
			} else {
				osd_fill_rect_hatched(s, bx, by, fw, bh, accent,
					OSD_ARGB(0x8C, 0x0B, 0x1A, 0x24), th->hatch_period, th->hatch_duty,
					th->hatch_slant);
			}
		}
	}
}

int osd_widgets_draw_all(osd_surface_t *s, const osd_theme_t *th, osd_font_t *font,
	osd_widget_state_t *st, const osd_element_t *els, int count, const osd_grid_t *grid) {
	if (!s || !th || !font || !st || !els || !grid)
		return 0;
	if (th->mode != OSD_MODE_FANCY)
		return 0;

	// Track the peak before drawing so the bar and the "/67A" agree this frame.
	for (int i = 0; i < count; i++) {
		if (els[i].type == OSD_ELEM_CURRENT && els[i].value_valid && els[i].value > st->current_peak)
			st->current_peak = els[i].value;
	}

	int drawn = 0;
	for (int i = 0; i < count; i++) {
		const osd_element_t *e = &els[i];
		if (!e->value_valid || !osd_theme_element_enabled(th, e->type))
			continue;

		float opacity = osd_theme_element_opacity(th, e->type);
		if (opacity <= 0.01f)
			continue;

		float px = (float)(grid->off_x + e->col * grid->cell_w);
		float py = (float)(grid->off_y + e->row * grid->cell_h);
		draw_one(s, th, font, e, st->current_peak, px, py, opacity);
		drawn++;
	}
	return drawn;
}

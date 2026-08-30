#include "osd_widgets.h"

#include "osd_heading.h"
#include "osd_tiles.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#ifndef M_PI
	#define M_PI 3.14159265358979323846
#endif

bool osd_widgets_placement(const osd_widget_state_t *st, osd_element_type_t type, float *x,
	float *y, float *w, float *h) {
	if (!st)
		return false;
	for (int i = 0; i < OSD_WIDGET_SLOTS; i++) {
		if (!st->slots[i].used || !st->slots[i].layout_valid || st->slots[i].el.type != type)
			continue;
		if (x)
			*x = st->slots[i].x;
		if (y)
			*y = st->slots[i].y;
		if (w)
			*w = st->slots[i].w;
		if (h)
			*h = st->slots[i].h;
		return true;
	}
	return false;
}

const osd_element_t *osd_widgets_cached(const osd_widget_state_t *st, osd_element_type_t type) {
	if (!st)
		return NULL;
	for (int i = 0; i < OSD_WIDGET_SLOTS; i++)
		if (st->slots[i].used && st->slots[i].el.type == type)
			return &st->slots[i].el;
	return NULL;
}

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
	case OSD_ELEM_LATITUDE: return "LATITUDE";
	case OSD_ELEM_LONGITUDE: return "LONGITUDE";
	case OSD_ELEM_SPEED: return e->is_airspeed ? "AIRSPEED" : "GROUND SPEED";
	case OSD_ELEM_VARIO: return "CLIMB RATE";
	case OSD_ELEM_HOME_DISTANCE: return "HOME";
	case OSD_ELEM_TOTAL_DISTANCE: return "DISTANCE FLOWN";
	case OSD_ELEM_TEMPERATURE: return "TEMPERATURE";
	case OSD_ELEM_LINK_QUALITY: return "LINK QUALITY";
	case OSD_ELEM_RSSI_DBM: return "SIGNAL";
	case OSD_ELEM_SNR: return "SNR";
	case OSD_ELEM_HEADING: return "HEADING";
	case OSD_ELEM_GFORCE: return "G-FORCE";
	case OSD_ELEM_POWER: return "POWER";
	case OSD_ELEM_WATT_HOURS: return "ENERGY USED";
	case OSD_ELEM_RANGEFINDER: return "RANGEFINDER";
	case OSD_ELEM_EFFICIENCY: return "EFFICIENCY";
	case OSD_ELEM_TX_POWER: return "TX POWER";
	default: return "";
	}
}

// Bar fill as a fraction, or -1 when the value has no meaningful range and the
// widget should render as a plain value panel.
static float fill_fraction(const osd_element_t *e, const osd_theme_t *th, float peak, int cells) {
	switch (e->type) {
	// Both are already percentages, so a bar is the honest way to draw them.
	case OSD_ELEM_LINK_QUALITY:
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
	if (e->type == OSD_ELEM_RSSI || e->type == OSD_ELEM_LINK_QUALITY) {
		if (e->value < 30.0f)
			return th->crit;
		if (e->value < 55.0f)
			return th->warn;
	}
	// dBm is a log scale, so the thresholds are not the percentage ones with a
	// sign flipped. Around -95 an ELRS link starts dropping packets; -105 is
	// where it goes.
	if (e->type == OSD_ELEM_RSSI_DBM) {
		if (e->value < -100.0f)
			return th->crit;
		if (e->value < -90.0f)
			return th->warn;
	}
	if (e->type == OSD_ELEM_SNR) {
		if (e->value < 0.0f)
			return th->crit;
		if (e->value < 6.0f)
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
	case OSD_ELEM_SATS: snprintf(main, mn, "%.0f Sats", e->value); break;
	case OSD_ELEM_THROTTLE: snprintf(main, mn, "%.0f%%", e->value); break;
	case OSD_ELEM_FLIGHT_TIME:
	case OSD_ELEM_FLIGHT_MODE:
	case OSD_ELEM_WARNING: snprintf(main, mn, "%s", e->text); break;
	case OSD_ELEM_CURRENT:
		snprintf(main, mn, "%.0fA", e->value);
		snprintf(sub, sn, "/%.0fA", peak);
		break;
	// The unit comes off the screen, not from an assumption here: Betaflight
	// converts before drawing, so a feet reading relabelled as metres would be
	// wrong by a factor of three.
	case OSD_ELEM_ALTITUDE:
	case OSD_ELEM_HOME_DISTANCE:
	case OSD_ELEM_TOTAL_DISTANCE: {
		const char *u = osd_unit_name(e->unit);
		// Kilometres and miles arrive with decimals; metres and feet do not.
		const bool fine = (e->unit == OSD_UNIT_KM || e->unit == OSD_UNIT_MILES);
		snprintf(main, mn, fine ? "%.2f%s" : "%.0f%s", e->value,
			u[0] ? u : (e->type == OSD_ELEM_ALTITUDE ? "M" : ""));
		break;
	}
	case OSD_ELEM_SPEED: snprintf(main, mn, "%.0f%s", e->value, osd_unit_name(e->unit)); break;
	// Signed, and the sign is the whole point - a climb and a dive of the same
	// rate must not print the same.
	case OSD_ELEM_VARIO:
		snprintf(main, mn, "%+.1f%s", e->value, osd_unit_name(e->unit));
		break;
	case OSD_ELEM_TEMPERATURE:
		snprintf(main, mn, "%.0f%s", e->value,
			e->unit == OSD_UNIT_FAHRENHEIT ? "F" : "C");
		break;
	case OSD_ELEM_LINK_QUALITY: snprintf(main, mn, "%.0f%%", e->value); break;
	case OSD_ELEM_RSSI_DBM: snprintf(main, mn, "%.0fdBm", e->value); break;
	case OSD_ELEM_SNR: snprintf(main, mn, "%.0fdB", e->value); break;
	case OSD_ELEM_HEADING: snprintf(main, mn, "%03.0f", e->value); break;
	case OSD_ELEM_GFORCE: snprintf(main, mn, "%.1fG", e->value); break;
	case OSD_ELEM_POWER: snprintf(main, mn, "%.0fW", e->value); break;
	case OSD_ELEM_WATT_HOURS: snprintf(main, mn, "%.2fWh", e->value); break;
	case OSD_ELEM_RANGEFINDER: snprintf(main, mn, "%.0fcm", e->value); break;
	case OSD_ELEM_EFFICIENCY:
		snprintf(main, mn, "%.0fmAh/%s", e->value, osd_unit_name(e->unit));
		break;
	// Held in milliwatts throughout, so the two ways the firmware prints it
	// cannot end up as two different scales on our side.
	case OSD_ELEM_TX_POWER:
		if (e->value >= 1000.0f)
			snprintf(main, mn, "%.1fW", e->value / 1000.0f);
		else
			snprintf(main, mn, "%.0fmW", e->value);
		break;
	case OSD_ELEM_MAH: snprintf(main, mn, "%.0fmAh", e->value); break;
	// Straight from the flight controller: a float would round away the last
	// digit of a coordinate, which is metres on the ground.
	case OSD_ELEM_LATITUDE:
	case OSD_ELEM_LONGITUDE: snprintf(main, mn, "%s", e->text); break;
	case OSD_ELEM_RSSI: snprintf(main, mn, "%.0f%%", e->value); break;
	default: snprintf(main, mn, "%s", e->text); break;
	}
}

// A satellite: dish plus signal arcs. Replaces the word "SATELLITES", which
// says nothing the icon does not.
static void draw_sat_icon(osd_surface_t *s, float x, float y, float size, osd_color_t c) {
	// Deliberately blunt: this renders around 12-16px across, where fine detail
	// turns to mush. A tilted body with two wing panels and one signal arc is the
	// most satellite-like shape that survives at that size.
	const float w = size * 0.16f < 1.6f ? 1.6f : size * 0.16f;
	const float cx = x + size * 0.5f, cy = y + size * 0.5f;
	const float r = size * 0.5f;

	osd_draw_line(s, cx - r * 0.30f, cy + r * 0.30f, cx + r * 0.30f, cy - r * 0.30f, w * 1.6f, c);
	osd_draw_line(s, cx - r * 0.95f, cy - r * 0.20f, cx - r * 0.20f, cy - r * 0.95f, w, c);
	osd_draw_line(s, cx + r * 0.20f, cy + r * 0.95f, cx + r * 0.95f, cy + r * 0.20f, w, c);
	osd_draw_line(s, cx + r * 0.45f, cy + r * 0.75f, cx + r * 0.95f, cy + r * 0.95f, w, c);
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

	float need = (float)(mm.width + sm.width) + th->pad_x * scale * 2.0f + 96.0f * scale;
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
		draw_sat_icon(s, px + pad_x, py + tab_h * 0.18f, tab_h * 0.72f, accent);

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

// Elements whose content is words, not a number, so `value_valid` says nothing
// about whether there is anything to draw.
static bool element_is_textual(osd_element_type_t type) {
	return type == OSD_ELEM_FLIGHT_TIME || type == OSD_ELEM_FLIGHT_MODE ||
		   type == OSD_ELEM_WARNING;
}

// Elements the recogniser locates but reads nothing out of. The compass bar is
// the only one so far: its glyphs say which way the aircraft points, but we
// take the heading from MSP_ATTITUDE instead, so the run is there purely to say
// where the display goes. Without this it would be dropped for having no value,
// exactly like a numeric element that failed to parse.
static bool element_is_position_only(osd_element_type_t type) {
	return type == OSD_ELEM_HEADING_BAR || type == OSD_ELEM_HOME_ARROW;
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

// Finds the slot holding this element, or claims a free one. Keyed on where the
// element sits as well as what it is, so two of the same kind on screen get a
// slot each instead of overwriting one another every frame.
static int slot_for(osd_widget_state_t *st, const osd_element_t *e, uint64_t now_ms,
	float hold_ms) {
	int free_slot = -1;
	int oldest = -1;
	uint64_t oldest_seen = (uint64_t)-1;
	for (int i = 0; i < OSD_WIDGET_SLOTS; i++) {
		if (!st->slots[i].used) {
			if (free_slot < 0)
				free_slot = i;
			continue;
		}
		if (st->slots[i].el.type == e->type && st->slots[i].el.row == e->row &&
			st->slots[i].el.anchor_col == e->anchor_col)
			return i;
		// Expired slots are reusable: an element the pilot moved leaves its old
		// slot behind, and without this a screen edited a few times fills up.
		if ((float)(now_ms - st->slots[i].last_seen_ms) > hold_ms) {
			st->slots[i].used = false;
			if (free_slot < 0)
				free_slot = i;
			continue;
		}
		if (st->slots[i].last_seen_ms < oldest_seen) {
			oldest_seen = st->slots[i].last_seen_ms;
			oldest = i;
		}
	}
	if (free_slot >= 0)
		return free_slot;
	// Full. Evicting the stalest is the least bad option, and it is the one
	// nearest to falling out of the hold window anyway.
	return oldest;
}

// The first slot of a given type still inside its hold window. Used where only
// one instance can mean anything - the map's corner coordinates, the fix the
// home bearing is measured from.
static const osd_element_t *live_element(
	const osd_widget_state_t *st, osd_element_type_t type, uint64_t now_ms, float hold_ms) {
	for (int i = 0; i < OSD_WIDGET_SLOTS; i++) {
		if (!st->slots[i].used || st->slots[i].el.type != type)
			continue;
		if ((float)(now_ms - st->slots[i].last_seen_ms) > hold_ms)
			continue;
		return &st->slots[i].el;
	}
	return NULL;
}

// Initial great-circle bearing from one fix to another, in compass degrees.
// Rhumb would be simpler, but home is usually within a few km and over that
// distance the two agree to well under a degree - and this one stays right if
// somebody flies a long way north.
static float bearing_deg(double lat1, double lon1, double lat2, double lon2) {
	const double r = M_PI / 180.0;
	const double dl = (lon2 - lon1) * r;
	const double y = sin(dl) * cos(lat2 * r);
	const double x = cos(lat1 * r) * sin(lat2 * r) - sin(lat1 * r) * cos(lat2 * r) * cos(dl);
	double b = atan2(y, x) / r;
	if (b < 0.0)
		b += 360.0;
	return (float)b;
}

// The home arrow, in the cell the flight controller drew its own in.
//
// The firmware picks one of sixteen arrow glyphs, which is 22.5 degrees of
// resolution; we have the bearing to a fraction of a degree, so it is redrawn
// rather than decoded. Relative to the nose, like the firmware's: straight up
// means home is dead ahead.
static void draw_home_arrow(osd_surface_t *s, const osd_theme_t *th, float cx, float cy, float r,
	float bearing_rel_deg, float opacity) {
	const float a = bearing_rel_deg * (float)M_PI / 180.0f;
	const float ux = sinf(a), uy = -cosf(a);
	// Perpendicular, for the barbs and the tail.
	const float px = -uy, py = ux;
	const osd_color_t c = osd_theme_apply_opacity(th->accent, opacity);
	const osd_color_t o = osd_theme_apply_opacity(th->text_outline_color, opacity);

	const osd_pointf_t head[3] = {{cx + ux * r, cy + uy * r},
		{cx - ux * r * 0.25f + px * r * 0.62f, cy - uy * r * 0.25f + py * r * 0.62f},
		{cx - ux * r * 0.25f - px * r * 0.62f, cy - uy * r * 0.25f - py * r * 0.62f}};
	if (th->text_outline) {
		// Same trick the compass uses: grow about the centroid so the outline is
		// even on every side.
		float mx = 0.0f, my = 0.0f;
		for (int i = 0; i < 3; i++) {
			mx += head[i].x;
			my += head[i].y;
		}
		mx /= 3.0f;
		my /= 3.0f;
		osd_pointf_t big[3];
		for (int i = 0; i < 3; i++) {
			const float dx = head[i].x - mx, dy = head[i].y - my;
			const float len = sqrtf(dx * dx + dy * dy);
			const float k = len > 0.001f ? (len + 2.0f) / len : 1.0f;
			big[i].x = mx + dx * k;
			big[i].y = my + dy * k;
		}
		osd_fill_poly(s, big, 3, o);
		osd_draw_line(s, cx - ux * r * 0.2f, cy - uy * r * 0.2f, cx - ux * r, cy - uy * r, 7.0f, o);
	}
	osd_draw_line(s, cx - ux * r * 0.2f, cy - uy * r * 0.2f, cx - ux * r, cy - uy * r, 3.0f, c);
	osd_fill_poly(s, head, 3, c);
}

// Bounding box of the heading display, for collision avoidance. Each style
// spreads differently around its centre - a rose is as tall as it is wide, a
// band is a letterbox - and a single box would either let panels sit on top of
// it or push them much further away than needed.
void osd_widgets_heading_box(const osd_theme_t *th, float cx, float cy, float *x, float *y,
	float *w, float *h) {
	const float size = th->heading_size;
	switch (th->heading_style) {
	case 1: // rose
	case 3: // navball
		*w = size * 1.15f;
		*h = size * 1.30f;
		break;
	case 2: // ring
		*w = size;
		*h = size * 0.18f + 90.0f;
		break;
	case 4: // numeric
		*w = size;
		*h = 90.0f;
		break;
	default: // band
		*w = size;
		*h = 130.0f;
		break;
	}
	*x = cx - *w * 0.5f;
	*y = cy - *h * 0.5f;
}

// Axis-aligned rectangle overlap.
static bool rects_overlap(float ax, float ay, float aw, float ah, float bx, float by, float bw,
	float bh) {
	return ax < bx + bw && bx < ax + aw && ay < by + bh && by < ay + ah;
}

// Arrowhead pointing along `angle`, for showing which way an off-map marker lies.
static void draw_arrowhead(osd_surface_t *s, float x, float y, float size, float angle,
	osd_color_t c) {
	const float ca = cosf(angle), sa = sinf(angle);
	const float pts[3][2] = {{size, 0.0f}, {-size * 0.6f, -size * 0.6f}, {-size * 0.6f, size * 0.6f}};
	osd_pointf_t tri[3];
	for (int i = 0; i < 3; i++) {
		tri[i].x = x + pts[i][0] * ca - pts[i][1] * sa;
		tri[i].y = y + pts[i][0] * sa + pts[i][1] * ca;
	}
	osd_fill_poly(s, tri, 3, c);
}

// House outline: the launch point stays identifiable once the aircraft has flown
// away from it.
static void draw_home(osd_surface_t *s, float x, float y, float size, osd_color_t c) {
	const float w = 2.0f, hh = size * 0.5f;
	osd_draw_line(s, x - hh, y, x, y - hh, w, c);
	osd_draw_line(s, x, y - hh, x + hh, y, w, c);
	osd_draw_line(s, x - hh * 0.7f, y, x - hh * 0.7f, y + hh * 0.8f, w, c);
	osd_draw_line(s, x + hh * 0.7f, y, x + hh * 0.7f, y + hh * 0.8f, w, c);
	osd_draw_line(s, x - hh * 0.7f, y + hh * 0.8f, x + hh * 0.7f, y + hh * 0.8f, w, c);
}

// The aircraft: a triangle along its heading, outlined so it stays visible over
// busy imagery.
static void draw_aircraft(osd_surface_t *s, float cx, float cy, float size, float heading_deg,
	osd_color_t c, osd_color_t halo) {
	const float a = heading_deg * (float)M_PI / 180.0f;
	const float ca = cosf(a), sa = sinf(a);
	const float pts[3][2] = {{0.0f, -size}, {size * 0.6f, size * 0.7f}, {-size * 0.6f, size * 0.7f}};
	osd_pointf_t tri[3];
	for (int i = 0; i < 3; i++) {
		tri[i].x = cx + pts[i][0] * ca - pts[i][1] * sa;
		tri[i].y = cy + pts[i][0] * sa + pts[i][1] * ca;
	}
	osd_stroke_poly(s, tri, 3, size * 0.55f, halo);
	osd_fill_poly(s, tri, 3, c);
}

// North needle, for when the map no longer keeps north up. Without it a turning
// map is unreadable: you can fly to it, but you cannot say where anything is.
static void draw_north(osd_surface_t *s, osd_font_t *font, float cx, float cy, float r,
	float rot_deg, osd_color_t c, osd_color_t halo) {
	// North sits opposite the bearing that has been rotated to the top.
	const float a = -rot_deg * (float)M_PI / 180.0f;
	const float ca = cosf(a), sa = sinf(a);
	// Needle tip, and the two tail corners, in a north-up frame then turned.
	const float pts[3][2] = {{0.0f, -r}, {r * 0.42f, r * 0.45f}, {-r * 0.42f, r * 0.45f}};
	osd_pointf_t tri[3];
	for (int i = 0; i < 3; i++) {
		tri[i].x = cx + pts[i][0] * ca - pts[i][1] * sa;
		tri[i].y = cy + pts[i][0] * sa + pts[i][1] * ca;
	}
	osd_stroke_poly(s, tri, 3, r * 0.5f, halo);
	osd_fill_poly(s, tri, 3, c);

	if (font) {
		// The letter rides just beyond the tip, so it labels the needle rather
		// than a fixed corner of the map.
		const float lx = cx + pts[0][0] * ca - (pts[0][1] - r * 0.62f) * sa;
		const float ly = cy + pts[0][0] * sa + (pts[0][1] - r * 0.62f) * ca;
		osd_text_metrics_t m;
		if (osd_text_measure(font, 12.0f, "N", &m)) {
			osd_text_draw(s, font, 12.0f, (int)(lx - m.width * 0.5f), (int)(ly + m.ascent * 0.5f),
				"N", c);
		}
	}
}

// The rectangle the map occupies, capped to the theme maximum. Separate from
// drawing so the layout pass can treat the map as an obstacle.
static bool map_rect(const osd_theme_t *th, const osd_element_t *lat_e, const osd_element_t *lon_e,
	const osd_grid_t *grid, int *out_x, int *out_y, int *out_w, int *out_h) {
	int lx, ly, lw, lh, ox, oy, ow, oh;
	cell_rect(grid, lat_e->col, lat_e->row, lat_e->width, &lx, &ly, &lw, &lh);
	cell_rect(grid, lon_e->col, lon_e->row, lon_e->width, &ox, &oy, &ow, &oh);

	// Latitude marks the top-left, longitude the bottom-right. Accepted in
	// either order so the map still appears if placed the other way up.
	int x0 = lx < ox ? lx : ox;
	int y0 = ly < oy ? ly : oy;
	int x1 = (ox + ow) > (lx + lw) ? (ox + ow) : (lx + lw);
	int y1 = (oy + oh) > (ly + lh) ? (oy + oh) : (ly + lh);

	// Two readouts on one row, or squarely one above the other, are a list - not
	// the opposite corners of a rectangle. Read as a map they give a letterbox
	// strip or a bare column, so the placement is taken at face value and the
	// coordinates are drawn as ordinary value panels instead.
	//
	// Only an exact column match is rejected. Merely *overlapping* columns are
	// how you ask for a narrow map: the readouts are 10 cells wide, so demanding
	// they sit fully side by side would put a 20-cell floor on the map's width.
	if (lat_e->row == lon_e->row)
		return false;
	if (lat_e->col == lon_e->col)
		return false; // exactly one above the other: a list, not a rectangle

	int w = x1 - x0, h = y1 - y0;
	if (w > th->map_max_w)
		w = th->map_max_w;
	if (h > th->map_max_h)
		h = th->map_max_h;
	if (w < 64 || h < 48)
		return false; // too small to be a map; leave the values as text

	*out_x = x0;
	*out_y = y0;
	*out_w = w;
	*out_h = h;
	return true;
}

// Draws the map into the rectangle the two coordinate elements span. Tiles that
// have not arrived leave their cell empty: the tile store never blocks, so a
// slow network costs detail rather than frame rate.
static void draw_map(osd_surface_t *s, const osd_theme_t *th, osd_font_t *font,
	const osd_element_t *lat_e, const osd_element_t *lon_e, const osd_grid_t *grid,
	osd_widget_state_t *st, uint64_t now_ms) {
	int x0, y0, w, h;
	if (!map_rect(th, lat_e, lon_e, grid, &x0, &y0, &w, &h))
		return;
	const int x1 = x0 + w, y1 = y0 + h;

	// Where the aircraft is, which is not the same as where the map looks: the
	// view runs ahead of it along the ground track.
	const double ac_lat = lat_e->value, ac_lon = lon_e->value;

	const osd_map_view_cfg_t vcfg = {
		.auto_zoom = th->map_auto_zoom,
		.fixed_zoom = th->map_zoom,
		.min_zoom = th->map_zoom_min,
		.max_zoom = th->map_zoom_max,
		.lookahead_s = th->map_lookahead_s,
		.lead_s = th->map_lead_s,
		.lead_max_frac = th->map_lead_max,
		.settle_ms = th->map_zoom_settle_ms,
		.smooth_ms = th->map_smooth_ms,
	};
	int zoom = th->map_zoom;
	double lat = ac_lat, lon = ac_lon;
	osd_map_view_update(&st->map_view, &vcfg, ac_lat, ac_lon, st->ground_speed_mps, st->course_deg,
		st->heading_deg, w, h, now_ms, &zoom, &lat, &lon);

	// Turning the map: track-up follows the ground course, heading-up follows
	// the nose - which is what agrees with a nose-mounted camera. Either way the
	// smoothed value is used, or the map twitches with every packet.
	const float rot = th->map_orientation == 1   ? osd_map_view_course(&st->map_view)
					  : th->map_orientation == 2 ? osd_map_view_heading(&st->map_view)
												 : 0.0f;

	const float heading_deg = st->heading_deg;
	const bool home_valid = st->home_valid;
	const double home_lat = st->home_lat, home_lon = st->home_lon;
	const osd_map_style_t style = (osd_map_style_t)th->map_style;
	const float op = th->map_opacity;

	osd_clear_rect(s, x0, y0, w, h);

	int px = s->clip_x, py = s->clip_y, pw = s->clip_w, ph = s->clip_h;
	osd_surface_set_clip(s, x0, y0, w, h);

	// Turning the map means the tiles no longer land on axis-aligned rectangles,
	// so the loop walks *destination* pixels and asks where each one samples
	// from. Walking source pixels instead would tear the image into gaps as the
	// rotation stretches them apart. At rot 0 this is the identity and costs
	// only the arithmetic.
	//
	// A turned viewport reaches into the corners of its bounding square, so the
	// tiles fetched cover the diagonal rather than the rectangle.
	const int span = rot != 0.0f ? (int)ceilf(sqrtf((float)(w * w + h * h))) : 0;
	const int fetch_w = rot != 0.0f ? span : w;
	const int fetch_h = rot != 0.0f ? span : h;

	double centre_wx, centre_wy;
	osd_map_project(lat, lon, zoom, &centre_wx, &centre_wy);

	// The tile grid covering that extent, resolved once per layer so the
	// per-pixel loop is arithmetic plus one array index rather than a cache
	// probe 100k times a frame.
	const double fetch_left = centre_wx - fetch_w / 2.0;
	const double fetch_top = centre_wy - fetch_h / 2.0;
	const int first_tx = (int)floor(fetch_left / OSD_TILE_SIZE);
	const int first_ty = (int)floor(fetch_top / OSD_TILE_SIZE);
	const int grid_w = (int)floor((fetch_left + fetch_w - 1) / OSD_TILE_SIZE) - first_tx + 1;
	const int grid_h = (int)floor((fetch_top + fetch_h - 1) / OSD_TILE_SIZE) - first_ty + 1;
	const int world_tiles = 1 << zoom;

	int layers = 1 + osd_map_overlay_count(style);
	int missing = 0;

	if (grid_w > 0 && grid_h > 0 && (size_t)(grid_w * grid_h) <= 64) {
		for (int layer = 0; layer < layers; layer++) {
			const osd_tile_bitmap_t *grid_tiles[64] = {NULL};
			for (int gy = 0; gy < grid_h; gy++) {
				for (int gx = 0; gx < grid_w; gx++) {
					const int ty = first_ty + gy;
					if (ty < 0 || ty >= world_tiles)
						continue; // latitude does not wrap; off-world tiles are absent
					int tx = (first_tx + gx) % world_tiles;
					if (tx < 0)
						tx += world_tiles; // longitude does
					const osd_tile_bitmap_t *b = osd_tiles_get(style, layer, zoom, tx, ty);
					grid_tiles[gy * grid_w + gx] = b;
					if (!b && layer == 0)
						missing++;
				}
			}

			for (int dy = 0; dy < h; dy++) {
				for (int dx = 0; dx < w; dx++) {
					double wx, wy;
					osd_map_screen_to_world(centre_wx, centre_wy, w, h, rot, dx + 0.5, dy + 0.5,
						&wx, &wy);

					const int gx = (int)floor(wx / OSD_TILE_SIZE) - first_tx;
					const int gy = (int)floor(wy / OSD_TILE_SIZE) - first_ty;
					if (gx < 0 || gx >= grid_w || gy < 0 || gy >= grid_h)
						continue;
					const osd_tile_bitmap_t *b = grid_tiles[gy * grid_w + gx];
					if (!b)
						continue;

					int ix = (int)(wx - floor(wx / OSD_TILE_SIZE) * OSD_TILE_SIZE);
					int iy = (int)(wy - floor(wy / OSD_TILE_SIZE) * OSD_TILE_SIZE);
					if (ix < 0 || ix >= b->width || iy < 0 || iy >= b->height)
						continue;

					const uint8_t *sp = b->pixels + ((size_t)iy * b->width + ix) * 4;
					// Label overlays are largely transparent; respect their alpha
					// so the imagery shows through.
					const float cov = (sp[3] / 255.0f) * op;
					if (cov <= 0.004f)
						continue;
					osd_blend_px(s, x0 + dx, y0 + dy, OSD_ARGB(255, sp[2], sp[1], sp[0]), cov);
				}
			}
		}
	}

	// The scale note goes down before the markers. It sits in the bottom-left
	// corner, which is exactly where an off-map home arrow is pinned once you
	// have flown south-west of the launch point - and losing the way back
	// behind a zoom readout is the wrong trade.
	if (font) {
	char note[48];
	if (missing > 0)
		snprintf(note, sizeof(note), "z%d  %d tiles...", zoom, missing);
	else if (th->map_auto_zoom)
		// The zoom moves on its own now, so show what is moving it.
		snprintf(note, sizeof(note), "z%d  %dm/s", zoom, (int)(st->map_view.speed_mps + 0.5f));
	else
		snprintf(note, sizeof(note), "z%d", zoom);
	// Same plinth the coordinate tab gets. Over bright imagery - a sandy
	// track, a field in full sun - small label-coloured text on bare tiles
	// is unreadable, which is exactly when you want to know the scale.
	osd_text_metrics_t nm;
	if (osd_text_measure(font, th->label_size, note, &nm)) {
		const int padx = 6, pady = 4;
		int nw = nm.width + (int)(th->label_tracking * (float)strlen(note)) + padx * 2;
		int nh = nm.height + pady * 2;
		osd_pointf_t plinth[4] = {{(float)x0, (float)(y1 - nh)},
			{(float)(x0 + nw), (float)(y1 - nh)},
			{(float)(x0 + nw + nh * 0.5f), (float)y1}, {(float)x0, (float)y1}};
		osd_fill_poly(s, plinth, 4, osd_theme_apply_opacity(th->panel_fill, op));
	}
	osd_text_draw_tracked(s, font, th->label_size, x0 + 6, y1 - 8, note, th->label_tracking,
		osd_theme_apply_opacity(th->label, op));
	}

	if (home_valid) {
		float hx, hy;
		osd_map_point_in_view_rot(home_lat, home_lon, lat, lon, zoom, w, h, rot, &hx, &hy);
		osd_color_t home_c = osd_theme_apply_opacity(th->good, op);
		const float inset = 15.0f;

		if (hx >= inset && hx <= w - inset && hy >= inset && hy <= h - inset) {
			draw_home(s, x0 + hx, y0 + hy, 14.0f, home_c);
		} else {
			// Home has left the visible span. Pin it to the edge in the right
			// direction with an arrow, so the way back stays readable once you
			// have flown beyond the map's coverage.
			const float ccx = w * 0.5f, ccy = h * 0.5f;
			float dx = hx - ccx, dy = hy - ccy;
			float len = sqrtf(dx * dx + dy * dy);
			if (len < 0.001f) {
				dx = 0.0f;
				dy = -1.0f;
				len = 1.0f;
			}
			dx /= len;
			dy /= len;
			float tx = (dx > 0.0f) ? (w - inset - ccx) / dx : (dx < 0.0f ? (inset - ccx) / dx : 1e9f);
			float ty = (dy > 0.0f) ? (h - inset - ccy) / dy : (dy < 0.0f ? (inset - ccy) / dy : 1e9f);
			float tmin = tx < ty ? tx : ty;
			float ex = ccx + dx * tmin, ey = ccy + dy * tmin;
			draw_home(s, x0 + ex, y0 + ey, 12.0f, home_c);
			draw_arrowhead(s, x0 + ex + dx * 13.0f, y0 + ey + dy * 13.0f, 6.0f, atan2f(dy, dx), home_c);
		}
	}

	float ax, ay;
	osd_map_point_in_view_rot(ac_lat, ac_lon, lat, lon, zoom, w, h, rot, &ax, &ay);
	// The marker turns with the map, so in track-up what is left is the crab
	// angle: the nose sits off straight-up by however much the wind is pushing.
	draw_aircraft(s, x0 + ax, y0 + ay, 11.0f, heading_deg - rot, osd_theme_apply_opacity(th->accent, op),
		osd_theme_apply_opacity(OSD_ARGB(0xC0, 0, 0, 0), op));

	if (rot != 0.0f) {
		// Below the coordinate tab: the needle's "N" rides beyond its tip and
		// would otherwise be swallowed by it.
		draw_north(s, font, (float)(x0 + w - 28), (float)(y0 + 44), 11.0f, rot,
			osd_theme_apply_opacity(th->warn, op),
			osd_theme_apply_opacity(OSD_ARGB(0xC0, 0, 0, 0), op));
	}

	osd_surface_set_clip(s, px, py, pw, ph);

	osd_color_t edge = osd_theme_apply_opacity(th->panel_edge, op);
	osd_color_t accent = osd_theme_apply_opacity(th->accent, op);
	osd_draw_line(s, (float)x0, (float)y0, (float)x1, (float)y0, 1.5f, edge);
	osd_draw_line(s, (float)x0, (float)y1, (float)x1, (float)y1, 1.5f, edge);
	osd_draw_line(s, (float)x0, (float)y0, (float)x0, (float)y1, 1.5f, edge);
	osd_draw_line(s, (float)x1, (float)y0, (float)x1, (float)y1, 1.5f, edge);
	osd_draw_line(s, (float)x0, (float)y0, (float)x0 + 22.0f, (float)y0, 2.5f, accent);
	osd_draw_line(s, (float)x0, (float)y0, (float)x0, (float)y0 + 22.0f, 2.5f, accent);
	osd_draw_line(s, (float)x1, (float)y1, (float)x1 - 22.0f, (float)y1, 2.5f, accent);
	osd_draw_line(s, (float)x1, (float)y1, (float)x1, (float)y1 - 22.0f, 2.5f, accent);

	if (font) {
		// Coordinates in a tab at the top-right: the map shows where you are, but
		// the numbers are still what gets read out over the radio.
		char coords[40];
		snprintf(coords, sizeof(coords), "%.5f %.5f", ac_lat, ac_lon);
		osd_text_metrics_t cm;
		if (osd_text_measure(font, th->label_size, coords, &cm)) {
			const int padx = 6, pady = 4;
			int tw = cm.width + padx * 2, thh = cm.height + pady * 2;
			int tx = x1 - tw, ty = y0;
			osd_pointf_t tab[4] = {{(float)tx - thh * 0.5f, (float)ty}, {(float)x1, (float)ty},
				{(float)x1, (float)(ty + thh)}, {(float)tx, (float)(ty + thh)}};
			osd_fill_poly(s, tab, 4, osd_theme_apply_opacity(th->panel_fill, op));
			osd_text_draw(s, font, th->label_size, tx + padx, ty + pady + cm.ascent, coords, accent);
		}
	}
}

int osd_widgets_draw_all(osd_surface_t *s, const osd_theme_t *th, osd_font_t *font,
	osd_widget_state_t *st, const osd_element_t *els, int count, const osd_grid_t *grid,
	uint64_t now_ms) {
	if (!s || !th || !font || !st || !els || !grid)
		return 0;
	if (th->mode != OSD_MODE_FANCY)
		return 0;

	// The text outline is a look applied to every string, so it is set once here
	// rather than per widget. Without this the theme's key parsed fine and did
	// nothing: only the compass, which sets its own, was ever outlined.
	osd_text_set_outline(th->text_outline, th->text_outline_color, th->text_outline_width);

	// Track the peak before drawing so the bar and the "/67A" agree this frame.
	for (int i = 0; i < count; i++) {
		if (els[i].type == OSD_ELEM_CURRENT && els[i].value_valid && els[i].value > st->current_peak)
			st->current_peak = els[i].value;
		// Infer pack size once, from the first battery reading we see.
		if (st->cell_count == 0 && els[i].type == OSD_ELEM_VOLTAGE && els[i].has_battery_icon &&
			els[i].value_valid)
			st->cell_count = infer_cell_count(els[i].value);
	}

	// The map's two elements, copied by value. list[] is sorted further down, so
	// pointers into it would end up referencing whichever elements moved into
	// those slots.
	osd_element_t map_lat_v, map_lon_v;
	bool have_map = false, map_drawn = false;

	// Where the compass display goes, in pixels. The flight controller's bar is
	// a fixed-width run of glyphs; the display that replaces it is sized by the
	// theme, so only the centre of the run is used.
	bool have_heading = false;
	float hdg_cx = 0.0f, hdg_cy = 0.0f;

	// Refresh the cache with everything visible this frame.
	for (int i = 0; i < count; i++) {
		const osd_element_t *e = &els[i];
		if (e->type <= OSD_ELEM_NONE || e->type >= OSD_ELEM_TYPE_COUNT)
			continue;

		// A flight controller redraws its OSD a cell at a time, so a scan can
		// catch a numeric field half-written and parse nothing out of it.
		// Letting that overwrite a good cached reading is worse than missing one
		// frame of it: the draw list drops numeric elements with no value, so
		// the widget blinks out - and for latitude and longitude, the whole map
		// goes with it for a frame. The hold below already covers an element
		// that vanishes; this covers one that arrives broken.
		const int slot = slot_for(st, e, now_ms, th->element_hold_ms);
		if (slot < 0)
			continue;
		if (!e->value_valid && !element_is_textual(e->type) &&
			!element_is_position_only(e->type) && st->slots[slot].used &&
			(float)(now_ms - st->slots[slot].last_seen_ms) <= th->element_hold_ms)
			continue;

		if (!st->slots[slot].used) {
			st->slots[slot].used = true;
			st->slots[slot].layout_valid = false;
		}
		st->slots[slot].el = *e;
		st->slots[slot].last_seen_ms = now_ms;
	}

	// Build the draw list from the cache, so an element that blinked out is
	// still drawn until its hold expires.
	osd_element_t list[OSD_WIDGET_SLOTS];
	int slot_of[OSD_WIDGET_SLOTS];
	int n = 0;
	for (int sl = 0; sl < OSD_WIDGET_SLOTS; sl++) {
		if (!st->slots[sl].used)
			continue;
		if ((float)(now_ms - st->slots[sl].last_seen_ms) > th->element_hold_ms) {
			st->slots[sl].used = false;
			continue;
		}
		const osd_element_t *e = &st->slots[sl].el;
		// Messages have to count as textual here too, or every failsafe is
		// recognised and then dropped silently for having no numeric value.
		const bool textual = element_is_textual(e->type) || element_is_position_only(e->type);
		if ((!e->value_valid && !textual) || !osd_theme_element_enabled(th, e->type))
			continue;
		if (osd_theme_element_opacity(th, e->type) <= 0.01f)
			continue;
		slot_of[n] = sl;
		list[n++] = *e;
	}

	for (int i = 0; i < n; i++) {
		if (list[i].type != OSD_ELEM_HEADING_BAR)
			continue;
		int hx, hy, hw, hh;
		cell_rect(grid, list[i].col, list[i].row, list[i].width, &hx, &hy, &hw, &hh);
		hdg_cx = (float)hx + (float)hw * 0.5f;
		hdg_cy = (float)hy + (float)hh * 0.5f;
		have_heading = osd_theme_element_opacity(th, OSD_ELEM_HEADING_BAR) > 0.01f;

		// Keep it on screen. Both firmwares put the compass bar hard against the
		// top or bottom edge, where it is one row tall; a rose is closer to
		// thirty, so centring it on the run drops half the dial outside the
		// viewport. Same reasoning as the panel clamp further down.
		float bx, by, bw, bh;
		osd_widgets_heading_box(th, hdg_cx, hdg_cy, &bx, &by, &bw, &bh);
		if (bx < 0.0f)
			hdg_cx -= bx;
		else if (bx + bw > (float)s->width)
			hdg_cx -= bx + bw - (float)s->width;
		if (by < 0.0f)
			hdg_cy -= by;
		else if (by + bh > (float)s->height)
			hdg_cy -= by + bh - (float)s->height;
		break;
	}

	if (th->map_enabled) {
		const osd_element_t *la = NULL, *lo = NULL;
		for (int i = 0; i < n; i++) {
			if (list[i].type == OSD_ELEM_LATITUDE && list[i].value_valid)
				la = &list[i];
			else if (list[i].type == OSD_ELEM_LONGITUDE && list[i].value_valid)
				lo = &list[i];
		}
		// Only claim the map when its rectangle really works out. draw_map used
		// to bail on a bad rectangle *after* the caller had already marked the
		// coordinates as handled, which dropped them off the screen entirely.
		if (la && lo) {
			int mx, my, mw, mh;
			if (map_rect(th, la, lo, grid, &mx, &my, &mw, &mh)) {
				map_lat_v = *la;
				map_lon_v = *lo;
				have_map = true;
			}
		}
	}

	// Place top-to-bottom, left-to-right, and push any panel that would collide
	// clear of the ones already placed. Flight controller layouts stack elements
	// a single row apart, while a panel is several rows tall, so overlaps are
	// the norm rather than the exception; without this the panels bury each
	// other. Order is preserved, so a vertical list of elements stays a
	// vertical list of panels.
	for (int i = 1; i < n; i++) {
		osd_element_t key = list[i];
		const int key_slot = slot_of[i];
		int j = i - 1;
		while (j >= 0 && (list[j].row > key.row ||
							 (list[j].row == key.row && list[j].col > key.col))) {
			list[j + 1] = list[j];
			slot_of[j + 1] = slot_of[j];
			j--;
		}
		list[j + 1] = key;
		slot_of[j + 1] = key_slot;
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

	// Map, drawn after the clearing pass. Its rectangle is capped to a minimap,
	// so it no longer reaches the elements that define it: clearing everything
	// first removes the coordinate text, and drawing the map second means the
	// clear cannot punch holes through the tiles.
	// Home is where the aircraft armed: the first valid fix after the disarmed
	// -> armed edge, which is what the flight controller uses too. Captured from
	// the cache rather than inside the map block, because the compass wants a
	// home bearing whether or not a map is on screen.
	const osd_element_t *fix_lat = live_element(st, OSD_ELEM_LATITUDE, now_ms, th->element_hold_ms);
	const osd_element_t *fix_lon = live_element(st, OSD_ELEM_LONGITUDE, now_ms, th->element_hold_ms);
	const bool have_fix = fix_lat && fix_lon && fix_lat->value_valid && fix_lon->value_valid;
	if (st->prev_armed && !st->home_valid && have_fix) {
		st->home_lat = fix_lat->value;
		st->home_lon = fix_lon->value;
		st->home_valid = true;
	}

	if (have_map) {
		draw_map(s, th, font, &map_lat_v, &map_lon_v, grid, st, now_ms);
		map_drawn = true;
	}

	// The home arrow, drawn wherever the flight controller drew its own. Shares
	// the compass's bearing and smoothing, so the two cannot disagree.
	for (int i = 0; i < n; i++) {
		if (list[i].type != OSD_ELEM_HOME_ARROW)
			continue;
		if (!st->home_valid || !have_fix)
			break; // nothing to point at yet
		int ax, ay, aw, ah;
		cell_rect(grid, list[i].col, list[i].row, list[i].width, &ax, &ay, &aw, &ah);
		const float op = osd_theme_element_opacity(th, OSD_ELEM_HOME_ARROW) * th->global_opacity;
		const float bearing =
			bearing_deg(fix_lat->value, fix_lon->value, st->home_lat, st->home_lon);
		const float r = (float)(aw < ah ? aw : ah) * 0.55f *
						osd_theme_element_scale(th, OSD_ELEM_HOME_ARROW) * th->global_scale;
		draw_home_arrow(s, th, (float)ax + (float)aw * 0.5f, (float)ay + (float)ah * 0.5f, r,
			bearing - st->heading_deg, op);
		break;
	}

	// Link statistics, from the file the ground station writes. Placed from the
	// theme rather than from the grid: wfb-ng and APFPV are on this side of the
	// link, so the flight controller has never heard of them and there is no
	// element to anchor to.
	bool have_link = false;
	float link_x = 0.0f, link_y = 0.0f, link_w = 0.0f, link_h = 0.0f;
	osd_link_params_t lp = {0};
	bool link_stale = false;
	if (th->link_enabled && th->link_source[0] && th->link_opacity > 0.01f) {
		// The poll clock advances whether or not the read succeeded. Advancing it
		// only on success meant a missing file was reopened every single frame,
		// which is the one case where it definitely will not appear.
		if (st->link_polled_ms == 0 || now_ms - st->link_polled_ms >= 200) {
			st->link_polled_ms = now_ms;
			osd_link_stats_load(th->link_source, &st->link, now_ms);
		}
		if (st->link.valid) {
			link_stale = osd_link_stats_stale(&st->link, now_ms, th->link_hold_ms);
			const float op = th->link_opacity * th->global_opacity;
			lp.style = (osd_link_style_t)th->link_style;
			lp.scale = th->link_scale * th->global_scale;
			lp.accent = osd_theme_apply_opacity(th->accent, op);
			lp.label = osd_theme_apply_opacity(th->label, op);
			lp.good = osd_theme_apply_opacity(th->good, op);
			lp.warn = osd_theme_apply_opacity(th->warn, op);
			lp.crit = osd_theme_apply_opacity(th->crit, op);
			lp.fill = osd_theme_apply_opacity(th->panel_fill, op);
			lp.edge = osd_theme_apply_opacity(th->panel_edge, op);
			lp.track = osd_theme_apply_opacity(th->track, op);
			lp.opacity = op;
			lp.outline = th->text_outline;
			lp.outline_color = th->text_outline_color;
			lp.outline_px = (float)th->text_outline_width + 1.0f;
			lp.pad_x = th->pad_x;
			lp.pad_y = th->pad_y;
			lp.chamfer = th->chamfer;
			lp.bar_height = th->bar_height;
			lp.value_size = th->value_size;
			lp.label_size = th->label_size;
			lp.label_tracking = th->label_tracking;

			osd_link_measure(&lp, &st->link, &link_w, &link_h);
			// The theme places the top-left corner as a fraction of the screen,
			// so a layout carries over between a 720p and a 1080p ground
			// station. Clamped, because 100% would put it entirely off-screen.
			link_x = (float)s->width * th->link_x * 0.01f;
			link_y = (float)s->height * th->link_y * 0.01f;
			if (link_x + link_w > (float)s->width)
				link_x = (float)s->width - link_w;
			if (link_y + link_h > (float)s->height)
				link_y = (float)s->height - link_h;
			if (link_x < 0.0f)
				link_x = 0.0f;
			if (link_y < 0.0f)
				link_y = 0.0f;
			osd_link_draw(s, font, link_x, link_y, &lp, &st->link, link_stale);
			have_link = true;
		}
	}

	if (have_heading) {
		// A compass that lags a turn is worse than no compass, so this is eased
		// on its own short constant rather than the map's - see the field's
		// comment. 200ms takes the jitter off an MSP_ATTITUDE stream without
		// being visible as lag.
		const bool track_ok = st->ground_speed_mps > 1.5f;
		osd_heading_smooth_update(
			&st->heading_smooth, st->heading_deg, st->course_deg, track_ok, now_ms, 200.0f);

		const float op = osd_theme_element_opacity(th, OSD_ELEM_HEADING_BAR) * th->global_opacity;
		osd_heading_params_t hp = {0};
		hp.style = (osd_heading_style_t)th->heading_style;
		hp.size = th->heading_size * th->global_scale *
				  osd_theme_element_scale(th, OSD_ELEM_HEADING_BAR);
		hp.span_deg = th->heading_span;
		hp.show_track = th->heading_show_track;
		hp.flip = th->heading_flip;
		hp.lens = th->heading_lens;
		hp.outline = th->heading_outline;
		hp.outline_color = th->text_outline_color;
		hp.outline_px = th->heading_outline_width;
		hp.accent = osd_theme_apply_opacity(th->accent, op);
		hp.label = osd_theme_apply_opacity(th->label, op);
		// `good`, not `track`: the theme's `track` is the unfilled trough of a
		// progress bar, which is nearly black. The ground-track marker wants the
		// green that means "healthy" everywhere else on the screen.
		hp.track = osd_theme_apply_opacity(th->good, op);
		hp.home = osd_theme_apply_opacity(th->warn, op);
		hp.fill = osd_theme_apply_opacity(th->panel_fill, op);
		hp.edge = osd_theme_apply_opacity(th->panel_edge, op);
		hp.opacity = op;
		hp.heading_deg = osd_heading_smooth_heading(&st->heading_smooth);
		hp.track_deg = osd_heading_smooth_track(&st->heading_smooth);
		hp.track_valid = track_ok;
		hp.pitch_deg = st->pitch_deg;
		hp.roll_deg = st->roll_deg;
		// Home only means something once we know where it is and where we are.
		if (st->home_valid && have_fix) {
			hp.home_deg =
				bearing_deg(fix_lat->value, fix_lon->value, st->home_lat, st->home_lon);
			hp.home_valid = true;
		}
		osd_heading_draw(s, font, hdg_cx, hdg_cy, &hp);
	}

	// Signature of what is on screen and where. Only a change here justifies
	// moving anything.
	uint32_t sig = 2166136261u;
	for (int i = 0; i < n; i++) {
		sig = (sig ^ (uint32_t)list[i].type) * 16777619u;
		sig = (sig ^ (uint32_t)list[i].row) * 16777619u;
		// The element's *anchor*, not its leftmost cell: a value gaining a digit
		// slides `col` one cell left, which would otherwise relayout everything
		// on screen every time a reading crossed 99 -> 100.
		sig = (sig ^ (uint32_t)list[i].anchor_col) * 16777619u;
	}
	bool relayout = (sig != st->layout_signature);
	if (relayout) {
		st->layout_signature = sig;
		for (int i = 0; i < OSD_WIDGET_SLOTS; i++)
			st->slots[i].layout_valid = false;
	}

	// +3 for the seeded rectangles: the map, the compass and the link panel.
	// None is a panel, but panels must keep clear of all three.
	float placed_x[OSD_WIDGET_SLOTS + 3], placed_y[OSD_WIDGET_SLOTS + 3];
	float placed_w[OSD_WIDGET_SLOTS + 3], placed_h[OSD_WIDGET_SLOTS + 3];
	int placed = 0;
	int drawn = 0;

	// Seed the layout with the map's rectangle so panels are pushed clear of it
	// rather than covering the thing you are navigating by.
	if (have_map) {
		int mx, my, mw, mh;
		if (map_rect(th, &map_lat_v, &map_lon_v, grid, &mx, &my, &mw, &mh)) {
			placed_x[placed] = (float)mx;
			placed_y[placed] = (float)my;
			placed_w[placed] = (float)mw;
			placed_h[placed] = (float)mh;
			placed++;
		}
	}

	if (have_heading) {
		osd_widgets_heading_box(th, hdg_cx, hdg_cy, &placed_x[placed], &placed_y[placed], &placed_w[placed],
			&placed_h[placed]);
		placed++;
	}

	if (have_link) {
		placed_x[placed] = link_x;
		placed_y[placed] = link_y;
		placed_w[placed] = link_w;
		placed_h[placed] = link_h;
		placed++;
	}

	for (int i = 0; i < n; i++) {
		const osd_element_t *e = &list[i];
		if (map_drawn && (e->type == OSD_ELEM_LATITUDE || e->type == OSD_ELEM_LONGITUDE))
			continue; // already rendered as the map
		if (e->type == OSD_ELEM_HEADING_BAR)
			continue; // already rendered as the compass
		if (e->type == OSD_ELEM_HOME_ARROW)
			continue; // already rendered as the arrow
		float opacity = osd_theme_element_opacity(th, e->type);
		float scale = osd_theme_element_scale(th, e->type);

		float w, h;
		measure_panel(th, font, e, st->current_peak, st->cell_count, scale, &w, &h);

		// Place against the anchor cell, the one the flight controller holds
		// still, rather than against the run's leftmost glyph. Readings are
		// right-aligned in their field, so a trailing-symbol value grows
		// leftwards: anchoring on the left edge walked the whole panel one cell
		// left at 99 -> 100 and again at 999 -> 1000.
		int ax, ay, aw, ah;
		cell_rect(grid, e->anchor_col, e->row, 1, &ax, &ay, &aw, &ah);
		float px = e->anchor_right ? (float)(ax + aw) - w : (float)ax;
		float py = (float)ay;

		const int sl = slot_of[i];
		if (!relayout && st->slots[sl].layout_valid) {
			// Reuse the settled position. Width may only grow - a longer reading
			// must still fit - and never shrinks, so the panel does not breathe
			// as digits come and go.
			px = st->slots[sl].x;
			py = st->slots[sl].y;
			h = st->slots[sl].h;
			if (w <= st->slots[sl].w) {
				w = st->slots[sl].w;
			} else {
				// A right-anchored panel grows leftwards, so the value stays put
				// instead of the panel edge dragging it sideways.
				if (e->anchor_right) {
					px -= w - st->slots[sl].w;
					if (px < 0.0f)
						px = 0.0f;
					st->slots[sl].x = px;
				}
				st->slots[sl].w = w;
			}

			draw_one(s, th, font, e, st->current_peak, st->cell_count, px, py, opacity, scale);
			drawn++;
			continue;
		}

		// Keep the panel on screen. An element in column 0 or on the last row
		// would otherwise put half its widget outside the viewport - the grid
		// position is where the *value* was, not where a whole panel fits.
		//
		// This has to happen *before* collision resolution, not after. An
		// element in one of the last columns has its panel pulled left to fit,
		// and pulling it left after the overlap test has passed drops it
		// straight on top of a panel already placed on that row.
		if (px + w > (float)s->width)
			px = (float)s->width - w;
		if (px < 0.0f)
			px = 0.0f;
		if (py + h > (float)s->height)
			py = (float)s->height - h;
		if (py < 0.0f)
			py = 0.0f;

		for (int attempt = 0; attempt < OSD_WIDGET_SLOTS; attempt++) {
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
		// A crowded screen can push the last panel off the bottom. Keeping it
		// visible matters more than the overlap it may land back on.
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

		st->slots[sl].layout_valid = true;
		st->slots[sl].x = px;
		st->slots[sl].y = py;
		st->slots[sl].w = w;
		st->slots[sl].h = h;
	}
	return drawn;
}

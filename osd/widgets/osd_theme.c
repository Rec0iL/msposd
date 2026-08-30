#include "osd_theme.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

void osd_theme_defaults(osd_theme_t *t) {
	if (!t)
		return;
	memset(t, 0, sizeof(*t));

	snprintf(t->name, sizeof(t->name), "%s", "Tactical");
	snprintf(t->font_path, sizeof(t->font_path), "%s", "fonts/UbuntuMono-Regular.ttf");

	t->mode = OSD_MODE_FANCY;
	t->global_opacity = 1.0f;
	t->global_scale = 1.0f;
	t->hide_glyphs = true;

	t->heading_style = 0; // band
	t->heading_size = 480.0f;
	t->heading_span = 90.0f;
	t->heading_show_track = true;
	t->heading_flip = false;
	t->heading_lens = 0.62f;
	t->heading_outline = true;
	t->heading_outline_width = 2.0f;

	t->accent = OSD_ARGB(0xFF, 0x00, 0xE5, 0xFF);
	t->warn = OSD_ARGB(0xFF, 0xFF, 0xB3, 0x00);
	t->crit = OSD_ARGB(0xFF, 0xFF, 0x3B, 0x30);
	t->good = OSD_ARGB(0xFF, 0x00, 0xFF, 0x9C);
	t->threat = OSD_ARGB(0xFF, 0xFF, 0x6A, 0x1F);
	t->panel_fill = OSD_ARGB(0xD6, 0x0A, 0x1A, 0x26);
	t->panel_edge = OSD_ARGB(0xFF, 0x0E, 0x3D, 0x52);
	t->track = OSD_ARGB(0xCC, 0x06, 0x22, 0x2E);
	t->label = OSD_ARGB(0xFF, 0x4F, 0xA8, 0xC4);
	t->peak = OSD_ARGB(0xFF, 0x8F, 0xD8, 0xEA);

	t->panel_min_width = 250.0f;
	t->panel_height = 104.0f;
	t->tab_height = 36.0f;
	t->chamfer = 14.0f;
	t->pad_x = 13.0f;
	t->pad_y = 11.0f;
	t->bar_height = 16.0f;
	t->value_size = 25.0f;
	t->label_size = 11.0f;
	t->label_tracking = 2.5f;

	t->text_outline = true;
	t->text_outline_color = OSD_ARGB(0xC0, 0x00, 0x00, 0x00);
	t->text_outline_width = 1;

	t->hatch_period = 7.0f;
	t->hatch_duty = 0.64f;
	t->hatch_slant = -0.45f;

	t->cell_min = 3.0f;
	t->cell_max = 4.35f;
	t->cell_warn = 3.60f;
	t->cell_crit = 3.40f;
	t->element_hold_ms = 2000.0f;

	t->map_enabled = true;
	t->map_style = 2; // hybrid
	t->map_zoom = 16;
	t->map_opacity = 1.0f;
	// "tactical": matches this theme's accent rather than msposd's stock green.
	t->ahi_level_color = 6;    // cyan
	t->ahi_moderate_color = 4; // yellow
	t->ahi_steep_color = 1;    // red
	t->ahi_line_color = 7;     // white
	t->ahi_level_max = 2.0f;
	t->ahi_moderate_max = 10.0f;
	t->ahi_steep_thickness = 5;

	t->map_max_w = 420;
	t->map_max_h = 300;
	t->map_orientation = 0; // north up
	t->map_auto_zoom = true;
	t->map_zoom_min = 13;
	t->map_zoom_max = 17;
	t->map_lookahead_s = 20.0f;
	t->map_lead_s = 6.0f;
	t->map_lead_max = 0.35f;
	t->map_zoom_settle_ms = 3000.0f;
	t->map_smooth_ms = 1500.0f;
	snprintf(t->map_cache_dir, sizeof(t->map_cache_dir), "%s", "/tmp/msposd-tiles");

	for (int i = 0; i < OSD_ELEM_TYPE_COUNT; i++) {
		t->elem_enabled[i] = true;
		t->elem_opacity[i] = 1.0f;
		t->elem_scale[i] = 1.0f;
	}
	// Lat/lon default off: on their own they are just numbers, and they are
	// reserved for defining the map rectangle.
	t->elem_enabled[OSD_ELEM_LATITUDE] = false;
	t->elem_enabled[OSD_ELEM_LONGITUDE] = false;
}

static char *trim(char *s) {
	while (*s && isspace((unsigned char)*s))
		s++;
	if (!*s)
		return s;
	char *e = s + strlen(s) - 1;
	while (e > s && isspace((unsigned char)*e))
		*e-- = '\0';
	return s;
}

// Accepts RRGGBB, AARRGGBB, and a leading '#'. Returns false on anything else so
// the caller can keep the default rather than drawing an invisible widget.
static bool parse_color(const char *v, osd_color_t *out) {
	if (!v || !out)
		return false;
	if (*v == '#')
		v++;
	size_t n = strlen(v);
	if (n != 6 && n != 8)
		return false;
	for (size_t i = 0; i < n; i++)
		if (!isxdigit((unsigned char)v[i]))
			return false;

	unsigned long raw = strtoul(v, NULL, 16);
	*out = (n == 6) ? (osd_color_t)(0xFF000000u | raw) : (osd_color_t)raw;
	return true;
}

static bool parse_float(const char *v, float *out) {
	if (!v || !*v)
		return false;
	char *end = NULL;
	float f = strtof(v, &end);
	if (end == v)
		return false;
	*out = f;
	return true;
}

static bool parse_bool(const char *v, bool *out) {
	if (!v)
		return false;
	if (!strcasecmp(v, "on") || !strcasecmp(v, "true") || !strcasecmp(v, "1") ||
		!strcasecmp(v, "yes")) {
		*out = true;
		return true;
	}
	if (!strcasecmp(v, "off") || !strcasecmp(v, "false") || !strcasecmp(v, "0") ||
		!strcasecmp(v, "no")) {
		*out = false;
		return true;
	}
	return false;
}

// Palette colour names, mirroring COLOR_* in bmp/bitmap.h. Named rather than
// numeric so a theme reads as intent, and so a settings UI can offer a list.
static bool parse_palette_color(const char *v, int *out) {
	static const struct {
		const char *name;
		int idx;
	} kNames[] = {{"red", 1}, {"green", 2}, {"blue", 3}, {"yellow", 4}, {"magenta", 5},
		{"cyan", 6}, {"white", 7}, {"black", 8}, {"gray", 10}, {"grey", 10}, {NULL, 0}};
	if (!v)
		return false;
	for (int i = 0; kNames[i].name; i++) {
		if (!strcasecmp(v, kNames[i].name)) {
			*out = kNames[i].idx;
			return true;
		}
	}
	return false;
}

static float clampf(float v, float lo, float hi) {
	return v < lo ? lo : (v > hi ? hi : v);
}

// Element keys are "<type>" and "<type>_opacity", so the names in the file match
// what osd_element_type_name() reports and what a settings UI would list.
static int element_index(const char *name) {
	for (int i = 1; i < OSD_ELEM_TYPE_COUNT; i++)
		if (!strcasecmp(name, osd_element_type_name((osd_element_type_t)i)))
			return i;
	return -1;
}

// Returns the element index for a key like "voltage_scale", or -1.
static int strip_suffix_index(const char *key, const char *suffix) {
	size_t klen = strlen(key), slen = strlen(suffix);
	if (klen <= slen || strcasecmp(key + klen - slen, suffix) != 0)
		return -1;
	char base[32];
	size_t n = klen - slen;
	if (n >= sizeof(base))
		return -1;
	memcpy(base, key, n);
	base[n] = '\0';
	return element_index(base);
}

static void apply_kv(osd_theme_t *t, const char *section, const char *key, const char *val) {
	float f;
	bool b;
	osd_color_t rgba;

	if (!strcasecmp(section, "osd")) {
		if (!strcasecmp(key, "mode")) {
			if (!strcasecmp(val, "classic"))
				t->mode = OSD_MODE_CLASSIC;
			else if (!strcasecmp(val, "fancy"))
				t->mode = OSD_MODE_FANCY;
		} else if (!strcasecmp(key, "opacity") && parse_float(val, &f)) {
			t->global_opacity = clampf(f, 0.0f, 1.0f);
		} else if (!strcasecmp(key, "scale") && parse_float(val, &f)) {
			t->global_scale = clampf(f, 0.3f, 4.0f);
		}
		return;
	}

	if (!strcasecmp(section, "theme")) {
		if (!strcasecmp(key, "element_hold_ms") && parse_float(val, &f))
			t->element_hold_ms = clampf(f, 0.0f, 10000.0f);
		else if (!strcasecmp(key, "name"))
			snprintf(t->name, sizeof(t->name), "%s", val);
		else if (!strcasecmp(key, "font"))
			snprintf(t->font_path, sizeof(t->font_path), "%s", val);
		else if (!strcasecmp(key, "panel_min_width") && parse_float(val, &f))
			t->panel_min_width = clampf(f, 40.0f, 4096.0f);
		else if (!strcasecmp(key, "panel_height") && parse_float(val, &f))
			t->panel_height = clampf(f, 20.0f, 2048.0f);
		else if (!strcasecmp(key, "tab_height") && parse_float(val, &f))
			t->tab_height = clampf(f, 0.0f, 512.0f);
		else if (!strcasecmp(key, "chamfer") && parse_float(val, &f))
			t->chamfer = clampf(f, 0.0f, 256.0f);
		else if (!strcasecmp(key, "pad_x") && parse_float(val, &f))
			t->pad_x = clampf(f, 0.0f, 256.0f);
		else if (!strcasecmp(key, "pad_y") && parse_float(val, &f))
			t->pad_y = clampf(f, 0.0f, 256.0f);
		else if (!strcasecmp(key, "bar_height") && parse_float(val, &f))
			t->bar_height = clampf(f, 1.0f, 256.0f);
		else if (!strcasecmp(key, "value_size") && parse_float(val, &f))
			t->value_size = clampf(f, 6.0f, 200.0f);
		else if (!strcasecmp(key, "label_size") && parse_float(val, &f))
			t->label_size = clampf(f, 4.0f, 200.0f);
		else if (!strcasecmp(key, "label_tracking") && parse_float(val, &f))
			t->label_tracking = clampf(f, -10.0f, 40.0f);
		else if (!strcasecmp(key, "text_outline") && parse_bool(val, &b))
			t->text_outline = b;
		else if (!strcasecmp(key, "text_outline_color") && parse_color(val, &rgba))
			t->text_outline_color = rgba;
		else if (!strcasecmp(key, "text_outline_width") && parse_float(val, &f))
			t->text_outline_width = (int)clampf(f, 1.0f, 3.0f);
		else if (!strcasecmp(key, "hatch_period") && parse_float(val, &f))
			t->hatch_period = clampf(f, 1.0f, 128.0f);
		else if (!strcasecmp(key, "hatch_duty") && parse_float(val, &f))
			t->hatch_duty = clampf(f, 0.05f, 1.0f);
		else if (!strcasecmp(key, "hatch_slant") && parse_float(val, &f))
			t->hatch_slant = clampf(f, -4.0f, 4.0f);
		return;
	}

	if (!strcasecmp(section, "colors") || !strcasecmp(section, "colours")) {
		osd_color_t c;
		if (!parse_color(val, &c))
			return;
		if (!strcasecmp(key, "accent"))
			t->accent = c;
		else if (!strcasecmp(key, "warn"))
			t->warn = c;
		else if (!strcasecmp(key, "crit"))
			t->crit = c;
		else if (!strcasecmp(key, "good"))
			t->good = c;
		else if (!strcasecmp(key, "threat"))
			t->threat = c;
		else if (!strcasecmp(key, "panel_fill"))
			t->panel_fill = c;
		else if (!strcasecmp(key, "panel_edge"))
			t->panel_edge = c;
		else if (!strcasecmp(key, "track"))
			t->track = c;
		else if (!strcasecmp(key, "label"))
			t->label = c;
		else if (!strcasecmp(key, "peak"))
			t->peak = c;
		return;
	}

	if (!strcasecmp(section, "ahi")) {
		int col;
		if (!strcasecmp(key, "scheme")) {
			// Presets, so the common choices are one word. Each still sets the
			// same four colours, which "custom" then lets a theme override.
			if (!strcasecmp(val, "classic")) { // msposd's original
				t->ahi_level_color = 2;
				t->ahi_moderate_color = 4;
				t->ahi_steep_color = 1;
				t->ahi_line_color = 7;
			} else if (!strcasecmp(val, "tactical")) {
				t->ahi_level_color = 6;
				t->ahi_moderate_color = 4;
				t->ahi_steep_color = 1;
				t->ahi_line_color = 7;
			} else if (!strcasecmp(val, "mono")) {
				t->ahi_level_color = 7;
				t->ahi_moderate_color = 7;
				t->ahi_steep_color = 7;
				t->ahi_line_color = 7;
			} else if (!strcasecmp(val, "heat")) {
				t->ahi_level_color = 6;
				t->ahi_moderate_color = 5;
				t->ahi_steep_color = 1;
				t->ahi_line_color = 3;
			}
		} else if (!strcasecmp(key, "level_color") && parse_palette_color(val, &col))
			t->ahi_level_color = col;
		else if (!strcasecmp(key, "moderate_color") && parse_palette_color(val, &col))
			t->ahi_moderate_color = col;
		else if (!strcasecmp(key, "steep_color") && parse_palette_color(val, &col))
			t->ahi_steep_color = col;
		else if (!strcasecmp(key, "line_color") && parse_palette_color(val, &col))
			t->ahi_line_color = col;
		else if (!strcasecmp(key, "level_max") && parse_float(val, &f))
			t->ahi_level_max = clampf(f, 0.0f, 90.0f);
		else if (!strcasecmp(key, "moderate_max") && parse_float(val, &f))
			t->ahi_moderate_max = clampf(f, 0.0f, 90.0f);
		else if (!strcasecmp(key, "steep_thickness") && parse_float(val, &f))
			t->ahi_steep_thickness = (int)clampf(f, 1.0f, 12.0f);
		return;
	}

	if (!strcasecmp(section, "heading")) {
		if (!strcasecmp(key, "style")) {
			if (!strcasecmp(val, "band"))
				t->heading_style = 0;
			else if (!strcasecmp(val, "rose"))
				t->heading_style = 1;
			else if (!strcasecmp(val, "ring"))
				t->heading_style = 2;
			else if (!strcasecmp(val, "navball"))
				t->heading_style = 3;
			else if (!strcasecmp(val, "numeric"))
				t->heading_style = 4;
		} else if (!strcasecmp(key, "size") && parse_float(val, &f))
			t->heading_size = clampf(f, 60.0f, 1920.0f);
		else if (!strcasecmp(key, "span") && parse_float(val, &f))
			// Band only. Under 45 degrees the tape scrolls too fast to read a
			// turn; over 120 the marks are too close together to tell apart.
			t->heading_span = clampf(f, 45.0f, 120.0f);
		else if (!strcasecmp(key, "outline") && parse_bool(val, &b))
			t->heading_outline = b;
		else if (!strcasecmp(key, "outline_width") && parse_float(val, &f))
			t->heading_outline_width = clampf(f, 1.0f, 5.0f);
		else if (!strcasecmp(key, "show_track") && parse_bool(val, &b))
			t->heading_show_track = b;
		else if (!strcasecmp(key, "flip") && parse_bool(val, &b))
			t->heading_flip = b;
		else if (!strcasecmp(key, "lens") && parse_float(val, &f))
			// Below ~0.3 the centre is stretched past the point of reading.
			t->heading_lens = clampf(f, 0.3f, 1.0f);
		return;
	}

	if (!strcasecmp(section, "map")) {
		if (!strcasecmp(key, "enabled") && parse_bool(val, &b))
			t->map_enabled = b;
		else if (!strcasecmp(key, "style")) {
			if (!strcasecmp(val, "roads"))
				t->map_style = 0;
			else if (!strcasecmp(val, "satellite"))
				t->map_style = 1;
			else if (!strcasecmp(val, "hybrid"))
				t->map_style = 2;
		} else if (!strcasecmp(key, "zoom") && parse_float(val, &f))
			t->map_zoom = (int)clampf(f, 1.0f, 19.0f);
		else if (!strcasecmp(key, "opacity") && parse_float(val, &f))
			t->map_opacity = clampf(f, 0.0f, 1.0f);
		else if (!strcasecmp(key, "max_width") && parse_float(val, &f))
			t->map_max_w = (int)clampf(f, 80.0f, 4096.0f);
		else if (!strcasecmp(key, "max_height") && parse_float(val, &f))
			t->map_max_h = (int)clampf(f, 60.0f, 2160.0f);
		else if (!strcasecmp(key, "cache_dir"))
			snprintf(t->map_cache_dir, sizeof(t->map_cache_dir), "%s", val);
		else if (!strcasecmp(key, "orientation")) {
			if (!strcasecmp(val, "north"))
				t->map_orientation = 0;
			else if (!strcasecmp(val, "track") || !strcasecmp(val, "course"))
				t->map_orientation = 1;
			else if (!strcasecmp(val, "heading") || !strcasecmp(val, "nose"))
				t->map_orientation = 2;
		} else if (!strcasecmp(key, "auto_zoom") && parse_bool(val, &b))
			t->map_auto_zoom = b;
		else if (!strcasecmp(key, "zoom_min") && parse_float(val, &f))
			t->map_zoom_min = (int)clampf(f, 1.0f, 19.0f);
		else if (!strcasecmp(key, "zoom_max") && parse_float(val, &f))
			t->map_zoom_max = (int)clampf(f, 1.0f, 19.0f);
		else if (!strcasecmp(key, "lookahead") && parse_float(val, &f))
			t->map_lookahead_s = clampf(f, 1.0f, 120.0f);
		else if (!strcasecmp(key, "lead") && parse_float(val, &f))
			t->map_lead_s = clampf(f, 0.0f, 60.0f);
		else if (!strcasecmp(key, "lead_max") && parse_float(val, &f))
			t->map_lead_max = clampf(f, 0.0f, 0.9f);
		else if (!strcasecmp(key, "zoom_settle_ms") && parse_float(val, &f))
			t->map_zoom_settle_ms = clampf(f, 0.0f, 30000.0f);
		else if (!strcasecmp(key, "smooth_ms") && parse_float(val, &f))
			t->map_smooth_ms = clampf(f, 0.0f, 10000.0f);
		return;
	}

	if (!strcasecmp(section, "voltage")) {
		if (!strcasecmp(key, "cell_min") && parse_float(val, &f))
			t->cell_min = f;
		else if (!strcasecmp(key, "cell_max") && parse_float(val, &f))
			t->cell_max = f;
		else if (!strcasecmp(key, "cell_warn") && parse_float(val, &f))
			t->cell_warn = f;
		else if (!strcasecmp(key, "cell_crit") && parse_float(val, &f))
			t->cell_crit = f;
		return;
	}

	if (!strcasecmp(section, "elements")) {
		// "<type>", "<type>_opacity" and "<type>_scale"
		int idx = strip_suffix_index(key, "_opacity");
		if (idx >= 0) {
			if (parse_float(val, &f))
				t->elem_opacity[idx] = clampf(f, 0.0f, 1.0f);
			return;
		}
		idx = strip_suffix_index(key, "_scale");
		if (idx >= 0) {
			if (parse_float(val, &f))
				t->elem_scale[idx] = clampf(f, 0.3f, 4.0f);
			return;
		}
		idx = element_index(key);
		if (idx >= 0 && parse_bool(val, &b))
			t->elem_enabled[idx] = b;
	}
}

bool osd_theme_load(osd_theme_t *t, const char *path) {
	if (!t || !path)
		return false;
	FILE *fp = fopen(path, "r");
	if (!fp)
		return false;

	// Parse into a copy: a file that fails halfway must not leave a
	// half-applied theme behind.
	osd_theme_t tmp = *t;

	char line[512];
	char section[64] = "";
	while (fgets(line, sizeof(line), fp)) {
		char *s = trim(line);
		if (!*s || *s == '#' || *s == ';')
			continue;

		if (*s == '[') {
			char *end = strchr(s, ']');
			if (!end)
				continue;
			*end = '\0';
			snprintf(section, sizeof(section), "%s", trim(s + 1));
			continue;
		}

		char *eq = strchr(s, '=');
		if (!eq)
			continue;
		*eq = '\0';
		char *key = trim(s);
		char *val = trim(eq + 1);
		// strip trailing inline comment
		for (char *c = val; *c; c++) {
			if (*c == ';' || *c == '#') {
				*c = '\0';
				break;
			}
		}
		val = trim(val);
		if (*key)
			apply_kv(&tmp, section, key, val);
	}
	fclose(fp);

	*t = tmp;
	return true;
}

bool osd_theme_reload_if_changed(osd_theme_t *t, const char *path) {
	static time_t last_mtime = 0;
	static char last_path[512] = "";

	if (!t || !path)
		return false;

	struct stat st;
	if (stat(path, &st) != 0)
		return false;

	if (strcmp(last_path, path) == 0 && st.st_mtime == last_mtime)
		return false;

	snprintf(last_path, sizeof(last_path), "%s", path);
	last_mtime = st.st_mtime;
	return osd_theme_load(t, path);
}

bool osd_theme_element_enabled(const osd_theme_t *t, osd_element_type_t type) {
	if (!t || type <= OSD_ELEM_NONE || type >= OSD_ELEM_TYPE_COUNT)
		return false;
	return t->mode == OSD_MODE_FANCY && t->elem_enabled[type];
}

float osd_theme_element_opacity(const osd_theme_t *t, osd_element_type_t type) {
	if (!t || type <= OSD_ELEM_NONE || type >= OSD_ELEM_TYPE_COUNT)
		return 0.0f;
	return clampf(t->elem_opacity[type] * t->global_opacity, 0.0f, 1.0f);
}

float osd_theme_element_scale(const osd_theme_t *t, osd_element_type_t type) {
	if (!t || type <= OSD_ELEM_NONE || type >= OSD_ELEM_TYPE_COUNT)
		return 1.0f;
	return clampf(t->elem_scale[type] * t->global_scale, 0.3f, 4.0f);
}

osd_color_t osd_theme_apply_opacity(osd_color_t c, float opacity) {
	int a = (int)(OSD_A(c) * clampf(opacity, 0.0f, 1.0f) + 0.5f);
	return OSD_ARGB(a, OSD_R(c), OSD_G(c), OSD_B(c));
}

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

	t->panel_min_width = 340.0f;
	t->panel_height = 150.0f;
	t->tab_height = 48.0f;
	t->chamfer = 20.0f;
	t->pad_x = 20.0f;
	t->pad_y = 18.0f;
	t->bar_height = 28.0f;
	t->value_size = 32.0f;
	t->label_size = 13.0f;
	t->label_tracking = 2.5f;

	t->hatch_period = 9.0f;
	t->hatch_duty = 0.64f;
	t->hatch_slant = -0.45f;

	t->cell_min = 3.0f;
	t->cell_max = 4.35f;
	t->cell_warn = 3.60f;
	t->cell_crit = 3.40f;

	for (int i = 0; i < OSD_ELEM_TYPE_COUNT; i++) {
		t->elem_enabled[i] = true;
		t->elem_opacity[i] = 1.0f;
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

static void apply_kv(osd_theme_t *t, const char *section, const char *key, const char *val) {
	float f;
	bool b;

	if (!strcasecmp(section, "osd")) {
		if (!strcasecmp(key, "mode")) {
			if (!strcasecmp(val, "classic"))
				t->mode = OSD_MODE_CLASSIC;
			else if (!strcasecmp(val, "fancy"))
				t->mode = OSD_MODE_FANCY;
		} else if (!strcasecmp(key, "opacity") && parse_float(val, &f)) {
			t->global_opacity = clampf(f, 0.0f, 1.0f);
		}
		return;
	}

	if (!strcasecmp(section, "theme")) {
		if (!strcasecmp(key, "name"))
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
		const char *suffix = strstr(key, "_opacity");
		if (suffix && suffix[8] == '\0') {
			char base[32];
			size_t n = (size_t)(suffix - key);
			if (n >= sizeof(base))
				return;
			memcpy(base, key, n);
			base[n] = '\0';
			int idx = element_index(base);
			if (idx >= 0 && parse_float(val, &f))
				t->elem_opacity[idx] = clampf(f, 0.0f, 1.0f);
		} else {
			int idx = element_index(key);
			if (idx >= 0 && parse_bool(val, &b))
				t->elem_enabled[idx] = b;
		}
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

osd_color_t osd_theme_apply_opacity(osd_color_t c, float opacity) {
	int a = (int)(OSD_A(c) * clampf(opacity, 0.0f, 1.0f) + 0.5f);
	return OSD_ARGB(a, OSD_R(c), OSD_G(c), OSD_B(c));
}

// Theme and OSD-mode configuration.
//
// A theme is a plain ini file, distributed the same way custom fonts already
// are: drop a folder next to the binary (or in the font directory on a camera).
// Nothing about the widget look is hard-coded, so a theme can be shared by
// zipping that folder.
//
// Parsing is deliberately forgiving: unknown keys are ignored and malformed
// values keep their default, because a broken user theme must never take the
// OSD down mid-flight.
#pragma once

#include "../elements/osd_elements.h"
#include "osd_paint.h"

typedef enum {
	OSD_MODE_CLASSIC = 0, // the FC's own glyph text, untouched
	OSD_MODE_FANCY = 1,   // graphical widgets
} osd_mode_t;

typedef struct {
	char name[64];
	char font_path[256];

	osd_mode_t mode;
	float global_opacity; // 0..1, multiplies every widget's alpha

	// palette
	osd_color_t accent, warn, crit, good, threat;
	osd_color_t panel_fill, panel_edge, track, label, peak;

	// geometry, in pixels unless noted
	float panel_min_width, panel_height, tab_height, chamfer;
	float pad_x, pad_y, bar_height;
	float value_size, label_size, label_tracking;

	// hatching
	float hatch_period, hatch_duty, hatch_slant;

	// per-cell voltage scale and thresholds
	float cell_min, cell_max, cell_warn, cell_crit;

	// per-element switches, indexed by osd_element_type_t
	bool elem_enabled[OSD_ELEM_TYPE_COUNT];
	float elem_opacity[OSD_ELEM_TYPE_COUNT];
} osd_theme_t;

/// Fills `t` with the built-in "Tactical" theme. Always succeeds, so there is
/// always something sane to draw with.
void osd_theme_defaults(osd_theme_t *t);

/// Merges `path` over whatever is already in `t`. Returns false if the file
/// could not be read, leaving `t` untouched and usable.
bool osd_theme_load(osd_theme_t *t, const char *path);

/// Reloads only when the file's mtime changed, so settings can be edited live
/// by any front-end without msposd polling the parser every frame.
bool osd_theme_reload_if_changed(osd_theme_t *t, const char *path);

bool osd_theme_element_enabled(const osd_theme_t *t, osd_element_type_t type);
/// Combined per-element and global opacity, clamped to 0..1.
float osd_theme_element_opacity(const osd_theme_t *t, osd_element_type_t type);
/// Applies an opacity factor to a colour's alpha channel.
osd_color_t osd_theme_apply_opacity(osd_color_t c, float opacity);

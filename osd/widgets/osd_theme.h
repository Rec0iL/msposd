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
	float global_scale;   // multiplies every widget's size

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

	// How long a vanished element keeps being drawn from cache. Flight
	// controllers blink a critical value by alternating it with blank, so this
	// must comfortably exceed the blink off-period or the widget flickers away
	// exactly when the reading matters most.
	float element_hold_ms;

	// Map. The rectangle is defined by where the latitude and longitude
	// elements were placed on the flight controller: latitude marks the
	// top-left corner, longitude the bottom-right.
	bool map_enabled;
	int map_style; // osd_map_style_t
	int map_zoom;
	float map_opacity;
	// Upper bound on the rectangle the elements span. Placing latitude and
	// longitude far apart would otherwise cover most of the video, which is
	// unflyable - a minimap is the point.
	int map_max_w, map_max_h;
	char map_cache_dir[256];
	// Speed-driven view. A fixed zoom is wrong at both ends of the speed range:
	// hovering it shows a patch you could walk across, and at 30 m/s it shows
	// where you have been. See osd_map_view_cfg_t for what each knob does.
	// 0 = north is up; 1 = the ground track is up; 2 = the heading is up, which
	// is what agrees with a nose-mounted camera - in wind the two differ. Either
	// turning mode costs a fixed frame of reference, so the compass needle is
	// drawn whenever one is on.
	int map_orientation;
	bool map_auto_zoom;
	int map_zoom_min, map_zoom_max;
	float map_lookahead_s;
	float map_lead_s;
	float map_lead_max;
	float map_zoom_settle_ms;
	float map_smooth_ms;

	// Artificial horizon. Colouring stays pitch-based - level, moderate and
	// steep read differently at a glance, which is the point of the thing - but
	// which colours those are comes from the theme.
	//
	// These are indices into msposd's drawing palette, not RGBA: the AHI is
	// drawn through the same fixed colour table the glyph layer uses. Values
	// mirror COLOR_* in bmp/bitmap.h.
	int ahi_level_color;    // |pitch| below ahi_level_max
	int ahi_moderate_color; // |pitch| below ahi_moderate_max
	int ahi_steep_color;    // beyond that
	int ahi_line_color;     // the ladder segments either side of centre
	float ahi_level_max;    // degrees
	float ahi_moderate_max; // degrees
	int ahi_steep_thickness;

	// per-element switches, indexed by osd_element_type_t
	bool elem_enabled[OSD_ELEM_TYPE_COUNT];
	float elem_opacity[OSD_ELEM_TYPE_COUNT];
	float elem_scale[OSD_ELEM_TYPE_COUNT];
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
/// Combined per-element and global size multiplier, clamped to a usable range.
float osd_theme_element_scale(const osd_theme_t *t, osd_element_type_t type);
/// Applies an opacity factor to a colour's alpha channel.
osd_color_t osd_theme_apply_opacity(osd_color_t c, float opacity);

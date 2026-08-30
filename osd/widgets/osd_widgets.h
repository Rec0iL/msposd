// Draws recognised OSD elements as graphical widgets.
//
// Placement follows the element: its top-left grid cell becomes the widget's
// top-left corner and the panel grows right and down. That keeps the layout the
// user configured on the flight controller meaningful - an element near an edge
// stays near that edge.
#pragma once

#include "../elements/osd_elements.h"
#include "osd_heading.h"
#include "osd_map.h"
#include "osd_paint.h"
#include "osd_text.h"
#include "osd_theme.h"

typedef struct {
	float current_peak; // highest current seen this flight, amps
	// Cell count inferred from pack voltage, so a percentage can be shown
	// without the user configuring anything. 0 until a battery is seen.
	int cell_count;
	float heading_deg; // aircraft heading, for the map marker
	// Attitude, for the navball. Filtered on msposd's own fast constant rather
	// than the heading's slower one: the artificial horizon is drawn from the
	// same numbers a few hundred pixels away, and the two must not visibly
	// disagree through a roll.
	float pitch_deg;
	float roll_deg;
	// Ground track, from MSP_RAW_GPS. The map zooms out with speed and leads
	// along the course, so what is ahead of the aircraft gets the screen.
	// Course rather than heading: in wind the nose and the track differ.
	float ground_speed_mps;
	float course_deg;
	osd_map_view_t map_view; // smoothed zoom and view centre
	// Heading and track for the compass display, smoothed on their own clock.
	// The map's smoothing is far slower - it is a view, and a view that snaps
	// around is unusable - but a compass that lags a turn by a second is
	// actively misleading, so the two cannot share one filter.
	osd_heading_smooth_t heading_smooth;
	// Launch point, captured at arming like the flight controller does.
	bool home_valid;
	double home_lat, home_lon;
	bool prev_armed;
	int overlap_warnings; // panels that had to be pushed clear, for logging

	// Last-seen cache, one slot per element type. Flight controllers blank a
	// field while updating it, and blink critical values by alternating them
	// with blank; without this the widget vanishes and the raw glyphs flash
	// through underneath. Hold duration comes from the theme.
	osd_element_t last[OSD_ELEM_TYPE_COUNT];
	uint64_t last_seen_ms[OSD_ELEM_TYPE_COUNT];

	// Resolved layout, held still once decided. Collision resolution depends on
	// panel widths, and widths follow the value text, so re-running it every
	// frame made panels shuffle as readings changed - "137" is wider than "70".
	// The flight controller's layout does not move in flight, so neither should
	// ours: positions are recomputed only when the set of elements or their grid
	// positions actually changes.
	struct {
		bool valid;
		uint8_t row, col;
		float x, y, w, h;
	} layout[OSD_ELEM_TYPE_COUNT];
	uint32_t layout_signature;
} osd_widget_state_t;

void osd_widgets_state_init(osd_widget_state_t *st);

/// Resets the peak on the disarmed -> armed edge, so "peak" means this flight.
void osd_widgets_update_arm(osd_widget_state_t *st, bool armed);

/// Maps a run of grid cells to its pixel rectangle.
typedef void (*osd_cell_rect_fn)(int col, int row, int span, void *ctx, int *x, int *y, int *w,
	int *h);

/// Grid geometry. cell_w/cell_h describe the uniform case; `cell_rect` overrides
/// it when the host places glyphs non-uniformly. msposd does exactly that in
/// small-font mode, where rows are 36px not 54px and the edges are bottom- and
/// right-aligned, so assuming a uniform grid clears the wrong pixels.
typedef struct {
	int cell_w, cell_h;
	int off_x, off_y;
	osd_cell_rect_fn cell_rect;
	void *ctx;
} osd_grid_t;

/// Bounding box of the compass display, centred on (cx, cy). Panels are pushed
/// clear of this, so it is exposed rather than kept private: a test that
/// recomputed the geometry itself could agree with a stale copy of it.
void osd_widgets_heading_box(
	const osd_theme_t *th, float cx, float cy, float *x, float *y, float *w, float *h);

/// Returns the number of widgets drawn.
int osd_widgets_draw_all(osd_surface_t *s,
	const osd_theme_t *th,
	osd_font_t *font,
	osd_widget_state_t *st,
	const osd_element_t *els,
	int count,
	const osd_grid_t *grid,
	uint64_t now_ms);

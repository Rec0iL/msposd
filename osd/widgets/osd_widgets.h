// Draws recognised OSD elements as graphical widgets.
//
// Placement follows the element: its top-left grid cell becomes the widget's
// top-left corner and the panel grows right and down. That keeps the layout the
// user configured on the flight controller meaningful - an element near an edge
// stays near that edge.
#pragma once

#include "../elements/osd_elements.h"
#include "osd_paint.h"
#include "osd_text.h"
#include "osd_theme.h"

typedef struct {
	float current_peak; // highest current seen this flight, amps
	bool prev_armed;
	int overlap_warnings; // panels that collided, for logging
} osd_widget_state_t;

void osd_widgets_state_init(osd_widget_state_t *st);

/// Resets the peak on the disarmed -> armed edge, so "peak" means this flight.
void osd_widgets_update_arm(osd_widget_state_t *st, bool armed);

/// Grid geometry: cell size in pixels and the overlay's origin offset.
typedef struct {
	int cell_w, cell_h;
	int off_x, off_y;
} osd_grid_t;

/// Returns the number of widgets drawn.
int osd_widgets_draw_all(osd_surface_t *s,
	const osd_theme_t *th,
	osd_font_t *font,
	osd_widget_state_t *st,
	const osd_element_t *els,
	int count,
	const osd_grid_t *grid);

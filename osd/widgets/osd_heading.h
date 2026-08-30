#pragma once

// Heading display, in several styles.
//
// The flight controller's compass bar says *where* this goes; the heading
// itself comes from MSP_ATTITUDE, so nothing here depends on decoding which way
// the bar's glyphs happen to be pointing. Both firmwares fix the bar's width,
// so the size comes from the theme rather than from the run.
//
// Everything that can show a bearing shows the same three: where the nose
// points, where the aircraft is actually going, and where home is. On a fixed
// wing the first two differ by the crab angle, which is invisible in an FPV
// feed - that gap is the point of drawing the track at all.
//
// Pure drawing: no msposd or platform dependencies, so it can be exercised on
// its own like the rest of the widget layer.

#include "osd_paint.h"
#include "osd_text.h"

#include <stdbool.h>
#include <stdint.h>

typedef enum {
	OSD_HEADING_BAND = 0, // scrolling tape, as a jet HUD does it
	OSD_HEADING_ROSE,     // round dial, nose up
	OSD_HEADING_RING,     // the tape in perspective, centre magnified
	OSD_HEADING_NAVBALL,  // sphere carrying pitch and roll as well
	OSD_HEADING_NUMERIC,  // just the number
} osd_heading_style_t;

/// Smoothed direction, kept as vectors so a bearing crossing north eases the
/// short way round rather than sweeping the long way through south.
typedef struct {
	bool valid;
	uint64_t last_ms;
	double hdg_e, hdg_n;
	double trk_e, trk_n;
} osd_heading_smooth_t;

void osd_heading_smooth_init(osd_heading_smooth_t *s);

/// Advances the smoothing one frame. `track_valid` is false when the aircraft
/// is too slow for its reported course to mean anything, in which case the
/// track is held rather than eased toward noise.
void osd_heading_smooth_update(osd_heading_smooth_t *s, float heading_deg, float track_deg,
	bool track_valid, uint64_t now_ms, float tau_ms);

float osd_heading_smooth_heading(const osd_heading_smooth_t *s);
float osd_heading_smooth_track(const osd_heading_smooth_t *s);

typedef struct {
	osd_heading_style_t style;
	float size;      // band/ring width, or rose/navball diameter
	float span_deg;  // band only: degrees visible across the tape
	bool show_track; // draw the ground-track marker
	bool flip;       // ring only: curve the other way
	float lens;      // ring only: <1 magnifies the centre, 1 is flat

	// Outline behind the whole display, geometry included - ticks, markers and
	// the aircraft glyph, not just the numbers. A compass is a lot of thin
	// lines, and thin lines over snow or sky disappear; this is what keeps them
	// readable without putting a backing plate over the video.
	bool outline;
	osd_color_t outline_color;
	float outline_px;

	// Palette, already faded by the caller's opacity.
	osd_color_t accent, label, track, home, fill, edge;
	// That same opacity, 0..1, kept separately for the navball: its sky and
	// ground are not theme colours - they are what makes up read as up - so
	// they cannot be pre-faded like the rest of the palette.
	float opacity;

	float heading_deg; // where the nose points
	float track_deg;   // where the aircraft is going
	bool track_valid;
	float home_deg; // bearing to the launch point
	bool home_valid;
	float pitch_deg, roll_deg; // navball only
} osd_heading_params_t;

/// Draws the display centred on (cx, cy).
void osd_heading_draw(
	osd_surface_t *s, osd_font_t *font, float cx, float cy, const osd_heading_params_t *p);

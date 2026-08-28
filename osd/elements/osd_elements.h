// Element recognition for MSP DisplayPort.
//
// MSP DisplayPort carries no semantics - only glyph codes at grid cells. To
// render widgets instead of characters we have to recover meaning from the
// grid, which is possible because flight controllers mark their OSD elements
// with fixed symbol glyphs (a volt symbol after the number, a lat symbol before
// it, and so on). Scanning for those anchors gives us the element's type, its
// value and, crucially, the position the *user* chose on the FC.
//
// Deliberately free of platform and msposd dependencies so it can be unit
// tested on its own and reused wherever msposd runs, PixelPilot included.
#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
	OSD_ELEM_NONE = 0,
	OSD_ELEM_VOLTAGE,
	OSD_ELEM_CURRENT,
	OSD_ELEM_MAH,
	OSD_ELEM_ALTITUDE,
	OSD_ELEM_LATITUDE,
	OSD_ELEM_LONGITUDE,
	OSD_ELEM_RSSI,
	OSD_ELEM_SATS,
	OSD_ELEM_THROTTLE,
	OSD_ELEM_FLIGHT_TIME,
	OSD_ELEM_FLIGHT_MODE,
	OSD_ELEM_WARNING,
	OSD_ELEM_TYPE_COUNT
} osd_element_type_t;

typedef struct {
	osd_element_type_t type;
	uint8_t row;   // grid row
	uint8_t col;   // leftmost grid column covered, including the symbol glyph
	uint8_t width; // number of grid cells covered
	// The edge of the run the flight controller keeps still. `col` and `width`
	// track the glyphs actually on screen, so they move as a value gains or
	// loses a digit (99 -> 100); anchoring a widget to them makes it jump.
	// Values are right-aligned in their field, so for a trailing symbol the
	// run grows leftwards and only its right edge is fixed.
	uint8_t anchor_col;
	bool anchor_right; // anchor_col is the run's rightmost cell, not its leftmost
	float value;
	bool value_valid;
	// True when a voltage looks like a single cell rather than a whole pack.
	// INAV/BF mark both with SYM_VOLT, so only magnitude can tell them apart.
	bool is_per_cell;
	// True when a battery icon sits beside the value, which is what identifies
	// this as *battery* voltage rather than any other volt reading. The icon is
	// absorbed into the element so it is not left behind next to the widget.
	bool has_battery_icon;
	// 0 = full .. 6 = empty, the flight controller's own coarse gauge.
	int battery_level;
	// For warnings: how loudly to shout. A failsafe and a "LANDED" notice are
	// both messages, but only one of them should be red.
	enum { OSD_SEV_INFO = 0, OSD_SEV_WARN, OSD_SEV_CRIT } severity;
	// Some values are not numbers (flight time "05:36", mode "ACRO", and OSD
	// messages); for those `value_valid` is false and only `text` is meaningful.
	// Sized for the longest message the firmwares emit, e.g.
	// "ACCELEROMETER NOT CALIBRATED".
	char text[48];
} osd_element_t;

/// Reads one glyph from the host's character map.
typedef uint16_t (*osd_glyph_getter)(int col, int row, void *ctx);

/// Scans the grid and fills `out`. Returns the number of elements found.
/// `fc_identifier` is the 4-char MSP FC variant ("INAV", "BTFL", "ARDU");
/// symbol codes differ per firmware.
int osd_elements_scan(osd_glyph_getter get,
	void *ctx,
	int cols,
	int rows,
	const char *fc_identifier,
	osd_element_t *out,
	int max_out);

const char *osd_element_type_name(osd_element_type_t type);

/// True when the flight controller has taken over the screen with its
/// post-flight summary. That page is a dense full-screen table with its own
/// layout, so widgets must stand aside and let the original text through.
bool osd_elements_is_summary_screen(osd_glyph_getter get, void *ctx, int cols, int rows);

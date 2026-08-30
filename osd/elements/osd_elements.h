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
	// The firmware's compass bar: a contiguous run of graphic glyphs. Only its
	// *position* is of interest - the heading itself comes from MSP_ATTITUDE,
	// the same source the artificial horizon uses. Recognising it is how the
	// pilot says "put a heading display here", exactly as latitude and
	// longitude say where the map goes.
	OSD_ELEM_HEADING_BAR,
	// Speed over ground, or airspeed where a pitot is fitted. Betaflight marks
	// airspeed by putting an 'a' between the symbol and the digits.
	OSD_ELEM_SPEED,
	OSD_ELEM_VARIO,          // climb rate, sign taken from the arrow glyph
	OSD_ELEM_HOME_DISTANCE,  // straight-line distance back to the launch point
	OSD_ELEM_TOTAL_DISTANCE, // distance flown this flight
	OSD_ELEM_TEMPERATURE,
	OSD_ELEM_LINK_QUALITY,
	// Betaflight draws RSSI percent, RSSI in dBm and link SNR with the same
	// SYM_RSSI, so these three can only be told apart by magnitude - see
	// classify_rssi(). Getting it wrong is worse than not showing it: -94 dBm
	// rendered as a percentage reads as a healthy link.
	OSD_ELEM_RSSI_DBM,
	OSD_ELEM_SNR,
	// The arrow pointing back to the launch point. Like the compass bar it
	// carries no reading - the glyph *is* the value, and we redraw the bearing
	// ourselves from the fix - so only its position matters.
	OSD_ELEM_HOME_ARROW,
	// The numerical heading, drawn as an arrow followed by three digits. A
	// separate element from the compass bar, and Betaflight lets both be placed
	// at once, so both are recognised.
	OSD_ELEM_HEADING,
	// Fields with no symbol glyph at all, recognised by the literal text the
	// firmware wraps them in - a trailing 'G', "WH", an "RF:" prefix. See the
	// comment on scan_literal_fields() for what this can and cannot reach.
	OSD_ELEM_GFORCE,
	OSD_ELEM_POWER,       // instantaneous draw in watts
	OSD_ELEM_WATT_HOURS,  // energy used this flight
	OSD_ELEM_RANGEFINDER, // "RF:" then a distance
	// mAh per km, which shares SYM_MAH with capacity used and was being read as
	// it: an efficiency of 180 showed up as 180mAh consumed.
	OSD_ELEM_EFFICIENCY,
	// Uplink transmit power. Carries SYM_RSSI, so "25MW" was being read as 25%
	// signal - a healthy-looking link that says nothing about the link.
	OSD_ELEM_TX_POWER,
	OSD_ELEM_TYPE_COUNT
} osd_element_type_t;

/// The unit a reading was drawn in. Flight controllers convert to the pilot's
/// configured units before drawing and mark the result with a unit glyph, so
/// this is read off the screen rather than assumed - a widget that relabelled
/// feet as metres would be worse than one that showed no label at all.
typedef enum {
	OSD_UNIT_NONE = 0,
	OSD_UNIT_METRES,
	OSD_UNIT_FEET,
	OSD_UNIT_KM,
	OSD_UNIT_MILES,
	OSD_UNIT_KPH,
	OSD_UNIT_MPH,
	OSD_UNIT_MPS,
	OSD_UNIT_FTPS,
	OSD_UNIT_CELSIUS,
	OSD_UNIT_FAHRENHEIT,
	OSD_UNIT_KNOTS,
	OSD_UNIT_NM,  // nautical miles
	OSD_UNIT_KFT, // thousands of feet, which INAV switches to on its own
} osd_unit_t;

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
	// The trailing unit glyph, where the field carries one. Absorbed into the
	// run so it is not left stranded beside the widget that replaces it.
	osd_unit_t unit;
	// Airspeed rather than ground speed: Betaflight writes an 'a' between the
	// speed symbol and the digits when a pitot tube is fitted.
	bool is_airspeed;
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

/// Short label for a unit, e.g. "m", "km/h", "degC". "" for OSD_UNIT_NONE.
const char *osd_unit_name(osd_unit_t unit);

/// True when the flight controller has taken over the screen with its
/// post-flight summary. That page is a dense full-screen table with its own
/// layout, so widgets must stand aside and let the original text through.
bool osd_elements_is_summary_screen(osd_glyph_getter get, void *ctx, int cols, int rows);

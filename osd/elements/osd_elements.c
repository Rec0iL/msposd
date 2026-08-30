#include "osd_elements.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Symbol glyph codes, taken from each firmware's src/main/drivers/osd_symbols.h.
// They differ between firmwares, so the table is selected by FC variant.
typedef enum {
	VAL_NUMBER, // plain numeric run
	VAL_TIME,   // digits and ':' e.g. "05:36"
} value_kind_t;

typedef struct {
	uint16_t glyph;
	// Optional icon in the cell before `glyph`, absorbed into the element so it
	// is not left rendered beside the widget (satellites use a two-cell icon).
	uint16_t lead_icon;
	osd_element_type_t type;
	bool anchor_leads; // symbol sits before the number
	value_kind_t kind;
	// The field ends in a unit glyph - metres, km/h, degrees C. Reading it tells
	// the widget what to label the number, and absorbing it keeps the glyph from
	// being left on screen beside the widget. Betaflight writes one on every
	// converted quantity; INAV encodes the unit in the symbol itself instead.
	bool trailing_unit;
	// Letters the firmware writes around the symbol, which have to be stepped
	// over rather than parsed. Betaflight puts them on both sides: temperature
	// is "C<sym>42<unit>" with the letter *before*, airspeed is "<sym>a92<unit>"
	// with it after. The table holds one row per letter, so a row whose letter is
	// not on screen is simply the wrong row. 0 for none.
	char prefix; // in the cell before the symbol
	char infix;  // between the symbol and the digits
} osd_anchor_t;

// Betaflight converts to the pilot's configured units before drawing and marks
// the result, so the unit is read off the screen rather than assumed.
static osd_unit_t unit_for_glyph(uint16_t g) {
	switch (g) {
	case 0x0C: return OSD_UNIT_METRES;     // SYM_M
	case 0x0F: return OSD_UNIT_FEET;       // SYM_FT
	case 0x7D: return OSD_UNIT_KM;         // SYM_KM
	case 0x7E: return OSD_UNIT_MILES;      // SYM_MILES
	case 0x9E: return OSD_UNIT_KPH;        // SYM_KPH
	case 0x9D: return OSD_UNIT_MPH;        // SYM_MPH
	case 0x9F: return OSD_UNIT_MPS;        // SYM_MPS
	case 0x99: return OSD_UNIT_FTPS;       // SYM_FTPS
	case 0x0E: return OSD_UNIT_CELSIUS;    // SYM_C
	case 0x0D: return OSD_UNIT_FAHRENHEIT; // SYM_F
	default: return OSD_UNIT_NONE;
	}
}

// INAV encodes the unit in the symbol itself - SYM_ALT_M and SYM_ALT_FT are
// different glyphs - so nothing here carries a trailing unit.
static const osd_anchor_t kAnchorsInav[] = {
	{0x1F, 0, OSD_ELEM_VOLTAGE, false, VAL_NUMBER, false, 0, 0},  // SYM_VOLT
	{0x6A, 0, OSD_ELEM_CURRENT, false, VAL_NUMBER, false, 0, 0},  // SYM_AMP
	{0x99, 0, OSD_ELEM_MAH, false, VAL_NUMBER, false, 0, 0},      // SYM_MAH
	{0x76, 0, OSD_ELEM_ALTITUDE, false, VAL_NUMBER, false, 0, 0}, // SYM_ALT_M
	{0x78, 0, OSD_ELEM_ALTITUDE, false, VAL_NUMBER, false, 0, 0}, // SYM_ALT_FT
	{0x03, 0, OSD_ELEM_LATITUDE, true, VAL_NUMBER, false, 0, 0},  // SYM_LAT
	{0x04, 0, OSD_ELEM_LONGITUDE, true, VAL_NUMBER, false, 0, 0}, // SYM_LON
	{0x01, 0, OSD_ELEM_RSSI, true, VAL_NUMBER, false, 0, 0},      // SYM_RSSI
	{0x09, 0x08, OSD_ELEM_SATS, true, VAL_NUMBER, false, 0, 0},   // SYM_SAT_R, icon SYM_SAT_L
	{0x95, 0, OSD_ELEM_THROTTLE, true, VAL_NUMBER, false, 0, 0},  // SYM_THR
	{0x9F, 0, OSD_ELEM_FLIGHT_TIME, true, VAL_TIME, false, 0, 0}, // SYM_FLY_M
	{0x9E, 0, OSD_ELEM_FLIGHT_TIME, true, VAL_TIME, false, 0, 0}, // SYM_ON_M
	{0xA0, 0, OSD_ELEM_FLIGHT_TIME, true, VAL_TIME, false, 0, 0}, // SYM_CLOCK
};

// Betaflight formats every one of these through osdPrintFloat() or a tfp_sprintf
// alongside it in src/main/osd/osd_elements.c, so the shape of each field - which
// side the symbol sits on, whether a unit follows - is taken from there rather
// than guessed.
static const osd_anchor_t kAnchorsBtfl[] = {
	{0x06, 0, OSD_ELEM_VOLTAGE, false, VAL_NUMBER, false, 0, 0},    // SYM_VOLT
	{0x9A, 0, OSD_ELEM_CURRENT, false, VAL_NUMBER, false, 0, 0},    // SYM_AMP
	{0x07, 0, OSD_ELEM_MAH, false, VAL_NUMBER, false, 0, 0},        // SYM_MAH
	// SYM_ALTITUDE *leads* in Betaflight - osdFormatAltitudeString passes it as
	// osdPrintFloat's leading symbol, with the unit trailing. Treating it as a
	// trailing symbol meant looking left at blank cells, so Betaflight altitude
	// was never recognised at all.
	{0x7F, 0, OSD_ELEM_ALTITUDE, true, VAL_NUMBER, true, 0, 0},     // SYM_ALTITUDE
	{0x89, 0, OSD_ELEM_LATITUDE, true, VAL_NUMBER, false, 0, 0},    // SYM_LAT
	{0x98, 0, OSD_ELEM_LONGITUDE, true, VAL_NUMBER, false, 0, 0},   // SYM_LON
	{0x01, 0, OSD_ELEM_RSSI, true, VAL_NUMBER, false, 0, 0},        // SYM_RSSI
	{0x1F, 0x1E, OSD_ELEM_SATS, true, VAL_NUMBER, false, 0, 0},     // SYM_SAT_R, icon SYM_SAT_L
	{0x04, 0, OSD_ELEM_THROTTLE, true, VAL_NUMBER, false, 0, 0},    // SYM_THR
	{0x9C, 0, OSD_ELEM_FLIGHT_TIME, true, VAL_TIME, false, 0, 0},   // SYM_FLY_M
	{0x9B, 0, OSD_ELEM_FLIGHT_TIME, true, VAL_TIME, false, 0, 0},   // SYM_ON_M
	{0x70, 0, OSD_ELEM_SPEED, true, VAL_NUMBER, true, 0, 0},        // SYM_SPEED
	{0x70, 0, OSD_ELEM_SPEED, true, VAL_NUMBER, true, 0, 'a'},      // the same, airspeed
	{0x75, 0, OSD_ELEM_VARIO, true, VAL_NUMBER, true, 0, 0},        // SYM_ARROW_SMALL_UP
	{0x76, 0, OSD_ELEM_VARIO, true, VAL_NUMBER, true, 0, 0},        // SYM_ARROW_SMALL_DOWN
	{0x11, 0, OSD_ELEM_HOME_DISTANCE, true, VAL_NUMBER, true, 0, 0},  // SYM_HOMEFLAG
	{0x71, 0, OSD_ELEM_TOTAL_DISTANCE, true, VAL_NUMBER, true, 0, 0}, // SYM_TOTAL_DISTANCE
	{0x7A, 0, OSD_ELEM_TEMPERATURE, true, VAL_NUMBER, true, 'C', 0},  // SYM_TEMPERATURE, core
	{0x7A, 0, OSD_ELEM_TEMPERATURE, true, VAL_NUMBER, true, 'E', 0},  // the same, ESC
	{0x7B, 0, OSD_ELEM_LINK_QUALITY, true, VAL_NUMBER, false, 0, 0},  // SYM_LINK_QUALITY
};

// The compass bar is built from a small alphabet of graphic glyphs - N, S, E, W,
// a divided line and a plain line - laid end to end. Nothing else in either
// firmware uses these codes, so a run of them can only be the bar. Both
// firmwares fix its width (Betaflight memcpy's 9 cells), which is why the
// widget takes its size from the theme and uses the run only as an anchor.
#define INAV_HEADING_FIRST 0xC8
#define INAV_HEADING_LAST 0xCD
#define BTFL_HEADING_FIRST 0x18
#define BTFL_HEADING_LAST 0x1D

// True when an earlier pass already took this cell. The passes run over the
// same grid in sequence, and without this a glyph that happens to fall inside a
// numeric run would be recognised a second time as an element of its own.
static bool cell_claimed(const osd_element_t *out, int found, int row, int col) {
	for (int i = 0; i < found; i++)
		if (out[i].row == row && col >= out[i].col && col < out[i].col + out[i].width)
			return true;
	return false;
}

// Shorter than the real bar so a partially redrawn one is still found.
#define HEADING_BAR_MIN_RUN 5

// The sixteen-point arrow rose, SYM_ARROW_SOUTH .. SYM_ARROW_16, plus the
// separate glyph drawn when the aircraft is directly over the launch point.
#define BTFL_ARROW_FIRST 0x60
#define BTFL_ARROW_LAST 0x6F
#define BTFL_OVER_HOME 0x05

// Battery icon ranges, full -> empty. The icon beside a voltage is what marks it
// as battery voltage, and its index is the FC's own coarse charge gauge.
// Blanks tolerated between a symbol and its value, for right-aligned fields.
// Two covers a three-digit field showing a single digit.
#define VALUE_MAX_LEADING_BLANKS 2

#define INAV_BATT_FULL 0x63
#define INAV_BATT_EMPTY 0x69
#define BTFL_BATT_FULL 0x90
#define BTFL_BATT_EMPTY 0x96

// OSD messages, from INAV's src/main/io/osd.h and Betaflight's
// src/main/osd/osd_warnings.c. Like flight modes these carry no symbol, so they
// are matched as text. Prefix matching, because several are formatted with a
// value appended at runtime.
//
// Severity decides the colour: a failsafe and a landing notice are both
// messages, but only one of them should be red.
typedef struct {
	const char *text;
	int severity; // matches osd_element_t::severity
} osd_message_t;

static const osd_message_t kMessages[] = {
	// --- critical: the aircraft is in trouble now
	{"FAILSAFE", 2},
	{"FAILSAFE MODE ENABLED", 2},
	{"!FS!", 2},
	{"FAIL SAFE", 2},
	{"!MOVE STICKS TO EXIT FS!", 2},
	{"DISABLED BY FAILSAFE", 2},
	{"EMERGENCY LANDING", 2},
	{"(EMERGENCY LANDING)", 2},
	{"NO RC LINK", 2},
	{"GPS FAILURE", 2},
	{"GYRO FAILURE", 2},
	{"COMPASS FAILURE", 2},
	{"BAROMETER FAILURE", 2},
	{"ACCELEROMETER FAILURE", 2},
	{"HARDWARE FAILURE", 2},
	{"LOW BATTERY", 2},
	{"LAND NOW", 2},
	{"BATTERY CONT", 2},
	{"RSSI LOW", 2},
	{"RSNR LOW", 2},
	{"CRASHFLIP SW", 2},
	{"RESCUE FAIL", 2},
	{"POSHOLD FAIL", 2},
	{"WP FLYAWAY", 2},
	{"WP GPS LOST", 2},
	{"WP MAG FAULT", 2},
	{"WP STALLED", 2},
	{"!NO HOME POSITION!", 2},
	{"AVOIDING FENCE BREACH", 2},
	{"NO FLY ZONE", 2},

	// --- warning: needs attention, not yet an emergency
	{"ACCELEROMETER NOT CALIBRATED", 1},
	{"COMPASS NOT CALIBRATED", 1},
	{"AIRCRAFT IS NOT LEVEL", 1},
	{"INVALID SETTING", 1},
	{"INVALID FONT", 1},
	{"NO PREARM", 1},
	{"DISABLE ARM SWITCH FIRST", 1},
	{"DISABLE NAVIGATION FIRST", 1},
	{"CANCEL WP TO EXIT RTH", 1},
	{"JUMP WAYPOINT MISCONFIGURED", 1},
	{"CLI IS ACTIVE", 1},
	{"NOT ENOUGH MEMORY", 1},
	{"AUTOTRIM IS ACTIVE", 1},
	{"GRD TEST > MOTORS DISABLED", 1},
	{"MOTOR BEEPER ACTIVE", 1},
	{"CPU OVERLOAD", 1},
	{"OVER CAP", 1},
	{"LINK QUALITY", 1},
	{"RSSI DBM", 1},
	{"MOVE STICKS TO ABORT", 1},
	{"ENTERING NFZ IN", 1},
	{"LEAVING FZ IN", 1},
	{"OUTSIDE FZ", 1},
	{"FLY OUT NFZ", 1},
	{"AVOIDING NO FLY ZONES", 1},
	{"** REARM PERIOD:", 1},
	{"WP ABORT", 1},
	{"WP FULL", 1},

	// --- info: normal state, worth showing but not alarming
	{"! ARMED !", 0},
	{"ARMED", 0},
	{"DISARMED", 0},
	{"LANDED", 0},
	{"LANDING", 0},
	{"HOVERING", 0},
	{"AUTOLAUNCH", 0},
	{"EN ROUTE TO HOME", 0},
	{"ADJUSTING RTH ALTITUDE", 0},
	{"ADJUSTING WP ALTITUDE", 0},
	{"LOITERING AROUND HOME", 0},
	{"LOITERING AROUND SAFEHOME", 0},
	{"DIVERTING TO SAFEHOME", 0},
	{"BEGIN LINEAR DESCENT", 0},
	{"* MISSION LOADED *", 0},
	{"*MISSION LOADED*", 0},
	{"HEADFREE", 0},
	{"(HEADFREE)", 0},
	{"BEACON ON", 0},
	{"WP COMPLETE", 0},
	{"WP LANDING", 0},
	{NULL, 0},
};

// Flight modes carry no symbol at all, so they are matched as words instead.
static const char *kFlightModes[] = {"ACRO", "ANGL", "ANGLE", "HOR", "HORIZON", "HORZ", "AIR",
	"MANU", "MANUAL", "RTH", "WP", "CRUZ", "CRUISE", "POSHOLD", "PH", "ALTHOLD", "AH", "LAUNCH",
	"FS", "FAILSAFE", "HOLD", "3CRS", "CRS", NULL};

// INAV packs decimals into dedicated glyphs to save grid cells. Note "HALF" in
// the upstream names (SYM_ZERO_HALF_TRAILING_DOT / SYM_ZERO_HALF_LEADING_DOT):
// each glyph carries *half* a decimal point, and a pair of adjacent cells
// renders one visual dot:
//   0xA1..0xAA -> digit 0-9 followed by the left half of a dot  ("4.")
//   0xB1..0xBA -> the right half of a dot followed by digit 0-9 (".4")
// So "16.7" arrives as '1', "6.", ".7" and the two halves must collapse into a
// single '.', otherwise the parsed number is wrong ("16..7" -> 16.0).
#define INAV_DIGIT_TRAILING_DOT 0xA1
#define INAV_DIGIT_LEADING_DOT 0xB1

// Expands one glyph into up to two plain characters. Returns how many were
// written, 0 if the glyph is not numeric.
static int glyph_to_chars_ex(uint16_t g, char out[2], bool allow_colon) {
	if (allow_colon && (g == ':' || g == 0x3A)) {
		out[0] = ':';
		return 1;
	}
	if (g >= '0' && g <= '9') {
		out[0] = (char)g;
		return 1;
	}
	if (g == '.' || g == ',') {
		out[0] = '.';
		return 1;
	}
	if (g == '-') {
		out[0] = '-';
		return 1;
	}
	if (g >= INAV_DIGIT_TRAILING_DOT && g <= INAV_DIGIT_TRAILING_DOT + 9) {
		out[0] = (char)('0' + (g - INAV_DIGIT_TRAILING_DOT));
		out[1] = '.';
		return 2;
	}
	if (g >= INAV_DIGIT_LEADING_DOT && g <= INAV_DIGIT_LEADING_DOT + 9) {
		out[0] = '.';
		out[1] = (char)('0' + (g - INAV_DIGIT_LEADING_DOT));
		return 2;
	}
	return 0;
}

static int glyph_to_chars(uint16_t g, char out[2]) {
	return glyph_to_chars_ex(g, out, false);
}

const char *osd_element_type_name(osd_element_type_t type) {
	switch (type) {
	case OSD_ELEM_VOLTAGE: return "voltage";
	case OSD_ELEM_CURRENT: return "current";
	case OSD_ELEM_MAH: return "mah";
	case OSD_ELEM_ALTITUDE: return "altitude";
	case OSD_ELEM_LATITUDE: return "latitude";
	case OSD_ELEM_LONGITUDE: return "longitude";
	case OSD_ELEM_RSSI: return "rssi";
	case OSD_ELEM_SATS: return "sats";
	case OSD_ELEM_THROTTLE: return "throttle";
	case OSD_ELEM_FLIGHT_TIME: return "flight_time";
	case OSD_ELEM_FLIGHT_MODE: return "flight_mode";
	case OSD_ELEM_WARNING: return "warning";
	case OSD_ELEM_HEADING_BAR: return "heading_bar";
	case OSD_ELEM_SPEED: return "speed";
	case OSD_ELEM_VARIO: return "vario";
	case OSD_ELEM_HOME_DISTANCE: return "home_distance";
	case OSD_ELEM_TOTAL_DISTANCE: return "total_distance";
	case OSD_ELEM_TEMPERATURE: return "temperature";
	case OSD_ELEM_LINK_QUALITY: return "link_quality";
	case OSD_ELEM_RSSI_DBM: return "rssi_dbm";
	case OSD_ELEM_SNR: return "snr";
	case OSD_ELEM_HOME_ARROW: return "home_arrow";
	case OSD_ELEM_HEADING: return "heading";
	case OSD_ELEM_GFORCE: return "gforce";
	case OSD_ELEM_POWER: return "power";
	case OSD_ELEM_WATT_HOURS: return "watt_hours";
	case OSD_ELEM_RANGEFINDER: return "rangefinder";
	case OSD_ELEM_EFFICIENCY: return "efficiency";
	case OSD_ELEM_TX_POWER: return "tx_power";
	default: return "none";
	}
}

const char *osd_unit_name(osd_unit_t unit) {
	switch (unit) {
	case OSD_UNIT_METRES: return "m";
	case OSD_UNIT_FEET: return "ft";
	case OSD_UNIT_KM: return "km";
	case OSD_UNIT_MILES: return "mi";
	case OSD_UNIT_KPH: return "km/h";
	case OSD_UNIT_MPH: return "mph";
	case OSD_UNIT_MPS: return "m/s";
	case OSD_UNIT_FTPS: return "ft/s";
	case OSD_UNIT_CELSIUS: return "degC";
	case OSD_UNIT_FAHRENHEIT: return "degF";
	default: return "";
	}
}

// INAV titles its post-disarm page "*** STATS ***" (or "*** STATS 1/2 -> ***").
// Betaflight uses a similar banner. Matching the banner is enough: it appears
// only on that page, and only while the aircraft is disarmed.
bool osd_elements_is_summary_screen(osd_glyph_getter get, void *ctx, int cols, int rows) {
	if (!get)
		return false;

	for (int row = 0; row < rows; row++) {
		char line[80];
		int n = 0;
		for (int c = 0; c < cols && n < (int)sizeof(line) - 1; c++) {
			uint16_t g = get(c, row, ctx);
			line[n++] = (g >= 0x20 && g < 0x7F) ? (char)g : ' ';
		}
		line[n] = '\0';
		if (strstr(line, "STATS") && (strstr(line, "***") || strstr(line, "---")))
			return true;
	}
	return false;
}

int osd_elements_scan(osd_glyph_getter get,
	void *ctx,
	int cols,
	int rows,
	const char *fc_identifier,
	osd_element_t *out,
	int max_out) {
	if (!get || !out || max_out <= 0)
		return 0;

	const osd_anchor_t *anchors = kAnchorsInav;
	int anchor_count = (int)(sizeof(kAnchorsInav) / sizeof(kAnchorsInav[0]));
	const bool is_btfl = fc_identifier && strncmp(fc_identifier, "BTFL", 4) == 0;
	if (is_btfl) {
		anchors = kAnchorsBtfl;
		anchor_count = (int)(sizeof(kAnchorsBtfl) / sizeof(kAnchorsBtfl[0]));
	}

	int found = 0;

	for (int row = 0; row < rows && found < max_out; row++) {
		for (int col = 0; col < cols && found < max_out; col++) {
			uint16_t g = get(col, row, ctx);
			if (g == 0 || g == 0x20)
				continue;

			// More than one row can share a glyph, differing only in the letter
			// that follows it. Prefer a row whose letter is actually on screen,
			// so speed does not match the airspeed field and lose the 'a'.
			// More than one row can share a glyph, differing only in the letter
			// beside it. Prefer a row whose letter is actually on screen, so
			// speed does not match the airspeed field and lose the 'a', and core
			// temperature is not read as ESC temperature.
			const osd_anchor_t *anchor = NULL;
			bool lettered = false;
			for (int a = 0; a < anchor_count; a++) {
				if (anchors[a].glyph != g)
					continue;
				if (anchors[a].prefix || anchors[a].infix) {
					const bool pre_ok = !anchors[a].prefix ||
										(col > 0 && get(col - 1, row, ctx) ==
														(uint16_t)anchors[a].prefix);
					const bool in_ok = !anchors[a].infix ||
									   (col + 1 < cols && get(col + 1, row, ctx) ==
															  (uint16_t)anchors[a].infix);
					if (pre_ok && in_ok) {
						anchor = &anchors[a];
						lettered = true;
						break;
					}
					continue;
				}
				if (!anchor)
					anchor = &anchors[a]; // the letterless fallback
			}
			if (!anchor)
				continue;
			// A letterless row must not claim a field that carries a letter: the
			// digits would start one cell late and the reading would be wrong.
			if (!lettered) {
				bool letter_variant = false;
				for (int a = 0; a < anchor_count; a++) {
					if (anchors[a].glyph != g)
						continue;
					if (anchors[a].infix && col + 1 < cols &&
						get(col + 1, row, ctx) == (uint16_t)anchors[a].infix)
						letter_variant = true;
					if (anchors[a].prefix && col > 0 &&
						get(col - 1, row, ctx) == (uint16_t)anchors[a].prefix)
						letter_variant = true;
				}
				if (letter_variant)
					continue;
			}

			// Walk away from the anchor across the contiguous numeric run.
			char text[24];
			int len = 0;
			int first_col = col, last_col = col;

			if (anchor->anchor_leads) {
				// Values are right-aligned in their field, so a short reading
				// leaves blanks between the symbol and the digits: RSSI 0 is
				// drawn as "<sym> 0", throttle as "<sym> 53". Stopping at the
				// first blank loses exactly those cases - which are the low,
				// blinking, and therefore most important ones.
				int c = col + 1;
				// Some fields carry a letter between the symbol and the digits
				// that says which of several readings this is - 'C' or 'E' for
				// core against ESC temperature, 'a' for airspeed. The table
				// holds one row per letter, so a row whose letter is not on
				// screen is simply the wrong row.
				if (anchor->infix) {
					if (c >= cols || get(c, row, ctx) != (uint16_t)anchor->infix)
						continue;
					c++;
				}
				int skipped = 0;
				while (c < cols && skipped < VALUE_MAX_LEADING_BLANKS) {
					uint16_t g2 = get(c, row, ctx);
					if (g2 != 0x20 && g2 != 0)
						break;
					c++;
					skipped++;
				}
				for (; c < cols && len < (int)sizeof(text) - 3; c++) {
					char ch[2];
					int n = glyph_to_chars_ex(get(c, row, ctx), ch, anchor->kind == VAL_TIME);
					if (n == 0)
						break;
					for (int i = 0; i < n; i++)
						text[len++] = ch[i];
					last_col = c;
				}
			} else {
				// Collect leftwards, then reverse: the number precedes the symbol.
				char rev[24];
				int rlen = 0;
				int c = col - 1;
				int skipped = 0;
				while (c >= 0 && skipped < VALUE_MAX_LEADING_BLANKS) {
					uint16_t g2 = get(c, row, ctx);
					if (g2 != 0x20 && g2 != 0)
						break;
					c--;
					skipped++;
				}
				for (; c >= 0 && rlen < (int)sizeof(rev) - 3; c--) {
					char ch[2];
					int n = glyph_to_chars(get(c, row, ctx), ch);
					if (n == 0)
						break;
					// Push in reverse so the final string reads left to right.
					for (int i = n - 1; i >= 0; i--)
						rev[rlen++] = ch[i];
					first_col = c;
				}
				for (int i = rlen - 1; i >= 0; i--)
					text[len++] = rev[i];
			}

			if (len == 0)
				continue; // a bare symbol with no number is not an element
			text[len] = '\0';

			// Collapse the split decimal point: two half-dot glyphs meeting
			// produce "..", which strtof() would stop at.
			{
				int w = 0;
				for (int r = 0; text[r] != '\0'; r++) {
					if (text[r] == '.' && w > 0 && text[w - 1] == '.')
						continue;
					text[w++] = text[r];
				}
				text[w] = '\0';
				len = w;
			}

			osd_element_t *e = &out[found];
			memset(e, 0, sizeof(*e));
			e->type = anchor->type;
			e->row = (uint8_t)row;
			// Above a watt the transmit power is printed in watts rather than
			// milliwatts. Noted here and applied after the number is parsed -
			// scaling e->value at this point would multiply a zero.
			bool tx_power_in_watts = false;

			// Absorb a two-cell icon (satellites) so its left half is not left
			// stranded beside the widget that replaces it.
			if (anchor->lead_icon && first_col > 0 &&
				get(first_col - 1, row, ctx) == anchor->lead_icon)
				first_col--;
			// The element spans from its symbol through its value, including
			// any blanks skipped between them, so clearing it removes the lot.
			if (anchor->anchor_leads && first_col > col)
				first_col = col;

			// A battery icon before the number is what makes a volt reading
			// *battery* voltage, and its position in the range is the flight
			// controller's own charge gauge. Absorb it too.
			if (anchor->type == OSD_ELEM_VOLTAGE && first_col > 0) {
				uint16_t ic = get(first_col - 1, row, ctx);
				uint16_t bf = is_btfl ? BTFL_BATT_FULL : INAV_BATT_FULL;
				uint16_t be = is_btfl ? BTFL_BATT_EMPTY : INAV_BATT_EMPTY;
				if (ic >= bf && ic <= be) {
					e->has_battery_icon = true;
					e->battery_level = (int)(ic - bf);
					first_col--;
				}
			}

			// A unit glyph closes the field. Absorbing it into the run both tells
			// the widget what to label the number and stops the glyph being left
			// on screen once the text underneath is cleared.
			if (anchor->trailing_unit && last_col + 1 < cols) {
				const osd_unit_t u = unit_for_glyph(get(last_col + 1, row, ctx));
				if (u != OSD_UNIT_NONE) {
					e->unit = u;
					last_col++;
				}
			}
			// Two Betaflight fields borrow another element's symbol and mean
			// something else entirely. Both were being reported as the element
			// whose symbol they carry, with a plausible-looking number, which is
			// worse than not reporting them: the reading is wrong rather than
			// absent. The literal that follows is what gives them away.
			if (is_btfl && e->type == OSD_ELEM_MAH && last_col + 2 < cols &&
				get(last_col + 1, row, ctx) == '/') {
				// "1234<mAh>/<km>" - efficiency, not capacity used.
				const osd_unit_t u = unit_for_glyph(get(last_col + 2, row, ctx));
				if (u == OSD_UNIT_KM || u == OSD_UNIT_MILES) {
					e->type = OSD_ELEM_EFFICIENCY;
					e->unit = u;
					last_col += 2;
				}
			}
			if (is_btfl && e->type == OSD_ELEM_RSSI && last_col + 1 < cols) {
				// "<rssi>250MW" or "<rssi>1.5W" - uplink transmit power.
				const uint16_t n1 = get(last_col + 1, row, ctx);
				const uint16_t n2 = last_col + 2 < cols ? get(last_col + 2, row, ctx) : 0;
				if (n1 == 'M' && n2 == 'W') {
					e->type = OSD_ELEM_TX_POWER;
					last_col += 2;
				} else if (n1 == 'W') {
					e->type = OSD_ELEM_TX_POWER;
					tx_power_in_watts = true;
					last_col += 1;
				}
			}

			// The letter before the symbol belongs to the field too - leaving it
			// behind puts a stray "C" beside the widget that replaced it.
			if (anchor->prefix && first_col > 0)
				first_col--;
			if (anchor->infix == 'a')
				e->is_airspeed = true;

			e->col = (uint8_t)first_col;
			e->width = (uint8_t)(last_col - first_col + 1);
			// The symbol is where the flight controller pinned the field; the
			// digits grow away from it. Record which end that is so a widget can
			// be placed against the edge that does not move.
			e->anchor_right = !anchor->anchor_leads;
			e->anchor_col = (uint8_t)(anchor->anchor_leads ? first_col : last_col);
			snprintf(e->text, sizeof(e->text), "%s", text);

			if (anchor->kind == VAL_TIME) {
				e->value_valid = false; // "05:36" is text, not a number
				e->value = 0.0f;
			} else {
				char *end = NULL;
				float v = strtof(text, &end);
				e->value_valid = (end != text);
				e->value = e->value_valid ? v : 0.0f;
			}

			if (tx_power_in_watts && e->value_valid)
				e->value *= 1000.0f;

			// Both cell and pack voltage are marked with SYM_VOLT, so magnitude
			// is the only available discriminator. A 1S pack is genuinely
			// ambiguous, but then per-cell and pack voltage are the same number.
			if (e->type == OSD_ELEM_VOLTAGE && e->value_valid)
				e->is_per_cell = (e->value > 0.5f && e->value <= 4.6f);

			// Betaflight draws the climb rate as an unsigned number under an
			// arrow, so the sign lives in the glyph and nowhere else.
			if (e->type == OSD_ELEM_VARIO && e->value_valid && g == 0x76)
				e->value = -e->value;

			// RSSI percent, RSSI in dBm and link SNR all carry SYM_RSSI, so the
			// value is the only thing that separates them. Percent is 0..100 and
			// never negative; dBm is negative and, for any link worth flying,
			// well below -20; SNR sits either side of zero but stays small.
			// The overlap - a small positive SNR against a low RSSI percent - is
			// genuinely undecidable, and percent is the commoner element, so it
			// wins. Guessing wrong the other way would draw -94dBm as a
			// nearly-full signal bar, which is the dangerous direction.
			if (e->type == OSD_ELEM_RSSI && e->value_valid) {
				if (e->value <= -20.0f)
					e->type = OSD_ELEM_RSSI_DBM;
				else if (e->value < 0.0f)
					e->type = OSD_ELEM_SNR;
			}

			found++;
			col = last_col; // do not rescan the digits we just consumed
		}
	}

	// OSD messages occupy a whole row of text, so the row is trimmed and matched
	// as a unit rather than word by word.
	for (int row = 0; row < rows && found < max_out; row++) {
		char line[80];
		int ll = 0;
		int first = -1, last = -1;
		for (int c = 0; c < cols && ll < (int)sizeof(line) - 1; c++) {
			uint16_t g = get(c, row, ctx);
			char ch = (g >= 0x20 && g < 0x7F) ? (char)g : ' ';
			line[ll++] = ch;
			if (ch != ' ') {
				if (first < 0)
					first = c;
				last = c;
			}
		}
		line[ll] = '\0';
		if (first < 0)
			continue;

		// Trim into a buffer the same size as osd_element_t::text, so the match
		// and the stored text cannot disagree about length. Nothing in
		// kMessages is anywhere near this long.
		char trimmed[48];
		int tl = 0;
		for (int i = first; i <= last && tl < (int)sizeof(trimmed) - 1; i++)
			trimmed[tl++] = line[i];
		trimmed[tl] = '\0';
		if (tl < 3)
			continue;

		for (int i = 0; kMessages[i].text; i++) {
			size_t mlen = strlen(kMessages[i].text);
			if (strncmp(trimmed, kMessages[i].text, mlen) != 0)
				continue;
			// Must be the whole message, not a prefix of a longer word.
			if (trimmed[mlen] != '\0' && trimmed[mlen] != ' ' && trimmed[mlen] != ':')
				continue;
			osd_element_t *e = &out[found++];
			memset(e, 0, sizeof(*e));
			e->type = OSD_ELEM_WARNING;
			e->row = (uint8_t)row;
			e->col = (uint8_t)first;
			e->width = (uint8_t)(last - first + 1);
			e->anchor_col = (uint8_t)first; // text grows rightwards from its start
			e->severity = kMessages[i].severity;
			e->value_valid = false;
			snprintf(e->text, sizeof(e->text), "%s", trimmed);
			break;
		}
	}

	// The compass bar, found as a run rather than by a leading symbol.
	{
		const uint16_t first = is_btfl ? BTFL_HEADING_FIRST : INAV_HEADING_FIRST;
		const uint16_t last = is_btfl ? BTFL_HEADING_LAST : INAV_HEADING_LAST;
		for (int row = 0; row < rows && found < max_out; row++) {
			int c = 0;
			while (c < cols && found < max_out) {
				uint16_t g = get(c, row, ctx);
				if (g < first || g > last) {
					c++;
					continue;
				}
				const int start = c;
				while (c < cols) {
					g = get(c, row, ctx);
					if (g < first || g > last)
						break;
					c++;
				}
				const int len = c - start;
				if (len < HEADING_BAR_MIN_RUN)
					continue;

				osd_element_t *e = &out[found++];
				memset(e, 0, sizeof(*e));
				e->type = OSD_ELEM_HEADING_BAR;
				e->row = (uint8_t)row;
				e->col = (uint8_t)start;
				e->width = (uint8_t)len;
				e->anchor_col = (uint8_t)start;
				// No value: the heading comes from telemetry, not from decoding
				// which way the glyphs happen to be pointing.
				e->value_valid = false;
				snprintf(e->text, sizeof(e->text), "%s", "HEADING");
			}
		}
	}

	// Fields with no symbol glyph, found by the literal text around the number.
	//
	// Betaflight draws about two dozen elements as plain characters. Where the
	// firmware wraps the number in something fixed - a trailing 'G', "WH", an
	// "RF:" prefix - that literal is as good an anchor as a symbol glyph, and
	// these are picked up here.
	//
	// The rest cannot be reached this way and are deliberately left alone. ESC
	// RPM is "%d", the RC channel readout "%5d", the ETA "%02u:%02u", the
	// PID/rate profile "%d-%d": bare numbers with nothing to distinguish them
	// from each other or from a number belonging to anything else on screen.
	// Matching those on shape alone would turn arbitrary digits into widgets,
	// and a widget confidently showing the wrong quantity is worse than one that
	// is missing. Reaching them needs the layout itself, which Betaflight will
	// hand over through MSP_OSD_CONFIG - a different mechanism, not a better
	// pattern match.
	if (is_btfl) {
		for (int row = 0; row < rows && found < max_out; row++) {
			for (int col = 0; col < cols && found < max_out; col++) {
				// The run has to start cleanly, or "13" out of "1234" matches.
				if (col > 0) {
					const uint16_t prev = get(col - 1, row, ctx);
					if (prev != 0x20 && prev != 0)
						continue;
				}

				// "RF:" then a distance - the only prefixed one worth having;
				// the others ("RATE_2", "PID_1") are legible as they are.
				int start = col;
				int c = col;
				osd_element_type_t type = OSD_ELEM_NONE;
				if (col + 3 < cols && get(col, row, ctx) == 'R' &&
					get(col + 1, row, ctx) == 'F' && get(col + 2, row, ctx) == ':') {
					type = OSD_ELEM_RANGEFINDER;
					c = col + 3;
					while (c < cols && get(c, row, ctx) == 0x20)
						c++;
				}

				char num[16];
				int nl = 0;
				const int dig_start = c;
				while (c < cols && nl < (int)sizeof(num) - 1) {
					const uint16_t d = get(c, row, ctx);
					if ((d < '0' || d > '9') && d != '.' && !(nl == 0 && d == '-'))
						break;
					num[nl++] = (char)d;
					c++;
				}
				num[nl] = '\0';
				if (nl == 0 || (nl == 1 && num[0] == '-')) {
					col = dig_start > col ? dig_start : col;
					continue;
				}

				// The suffix decides what it is. Longest first: "MWH" would
				// otherwise be read as "WH" with a stray M, and "WH" as "W".
				int suffix = 0;
				float scale = 1.0f;
				if (type == OSD_ELEM_NONE) {
					const uint16_t s1 = c < cols ? get(c, row, ctx) : 0;
					const uint16_t s2 = c + 1 < cols ? get(c + 1, row, ctx) : 0;
					const uint16_t s3 = c + 2 < cols ? get(c + 2, row, ctx) : 0;
					if (s1 == 'M' && s2 == 'W' && s3 == 'H') {
						type = OSD_ELEM_WATT_HOURS;
						suffix = 3;
						scale = 0.001f; // printed in milliwatt-hours
					} else if (s1 == 'W' && s2 == 'H') {
						type = OSD_ELEM_WATT_HOURS;
						suffix = 2;
					} else if (s1 == 'G' && s2 != 'H') {
						type = OSD_ELEM_GFORCE;
						suffix = 1;
					} else if (s1 == 'W') {
						type = OSD_ELEM_POWER;
						suffix = 1;
					}
					if (type == OSD_ELEM_NONE) {
						col = c > col ? c : col;
						continue;
					}
					// A letter after the suffix means this is a word, not a
					// reading: "12WHATEVER" is not 12 watt-hours.
					const uint16_t after = c + suffix < cols ? get(c + suffix, row, ctx) : 0;
					if ((after >= 'A' && after <= 'Z') || (after >= '0' && after <= '9')) {
						col = c > col ? c : col;
						continue;
					}
				}

				const int end = c + suffix - 1;
				if (cell_claimed(out, found, row, start) || cell_claimed(out, found, row, end)) {
					col = end;
					continue;
				}

				osd_element_t *e = &out[found++];
				memset(e, 0, sizeof(*e));
				e->type = type;
				e->row = (uint8_t)row;
				e->col = (uint8_t)start;
				e->width = (uint8_t)(end - start + 1);
				e->anchor_col = (uint8_t)start;
				e->value = strtof(num, NULL) * scale;
				e->value_valid = true;
				snprintf(e->text, sizeof(e->text), "%s", num);
				col = end;
			}
		}
	}

	// The home arrow, and the numerical heading that shares its glyphs.
	//
	// Betaflight draws both from the same sixteen-glyph compass rose: the home
	// direction element is a lone arrow, and the numerical heading is an arrow
	// followed by three digits. So the digits are the discriminator, and neither
	// needs a table entry of its own.
	//
	// Only Betaflight for now - INAV's arrow block sits elsewhere in its font and
	// is not confirmed here, and a wrong range would turn ordinary glyphs into
	// widgets.
	if (is_btfl) {
		for (int row = 0; row < rows && found < max_out; row++) {
			for (int col = 0; col < cols && found < max_out; col++) {
				const uint16_t g = get(col, row, ctx);
				const bool is_arrow = (g >= BTFL_ARROW_FIRST && g <= BTFL_ARROW_LAST);
				if (!is_arrow && g != BTFL_OVER_HOME)
					continue;
				if (cell_claimed(out, found, row, col))
					continue;

				// Three digits after it make this the heading readout.
				char digits[8];
				int dl = 0;
				for (int c = col + 1; c < cols && dl < 3; c++) {
					const uint16_t d = get(c, row, ctx);
					if (d < '0' || d > '9')
						break;
					digits[dl++] = (char)d;
				}
				digits[dl] = '\0';

				osd_element_t *e = &out[found++];
				memset(e, 0, sizeof(*e));
				e->row = (uint8_t)row;
				e->col = (uint8_t)col;
				e->anchor_col = (uint8_t)col;
				if (dl == 3) {
					e->type = OSD_ELEM_HEADING;
					e->width = (uint8_t)(1 + dl);
					e->value = (float)atoi(digits);
					e->value_valid = true;
					snprintf(e->text, sizeof(e->text), "%s", digits);
					col += dl;
				} else {
					// The glyph is the whole element - it *is* the bearing. We
					// redraw it from the fix rather than decoding which of the
					// sixteen it happens to be.
					e->type = OSD_ELEM_HOME_ARROW;
					e->width = 1;
					e->value_valid = false;
					snprintf(e->text, sizeof(e->text), "%s", "HOME");
				}
			}
		}
	}

	// Flight modes carry no anchor glyph - the firmware just prints the word - so
	// they are matched as text. Only exact matches against the known list are
	// accepted, to avoid turning arbitrary on-screen words into widgets.
	for (int row = 0; row < rows && found < max_out; row++) {
		bool row_is_message = false;
		for (int i = 0; i < found; i++)
			if (out[i].type == OSD_ELEM_WARNING && out[i].row == row)
				row_is_message = true;
		if (row_is_message)
			continue;

		int c = 0;
		while (c < cols) {
			uint16_t g = get(c, row, ctx);
			if (g < 'A' || g > 'Z') {
				c++;
				continue;
			}
			char word[16];
			int wl = 0;
			int start = c;
			while (c < cols && wl < (int)sizeof(word) - 1) {
				uint16_t w = get(c, row, ctx);
				if (w < 'A' || w > 'Z')
					break;
				word[wl++] = (char)w;
				c++;
			}
			word[wl] = '\0';

			for (int i = 0; kFlightModes[i]; i++) {
				if (strcmp(word, kFlightModes[i]) != 0)
					continue;
				osd_element_t *e = &out[found++];
				memset(e, 0, sizeof(*e));
				e->type = OSD_ELEM_FLIGHT_MODE;
				e->row = (uint8_t)row;
				e->col = (uint8_t)start;
				e->width = (uint8_t)wl;
				e->anchor_col = (uint8_t)start;
				e->value_valid = false;
				snprintf(e->text, sizeof(e->text), "%s", word);
				break;
			}
		}
	}

	return found;
}

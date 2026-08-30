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
} osd_anchor_t;

static const osd_anchor_t kAnchorsInav[] = {
	{0x1F, 0, OSD_ELEM_VOLTAGE, false, VAL_NUMBER},      // SYM_VOLT
	{0x6A, 0, OSD_ELEM_CURRENT, false, VAL_NUMBER},      // SYM_AMP
	{0x99, 0, OSD_ELEM_MAH, false, VAL_NUMBER},          // SYM_MAH
	{0x76, 0, OSD_ELEM_ALTITUDE, false, VAL_NUMBER},     // SYM_ALT_M
	{0x78, 0, OSD_ELEM_ALTITUDE, false, VAL_NUMBER},     // SYM_ALT_FT
	{0x03, 0, OSD_ELEM_LATITUDE, true, VAL_NUMBER},      // SYM_LAT
	{0x04, 0, OSD_ELEM_LONGITUDE, true, VAL_NUMBER},     // SYM_LON
	{0x01, 0, OSD_ELEM_RSSI, true, VAL_NUMBER},          // SYM_RSSI
	{0x09, 0x08, OSD_ELEM_SATS, true, VAL_NUMBER},       // SYM_SAT_R, icon SYM_SAT_L
	{0x95, 0, OSD_ELEM_THROTTLE, true, VAL_NUMBER},      // SYM_THR
	{0x9F, 0, OSD_ELEM_FLIGHT_TIME, true, VAL_TIME},     // SYM_FLY_M
	{0x9E, 0, OSD_ELEM_FLIGHT_TIME, true, VAL_TIME},     // SYM_ON_M
	{0xA0, 0, OSD_ELEM_FLIGHT_TIME, true, VAL_TIME},     // SYM_CLOCK
};

static const osd_anchor_t kAnchorsBtfl[] = {
	{0x06, 0, OSD_ELEM_VOLTAGE, false, VAL_NUMBER},      // SYM_VOLT
	{0x9A, 0, OSD_ELEM_CURRENT, false, VAL_NUMBER},      // SYM_AMP
	{0x07, 0, OSD_ELEM_MAH, false, VAL_NUMBER},          // SYM_MAH
	{0x7F, 0, OSD_ELEM_ALTITUDE, false, VAL_NUMBER},     // SYM_ALTITUDE
	{0x89, 0, OSD_ELEM_LATITUDE, true, VAL_NUMBER},      // SYM_LAT
	{0x98, 0, OSD_ELEM_LONGITUDE, true, VAL_NUMBER},     // SYM_LON
	{0x01, 0, OSD_ELEM_RSSI, true, VAL_NUMBER},          // SYM_RSSI
	{0x1F, 0x1E, OSD_ELEM_SATS, true, VAL_NUMBER},       // SYM_SAT_R, icon SYM_SAT_L
	{0x04, 0, OSD_ELEM_THROTTLE, true, VAL_NUMBER},      // SYM_THR
	{0x9C, 0, OSD_ELEM_FLIGHT_TIME, true, VAL_TIME},     // SYM_FLY_M
	{0x9B, 0, OSD_ELEM_FLIGHT_TIME, true, VAL_TIME},     // SYM_ON_M
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

// Shorter than the real bar so a partially redrawn one is still found.
#define HEADING_BAR_MIN_RUN 5

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
	default: return "none";
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

			const osd_anchor_t *anchor = NULL;
			for (int a = 0; a < anchor_count; a++) {
				if (anchors[a].glyph == g) {
					anchor = &anchors[a];
					break;
				}
			}
			if (!anchor)
				continue;

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

			// Both cell and pack voltage are marked with SYM_VOLT, so magnitude
			// is the only available discriminator. A 1S pack is genuinely
			// ambiguous, but then per-cell and pack voltage are the same number.
			if (e->type == OSD_ELEM_VOLTAGE && e->value_valid)
				e->is_per_cell = (e->value > 0.5f && e->value <= 4.6f);

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

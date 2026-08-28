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

// Battery icon ranges, full -> empty. The icon beside a voltage is what marks it
// as battery voltage, and its index is the FC's own coarse charge gauge.
#define INAV_BATT_FULL 0x63
#define INAV_BATT_EMPTY 0x69
#define BTFL_BATT_FULL 0x90
#define BTFL_BATT_EMPTY 0x96

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
	default: return "none";
	}
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
				for (int c = col + 1; c < cols && len < (int)sizeof(text) - 3; c++) {
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
				for (int c = col - 1; c >= 0 && rlen < (int)sizeof(rev) - 3; c--) {
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

	// Flight modes carry no anchor glyph - the firmware just prints the word - so
	// they are matched as text. Only exact matches against the known list are
	// accepted, to avoid turning arbitrary on-screen words into widgets.
	for (int row = 0; row < rows && found < max_out; row++) {
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
				e->value_valid = false;
				snprintf(e->text, sizeof(e->text), "%s", word);
				break;
			}
		}
	}

	return found;
}

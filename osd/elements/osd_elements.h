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
	OSD_ELEM_TYPE_COUNT
} osd_element_type_t;

typedef struct {
	osd_element_type_t type;
	uint8_t row;   // grid row
	uint8_t col;   // leftmost grid column covered, including the symbol glyph
	uint8_t width; // number of grid cells covered
	float value;
	bool value_valid;
	// True when a voltage looks like a single cell rather than a whole pack.
	// INAV/BF mark both with SYM_VOLT, so only magnitude can tell them apart.
	bool is_per_cell;
	char text[24]; // decoded numeric text, for debugging and fallback rendering
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

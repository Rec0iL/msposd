#pragma once

// Ground-side link statistics, as a widget.
//
// Unlike every other widget here, this one is not fed by the flight controller.
// wfb-ng and APFPV live on the ground station, and msposd only ever sees MSP
// coming *down* from the air unit - so there is no element on the glyph grid to
// anchor to, and no position the pilot chose on the FC to inherit. Position,
// size and style therefore come from the theme, and the numbers arrive through
// a small file the ground station writes.
//
// That file is the same seam the theme itself uses: the front end writes, we
// poll. It means a ground station that has not been taught to write it simply
// shows nothing, rather than msposd having to know about wfb-ng, APFPV, or
// whatever comes next.
//
// Pure drawing plus one small parser, no msposd or platform dependencies, so it
// can be exercised on its own like the rest of the widget layer.

#include "osd_paint.h"
#include "osd_text.h"

#include <stdbool.h>
#include <stdint.h>

typedef enum {
	OSD_LINK_VERTICAL = 0, // antennas stacked, for an edge of the screen
	OSD_LINK_HORIZONTAL,   // antennas side by side, for the top or bottom
	// A wide, shallow strip for the very top or bottom of the screen: the
	// channel, packet loss and throughput all share the header line, leaving the
	// whole width below it for the link quality bar.
	OSD_LINK_ULTRAWIDE,
} osd_link_style_t;

/// More than any ground station in use, and small enough that the widget still
/// fits on screen with every slot filled.
#define OSD_LINK_MAX_ANTENNAS 6

typedef struct {
	bool valid; // false until a stats file has been read successfully
	// Who produced these. Shown on the widget, because "-71dBm" means rather
	// different things coming from wfb-ng and from a WiFi driver.
	char source[16];

	int antennas;
	int rssi_dbm[OSD_LINK_MAX_ANTENNAS];
	int snr_db[OSD_LINK_MAX_ANTENNAS];
	bool rssi_valid[OSD_LINK_MAX_ANTENNAS];
	bool snr_valid[OSD_LINK_MAX_ANTENNAS];

	// What the receiver is tuned to. Zero means the ground station did not say.
	// Channel and frequency are both carried because stations report one or the
	// other and pilots think in both.
	int channel;
	int freq_mhz;
	int bandwidth_mhz;

	// Negative means the ground station did not report it, which is different
	// from reporting zero - a link quality of 0 is a dead link, an absent one is
	// a station that does not measure it.
	float quality_pct;
	float loss_pct;
	float bitrate_mbps;

	uint64_t updated_ms; // our clock, not the writer's
} osd_link_stats_t;

/// Where the ground station is expected to publish, when the theme does not say.
///
/// One fixed rule, deliberately: `$MSPOSD_LINK_STATS` if set, otherwise
/// /tmp/msposd-link.ini. Every writer resolves it the same way, so a ground
/// station and msposd agree without either being configured.
///
/// Not $XDG_RUNTIME_DIR, tempting as that is: msposd is often started from a
/// service where it is unset while the ground station runs in a user session
/// where it is not, and the two would then quietly disagree about the path. A
/// rule that can resolve differently in two processes is worse than a plain one.
const char *osd_link_default_path(void);

/// Reads the ini the ground station writes. Returns false if the file cannot be
/// read or holds nothing usable, leaving `out` untouched so a truncated write -
/// which is what a reader catches sooner or later at ten updates a second - does
/// not blank the widget for a frame.
bool osd_link_stats_load(const char *path, osd_link_stats_t *out, uint64_t now_ms);

/// True once the stats are older than `hold_ms`, so the widget can say the link
/// data has stopped rather than showing the last numbers for ever.
bool osd_link_stats_stale(const osd_link_stats_t *s, uint64_t now_ms, float hold_ms);

typedef struct {
	osd_link_style_t style;
	float scale;
	// Per-aerial readings. Off leaves the headline - who, what channel, how good
	// the link is - which is all most pilots watch in the air. A six-aerial
	// station is a lot of panel for something you only study on the ground.
	bool show_antennas;

	// Palette, matching the panels so this does not read as a different OSD.
	osd_color_t accent, label, good, warn, crit, fill, edge, track;
	float opacity;

	// Outline behind the geometry as well as the text, as the compass has.
	bool outline;
	osd_color_t outline_color;
	float outline_px;

	// Panel geometry, taken from the theme so the widget matches the others.
	float pad_x, pad_y, chamfer, bar_height;
	float value_size, label_size, label_tracking;
} osd_link_params_t;

/// The rectangle the widget will occupy. Shared with the placement pass, so
/// what is reserved is exactly what gets painted.
void osd_link_measure(
	const osd_link_params_t *p, const osd_link_stats_t *s, float *out_w, float *out_h);

/// Draws with its top-left corner at (x, y).
void osd_link_draw(osd_surface_t *s, osd_font_t *font, float x, float y,
	const osd_link_params_t *p, const osd_link_stats_t *st, bool stale);

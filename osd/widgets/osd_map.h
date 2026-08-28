// Slippy-map tile maths and providers for the map widget.
//
// The map's rectangle is defined by where the user placed two OSD elements on
// the flight controller: latitude marks the top-left corner, longitude the
// bottom-right. Those same two elements supply the coordinates, so no extra
// configuration is needed - the placement *is* the configuration.
//
// This file is pure maths and string building: no I/O, no platform calls, so it
// can be tested on its own and reused wherever msposd runs.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define OSD_TILE_SIZE 256

typedef enum {
	OSD_MAP_ROADS = 0, // OpenStreetMap standard tiles
	OSD_MAP_SATELLITE, // Esri World Imagery
	OSD_MAP_HYBRID,    // Esri World Imagery + place/boundary labels on top
} osd_map_style_t;

/// Web Mercator projection to world pixel coordinates at `zoom`.
/// Latitude is clamped to +/-85.0511, the limit of the projection.
void osd_map_project(double lat, double lon, int zoom, double *world_x, double *world_y);

/// Inverse of osd_map_project, used to work out what a screen pixel refers to.
void osd_map_unproject(double world_x, double world_y, int zoom, double *lat, double *lon);

/// Tile URL for a style. `layer` selects the base (0) or, for hybrid, the label
/// overlay (1). Returns false if the layer does not apply to the style.
bool osd_map_tile_url(osd_map_style_t style, int layer, int zoom, int tile_x, int tile_y,
	char *out, size_t out_size);

/// Cache filename for a tile. No extension: the bytes are stored verbatim and
/// may be PNG (OpenStreetMap) or JPEG (Esri imagery).
bool osd_map_tile_key(osd_map_style_t style, int layer, int zoom, int tile_x, int tile_y,
	char *out, size_t out_size);

/// How many label layers a style draws on top of its base (0 or 1).
int osd_map_overlay_count(osd_map_style_t style);

/// Tiles covering a viewport of `w` x `h` pixels centred on lat/lon.
typedef struct {
	int tile_x, tile_y;
	int screen_x, screen_y; // top-left of this tile within the viewport
} osd_map_tile_t;

/// Fills `out` with the tiles needed. Returns the count, never exceeding
/// `max_out`. Tiles outside the valid range for the zoom are skipped, so the
/// poles and the antimeridian do not produce bogus requests.
int osd_map_visible_tiles(double lat, double lon, int zoom, int w, int h, osd_map_tile_t *out,
	int max_out);

/// Where a coordinate falls inside the viewport, in pixels from its top-left.
void osd_map_point_in_view(double lat, double lon, double centre_lat, double centre_lon, int zoom,
	int w, int h, float *x, float *y);

/// The same, for a map rotated so that `rot_deg` (a compass bearing) points up
/// the screen. `rot_deg` of 0 is north-up and matches osd_map_point_in_view.
void osd_map_point_in_view_rot(double lat, double lon, double centre_lat, double centre_lon,
	int zoom, int w, int h, float rot_deg, float *x, float *y);

/// Inverse of the above for a whole viewport: the world pixel a screen pixel
/// samples from. Screen coordinates are relative to the viewport's top-left.
/// Used by the renderer to walk destination pixels, which leaves no gaps however
/// the map is turned.
void osd_map_screen_to_world(double centre_x, double centre_y, int w, int h, float rot_deg,
	double screen_x, double screen_y, double *world_x, double *world_y);

/// Largest zoom at which `span_m` metres still fits across `w` pixels, clamped
/// to [min_zoom, max_zoom]. Used to pick a sensible default zoom for the rect.
int osd_map_zoom_for_span(double lat, double span_m, int w, int min_zoom, int max_zoom);

/// Ground resolution at `lat` and `zoom`, metres per pixel.
double osd_map_mpp(double lat, int zoom);

// --- Moving view -----------------------------------------------------------
//
// A fixed zoom centred on the aircraft is the wrong map at both ends of the
// speed range: hovering, it shows a field of view you could walk across; at
// 30 m/s it shows where you were rather than where you are about to be. The
// view solver scales the zoom to ground speed and pushes the centre forward
// along the ground track, so what is ahead of the aircraft gets the screen.
//
// Ground *course*, not heading: in wind an aeroplane's nose and its track can
// differ by 20 degrees, and the map has to follow the track.

typedef struct {
	bool auto_zoom;      // false pins the map at fixed_zoom
	int fixed_zoom;      // zoom when auto is off, and the value the view starts at
	int min_zoom, max_zoom;
	float lookahead_s;   // seconds of travel that must fit across the map
	// How far ahead the centre runs, and where that stops. lead_s sets how
	// quickly the aircraft marker slides back as speed rises; lead_max_frac is
	// the ceiling it slides back to. With the shipped defaults the ceiling is
	// reached around 6 m/s, so in cruise the marker parks at a fixed fraction of
	// the way back and it is the zoom that keeps widening the ground shown
	// ahead. Lower lead_s to keep the seconds figure governing further up the
	// speed range; set it to 0 to centre on the aircraft as before.
	float lead_s;
	float lead_max_frac; // fraction of the half-viewport
	float settle_ms;     // how long a new zoom must be wanted before it is taken
	float smooth_ms;     // time constant for easing speed and the lead offset
} osd_map_view_cfg_t;

typedef struct {
	bool valid;
	uint64_t last_ms;
	float speed_mps;       // smoothed ground speed
	double lead_e, lead_n; // smoothed lead offset from the aircraft, metres
	// Smoothed ground track as a unit vector, for turning the map. Kept as a
	// vector so a track crossing north eases the short way round, and held
	// still below walking pace, where reported course is noise.
	double dir_e, dir_n;
	// Smoothed *heading* - where the nose points - kept separately from the
	// track. A camera looks where the nose looks, so a map meant to agree with
	// the video follows this, not the ground course; in wind they differ.
	double hdg_e, hdg_n;
	int zoom;              // the zoom in force
	int want_zoom;         // what the current speed asks for
	uint64_t want_since_ms;
} osd_map_view_t;

/// The bearing the smoothed track points, in degrees. 0 until the aircraft has
/// moved fast enough for its course to mean anything.
float osd_map_view_course(const osd_map_view_t *v);

/// The smoothed heading, in degrees. Unlike the track this has no speed gate -
/// which way the nose points is meaningful standing still.
float osd_map_view_heading(const osd_map_view_t *v);

void osd_map_view_init(osd_map_view_t *v);

/// Advances the view one frame and reports where to centre the map and at what
/// zoom. `w` and `h` are the map rectangle in pixels; `speed_mps` and
/// `course_deg` are the aircraft's ground track.
void osd_map_view_update(osd_map_view_t *v,
	const osd_map_view_cfg_t *cfg,
	double lat,
	double lon,
	float speed_mps,
	float course_deg,
	float heading_deg,
	int w,
	int h,
	uint64_t now_ms,
	int *out_zoom,
	double *out_lat,
	double *out_lon);

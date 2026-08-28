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

/// Cache-friendly filename for a tile, e.g. "sat_16_35252_21503.png".
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

/// Largest zoom at which `span_m` metres still fits across `w` pixels, clamped
/// to [min_zoom, max_zoom]. Used to pick a sensible default zoom for the rect.
int osd_map_zoom_for_span(double lat, double span_m, int w, int min_zoom, int max_zoom);

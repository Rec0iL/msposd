#include "osd_map.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#ifndef M_PI
	#define M_PI 3.14159265358979323846
#endif

// Web Mercator cannot represent the poles; this is the conventional cutoff.
#define MAX_LAT 85.05112878

static double clamp_lat(double lat) {
	if (lat > MAX_LAT)
		return MAX_LAT;
	if (lat < -MAX_LAT)
		return -MAX_LAT;
	return lat;
}

void osd_map_project(double lat, double lon, int zoom, double *world_x, double *world_y) {
	lat = clamp_lat(lat);
	while (lon > 180.0)
		lon -= 360.0;
	while (lon < -180.0)
		lon += 360.0;

	const double n = (double)(1u << zoom) * OSD_TILE_SIZE;
	const double lat_rad = lat * M_PI / 180.0;

	if (world_x)
		*world_x = (lon + 180.0) / 360.0 * n;
	if (world_y)
		*world_y = (1.0 - log(tan(lat_rad) + 1.0 / cos(lat_rad)) / M_PI) / 2.0 * n;
}

void osd_map_unproject(double world_x, double world_y, int zoom, double *lat, double *lon) {
	const double n = (double)(1u << zoom) * OSD_TILE_SIZE;
	if (lon)
		*lon = world_x / n * 360.0 - 180.0;
	if (lat) {
		double t = M_PI * (1.0 - 2.0 * world_y / n);
		*lat = 180.0 / M_PI * atan(0.5 * (exp(t) - exp(-t)));
	}
}

int osd_map_overlay_count(osd_map_style_t style) {
	return style == OSD_MAP_HYBRID ? 1 : 0;
}

bool osd_map_tile_url(osd_map_style_t style, int layer, int zoom, int tile_x, int tile_y,
	char *out, size_t out_size) {
	if (!out || out_size == 0)
		return false;

	switch (style) {
	case OSD_MAP_ROADS:
		if (layer != 0)
			return false;
		snprintf(out, out_size, "https://tile.openstreetmap.org/%d/%d/%d.png", zoom, tile_x, tile_y);
		return true;

	case OSD_MAP_SATELLITE:
	case OSD_MAP_HYBRID:
		if (layer == 0) {
			// Note the y/x order: Esri's REST tile scheme is {z}/{row}/{col},
			// the opposite of the OSM {z}/{x}/{y} convention.
			snprintf(out, out_size,
				"https://server.arcgisonline.com/ArcGIS/rest/services/World_Imagery/MapServer/"
				"tile/%d/%d/%d",
				zoom, tile_y, tile_x);
			return true;
		}
		if (layer == 1 && style == OSD_MAP_HYBRID) {
			// Place names and boundaries, drawn over the imagery.
			snprintf(out, out_size,
				"https://server.arcgisonline.com/ArcGIS/rest/services/"
				"Reference/World_Boundaries_and_Places/MapServer/tile/%d/%d/%d",
				zoom, tile_y, tile_x);
			return true;
		}
		return false;
	}
	return false;
}

bool osd_map_tile_key(osd_map_style_t style, int layer, int zoom, int tile_x, int tile_y,
	char *out, size_t out_size) {
	if (!out || out_size == 0)
		return false;
	const char *prefix = (style == OSD_MAP_ROADS) ? "osm"
					   : (layer == 1)             ? "lbl"
													: "sat";
	snprintf(out, out_size, "%s_%d_%d_%d.png", prefix, zoom, tile_x, tile_y);
	return true;
}

int osd_map_visible_tiles(double lat, double lon, int zoom, int w, int h, osd_map_tile_t *out,
	int max_out) {
	if (!out || max_out <= 0 || w <= 0 || h <= 0)
		return 0;

	double cx, cy;
	osd_map_project(lat, lon, zoom, &cx, &cy);

	// World pixel of the viewport's top-left corner.
	const double left = cx - w / 2.0;
	const double top = cy - h / 2.0;

	const int first_tx = (int)floor(left / OSD_TILE_SIZE);
	const int first_ty = (int)floor(top / OSD_TILE_SIZE);
	const int last_tx = (int)floor((left + w - 1) / OSD_TILE_SIZE);
	const int last_ty = (int)floor((top + h - 1) / OSD_TILE_SIZE);

	const int max_tile = (int)(1u << zoom);
	int count = 0;

	for (int ty = first_ty; ty <= last_ty && count < max_out; ty++) {
		// Latitude does not wrap, so a tile above or below the world is simply
		// absent rather than something to fetch.
		if (ty < 0 || ty >= max_tile)
			continue;
		for (int tx = first_tx; tx <= last_tx && count < max_out; tx++) {
			// Longitude does wrap, so the tile index is taken modulo the world.
			int wrapped = tx % max_tile;
			if (wrapped < 0)
				wrapped += max_tile;

			out[count].tile_x = wrapped;
			out[count].tile_y = ty;
			out[count].screen_x = (int)lround(tx * (double)OSD_TILE_SIZE - left);
			out[count].screen_y = (int)lround(ty * (double)OSD_TILE_SIZE - top);
			count++;
		}
	}
	return count;
}

void osd_map_point_in_view(double lat, double lon, double centre_lat, double centre_lon, int zoom,
	int w, int h, float *x, float *y) {
	double px, py, cx, cy;
	osd_map_project(lat, lon, zoom, &px, &py);
	osd_map_project(centre_lat, centre_lon, zoom, &cx, &cy);
	if (x)
		*x = (float)(px - (cx - w / 2.0));
	if (y)
		*y = (float)(py - (cy - h / 2.0));
}

int osd_map_zoom_for_span(double lat, double span_m, int w, int min_zoom, int max_zoom) {
	if (span_m <= 1.0 || w <= 0)
		return max_zoom;

	// Ground resolution at the equator for zoom 0, metres per pixel.
	const double equator_m = 40075016.686;
	const double lat_rad = clamp_lat(lat) * M_PI / 180.0;

	for (int z = max_zoom; z >= min_zoom; z--) {
		double mpp = equator_m * cos(lat_rad) / ((double)(1u << z) * OSD_TILE_SIZE);
		if (mpp * w >= span_m)
			return z;
	}
	return min_zoom;
}

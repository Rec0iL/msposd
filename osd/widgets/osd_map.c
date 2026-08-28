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
	// No extension: the cached bytes are stored verbatim and may be PNG (OSM) or
	// JPEG (Esri imagery), so claiming ".png" would be misleading.
	snprintf(out, out_size, "%s_%d_%d_%d.tile", prefix, zoom, tile_x, tile_y);
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
	osd_map_point_in_view_rot(lat, lon, centre_lat, centre_lon, zoom, w, h, 0.0f, x, y);
}

// Turning the map means rotating the (east, north) offset from the centre so
// that the chosen bearing lands on screen-up, then projecting as usual. At
// rot_deg 0 the matrix is the identity and this is plain north-up.
void osd_map_point_in_view_rot(double lat, double lon, double centre_lat, double centre_lon,
	int zoom, int w, int h, float rot_deg, float *x, float *y) {
	double px, py, cx, cy;
	osd_map_project(lat, lon, zoom, &px, &py);
	osd_map_project(centre_lat, centre_lon, zoom, &cx, &cy);

	// Screen y grows downwards, so north is -y.
	const double east = px - cx;
	const double north = -(py - cy);

	const double a = (double)rot_deg * M_PI / 180.0;
	const double ca = cos(a), sa = sin(a);
	const double east_r = east * ca - north * sa;
	const double north_r = east * sa + north * ca;

	if (x)
		*x = (float)(w / 2.0 + east_r);
	if (y)
		*y = (float)(h / 2.0 - north_r);
}

void osd_map_screen_to_world(double centre_x, double centre_y, int w, int h, float rot_deg,
	double screen_x, double screen_y, double *world_x, double *world_y) {
	const double sx = screen_x - w / 2.0;
	const double sy = screen_y - h / 2.0;

	const double a = (double)rot_deg * M_PI / 180.0;
	const double ca = cos(a), sa = sin(a);
	// Inverse of the rotation in osd_map_point_in_view_rot.
	const double east = sx * ca - sy * sa;
	const double north = -sx * sa - sy * ca;

	if (world_x)
		*world_x = centre_x + east;
	if (world_y)
		*world_y = centre_y - north;
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

double osd_map_mpp(double lat, int zoom) {
	if (zoom < 0)
		zoom = 0;
	if (zoom > 22)
		zoom = 22;
	const double equator_m = 40075016.686;
	return equator_m * cos(clamp_lat(lat) * M_PI / 180.0) / ((double)(1u << zoom) * OSD_TILE_SIZE);
}

void osd_map_view_init(osd_map_view_t *v) {
	if (v)
		memset(v, 0, sizeof(*v));
}

// Exponential easing that does not change character with the frame rate: at
// 60fps and at 10fps the view takes the same wall-clock time to settle.
static float ease_alpha(float dt_ms, float tau_ms) {
	if (dt_ms <= 0.0f)
		return 0.0f; // no time has passed, so nothing moves
	if (tau_ms <= 1.0f)
		return 1.0f; // smoothing turned off in the theme
	float a = 1.0f - expf(-dt_ms / tau_ms);
	return a < 0.0f ? 0.0f : (a > 1.0f ? 1.0f : a);
}

void osd_map_view_update(osd_map_view_t *v, const osd_map_view_cfg_t *cfg, double lat, double lon,
	float speed_mps, float course_deg, float heading_deg, int w, int h, uint64_t now_ms,
	int *out_zoom, double *out_lat, double *out_lon) {
	if (!v || !cfg)
		return;

	int min_z = cfg->min_zoom, max_z = cfg->max_zoom;
	if (min_z > max_z) {
		int t = min_z;
		min_z = max_z;
		max_z = t;
	}
	// The track can run along either axis of the rectangle, so the shorter one
	// is what has to hold the look-ahead.
	int span_px = (w < h ? w : h);
	if (span_px < 16)
		span_px = 16;
	if (!(speed_mps > 0.0f)) // also catches NaN from a bad telemetry frame
		speed_mps = 0.0f;

	if (!v->valid) {
		v->valid = true;
		v->speed_mps = speed_mps;
		v->zoom = cfg->auto_zoom ? max_z : cfg->fixed_zoom;
		v->want_zoom = v->zoom;
		v->want_since_ms = now_ms;
		v->lead_e = v->lead_n = 0.0;
		v->last_ms = now_ms;
	}

	float dt_ms = (float)(now_ms - v->last_ms);
	// A stall - a lost link, a paused replay - must not arrive as one enormous
	// step that snaps the view across the map.
	if (dt_ms < 0.0f || dt_ms > 2000.0f)
		dt_ms = 0.0f;
	v->last_ms = now_ms;

	const float a = ease_alpha(dt_ms, cfg->smooth_ms);
	v->speed_mps += (speed_mps - v->speed_mps) * a;

	int target = cfg->fixed_zoom;
	if (cfg->auto_zoom) {
		double span_m = (double)v->speed_mps * (double)cfg->lookahead_s;
		// Below the speed that fills the screen at max zoom there is nothing to
		// gain by zooming out, so slow flight simply sits at the closest zoom.
		double floor_m = osd_map_mpp(lat, max_z) * span_px;
		if (!(span_m > floor_m))
			span_m = floor_m;
		target = osd_map_zoom_for_span(lat, span_m, span_px, min_z, max_z);
	}
	if (target != v->want_zoom) {
		v->want_zoom = target;
		v->want_since_ms = now_ms;
	}
	// A zoom change discards every tile on screen and fetches a new set, so it
	// has to be worth doing: the new zoom must be what the speed has been asking
	// for continuously, not what it asked for during one gust.
	if (v->want_zoom != v->zoom && (float)(now_ms - v->want_since_ms) >= cfg->settle_ms)
		v->zoom = v->want_zoom;
	if (v->zoom < min_z)
		v->zoom = min_z;
	if (v->zoom > max_z)
		v->zoom = max_z;

	const double mpp = osd_map_mpp(lat, v->zoom);

	double lead_m = (double)v->speed_mps * (double)cfg->lead_s;
	// Cap the lead so the aircraft marker cannot be pushed off its own map.
	double cap_m = (double)cfg->lead_max_frac * (span_px * 0.5) * mpp;
	if (cap_m < 0.0)
		cap_m = 0.0;
	if (lead_m > cap_m)
		lead_m = cap_m;

	const double crs = (double)course_deg * M_PI / 180.0;

	// Track direction for turning the map. Below walking pace a GPS course is
	// noise, so the last usable heading is held rather than eased toward
	// whatever the receiver happened to report.
	// The *reported* speed, not the smoothed one: if the receiver says the
	// aircraft is barely moving right now, its course is noise right now,
	// whatever it was doing three seconds ago.
	if (speed_mps > 1.5f) {
		const double te = sin(crs), tn = cos(crs);
		if (v->dir_e == 0.0 && v->dir_n == 0.0) {
			v->dir_e = te;
			v->dir_n = tn;
		} else {
			v->dir_e += (te - v->dir_e) * a;
			v->dir_n += (tn - v->dir_n) * a;
		}
	}

	// Heading has no speed gate: the nose points somewhere even at a standstill.
	// Eased as a vector for the same reason as the track - so passing through
	// north takes the short way round - but on a much shorter constant.
	//
	// The track is smoothed hard because a GPS course is noisy and only wanted
	// as a trend. Heading comes from the attitude estimate at high rate and is
	// already smooth, and a heading-up map has to *stay* locked: at the
	// track's 1.5s the map lags most of a turn behind the nose, which is
	// exactly the "not locking properly" you see on a roll-in.
	{
		const float a_hdg = ease_alpha(dt_ms, 200.0f);
		const double hdg = (double)heading_deg * M_PI / 180.0;
		const double he = sin(hdg), hn = cos(hdg);
		if (v->hdg_e == 0.0 && v->hdg_n == 0.0) {
			v->hdg_e = he;
			v->hdg_n = hn;
		} else {
			v->hdg_e += (he - v->hdg_e) * a_hdg;
			v->hdg_n += (hn - v->hdg_n) * a_hdg;
		}
	}
	// Eased as a vector rather than as a bearing and a length: a track crossing
	// north would otherwise sweep the view the long way round through south.
	const double target_e = lead_m * sin(crs);
	const double target_n = lead_m * cos(crs);
	v->lead_e += (target_e - v->lead_e) * a;
	v->lead_n += (target_n - v->lead_n) * a;

	double cx, cy;
	osd_map_project(lat, lon, v->zoom, &cx, &cy);
	// Mercator is conformal, so one metre on the ground is the same number of
	// pixels in x and in y at a given latitude.
	cx += v->lead_e / mpp;
	cy -= v->lead_n / mpp;
	osd_map_unproject(cx, cy, v->zoom, out_lat, out_lon);

	if (out_zoom)
		*out_zoom = v->zoom;
}

float osd_map_view_course(const osd_map_view_t *v) {
	if (!v || (v->dir_e == 0.0 && v->dir_n == 0.0))
		return 0.0f;
	float deg = (float)(atan2(v->dir_e, v->dir_n) * 180.0 / M_PI);
	if (deg < 0.0f)
		deg += 360.0f;
	return deg;
}

float osd_map_view_heading(const osd_map_view_t *v) {
	if (!v || (v->hdg_e == 0.0 && v->hdg_n == 0.0))
		return 0.0f;
	float deg = (float)(atan2(v->hdg_e, v->hdg_n) * 180.0 / M_PI);
	if (deg < 0.0f)
		deg += 360.0f;
	return deg;
}

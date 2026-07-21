/*
 * map_render.c — native offline moving-map overlay for the ground station.
 *
 * Ground-Side rendering only (_x86 / __ROCKCHIP__): this file is #included from
 * osd.c inside that guard, exactly like poi_osd.c, so camera/OpenIPC builds never
 * see it or sqlite3.
 *
 * Reads raster tiles from an MBTiles (SQLite) file — the same maps the gs/ WebKit
 * viewer uses — and composites them into the OSD Cairo canvas BENEATH the OSD
 * glyphs using CAIRO_OPERATOR_DEST_OVER. The result layers as video < map < OSD
 * on both platforms (X11 present on x86, /msposd shm -> PixelPilot on Rockchip),
 * because DrawMap() is called right after Render() lays down the glyph base and
 * before the vector overlays (ladder/AHI/POI) draw on top.
 *
 * This renderer is independent of and does not touch the interactive WebKit map
 * (gs/map.sh + gs/mapwin); it is a separate, config-gated feature that shares the
 * same MBTiles (opened read-only) and gs/state.ini (home/target).
 */

#include <sqlite3.h>
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#define MAP_D2R        (M_PI / 180.0)
#define MAP_TILE_PX    256
#define MAP_TILE_CACHE 128          /* decoded tile surfaces kept in LRU */
#define MAP_MAX_ZOOMS  32

/* Follow modes mirror the WebKit map (gs/map.sh --follow ...):
 *   PLANE  : north-up, plane offset opposite its course so map leads ahead (default)
 *   NORTH  : north-up, plane centred
 *   FIT    : north-up, zoom+centre to fit home and plane
 *   CENTER : track-up (map rotates on course), plane pinned near the bottom edge */
enum { MAP_FOLLOW_NORTH = 0, MAP_FOLLOW_PLANE = 1, MAP_FOLLOW_FIT = 2, MAP_FOLLOW_CENTER = 3 };
enum { MAP_LAYOUT_CORNER = 0, MAP_LAYOUT_FULL = 1 };
enum { MAP_CORNER_TL = 0, MAP_CORNER_TR = 1, MAP_CORNER_BL = 2, MAP_CORNER_BR = 3, MAP_CORNER_CENTER = 4 };

/* ---- config (read once) ------------------------------------------------- */
static int   map_enabled     = -1;      /* -1 = config not read yet */
static char  map_mbtiles[512] = "gs/maps/OpenTopoMap.mbtiles";
static int   map_zoom        = 15;      /* requested zoom, snapped to available */
static int   map_follow      = MAP_FOLLOW_PLANE;
static int   map_shown       = 0;       /* hidden on start; show/hide with the 'g' key, gated by enabled */
static int   map_layout      = MAP_LAYOUT_CORNER;
static char  map_geometry[64] = "";     /* "WxH+X+Y" absolute px; overrides the anchor scheme below */
/* Anchor+size scheme (used when geometry is empty): a named corner plus
 * width/height/margin, each given in px ("480") or percent of the overlay
 * ("28%"). map_anchor_set is true once any of these keys appears in the ini. */
static int   map_corner       = MAP_CORNER_TR;
static char  map_w_spec[16]   = "";     /* width  spec, e.g. "28%" or "480" */
static char  map_h_spec[16]   = "";     /* height spec */
static char  map_margin_spec[16] = "";  /* gap from the docked edges */
static bool  map_anchor_set   = false;
static double map_opacity    = 1.0;     /* 0..1 */
static double map_lead_frac  = 0.30;    /* plane mode: offset plane this fraction of the region opposite its course (WebKit LEAD_FRAC) */
static double map_plane_frac = 0.8;     /* center mode: plane's Y as fraction of region (0.8=near bottom edge) */
static int    map_scalebar   = 1;       /* draw the distance scale bar (bottom-left) */
static int    map_frame_on    = 1;       /* rounded window frame around a corner map (ignored for full) */
static int    map_on_top      = 0;       /* 0 = map under the OSD text/icons; 1 = over them */
static double map_recenter_px   = 48.0; /* re-render tiles once the plane drifts this many px (WebKit VIEW_UPDATE_PX) */
static double map_head_gate_deg = 3.0;  /* re-render when course changes this much (affects lead/rotation) */

/* ---- MBTiles handle + available zooms ----------------------------------- */
static sqlite3      *map_db        = NULL;
static sqlite3_stmt *map_tile_stmt = NULL;
static int  map_zooms[MAP_MAX_ZOOMS];
static int  map_nzoom = 0;

/* Why the MBTiles couldn't be opened, surfaced as an on-screen message instead
 * of silently disabling the map. Reopen is retried on a backoff so a file that
 * appears later (e.g. after a preflight download) recovers on its own. */
enum { MAP_DB_OK = 0, MAP_DB_NO_FILE, MAP_DB_NO_TILES, MAP_DB_OPEN_ERR };
static int      map_db_fail = MAP_DB_OK;
static char     map_db_errmsg[128] = "";
static uint64_t map_open_next_try_ms = 0;

/* ---- decoded-tile LRU cache --------------------------------------------- */
typedef struct {
	int z, x, y;
	cairo_surface_t *surf;
	uint64_t used;
} map_tile_t;
static map_tile_t map_cache[MAP_TILE_CACHE];
static int        map_cache_n = 0;

/* ---- cached tile layer + per-frame scratch ------------------------------ *
 * map_surface holds the composited tiles (+ home/target), rendered only when the
 * plane drifts past map_recenter_px or the view otherwise changes — like the
 * WebKit map, the tiles stay put and only the plane marker moves each frame.
 * map_frame is a scratch surface where we stamp the moving plane over a copy of
 * the tiles before compositing beneath the OSD glyphs. */
static cairo_surface_t *map_surface = NULL;   /* cached tiles (region-sized) */
static cairo_surface_t *map_frame   = NULL;   /* per-frame scratch (tiles + plane) */
static int    map_frame_w = -1, map_frame_h = -1;

/* Render-time parameters: the mapping from world px -> region screen px that the
 * cached tiles were drawn with, used to project the moving plane each frame. */
static double map_r_cwx = 1e18, map_r_cwy = 1e18;  /* centre world px at last render */
static double map_r_ax = 0, map_r_ay = 0, map_r_rot = 0; /* anchor + rotation */
static double map_r_course = 1e9;
static int    map_r_z = -1, map_r_w = -1, map_r_h = -1, map_r_follow = -1;
static int    map_last_want = 0, map_last_got = 0;   /* tiles wanted/decoded (diag) */

/* ------------------------------------------------------------------------- */

static const char *map_follow_name(int f);
static int map_snap_zoom(int z);

static void map_read_config(void) {
	int v;
	int has_en = ReadIniInt("map", "enabled", &v);
	map_enabled = has_en ? (v ? 1 : 0) : 0;    /* default off */
	if (ReadIniInt("map", "zoom", &v) && v > 0) map_zoom = v;
	if (ReadIniInt("map", "opacity", &v)) map_opacity = (v < 0 ? 0 : v > 100 ? 100 : v) / 100.0;
	if (ReadIniInt("map", "plane_y", &v)) map_plane_frac = (v < 0 ? 0 : v > 100 ? 100 : v) / 100.0;
	if (ReadIniInt("map", "lead", &v))    map_lead_frac  = (v < 0 ? 0 : v > 49  ? 49  : v) / 100.0;
	if (ReadIniInt("map", "scalebar", &v)) map_scalebar = v ? 1 : 0;
	if (ReadIniInt("map", "frame", &v))    map_frame_on = v ? 1 : 0;
	if (ReadIniInt("map", "on_top", &v))   map_on_top   = v ? 1 : 0;
	if (ReadIniInt("map", "recenter_px", &v) && v > 0) map_recenter_px = v;
	if (ReadIniInt("map", "heading_gate_deg", &v) && v >= 0) map_head_gate_deg = v;

	ReadIniString("map", "mbtiles", map_mbtiles, sizeof(map_mbtiles));
	if (map_mbtiles[0] && map_mbtiles[0] != '/') {
		char abs[1024];
		snprintf(abs, sizeof(abs), "%s/%s", exe_dir(), map_mbtiles);
		snprintf(map_mbtiles, sizeof(map_mbtiles), "%s", abs);
	}
	char s[32] = "";
	if (ReadIniString("map", "follow", s, sizeof(s))) {
		if      (strcmp(s, "north")  == 0) map_follow = MAP_FOLLOW_NORTH;
		else if (strcmp(s, "fit")    == 0) map_follow = MAP_FOLLOW_FIT;
		else if (strcmp(s, "center") == 0 ||
		         strcmp(s, "centre") == 0) map_follow = MAP_FOLLOW_CENTER;
		else                               map_follow = MAP_FOLLOW_PLANE;
	}
	if (ReadIniString("map", "layout", s, sizeof(s)))
		map_layout = (strcmp(s, "full") == 0) ? MAP_LAYOUT_FULL : MAP_LAYOUT_CORNER;
	ReadIniString("map", "geometry", map_geometry, sizeof(map_geometry));

	/* Anchor+size scheme: corner + width/height/margin (px or %). Any of these
	 * keys opts into it; missing pieces fall back to the default corner box. */
	if (ReadIniString("map", "corner", s, sizeof(s))) {
		if      (strcmp(s, "top-left")     == 0) map_corner = MAP_CORNER_TL;
		else if (strcmp(s, "top-right")    == 0) map_corner = MAP_CORNER_TR;
		else if (strcmp(s, "bottom-left")  == 0) map_corner = MAP_CORNER_BL;
		else if (strcmp(s, "bottom-right") == 0) map_corner = MAP_CORNER_BR;
		else if (strcmp(s, "center")       == 0 ||
		         strcmp(s, "centre")       == 0) map_corner = MAP_CORNER_CENTER;
		map_anchor_set = true;
	}
	if (ReadIniString("map", "width",  map_w_spec,      sizeof(map_w_spec)))      map_anchor_set = true;
	if (ReadIniString("map", "height", map_h_spec,      sizeof(map_h_spec)))      map_anchor_set = true;
	if (ReadIniString("map", "margin", map_margin_spec, sizeof(map_margin_spec))) map_anchor_set = true;

	if (verbose)
		printf("[map] %s (mbtiles: %s, follow=%s, zoom=%d)\n",
		       map_enabled ? "enabled" : "disabled", map_mbtiles,
		       map_follow_name(map_follow), map_zoom);
}

static bool map_open_db(void) {
	if (sqlite3_open_v2(map_mbtiles, &map_db,
	        SQLITE_OPEN_READONLY, NULL) != SQLITE_OK) {
		const char *err = map_db ? sqlite3_errmsg(map_db) : "?";
		fprintf(stderr, "[map] cannot open %s: %s\n", map_mbtiles, err);
		/* missing file vs. present-but-unopenable (corrupt/locked/permissions) */
		map_db_fail = (access(map_mbtiles, F_OK) != 0) ? MAP_DB_NO_FILE : MAP_DB_OPEN_ERR;
		snprintf(map_db_errmsg, sizeof(map_db_errmsg), "%s", err);
		if (map_db) { sqlite3_close(map_db); map_db = NULL; }
		return false;
	}
	sqlite3_busy_timeout(map_db, 250);   /* tolerate preflight download writers */

	/* available zoom levels, ascending */
	sqlite3_stmt *zs = NULL;
	if (sqlite3_prepare_v2(map_db,
	        "SELECT DISTINCT zoom_level FROM tiles ORDER BY zoom_level", -1, &zs, NULL)
	    == SQLITE_OK) {
		while (map_nzoom < MAP_MAX_ZOOMS && sqlite3_step(zs) == SQLITE_ROW)
			map_zooms[map_nzoom++] = sqlite3_column_int(zs, 0);
	}
	sqlite3_finalize(zs);
	if (map_nzoom == 0) {
		fprintf(stderr, "[map] no tiles in %s\n", map_mbtiles);
		map_db_fail = MAP_DB_NO_TILES;
		sqlite3_close(map_db); map_db = NULL;
		return false;
	}

	if (sqlite3_prepare_v2(map_db,
	        "SELECT tile_data FROM tiles WHERE zoom_level=? AND tile_column=? AND tile_row=?",
	        -1, &map_tile_stmt, NULL) != SQLITE_OK) {
		snprintf(map_db_errmsg, sizeof(map_db_errmsg), "%s", sqlite3_errmsg(map_db));
		fprintf(stderr, "[map] prepare failed: %s\n", map_db_errmsg);
		map_db_fail = MAP_DB_OPEN_ERR;
		sqlite3_close(map_db); map_db = NULL;
		return false;
	}
	if (verbose) {
		printf("[map] %d zoom level(s):", map_nzoom);
		for (int i = 0; i < map_nzoom; i++) printf(" %d", map_zooms[i]);
		printf("\n");
	}
	map_db_fail = MAP_DB_OK;
	return true;
}

/* Master switch: the map feature (draw + all hotkeys + X grabs) is active only
 * when [map] enabled=1. When 0 the code is fully inert, exactly as before. */
static int map_config_enabled(void) {
	if (map_enabled < 0) map_read_config();
	return map_enabled == 1;
}

static const char *map_follow_name(int f) {
	return f == MAP_FOLLOW_NORTH  ? "north"
	     : f == MAP_FOLLOW_FIT    ? "fit"
	     : f == MAP_FOLLOW_CENTER ? "center"
	     :                          "plane";
}

/* 'g' — show/hide the overlay. No-op unless the feature is enabled in config, so
 * a disabled map can never be turned on by a keypress. */
static void map_toggle_enabled(void) {
	if (!map_config_enabled()) return;
	if (!map_db && !map_open_db()) {
		printf("[map] cannot show: no tiles in %s\n", map_mbtiles);
		return;
	}
	map_shown = !map_shown;
	if (map_shown) map_r_z = -1;            /* force a fresh compose */
	else if (map_surface) { cairo_surface_destroy(map_surface); map_surface = NULL; }
	printf("[map] %s\n", map_shown ? "shown" : "hidden");
}

/* '+'/'-' — step the effective zoom through the levels stored in the MBTiles. */
static void map_zoom_step(int dir) {
	if (!map_config_enabled()) return;
	if (!map_db && !map_open_db()) return;
	if (map_nzoom == 0) return;
	int cur = map_snap_zoom(map_zoom), idx = 0;
	for (int i = 0; i < map_nzoom; i++) if (map_zooms[i] == cur) { idx = i; break; }
	idx += (dir > 0) ? 1 : -1;
	if (idx < 0) idx = 0;
	if (idx >= map_nzoom) idx = map_nzoom - 1;
	map_zoom = map_zooms[idx];
	map_r_z = -1;                           /* force recompose at the new zoom */
	printf("[map] zoom %d%s\n", map_zoom,
	       map_follow == MAP_FOLLOW_FIT ? " (ignored in fit mode)" : "");
}

/* 'f' — cycle follow mode plane -> center -> north -> fit -> plane. */
static void map_follow_cycle(void) {
	if (!map_config_enabled()) return;
	static const int order[4] = { MAP_FOLLOW_PLANE, MAP_FOLLOW_CENTER,
	                              MAP_FOLLOW_NORTH, MAP_FOLLOW_FIT };
	int idx = 0;
	for (int i = 0; i < 4; i++) if (order[i] == map_follow) { idx = i; break; }
	map_follow = order[(idx + 1) % 4];
	map_r_z = -1; map_r_follow = -1;        /* force recompose */
	printf("[map] follow=%s\n", map_follow_name(map_follow));
}

/* Nearest available zoom to `z`. */
static int map_snap_zoom(int z) {
	if (map_nzoom == 0) return z;
	int best = map_zooms[0], bestd = abs(z - best);
	for (int i = 1; i < map_nzoom; i++) {
		int d = abs(z - map_zooms[i]);
		if (d < bestd) { bestd = d; best = map_zooms[i]; }
	}
	return best;
}

/* ---- Web-Mercator: lon/lat (deg) -> world pixel at zoom z ---------------- */
static void map_lonlat_px(double lon, double lat, int z, double *wx, double *wy) {
	double n = (double)(MAP_TILE_PX << z);          /* 256 * 2^z */
	*wx = (lon + 180.0) / 360.0 * n;
	double lr = lat * MAP_D2R;
	*wy = (1.0 - log(tan(lr) + 1.0 / cos(lr)) / M_PI) / 2.0 * n;
}

/* Inverse of the above: world pixel -> lon/lat (deg). Used to centre fit-mode on
 * the true pixel midpoint of two points (Mercator y is nonlinear, so the lat/lon
 * average is not the pixel centre). */
static void map_px_lonlat(double wx, double wy, int z, double *lat, double *lon) {
	double n = (double)(MAP_TILE_PX << z);
	*lon = wx / n * 360.0 - 180.0;
	double t = M_PI * (1.0 - 2.0 * wy / n);
	*lat = atan(sinh(t)) / MAP_D2R;
}

/* ---- PNG blob -> cairo surface ------------------------------------------ */
typedef struct { const unsigned char *data; unsigned long len, pos; } map_blob_t;
static cairo_status_t map_blob_read(void *closure, unsigned char *out, unsigned int len) {
	map_blob_t *r = (map_blob_t *)closure;
	if (r->pos + len > r->len) return CAIRO_STATUS_READ_ERROR;
	memcpy(out, r->data + r->pos, len);
	r->pos += len;
	return CAIRO_STATUS_SUCCESS;
}

/* Fetch a tile surface (from cache or MBTiles). Returns NULL if absent. */
static cairo_surface_t *map_get_tile(int z, int x, int y) {
	int world = 1 << z;
	if (x < 0 || y < 0 || x >= world || y >= world) return NULL;

	uint64_t now = get_time_ms();
	for (int i = 0; i < map_cache_n; i++) {
		if (map_cache[i].z == z && map_cache[i].x == x && map_cache[i].y == y) {
			map_cache[i].used = now;
			return map_cache[i].surf;      /* may be NULL (negative cache) */
		}
	}

	cairo_surface_t *surf = NULL;
	int tms_row = (world - 1) - y;         /* MBTiles stores TMS row order */
	sqlite3_reset(map_tile_stmt);
	sqlite3_clear_bindings(map_tile_stmt);
	sqlite3_bind_int(map_tile_stmt, 1, z);
	sqlite3_bind_int(map_tile_stmt, 2, x);
	sqlite3_bind_int(map_tile_stmt, 3, tms_row);
	if (sqlite3_step(map_tile_stmt) == SQLITE_ROW) {
		const void *blob = sqlite3_column_blob(map_tile_stmt, 0);
		int n = sqlite3_column_bytes(map_tile_stmt, 0);
		if (blob && n > 0) {
			map_blob_t rd = { (const unsigned char *)blob, (unsigned long)n, 0 };
			cairo_surface_t *s = cairo_image_surface_create_from_png_stream(map_blob_read, &rd);
			if (cairo_surface_status(s) == CAIRO_STATUS_SUCCESS) surf = s;
			else cairo_surface_destroy(s);
		}
	}
	sqlite3_reset(map_tile_stmt);

	/* insert into LRU (evict oldest when full) */
	int slot;
	if (map_cache_n < MAP_TILE_CACHE) {
		slot = map_cache_n++;
	} else {
		slot = 0;
		for (int i = 1; i < map_cache_n; i++)
			if (map_cache[i].used < map_cache[slot].used) slot = i;
		if (map_cache[slot].surf) cairo_surface_destroy(map_cache[slot].surf);
	}
	map_cache[slot].z = z; map_cache[slot].x = x; map_cache[slot].y = y;
	map_cache[slot].surf = surf; map_cache[slot].used = now;
	return surf;
}

/* ---- home from gs/state.ini (throttled: at most once a second) ----------- *
 * Home is captured live on the station at arm, so it stays in state.ini. The
 * target is preflight-authored and comes from the landmarks DB via poi_osd's
 * poi_get_target(), so map_render opens no DB of its own for it. */
static bool map_read_pt(const char *section, double *lat, double *lon) {
	int set = 0;
	if (!ReadIniIntPath(GS_STATE_PATH, section, "set", &set) || !set) return false;
	char buf[64];
	if (!ReadIniStringPath(GS_STATE_PATH, section, "lat", buf, sizeof(buf))) return false;
	*lat = atof(buf);
	if (!ReadIniStringPath(GS_STATE_PATH, section, "lon", buf, sizeof(buf))) return false;
	*lon = atof(buf);
	return true;
}

static bool     map_home_set = false, map_tgt_set = false;
static double   map_home_lat = 0, map_home_lon = 0, map_tgt_lat = 0, map_tgt_lon = 0;
static uint64_t map_pts_last_ms = 0;
static bool     map_pts_dirty = false;   /* set when home/target changed -> recompose */

static void map_refresh_points(void) {
	uint64_t now = get_time_ms();
	if (map_pts_last_ms != 0 && now - map_pts_last_ms < 1000) return;
	map_pts_last_ms = now;
	bool oh = map_home_set, ot = map_tgt_set;
	double ohla = map_home_lat, ohlo = map_home_lon, otla = map_tgt_lat, otlo = map_tgt_lon;
	map_home_set = map_read_pt("home", &map_home_lat, &map_home_lon);
	map_tgt_set  = poi_get_target(&map_tgt_lat, &map_tgt_lon);   /* from landmarks DB */
	if (map_home_set != oh || map_tgt_set != ot ||
	    map_home_lat != ohla || map_home_lon != ohlo ||
	    map_tgt_lat != otla || map_tgt_lon != otlo)
		map_pts_dirty = true;
}

/* ---- markers (drawn directly on the compose context) -------------------- */
static void map_marker_ring(cairo_t *c, double px, double py, double r,
                            double cr_, double cg, double cb) {
	cairo_new_path(c);
	cairo_arc(c, px, py, r, 0, 2 * M_PI);
	cairo_set_source_rgba(c, 0, 0, 0, 0.6);        /* halo */
	cairo_set_line_width(c, 4);
	cairo_stroke_preserve(c);
	cairo_set_source_rgba(c, cr_, cg, cb, 1.0);
	cairo_set_line_width(c, 2);
	cairo_stroke(c);
}

/* Home icon: a small house silhouette (gabled roof + body), haloed for contrast. */
static void map_marker_home(cairo_t *c, double px, double py) {
	cairo_save(c);
	cairo_translate(c, px, py);
	cairo_new_path(c);
	cairo_move_to(c, -6,  7);   /* bottom-left of body */
	cairo_line_to(c, -6, -1);   /* left wall up to eave */
	cairo_line_to(c, -8, -1);   /* left roof overhang */
	cairo_line_to(c,  0, -9);   /* roof apex */
	cairo_line_to(c,  8, -1);   /* right roof overhang */
	cairo_line_to(c,  6, -1);
	cairo_line_to(c,  6,  7);   /* right wall down */
	cairo_close_path(c);
	cairo_set_line_join(c, CAIRO_LINE_JOIN_ROUND);
	cairo_set_source_rgba(c, 0, 0, 0, 0.7);          /* halo */
	cairo_set_line_width(c, 3);
	cairo_stroke_preserve(c);
	cairo_set_source_rgba(c, 0.25, 1.0, 0.35, 1.0);  /* green */
	cairo_fill(c);
	cairo_restore(c);
}

static void map_marker_target(cairo_t *c, double px, double py) {
	map_marker_ring(c, px, py, 9, 1.0, 0.5, 0.5);  /* light red outer ring */
	map_marker_ring(c, px, py, 3, 1.0, 0.1, 0.1);  /* red inner ring */
}

/* Plane triangle at (px,py), nose rotated `ang` rad (0 = screen up). */
static void map_marker_plane(cairo_t *c, double px, double py, double ang) {
	cairo_save(c);
	cairo_translate(c, px, py);
	cairo_rotate(c, ang);
	cairo_new_path(c);
	cairo_move_to(c,  0, -11);
	cairo_line_to(c,  8,  10);
	cairo_line_to(c,  0,   5);
	cairo_line_to(c, -8,  10);
	cairo_close_path(c);
	cairo_set_source_rgba(c, 0, 0, 0, 0.7);        /* outline */
	cairo_set_line_width(c, 3);
	cairo_stroke_preserve(c);
	/* animate the fill so the plane is easy to spot: step yellow -> red -> pink.
	 * Advance by ONE colour each time enough time has elapsed since the last
	 * change, rather than mapping absolute time -> colour (which skips colours,
	 * sometimes for seconds, when the redraw cadence aliases with the period).
	 * This way every colour is shown in turn regardless of the frame timing. */
	static const double cyc[3][3] = {
		{ 1.0, 1.0, 0.0 },   /* yellow */
		{ 1.0, 0.0, 0.0 },   /* red */
		{ 1.0, 0.35, 0.7 },  /* pink */
	};
	static int      ph = 0;
	static uint64_t last_change = 0;
	uint64_t now = get_time_ms();
	if (now - last_change >= 150) { ph = (ph + 1) % 3; last_change = now; }
	cairo_set_source_rgba(c, cyc[ph][0], cyc[ph][1], cyc[ph][2], 1.0);
	cairo_fill(c);
	cairo_restore(c);
}

/* ---- fit-mode zoom: largest available zoom at which all points fit -------- *
 * Fits the pixel bounding box of every supplied point (plane, home, target) so
 * none is clipped. Reserve MAP_FIT_PAD px around the edges so an icon that lands
 * on the border stays whole (mirrors the WebKit map's fitBounds padding). */
#define MAP_FIT_PAD 28
static void map_bbox_px(const double *lats, const double *lons, int n, int z,
                        double *minx, double *miny, double *maxx, double *maxy) {
	*minx = *miny = 1e18; *maxx = *maxy = -1e18;
	for (int i = 0; i < n; i++) {
		double x, y;
		map_lonlat_px(lons[i], lats[i], z, &x, &y);
		if (x < *minx) *minx = x;  if (x > *maxx) *maxx = x;
		if (y < *miny) *miny = y;  if (y > *maxy) *maxy = y;
	}
}
static int map_fit_zoom(const double *lats, const double *lons, int n, int W, int H) {
	double usableW = fmax((double)W - 2 * MAP_FIT_PAD, 1.0);
	double usableH = fmax((double)H - 2 * MAP_FIT_PAD, 1.0);
	for (int i = map_nzoom - 1; i >= 0; i--) {
		int z = map_zooms[i];
		double minx, miny, maxx, maxy;
		map_bbox_px(lats, lons, n, z, &minx, &miny, &maxx, &maxy);
		if ((maxx - minx) <= usableW && (maxy - miny) <= usableH) return z;
	}
	return map_zooms[0];   /* too far apart even at the widest stored zoom */
}

/* Distance scale bar at the region's bottom-left, drawn in screen space so it
 * never rotates in track-up mode. z + centre latitude give Web-Mercator ground
 * metres-per-pixel; the bar length is snapped to a 1/2/3/5 x 10^n distance. */
static void map_draw_scalebar(cairo_t *c, int W, int H, int z, double clat) {
	double res = 156543.03392 * cos(clat * MAP_D2R) / (double)(1 << z);   /* m/px */
	if (!(res > 0)) return;
	double max_m = fmin(W * 0.33, 150.0) * res;      /* longest bar we allow */
	if (max_m < 1) return;
	double p10  = pow(10.0, floor(log10(max_m)));
	double d    = max_m / p10;
	double nice = (d >= 5 ? 5 : d >= 3 ? 3 : d >= 2 ? 2 : 1) * p10;       /* 1/2/3/5 x 10^n */
	double bar  = nice / res;                         /* bar length in px */
	if (bar < 8) return;

	char label[32];
	if (nice >= 1000) snprintf(label, sizeof(label), "%g km", nice / 1000.0);
	else              snprintf(label, sizeof(label), "%g m", nice);

	double x0 = 10.5, y0 = H - 10.5, tick = 5.0;     /* .5 keeps 1px lines crisp */
	cairo_new_path(c);
	cairo_move_to(c, x0, y0 - tick);
	cairo_line_to(c, x0, y0);
	cairo_line_to(c, x0 + bar, y0);
	cairo_line_to(c, x0 + bar, y0 - tick);
	cairo_set_line_join(c, CAIRO_LINE_JOIN_MITER);
	cairo_set_source_rgba(c, 0, 0, 0, 0.7);          /* halo */
	cairo_set_line_width(c, 4);
	cairo_stroke_preserve(c);
	cairo_set_source_rgba(c, 1, 1, 1, 1);            /* white bar */
	cairo_set_line_width(c, 2);
	cairo_stroke(c);

	cairo_select_font_face(c, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
	cairo_set_font_size(c, 13);
	cairo_text_extents_t ext;
	cairo_text_extents(c, label, &ext);
	cairo_move_to(c, x0 + (bar - ext.width) / 2.0 - ext.x_bearing, y0 - tick - 3);
	cairo_text_path(c, label);
	cairo_set_line_join(c, CAIRO_LINE_JOIN_ROUND);
	cairo_set_source_rgba(c, 0, 0, 0, 0.8);          /* text halo */
	cairo_set_line_width(c, 3);
	cairo_stroke_preserve(c);
	cairo_set_source_rgba(c, 1, 1, 1, 1);
	cairo_fill(c);
}

/* (Re)render the static tile layer into map_surface and record the world->screen
 * mapping (anchor + rotation) used, so the moving plane can be projected onto it
 * each frame. The plane is NOT drawn here — only the tiles and the geo-fixed
 * home/target markers, which move only when the view recenters.
 *   clat   : centre latitude — for the scale bar's metres-per-pixel.
 *   course : ground course — drives the plane-mode lead offset and center rotation. */
static void map_compose(double cwx, double cwy, int z, double clat, double course, int W, int H,
                        bool have_home, double hwx, double hwy,
                        bool have_tgt,  double twx, double twy) {
	if (map_surface) { cairo_surface_destroy(map_surface); map_surface = NULL; }
	map_surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, W, H);
	cairo_t *c = cairo_create(map_surface);

	/* anchor = where the centre world point (cwx,cwy) lands on screen, and the
	 * map rotation. Only center mode is track-up; the rest are north-up. */
	double ax = W / 2.0, ay = H / 2.0, rot = 0.0;
	if (map_follow == MAP_FOLLOW_CENTER) {
		ay = H * map_plane_frac;                       /* plane low (near edge) */
		rot = -course * MAP_D2R;                       /* rotate so course is up */
	} else if (map_follow == MAP_FOLLOW_PLANE) {
		/* offset the plane opposite its course so more map shows ahead. Course
		 * unit vector in screen coords (east=+x, north=-y), Chebyshev-normalised
		 * so the dominant axis gets the full lead. Matches WebKit leadCenter(). */
		double dx = sin(course * MAP_D2R), dy = -cos(course * MAP_D2R);
		double m = fmax(fabs(dx), fabs(dy));
		if (m < 1e-6) m = 1.0;
		ax = W * (0.5 - map_lead_frac * (dx / m));
		ay = H * (0.5 - map_lead_frac * (dy / m));
	}

	cairo_save(c);
	cairo_translate(c, ax, ay);
	cairo_rotate(c, rot);

	/* tiles: cover from the anchor out to the farthest screen corner, so an
	 * off-centre anchor and any rotation never expose unfilled area. */
	double half = 0.0;
	double cxs[4] = { 0, W, 0, W }, cys[4] = { 0, 0, H, H };
	for (int i = 0; i < 4; i++) {
		double d = hypot(cxs[i] - ax, cys[i] - ay);
		if (d > half) half = d;
	}
	half += MAP_TILE_PX;
	int tx0 = (int)floor((cwx - half) / MAP_TILE_PX);
	int tx1 = (int)floor((cwx + half) / MAP_TILE_PX);
	int ty0 = (int)floor((cwy - half) / MAP_TILE_PX);
	int ty1 = (int)floor((cwy + half) / MAP_TILE_PX);
	int want = 0, got = 0;
	for (int ty = ty0; ty <= ty1; ty++) {
		for (int tx = tx0; tx <= tx1; tx++) {
			want++;
			cairo_surface_t *t = map_get_tile(z, tx, ty);
			if (!t) continue;
			got++;
			double lx = tx * MAP_TILE_PX - cwx;
			double ly = ty * MAP_TILE_PX - cwy;
			cairo_set_source_surface(c, t, lx, ly);
			cairo_paint(c);
		}
	}
	map_last_want = want;
	map_last_got = got;

	/* geo-anchored markers (rings are rotation-invariant, so fine inside rot) */
	if (have_home) map_marker_home(c, hwx - cwx, hwy - cwy);
	if (have_tgt)  map_marker_target(c, twx - cwx, twy - cwy);

	cairo_restore(c);

	/* scale bar in screen space (unrotated), baked into the cached layer */
	if (map_scalebar) map_draw_scalebar(c, W, H, z, clat);

	cairo_destroy(c);

	/* remember the mapping so DrawMap() can project the moving plane onto it */
	map_r_cwx = cwx; map_r_cwy = cwy;
	map_r_ax = ax; map_r_ay = ay; map_r_rot = rot;
	map_r_course = course;
	map_r_z = z; map_r_w = W; map_r_h = H; map_r_follow = map_follow;
}

/* Project a world-pixel point onto the region using the last render's mapping. */
static void map_world_to_screen(double wx, double wy, double *sx, double *sy) {
	double dx = wx - map_r_cwx, dy = wy - map_r_cwy;
	double cs = cos(map_r_rot), sn = sin(map_r_rot);
	*sx = map_r_ax + (dx * cs - dy * sn);
	*sy = map_r_ay + (dx * sn + dy * cs);
}

/* ---- region geometry ---------------------------------------------------- */
/* Resolve a width/height/margin spec against a full extent: a trailing '%' means
 * percent of `full`, a plain number means pixels. Empty/invalid -> fallback. */
static int map_resolve_dim(const char *spec, int full, int fallback) {
	if (!spec[0]) return fallback;
	char *end = NULL;
	double v = strtod(spec, &end);
	if (end == spec) return fallback;                 /* not a number */
	while (*end == ' ' || *end == '\t') end++;
	if (*end == '%') v = v / 100.0 * (double)full;
	int px = (int)lround(v);
	return px > 0 ? px : fallback;
}

static void map_region(int *X, int *Y, int *W, int *H) {
	if (map_layout == MAP_LAYOUT_FULL) {
		*X = 0; *Y = 0; *W = OVERLAY_WIDTH; *H = OVERLAY_HEIGHT;
		return;
	}
	/* default corner box — also the per-field fallback for the anchor scheme:
	 * 20% narrower than a 1/3-screen box and 15% taller. */
	int defW = OVERLAY_WIDTH  * 4 / 15;    /* (1/3) * 0.8 */
	int defH = OVERLAY_HEIGHT * 19 / 50;   /* (1/3) * 1.2 * 1.15 */

	/* 1) explicit absolute geometry "WxH+X+Y" overrides everything */
	int w, h, x, y;
	if (map_geometry[0] && sscanf(map_geometry, "%dx%d+%d+%d", &w, &h, &x, &y) == 4) {
		*W = w; *H = h; *X = x; *Y = y;
		return;
	}

	/* 2) anchor+size scheme: named corner + width/height/margin (px or %).
	 * Percent margin is per-axis (2% of width horizontally, of height vertically). */
	if (map_anchor_set) {
		int mw  = map_resolve_dim(map_w_spec, OVERLAY_WIDTH,  defW);
		int mh  = map_resolve_dim(map_h_spec, OVERLAY_HEIGHT, defH);
		int mgx = map_resolve_dim(map_margin_spec, OVERLAY_WIDTH,  40);
		int mgy = map_resolve_dim(map_margin_spec, OVERLAY_HEIGHT, 40);
		if (mw > OVERLAY_WIDTH)  mw = OVERLAY_WIDTH;
		if (mh > OVERLAY_HEIGHT) mh = OVERLAY_HEIGHT;
		*W = mw; *H = mh;
		switch (map_corner) {
		case MAP_CORNER_TL:     *X = mgx;                    *Y = mgy;                       break;
		case MAP_CORNER_BL:     *X = mgx;                    *Y = OVERLAY_HEIGHT - mh - mgy; break;
		case MAP_CORNER_BR:     *X = OVERLAY_WIDTH - mw - mgx; *Y = OVERLAY_HEIGHT - mh - mgy; break;
		case MAP_CORNER_CENTER: *X = (OVERLAY_WIDTH - mw) / 2; *Y = (OVERLAY_HEIGHT - mh) / 2; break;
		case MAP_CORNER_TR:
		default:                *X = OVERLAY_WIDTH - mw - mgx; *Y = mgy;                       break;
		}
		return;
	}

	/* 3) legacy default corner: top-right, X margin 140, Y=50 */
	*W = defW; *H = defH;
	*X = OVERLAY_WIDTH - *W - 140;
	*Y = 50;
}

/* ---- rounded window frame ----------------------------------------------- *
 * Makes the corner map read as a floating widget: the tiles are clipped to a
 * rounded rect and framed with a soft outer halo, a 2px semi-transparent dark
 * border, and a 1px inner highlight (a subtle bevel). Screen space, so it never
 * rotates in track-up mode. Skipped for full-screen layout. */
#define MAP_FRAME_RADIUS 10.0

static void map_rounded_rect(cairo_t *c, double x, double y, double w, double h, double r) {
	if (r > w / 2) r = w / 2;
	if (r > h / 2) r = h / 2;
	cairo_new_sub_path(c);
	cairo_arc(c, x + w - r, y + r,     r, -90 * MAP_D2R,   0);
	cairo_arc(c, x + w - r, y + h - r, r,   0,             90 * MAP_D2R);
	cairo_arc(c, x + r,     y + h - r, r,  90 * MAP_D2R,  180 * MAP_D2R);
	cairo_arc(c, x + r,     y + r,     r, 180 * MAP_D2R,  270 * MAP_D2R);
	cairo_close_path(c);
}

static void map_draw_frame(cairo_t *c, int X, int Y, int W, int H) {
	double r = MAP_FRAME_RADIUS;
	/* soft halo/shadow: a few faint strokes fanning outward (cheap blur) */
	for (int i = 3; i >= 1; i--) {
		map_rounded_rect(c, X - i, Y - i, W + 2 * i, H + 2 * i, r + i);
		cairo_set_source_rgba(c, 0, 0, 0, 0.10);
		cairo_set_line_width(c, 2);
		cairo_stroke(c);
	}
	/* main 2px border, centred on the region edge (.5 keeps it crisp) */
	map_rounded_rect(c, X + 0.5, Y + 0.5, W - 1, H - 1, r);
	cairo_set_source_rgba(c, 0, 0, 0, 0.70);
	cairo_set_line_width(c, 2);
	cairo_stroke(c);
	/* 1px inner highlight just inside the border -> bevelled window edge */
	map_rounded_rect(c, X + 2.0, Y + 2.0, W - 4, H - 4, r - 1.5);
	cairo_set_source_rgba(c, 1, 1, 1, 0.22);
	cairo_set_line_width(c, 1);
	cairo_stroke(c);
}

/* ---- status / problem message ------------------------------------------- *
 * When the map can't be shown (no file, no tiles, outside coverage, no fix) we
 * still draw the window — a dark panel + framed border + centred text — so the
 * cause is visible instead of a blank or vanished overlay. */
static void map_text_center(cairo_t *c, double cx, double y, double size, int bold,
                            double r, double g, double b, const char *text) {
	cairo_select_font_face(c, "sans-serif", CAIRO_FONT_SLANT_NORMAL,
	                       bold ? CAIRO_FONT_WEIGHT_BOLD : CAIRO_FONT_WEIGHT_NORMAL);
	cairo_set_font_size(c, size);
	cairo_text_extents_t ext;
	cairo_text_extents(c, text, &ext);
	cairo_move_to(c, cx - ext.width / 2.0 - ext.x_bearing, y);
	cairo_text_path(c, text);
	cairo_set_line_join(c, CAIRO_LINE_JOIN_ROUND);
	cairo_set_source_rgba(c, 0, 0, 0, 0.85);          /* halo for legibility */
	cairo_set_line_width(c, 3);
	cairo_stroke_preserve(c);
	cairo_set_source_rgba(c, r, g, b, 1.0);
	cairo_fill(c);
}

/* Shrink a long string to fit maxw px by dropping the middle: keeps the leading
 * app-root and the trailing filename, e.g. "/home/…/OpenTopoMap.mbtiles". Sets
 * the (normal-weight) font at `size` for measuring, matching how it is drawn. */
static void map_ellipsize_middle(cairo_t *c, char *out, size_t outsz,
                                 const char *s, double maxw, double size) {
	cairo_select_font_face(c, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
	cairo_set_font_size(c, size);
	snprintf(out, outsz, "%s", s);
	cairo_text_extents_t ext;
	cairo_text_extents(c, out, &ext);
	if (ext.width <= maxw) return;
	size_t n = strlen(s);
	for (size_t keep = n; keep > 4; keep--) {
		size_t head = keep / 2, tail = keep - head;
		char buf[256];
		snprintf(buf, sizeof(buf), "%.*s…%s", (int)head, s, s + (n - tail));
		cairo_text_extents(c, buf, &ext);
		if (ext.width <= maxw) { snprintf(out, outsz, "%s", buf); return; }
	}
	snprintf(out, outsz, "…");
}

static void map_draw_message(cairo_t *cr, int X, int Y, int W, int H,
                             const char *title, const char *detail, const char *hint) {
	bool framed = map_frame_on && map_layout != MAP_LAYOUT_FULL;

	/* dark translucent panel so text reads over the video */
	cairo_save(cr);
	if (framed) map_rounded_rect(cr, X, Y, W, H, MAP_FRAME_RADIUS);
	else        cairo_rectangle(cr, X, Y, W, H);
	cairo_clip(cr);
	cairo_set_source_rgba(cr, 0.08, 0.09, 0.11, 0.82);
	cairo_paint(cr);
	cairo_restore(cr);

	if (framed) {
		cairo_save(cr);
		cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
		map_draw_frame(cr, X, Y, W, H);
		cairo_restore(cr);
	}

	/* centred text block: amber title, white detail (ellipsised), dim hint */
	cairo_save(cr);
	cairo_rectangle(cr, X, Y, W, H);
	cairo_clip(cr);
	double cx = X + W / 2.0, pad = 10.0;
	double tsz = 15, dsz = 12, hsz = 11, gap = 7;
	double block = tsz + (detail ? dsz + gap : 0) + (hint ? hsz + gap : 0);
	double y = Y + (H - block) / 2.0 + tsz;              /* baseline of the title */
	map_text_center(cr, cx, y, tsz, 1, 1.0, 0.78, 0.28, title);
	if (detail) {
		char buf[256];
		map_ellipsize_middle(cr, buf, sizeof(buf), detail, W - 2 * pad, dsz);
		y += dsz + gap;
		map_text_center(cr, cx, y, dsz, 0, 0.92, 0.93, 0.95, buf);
	}
	if (hint) {
		y += hsz + gap;
		map_text_center(cr, cx, y, hsz, 0, 0.70, 0.75, 0.80, hint);
	}
	cairo_restore(cr);
}

/*
 * Draw the moving map into the OSD canvas. Called twice per frame from the GS
 * compose path: once right after Render() (over_phase=false, the "under the OSD"
 * slot) and once just before FlushDrawing() (over_phase=true, the "over the OSD"
 * slot). The map only paints at the slot matching [map] on_top, so exactly one
 * call does work; DEST_OVER puts it under the OSD, OVER puts it on top.
 * lat_e7/lon_e7: plane position (MSP_RAW_GPS). heading_deg: aircraft yaw
 * (plane-icon direction in north-up modes). course_deg: ground course (plane-mode
 * lead offset and center-mode rotation) — matching the WebKit map.
 */
static void DrawMap(int32_t lat_e7, int32_t lon_e7, int16_t heading_deg, int16_t course_deg,
                    bool over_phase) {
	if (map_enabled < 0) map_read_config();
	if (map_enabled != 1) return;    /* master switch off -> behave exactly as before */
	if (!map_shown) return;          /* hidden via 'g' */
	if ((map_on_top ? 1 : 0) != (over_phase ? 1 : 0)) return;  /* paint at the matching slot only */
	if (!map_db) {
		/* Retry opening on a backoff (a file may appear after a preflight download)
		 * rather than disabling the map — keep the window and explain the problem. */
		uint64_t now = get_time_ms();
		if (now >= map_open_next_try_ms) {
			map_open_db();                       /* sets map_db_fail on failure */
			map_open_next_try_ms = now + 3000;
		}
		if (!map_db) {
			int X, Y, W, H;
			map_region(&X, &Y, &W, &H);
			if (W > 0 && H > 0) {
				const char *title, *detail = map_mbtiles, *hint;
				switch (map_db_fail) {
				case MAP_DB_NO_FILE:  title = "No offline map file";   hint = "Download it in preflight";    break;
				case MAP_DB_NO_TILES: title = "Map file has no tiles"; hint = "Re-download in preflight";     break;
				default:              title = "Cannot open map"; detail = map_db_errmsg; hint = "Check the map file"; break;
				}
				map_draw_message(cr, X, Y, W, H, title, detail, hint);
			}
			return;
		}
	}

	/* throttle verbose diagnostics to once per second */
	static uint64_t map_diag_ms = 0;
	bool diag = false;
	if (verbose) {
		uint64_t now = get_time_ms();
		if (now - map_diag_ms >= 1000) { map_diag_ms = now; diag = true; }
	}

	int X, Y, W, H;
	map_region(&X, &Y, &W, &H);
	if (W <= 0 || H <= 0) return;

	bool have_plane = !(lat_e7 == 0 && lon_e7 == 0);
	double plat = lat_e7 / 1e7, plon = lon_e7 / 1e7;
	map_refresh_points();
	bool have_home = map_home_set, have_tgt = map_tgt_set;
	double hlat = map_home_lat, hlon = map_home_lon, tlat = map_tgt_lat, tlon = map_tgt_lon;

	/* centre + zoom */
	double clat, clon;
	int z;
	/* FIT: zoom+centre to hold every point we have — plane, home and target —
	 * so none is clipped. Falls through to fixed-zoom centring when fewer than two
	 * points exist (a single point defines no box). */
	int fit_n = 0;
	double fit_lat[3], fit_lon[3];
	if (map_follow == MAP_FOLLOW_FIT) {
		if (have_plane) { fit_lat[fit_n] = plat; fit_lon[fit_n] = plon; fit_n++; }
		if (have_home)  { fit_lat[fit_n] = hlat; fit_lon[fit_n] = hlon; fit_n++; }
		if (have_tgt)   { fit_lat[fit_n] = tlat; fit_lon[fit_n] = tlon; fit_n++; }
	}
	if (map_follow == MAP_FOLLOW_FIT && fit_n >= 2) {
		z = map_fit_zoom(fit_lat, fit_lon, fit_n, W, H);
		/* centre on the pixel bounding-box centre at that zoom (not the lat/lon
		 * average, which in Mercator is not the pixel centre) so the set sits
		 * symmetric in the region. */
		double minx, miny, maxx, maxy;
		map_bbox_px(fit_lat, fit_lon, fit_n, z, &minx, &miny, &maxx, &maxy);
		map_px_lonlat((minx + maxx) / 2.0, (miny + maxy) / 2.0, z, &clat, &clon);
	} else if (map_follow == MAP_FOLLOW_FIT && fit_n == 1) {
		clat = fit_lat[0]; clon = fit_lon[0];   /* one point: centre on it, fixed zoom */
		z = map_snap_zoom(map_zoom);
	} else {
		if (have_plane)      { clat = plat; clon = plon; }
		else if (have_home)  { clat = hlat; clon = hlon; }
		else {                             /* nothing to centre on yet */
			if (diag) printf("[map] not drawn: no GPS fix and no home in %s\n", GS_STATE_PATH);
			map_draw_message(cr, X, Y, W, H, "Waiting for GPS fix…", NULL,
			                 "No plane position or home yet");
			return;
		}
		z = map_snap_zoom(map_zoom);
	}

	double cwx, cwy, pwx = 0, pwy = 0, hwx = 0, hwy = 0, twx = 0, twy = 0;
	map_lonlat_px(clon, clat, z, &cwx, &cwy);
	if (have_plane) map_lonlat_px(plon, plat, z, &pwx, &pwy);
	if (have_home)  map_lonlat_px(hlon, hlat, z, &hwx, &hwy);
	if (have_tgt)   map_lonlat_px(tlon, tlat, z, &twx, &twy);

	double course = course_deg, heading = heading_deg;

	/* Re-render the TILE layer only when the view really changes: the plane has
	 * drifted past map_recenter_px from the rendered centre, the course turned
	 * enough to move the lead/rotation, or zoom/region/mode/points changed. The
	 * plane itself is NOT a trigger — it is stamped over the static tiles below,
	 * so between re-renders the map holds still and only the plane moves. */
	double ddx = cwx - map_r_cwx, ddy = cwy - map_r_cwy;
	double dcourse = fabs(course - map_r_course); if (dcourse > 180) dcourse = 360 - dcourse;
	bool recenter = !map_surface || map_pts_dirty
	          || z != map_r_z || W != map_r_w || H != map_r_h || map_follow != map_r_follow
	          || (ddx * ddx + ddy * ddy) > (map_recenter_px * map_recenter_px)
	          || (have_plane && dcourse > map_head_gate_deg);
	if (recenter) {
		map_compose(cwx, cwy, z, clat, course, W, H,
		            have_home, hwx, hwy, have_tgt, twx, twy);
		map_pts_dirty = false;
	}
	if (!map_surface) return;

	if (diag)
		printf("[map] %s: centre %.5f,%.5f z=%d region %dx%d+%d+%d tiles %d/%d "
		       "(plane=%d home=%d tgt=%d)\n", recenter ? "re-render" : "move",
		       clat, clon, z, W, H, X, Y, map_last_got, map_last_want,
		       have_plane, have_home, have_tgt);
	if (map_last_got == 0 && diag)
		printf("[map] WARNING: 0 tiles at this location/zoom — is %.5f,%.5f inside "
		       "the MBTiles coverage at z=%d?\n", clat, clon, z);

	/* No tiles at all here -> the position is outside the downloaded area. Show
	 * the window with the reason instead of an empty (transparent) map. */
	if (map_last_got == 0) {
		char det[64];
		snprintf(det, sizeof(det), "GPS %.5f, %.5f", clat, clon);
		map_draw_message(cr, X, Y, W, H, "No offline map here", det,
		                 "Download this area in preflight");
		return;
	}

	/* Per-frame: stamp the moving plane over a copy of the static tiles, then
	 * composite the result beneath the OSD glyphs. Reuse a scratch surface. */
	if (map_frame && (map_frame_w != W || map_frame_h != H)) {
		cairo_surface_destroy(map_frame); map_frame = NULL;
	}
	if (!map_frame) {
		map_frame = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, W, H);
		map_frame_w = W; map_frame_h = H;
	}
	cairo_t *fc = cairo_create(map_frame);
	cairo_set_operator(fc, CAIRO_OPERATOR_SOURCE);
	cairo_set_source_surface(fc, map_surface, 0, 0);
	cairo_paint(fc);                                  /* copy the static tiles */
	if (have_plane) {
		double sx, sy;
		map_world_to_screen(pwx, pwy, &sx, &sy);      /* current plane position */
		/* center mode is track-up so the pinned plane points up; otherwise the
		 * icon points along the aircraft heading. */
		double ang = (map_follow == MAP_FOLLOW_CENTER) ? 0.0 : heading * MAP_D2R;
		cairo_set_operator(fc, CAIRO_OPERATOR_OVER);
		map_marker_plane(fc, sx, sy, ang);
	}
	cairo_destroy(fc);

	/* Composite into the OSD canvas, clipped to the region. on_top -> OVER (draw
	 * atop the OSD text/icons already in the frame); default -> DEST_OVER (slip
	 * beneath them). map_opacity lets the OSD bleed through when on top. */
	bool framed = map_frame_on && map_layout != MAP_LAYOUT_FULL;
	cairo_save(cr);
	if (framed) map_rounded_rect(cr, X, Y, W, H, MAP_FRAME_RADIUS);
	else        cairo_rectangle(cr, X, Y, W, H);
	cairo_clip(cr);
	cairo_set_source_surface(cr, map_frame, X, Y);
	cairo_set_operator(cr, over_phase ? CAIRO_OPERATOR_OVER : CAIRO_OPERATOR_DEST_OVER);
	cairo_paint_with_alpha(cr, map_opacity);
	cairo_restore(cr);

	/* The frame is a decoration drawn atop the map edge regardless of on_top, so
	 * the window outline stays visible whether the tiles sit over or under the OSD. */
	if (framed) {
		cairo_save(cr);
		cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
		map_draw_frame(cr, X, Y, W, H);
		cairo_restore(cr);
	}
}

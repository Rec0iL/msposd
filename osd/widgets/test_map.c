#include "osd_map.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static int fails = 0;
static void ck(const char *n, int c) {
    printf("  %-52s %s\n", n, c ? "PASS" : "FAIL");
    if (!c) fails++;
}

int main(void) {
    // Berlin, from the actual flight log we replay
    const double LAT = 52.4788960, LON = 13.6512704;

    double wx, wy;
    osd_map_project(LAT, LON, 16, &wx, &wy);
    int tx = (int)(wx / OSD_TILE_SIZE), ty = (int)(wy / OSD_TILE_SIZE);
    printf("  z16 world px (%.1f, %.1f) -> tile (%d, %d)\n", wx, wy, tx, ty);
    // Expected values come from the canonical slippy-map formula
    // (OSM wiki), not from hand arithmetic.
    ck("z16 tile x is 35253", tx == 35253);
    ck("z16 tile y is 21505", ty == 21505);

    // round trip
    double lat2, lon2;
    osd_map_unproject(wx, wy, 16, &lat2, &lon2);
    ck("unproject round-trips latitude", fabs(lat2 - LAT) < 1e-6);
    ck("unproject round-trips longitude", fabs(lon2 - LON) < 1e-6);

    // known anchors
    osd_map_project(0.0, 0.0, 0, &wx, &wy);
    ck("null island is the centre at z0", fabs(wx - 128.0) < 1e-6 && fabs(wy - 128.0) < 1e-6);
    osd_map_project(0.0, -180.0, 2, &wx, &wy);
    ck("antimeridian is x=0", fabs(wx) < 1e-6);

    // poles are clamped rather than producing infinities
    osd_map_project(89.9, 0.0, 10, &wx, &wy);
    // At the clamp latitude y is 0 by definition, give or take rounding.
    ck("north pole clamped to the top edge", isfinite(wy) && fabs(wy) < 1.0);
    osd_map_project(-89.9, 0.0, 10, &wx, &wy);
    ck("south pole clamped, finite", isfinite(wy));

    // URLs: Esri uses {z}/{row}/{col}, OSM uses {z}/{x}/{y}. Getting this
    // backwards silently returns the wrong part of the world.
    char url[256];
    osd_map_tile_url(OSD_MAP_ROADS, 0, 16, 35253, 21505, url, sizeof(url));
    ck("OSM url is z/x/y", strcmp(url, "https://tile.openstreetmap.org/16/35253/21505.png") == 0);
    osd_map_tile_url(OSD_MAP_SATELLITE, 0, 16, 35253, 21505, url, sizeof(url));
    ck("Esri url is z/y/x", strstr(url, "/tile/16/21505/35253") != NULL);
    ck("hybrid has one overlay", osd_map_overlay_count(OSD_MAP_HYBRID) == 1);
    ck("satellite has none", osd_map_overlay_count(OSD_MAP_SATELLITE) == 0);
    ck("roads has no overlay layer", !osd_map_tile_url(OSD_MAP_ROADS, 1, 16, 1, 1, url, sizeof(url)));
    ck("hybrid overlay url differs from base",
       osd_map_tile_url(OSD_MAP_HYBRID, 1, 16, 1, 1, url, sizeof(url)) && strstr(url, "Boundaries"));

    // cache keys must not collide across styles
    char a[64], b[64];
    osd_map_tile_key(OSD_MAP_ROADS, 0, 16, 1, 2, a, sizeof(a));
    osd_map_tile_key(OSD_MAP_SATELLITE, 0, 16, 1, 2, b, sizeof(b));
    ck("road and satellite keys differ", strcmp(a, b) != 0);
    osd_map_tile_key(OSD_MAP_HYBRID, 1, 16, 1, 2, b, sizeof(b));
    ck("overlay key differs from base", strcmp(a, b) != 0);

    // viewport coverage
    osd_map_tile_t tiles[64];
    int n = osd_map_visible_tiles(LAT, LON, 16, 512, 512, tiles, 64);
    ck("512x512 needs 4..9 tiles", n >= 4 && n <= 9);
    int covers_centre = 0;
    for (int i = 0; i < n; i++)
        if (tiles[i].tile_x == 35253 && tiles[i].tile_y == 21505) covers_centre = 1;
    ck("centre tile is included", covers_centre);

    // a small viewport must still fetch something
    n = osd_map_visible_tiles(LAT, LON, 16, 100, 60, tiles, 64);
    ck("small viewport still covered", n >= 1);

    // the aircraft sits at the centre of its own view
    float px, py;
    osd_map_point_in_view(LAT, LON, LAT, LON, 16, 400, 300, &px, &py);
    ck("aircraft centred in its own view", fabsf(px - 200.0f) < 0.5f && fabsf(py - 150.0f) < 0.5f);

    // a point east of centre is to the right, north of centre is above
    osd_map_point_in_view(LAT, LON + 0.001, LAT, LON, 16, 400, 300, &px, &py);
    ck("east is to the right", px > 200.0f);
    osd_map_point_in_view(LAT + 0.001, LON, LAT, LON, 16, 400, 300, &px, &py);
    ck("north is above", py < 150.0f);

    // zoom picking
    int z = osd_map_zoom_for_span(LAT, 500.0, 400, 3, 19);
    ck("500m across 400px picks a sane zoom", z >= 14 && z <= 18);
    ck("huge span clamps to min zoom", osd_map_zoom_for_span(LAT, 4e7, 400, 3, 19) == 3);
    ck("tiny span clamps to max zoom", osd_map_zoom_for_span(LAT, 0.5, 400, 3, 19) == 19);

    printf("\n%s (%d failure%s)\n", fails ? "FAILURES" : "ALL PASS", fails, fails == 1 ? "" : "s");
    return fails != 0;
}

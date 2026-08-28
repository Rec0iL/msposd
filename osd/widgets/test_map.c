#include "osd_map.h"
#include <math.h>
#include <stdbool.h>
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

    // --- the moving view: zoom follows ground speed, centre leads the track
    const osd_map_view_cfg_t cfg = {
        .auto_zoom = true, .fixed_zoom = 16, .min_zoom = 13, .max_zoom = 17,
        .lookahead_s = 20.0f, .lead_s = 6.0f, .lead_max_frac = 0.35f,
        .settle_ms = 3000.0f, .smooth_ms = 1500.0f,
    };
    const int MW = 420, MH = 300;

    // Runs the solver until it has settled, so a test asserts on the steady
    // state rather than on wherever the easing happened to be at frame one.
    #define SETTLE(v, sp, crs, secs) do { \
        for (int _i = 0; _i < (secs) * 10; _i++) { \
            osd_map_view_update(&(v), &cfg, LAT, LON, (sp), (crs), (crs), MW, MH, \
                                1000 + (uint64_t)_i * 100, &vz, &vlat, &vlon);; \
        } \
    } while (0)

    int vz; double vlat, vlon;
    osd_map_view_t view;

    osd_map_view_init(&view);
    SETTLE(view, 0.0f, 0.0f, 30);
    ck("stationary sits at max zoom", vz == 17);
    ck("stationary centres on the aircraft",
       fabs(vlat - LAT) < 1e-7 && fabs(vlon - LON) < 1e-7);

    osd_map_view_init(&view);
    SETTLE(view, 28.0f, 0.0f, 30);
    int fast_z = vz;
    ck("28m/s zooms further out than stationary", fast_z < 17);
    ck("28m/s stays within the configured floor", fast_z >= 13);
    // 28 m/s over the 20s look-ahead is 560m, which must fit across the map.
    ck("the look-ahead window fits across the map",
       osd_map_mpp(LAT, fast_z) * MH >= 560.0);
    ck("one zoom closer would not fit it",
       fast_z == 17 || osd_map_mpp(LAT, fast_z + 1) * MH < 560.0);

    // Heading north: the view centre must sit north of the aircraft.
    ck("northbound view leads north", vlat > LAT);
    ck("northbound view does not drift sideways", fabs(vlon - LON) < 1e-6);

    osd_map_view_init(&view);
    SETTLE(view, 28.0f, 90.0f, 30);
    ck("eastbound view leads east", vlon > LON);
    ck("eastbound view does not drift north", fabs(vlat - LAT) < 1e-6);

    // Below the ceiling the lead is proportional to speed - that is what the
    // "seconds ahead" figure means - and above it the marker parks.
    {
        double lead_a, lead_b;
        osd_map_view_init(&view);
        SETTLE(view, 3.0f, 0.0f, 30);
        lead_a = view.lead_n;
        osd_map_view_init(&view);
        SETTLE(view, 6.0f, 0.0f, 30);
        lead_b = view.lead_n;
        ck("slow flight leads in proportion to speed",
           fabs(lead_b - 2.0 * lead_a) < 0.05 * lead_b);
        ck("3m/s leads about lead_s seconds ahead", fabs(lead_a - 18.0) < 1.0);
    }
    {
        // At cruise the ceiling binds, so the marker sits lead_max_frac of the
        // half-viewport back from centre whatever the zoom works out to be.
        osd_map_view_init(&view);
        SETTLE(view, 28.0f, 0.0f, 30);
        float px, py;
        osd_map_point_in_view(LAT, LON, vlat, vlon, vz, MW, MH, &px, &py);
        double back = py - MH * 0.5;
        double want = cfg.lead_max_frac * (MH * 0.5); // shorter axis sets the cap
        ck("at cruise the marker parks at the lead ceiling", fabs(back - want) < 2.0);
    }

    // The lead must never push the aircraft marker off its own map.
    osd_map_view_init(&view);
    SETTLE(view, 400.0f, 45.0f, 60);
    {
        float px, py;
        osd_map_point_in_view(LAT, LON, vlat, vlon, vz, MW, MH, &px, &py);
        ck("absurd speed keeps the aircraft on the map",
           px > 0.0f && px < MW && py > 0.0f && py < MH);
    }

    // A zoom change discards every tile on screen, so a momentary gust must not
    // trigger one: the new zoom has to hold for settle_ms first.
    osd_map_view_init(&view);
    SETTLE(view, 0.0f, 0.0f, 30);
    {
        int before = vz;
        uint64_t t = 100000;
        for (int i = 0; i < 10; i++) // 1s of high speed, settle is 3s
            osd_map_view_update(&view, &cfg, LAT, LON, 30.0f, 0.0f, 0.0f, MW, MH,
                                t + (uint64_t)i * 100, &vz, &vlat, &vlon);;
        ck("a one-second gust does not change zoom", vz == before);
        for (int i = 10; i < 100; i++)
            osd_map_view_update(&view, &cfg, LAT, LON, 30.0f, 0.0f, 0.0f, MW, MH,
                                t + (uint64_t)i * 100, &vz, &vlat, &vlon);;
        ck("sustained speed does change it", vz < before);
    }

    // Crossing north: eased as a vector, so the view must not swing the long way
    // round through south while the track goes 350 -> 010.
    osd_map_view_init(&view);
    SETTLE(view, 25.0f, 350.0f, 30);
    {
        bool went_south = false;
        uint64_t t = 200000;
        for (int i = 0; i < 100; i++) {
            osd_map_view_update(&view, &cfg, LAT, LON, 25.0f, 10.0f, 10.0f, MW, MH,
                                t + (uint64_t)i * 100, &vz, &vlat, &vlon);;
            if (vlat < LAT) went_south = true;
        }
        ck("350 -> 010 never swings the view south", !went_south);
    }

    // A stalled link arrives as one huge dt; it must not fling the view.
    osd_map_view_init(&view);
    SETTLE(view, 0.0f, 0.0f, 10);
    {
        double before_lat = vlat, before_lon = vlon;
        osd_map_view_update(&view, &cfg, LAT, LON, 30.0f, 90.0f, 90.0f, MW, MH, 9000000,
                            &vz, &vlat, &vlon);;
        ck("a telemetry stall does not jump the view",
           fabs(vlat - before_lat) < 1e-9 && fabs(vlon - before_lon) < 1e-9);
    }

    // auto_zoom off must pin the zoom, leaving the lead alone.
    {
        osd_map_view_cfg_t fixed = cfg;
        fixed.auto_zoom = false;
        osd_map_view_init(&view);
        for (int i = 0; i < 600; i++)
            osd_map_view_update(&view, &fixed, LAT, LON, 28.0f, 90.0f, 90.0f, MW, MH,
                                1000 + (uint64_t)i * 100, &vz, &vlat, &vlon);;
        ck("auto_zoom off pins the zoom", vz == 16);
        ck("auto_zoom off still leads the track", vlon > LON);
    }

    // --- turning the map: rot 0 must be exactly the old north-up behaviour, and
    // the rotated case must put the chosen bearing at the top of the screen.
    {
        float px0, py0, px1, py1;
        osd_map_point_in_view(LAT, LON + 0.001, LAT, LON, 16, 400, 300, &px0, &py0);
        osd_map_point_in_view_rot(LAT, LON + 0.001, LAT, LON, 16, 400, 300, 0.0f, &px1, &py1);
        ck("rot 0 matches the unrotated projection",
           fabsf(px0 - px1) < 0.01f && fabsf(py0 - py1) < 0.01f);

        // Heading east with the map turned east-up: a point to the east must
        // appear above the centre, not to the right of it.
        osd_map_point_in_view_rot(LAT, LON + 0.001, LAT, LON, 16, 400, 300, 90.0f, &px1, &py1);
        ck("east-up puts an eastern point above centre", py1 < 150.0f && fabsf(px1 - 200.0f) < 1.0f);

        // And north then lies to the left.
        osd_map_point_in_view_rot(LAT + 0.001, LON, LAT, LON, 16, 400, 300, 90.0f, &px1, &py1);
        ck("east-up puts north to the left", px1 < 200.0f && fabsf(py1 - 150.0f) < 1.0f);
    }
    {
        // screen_to_world is the exact inverse of the forward transform, which
        // is what stops the rotated renderer tearing gaps in the tiles.
        double cxw, cyw;
        osd_map_project(LAT, LON, 16, &cxw, &cyw);
        for (float rot = 0.0f; rot < 360.0f; rot += 37.0f) {
            float sx, sy;
            osd_map_point_in_view_rot(LAT + 0.0008, LON + 0.0011, LAT, LON, 16, 400, 300, rot, &sx, &sy);
            double wx, wy;
            osd_map_screen_to_world(cxw, cyw, 400, 300, rot, sx, sy, &wx, &wy);
            double ex, ey;
            osd_map_project(LAT + 0.0008, LON + 0.0011, 16, &ex, &ey);
            if (fabs(wx - ex) > 0.01 || fabs(wy - ey) > 0.01) {
                ck("screen_to_world inverts point_in_view_rot", false);
                break;
            }
            if (rot > 320.0f) ck("screen_to_world inverts point_in_view_rot", true);
        }
    }
    {
        // The smoothed track holds still below walking pace: a stationary
        // aircraft reporting jittery course must not spin the map.
        osd_map_view_init(&view);
        SETTLE(view, 20.0f, 90.0f, 30);
        const float moving = osd_map_view_course(&view);
        ck("track follows a real course", fabsf(moving - 90.0f) < 3.0f);

        uint64_t t = 300000;
        for (int i = 0; i < 200; i++) {
            osd_map_view_update(&view, &cfg, LAT, LON, 0.2f, (float)((i * 71) % 360), (float)((i * 71) % 360), MW, MH, t + (uint64_t)i * 100, &vz, &vlat, &vlon);
        }
        ck("a stationary aircraft does not spin the map",
           fabsf(osd_map_view_course(&view) - moving) < 1.0f);
    }

    {
        // Heading has no speed gate: a stationary aircraft still points somewhere,
        // and a heading-up map has to follow it. The track, by contrast, stays
        // put because a GPS course at a standstill is noise.
        osd_map_view_init(&view);
        for (int i = 0; i < 300; i++)
            osd_map_view_update(&view, &cfg, LAT, LON, 0.0f, 0.0f, 270.0f, MW, MH,
                                1000 + (uint64_t)i * 100, &vz, &vlat, &vlon);
        ck("heading follows the nose at a standstill",
           fabsf(osd_map_view_heading(&view) - 270.0f) < 3.0f);
        ck("the track ignores a standstill", osd_map_view_course(&view) == 0.0f);

        // In wind the two differ, and a heading-up map has to follow the nose.
        osd_map_view_init(&view);
        SETTLE(view, 22.0f, 90.0f, 30);          // course east...
        for (int i = 0; i < 300; i++)             // ...nose 20 degrees off it
            osd_map_view_update(&view, &cfg, LAT, LON, 22.0f, 90.0f, 70.0f, MW, MH,
                                400000 + (uint64_t)i * 100, &vz, &vlat, &vlon);
        ck("heading and track are reported separately",
           fabsf(osd_map_view_heading(&view) - 70.0f) < 3.0f &&
           fabsf(osd_map_view_course(&view) - 90.0f) < 3.0f);

        // And it must keep up: a heading-up map that lags is the whole complaint.
        {
            osd_map_view_init(&view);
            SETTLE(view, 20.0f, 0.0f, 10);
            float worst = 0.0f;
            uint64_t t2 = 900000;
            for (int i = 0; i < 40; i++) {   // 4s of turn at 45 deg/s
                float nose = (float)(i * 4.5);
                osd_map_view_update(&view, &cfg, LAT, LON, 20.0f, nose, nose, MW, MH,
                                    t2 + (uint64_t)i * 100, &vz, &vlat, &vlon);
                float lag = fabsf(osd_map_view_heading(&view) - nose);
                if (lag > 180.0f) lag = 360.0f - lag;
                if (i > 10 && lag > worst) worst = lag;
            }
            ck("heading keeps up through a 45deg/s turn", worst < 15.0f);
        }

    }

    printf("\n%s (%d failure%s)\n", fails ? "FAILURES" : "ALL PASS", fails, fails == 1 ? "" : "s");
    return fails != 0;
}

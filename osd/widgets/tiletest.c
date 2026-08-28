// Proves the tile pipeline end to end: fetch -> disk cache -> decode -> blit.
#include "osd/widgets/osd_tiles.h"
#include "osd/widgets/osd_paint.h"
#include "libpng/lodepng.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define VW 512
#define VH 384

int main(int argc, char **argv) {
    const double LAT = 52.4788960, LON = 13.6512704;  // from our flight log
    int zoom = 15;
    osd_map_style_t style = (argc > 1 && !strcmp(argv[1], "sat")) ? OSD_MAP_SATELLITE : OSD_MAP_ROADS;

    if (!osd_tiles_init("/tmp/msposd-tiles")) { printf("init failed\n"); return 1; }

    osd_map_tile_t tiles[32];
    int n = osd_map_visible_tiles(LAT, LON, zoom, VW, VH, tiles, 32);
    printf("viewport %dx%d at z%d needs %d tiles\n", VW, VH, zoom, n);

    // Ask once to queue them, then wait for the background worker.
    for (int i = 0; i < n; i++) osd_tiles_get(style, 0, zoom, tiles[i].tile_x, tiles[i].tile_y);
    for (int s = 0; s < 40; s++) {
        int cached=0, queued=0, fetched=0, failed=0;
        osd_tiles_stats(&cached,&queued,&fetched,&failed);
        if (fetched + failed >= n) break;
        struct timespec ts = {0, 250L*1000*1000}; nanosleep(&ts, NULL);
    }
    int cached=0, queued=0, fetched=0, failed=0;
    osd_tiles_stats(&cached,&queued,&fetched,&failed);
    printf("cached=%d queued=%d fetched=%d failed=%d\n", cached, queued, fetched, failed);

    uint8_t *buf = calloc((size_t)VW*VH*4, 1);
    osd_surface_t s; osd_surface_init(&s, buf, VW, VH, VW*4);
    int drawn = 0;
    for (int i = 0; i < n; i++) {
        const osd_tile_bitmap_t *b = osd_tiles_get(style, 0, zoom, tiles[i].tile_x, tiles[i].tile_y);
        if (!b) continue;
        if (drawn == 0) {
            long sum = 0; size_t npx = (size_t)b->width * b->height;
            for (size_t k = 0; k < npx; k++) sum += b->pixels[k*4] + b->pixels[k*4+1] + b->pixels[k*4+2];
            printf("  first tile %dx%d mean channel = %.1f  alpha[0]=%d\n",
                   b->width, b->height, (double)sum/(npx*3), b->pixels[3]);
        }
        drawn++;
        for (int y = 0; y < b->height; y++) {
            int dy = tiles[i].screen_y + y;
            if (dy < 0 || dy >= VH) continue;
            for (int x = 0; x < b->width; x++) {
                int dx = tiles[i].screen_x + x;
                if (dx < 0 || dx >= VW) continue;
                const uint8_t *px = b->pixels + ((size_t)y*b->width + x)*4;
                uint8_t *dp = buf + ((size_t)dy*VW + dx)*4;
                dp[0]=px[0]; dp[1]=px[1]; dp[2]=px[2]; dp[3]=255;
            }
        }
    }
    printf("blitted %d/%d tiles\n", drawn, n);

    // aircraft marker at the centre
    float ax, ay; osd_map_point_in_view(LAT, LON, LAT, LON, zoom, VW, VH, &ax, &ay);
    osd_draw_line(&s, ax-10, ay, ax+10, ay, 3.0f, OSD_ARGB(255,0,229,255));
    osd_draw_line(&s, ax, ay-10, ax, ay+10, 3.0f, OSD_ARGB(255,0,229,255));

    uint8_t *rgba = malloc((size_t)VW*VH*4);
    for (size_t i = 0; i < (size_t)VW*VH; i++) {
        rgba[i*4+0]=buf[i*4+2]; rgba[i*4+1]=buf[i*4+1]; rgba[i*4+2]=buf[i*4+0]; rgba[i*4+3]=255;
    }
    const char *out = (style == OSD_MAP_SATELLITE) ? "map-sat.png" : "map-roads.png";
    printf(lodepng_encode32_file(out, rgba, VW, VH) ? "png error\n" : "wrote %s\n", out);
    osd_tiles_shutdown();
    return drawn == 0;
}

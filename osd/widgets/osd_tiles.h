// Tile store: memory cache, disk cache, and background fetching.
//
// Rendering must never block on the network. Every lookup returns immediately,
// either with a decoded tile or with nothing, and queues a fetch for next time.
// A map that is missing a tile draws a gap for a moment; a map that stalls the
// OSD waiting for one is unusable.
#pragma once

#include "osd_map.h"
#include "osd_paint.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct {
	uint8_t *pixels; // BGRA, OSD_TILE_SIZE square, owned by the store
	int width, height;
} osd_tile_bitmap_t;

/// `cache_dir` is created if missing. Passing NULL disables the disk cache and
/// keeps tiles in memory only.
bool osd_tiles_init(const char *cache_dir);
void osd_tiles_shutdown(void);

/// Returns a decoded tile, or NULL if it is not available yet. Never blocks:
/// a miss queues a background fetch and returns NULL straight away.
const osd_tile_bitmap_t *osd_tiles_get(osd_map_style_t style, int layer, int zoom, int x, int y);

/// True once at least one fetch has failed, so the UI can say "no tiles"
/// instead of silently drawing an empty rectangle forever.
bool osd_tiles_had_error(void);

/// Counters for diagnostics: tiles in memory, queued, fetched, failed.
void osd_tiles_stats(int *cached, int *queued, int *fetched, int *failed);

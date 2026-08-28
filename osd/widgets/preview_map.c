// Visual check for the speed-driven map view: the same position and track at
// three ground speeds, rendered through the real recogniser and the real widget
// renderer, with real tiles.
//
// What to look for, left to right: the zoom steps out as the speed rises, and
// the aircraft marker slides back from the centre so more of the track ahead is
// on screen. The note in the bottom-left corner reports both.
#include "osd/elements/osd_elements.h"
#include "osd/widgets/osd_tiles.h"
#include "osd/widgets/osd_widgets.h"
#include "libpng/lodepng.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define COLS 53
#define ROWS 20
#define CELL_W 36
#define CELL_H 54
#define MARGIN 10
#define PANE_W 440
#define PANE_H 320

// INAV symbol glyphs, from src/main/drivers/osd_symbols.h.
#define SYM_LAT 0x03
#define SYM_LON 0x04

static uint16_t grid[ROWS][COLS];

static uint16_t getter(int c, int r, void *x) {
	(void)x;
	if (c < 0 || c >= COLS || r < 0 || r >= ROWS)
		return 0;
	return grid[r][c];
}

static void put_text(int row, int col, uint16_t sym, const char *txt) {
	grid[row][col] = sym;
	for (int i = 0; txt[i]; i++)
		grid[row][col + 1 + i] = (uint16_t)txt[i];
}

int main(void) {
	// Berlin, from the blackbox log the SITL replay uses.
	const char *LAT_TXT = "52.47890";
	const char *LON_TXT = "13.65127";
	const float COURSE = 45.0f; // north-east, so the lead shows on both axes
	const float speeds[3] = {0.0f, 12.0f, 28.0f};
	const int orientations[3] = {0, 0, 1}; // last pane turns with the track

	for (int r = 0; r < ROWS; r++)
		for (int c = 0; c < COLS; c++)
			grid[r][c] = 0x20;
	// Latitude marks the map's top-left corner, longitude the bottom-right.
	put_text(0, 0, SYM_LAT, LAT_TXT);
	put_text(6, 12, SYM_LON, LON_TXT);

	osd_element_t els[16];
	int n = osd_elements_scan(getter, NULL, COLS, ROWS, "INAV", els, 16);
	printf("recognised %d elements\n", n);

	osd_theme_t th;
	osd_theme_defaults(&th);
	if (!osd_theme_load(&th, "themes/tactical/theme.ini"))
		printf("!! theme load failed, using defaults\n");
	osd_font_t *font = osd_font_load(th.font_path);
	if (!font) {
		printf("!! font load failed: %s\n", th.font_path);
		return 1;
	}
	if (!osd_tiles_init(th.map_cache_dir))
		printf("!! tile store init failed, expect gaps\n");

	const int W = PANE_W * 3, H = PANE_H;
	uint8_t *buf = calloc((size_t)W * H * 4, 1);
	uint8_t *pane = malloc((size_t)PANE_W * PANE_H * 4);

	for (int i = 0; i < 3; i++) {
		osd_widget_state_t st;
		osd_widgets_state_init(&st);
		osd_widgets_update_arm(&st, true);
		st.heading_deg = COURSE;
		st.ground_speed_mps = speeds[i];
		th.map_orientation = orientations[i];
		st.course_deg = COURSE;

		osd_surface_t s;
		osd_grid_t g = {CELL_W, CELL_H, MARGIN, MARGIN, NULL, NULL};

		// Run 40s of simulated time so the zoom hysteresis and the easing settle,
		// with real time in between so background tile fetches actually land.
		for (int f = 0; f < 400; f++) {
			memset(pane, 0, (size_t)PANE_W * PANE_H * 4);
			osd_surface_init(&s, pane, PANE_W, PANE_H, PANE_W * 4);
			osd_widgets_draw_all(&s, &th, font, &st, els, n, &g, 1000 + (uint64_t)f * 100);
			usleep(20000);
		}
		printf("  %4.0f m/s %-9s -> z%d, lead %.0fm NE\n", (double)speeds[i],
			orientations[i] ? "track-up" : "north-up", st.map_view.zoom,
			(double)(st.map_view.lead_n / 0.7071));

		for (int y = 0; y < PANE_H; y++)
			memcpy(buf + ((size_t)y * W + (size_t)i * PANE_W) * 4,
				pane + (size_t)y * PANE_W * 4, (size_t)PANE_W * 4);
	}

	// Composite over a stand-in video frame, as the hardware does: without it
	// the translucent panel fill reads as opaque white.
	uint8_t *rgba = malloc((size_t)W * H * 4);
	for (size_t i = 0; i < (size_t)W * H; i++) {
		int x = (int)(i % W), y = (int)(i / W);
		int vb = 0x14 + ((x / 64 + y / 64) % 2) * 0x0A, vg = 0x18 + ((y / 160) % 3) * 0x06, vr = 0x10;
		float a = buf[i * 4 + 3] / 255.0f;
		rgba[i * 4 + 0] = (uint8_t)(buf[i * 4 + 2] * a + vr * (1 - a));
		rgba[i * 4 + 1] = (uint8_t)(buf[i * 4 + 1] * a + vg * (1 - a));
		rgba[i * 4 + 2] = (uint8_t)(buf[i * 4 + 0] * a + vb * (1 - a));
		rgba[i * 4 + 3] = 255;
	}
	printf(lodepng_encode32_file("map-speed.png", rgba, W, H) ? "png err\n"
																: "wrote map-speed.png\n");
	osd_tiles_shutdown();
	return 0;
}

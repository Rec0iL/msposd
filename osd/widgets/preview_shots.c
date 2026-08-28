// Shareable screenshots: the widget OSD over a still frame standing in for live
// video.
//
//   ./preview_shots <backdrop.png> [outdir]
//
// The glyph grid is built exactly as INAV would send it and run through the real
// recogniser, theme and renderer, so what comes out is what the ground station
// draws - not a mock-up. The backdrop is composited underneath with real alpha,
// which is what the hardware does; without that step the translucent panel fill
// reads as opaque.
//
// Not included: the artificial horizon. msposd draws that from osd.c with its
// own ladder code, well outside the widget layer this harness links.
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

// INAV symbol glyphs, from src/main/drivers/osd_symbols.h.
#define SYM_RSSI 0x01
#define SYM_LAT 0x03
#define SYM_LON 0x04
#define SYM_SAT_L 0x08
#define SYM_SAT_R 0x09
#define SYM_VOLT 0x1F
#define SYM_AMP 0x6A
#define SYM_ALT_M 0x76
#define SYM_THR 0x95
#define SYM_MAH 0x99
#define SYM_FLY_M 0x9F
#define SYM_BATT_FULL 0x63 // 0x63..0x69, full to empty

static uint16_t grid[ROWS][COLS];

static uint16_t getter(int c, int r, void *x) {
	(void)x;
	if (c < 0 || c >= COLS || r < 0 || r >= ROWS)
		return 0;
	return grid[r][c];
}

static void clear_grid(void) {
	for (int r = 0; r < ROWS; r++)
		for (int c = 0; c < COLS; c++)
			grid[r][c] = 0x20;
}

static void put_str(int row, int col, const char *t) {
	for (int i = 0; t[i]; i++)
		grid[row][col + i] = (uint16_t)t[i];
}

// Symbol first, value right-aligned in a field of `field` cells after it - the
// layout INAV uses for RSSI, throttle and the like.
static void put_leading(int row, int col, uint16_t sym, const char *v, int field) {
	grid[row][col] = sym;
	int n = (int)strlen(v);
	for (int i = 0; i < n; i++)
		grid[row][col + field - n + 1 + i] = (uint16_t)v[i];
}

// Value then symbol, right-aligned: the digits end at `sym_col - 1`.
static void put_trailing(int row, int sym_col, const char *v, uint16_t sym) {
	int n = (int)strlen(v);
	for (int i = 0; i < n; i++)
		grid[row][sym_col - n + i] = (uint16_t)v[i];
	grid[row][sym_col] = sym;
}

typedef struct {
	const char *name;
	const char *alt, *thr, *volts, *amps, *mah, *rssi, *sats, *ftime, *mode;
	int batt_icon;       // 0 = full .. 6 = empty
	const char *message; // OSD message row, or NULL
	float speed_mps, course_deg, heading_deg;
} scene_t;

// One frame of the flight, laid out the way the flight controller sends it.
static void build_grid(const scene_t *s, const char *lat_txt, const char *lon_txt) {
	clear_grid();

	// left column: how the aircraft is being flown
	put_trailing(0, 5, s->alt, SYM_ALT_M);
	put_leading(2, 1, SYM_THR, s->thr, 4);

	// right column: what the battery and the link are doing. The charge icon
	// sits immediately before the digits - that adjacency is what marks the
	// reading as *battery* voltage rather than any other volt reading.
	put_trailing(0, 47, s->volts, SYM_VOLT);
	grid[0][47 - (int)strlen(s->volts) - 1] = (uint16_t)(SYM_BATT_FULL + s->batt_icon);
	put_leading(0, 49, SYM_RSSI, s->rssi, 3);
	put_trailing(2, 47, s->amps, SYM_AMP);
	put_trailing(4, 47, s->mah, SYM_MAH);

	// lower right: time and mode
	put_leading(15, 41, SYM_FLY_M, s->ftime, 6);
	put_str(17, 44, s->mode);

	// Latitude marks the map's top-left corner, longitude the bottom-right;
	// where they are placed on the flight controller *is* the map's rectangle.
	grid[8][1] = SYM_LAT;
	put_str(8, 2, lat_txt);
	grid[14][12] = SYM_LON;
	put_str(14, 13, lon_txt);

	// satellites, under the map
	grid[18][1] = SYM_SAT_L;
	grid[18][2] = SYM_SAT_R;
	put_str(18, 4, s->sats);

	if (s->message)
		put_str(11, (COLS - (int)strlen(s->message)) / 2, s->message);
}

int main(int argc, char **argv) {
	if (argc < 2) {
		printf("usage: %s <backdrop.png> [outdir]\n", argv[0]);
		return 2;
	}
	const char *outdir = argc > 2 ? argv[2] : ".";

	unsigned bw = 0, bh = 0;
	unsigned char *back = NULL;
	if (lodepng_decode32_file(&back, &bw, &bh, argv[1])) {
		printf("!! cannot read backdrop %s\n", argv[1]);
		return 1;
	}
	const int W = (int)bw, H = (int)bh;
	printf("backdrop %dx%d\n", W, H);

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
		printf("!! tile store init failed, expect gaps in the map\n");

	// The braided Isar above Wallgau: pale gravel channels through conifer
	// forest, which is what the drone photo behind these shots looks down on. A
	// minimap showing somewhere else entirely is the first thing that gives a
	// screenshot away.
	const char *LAT_TXT = "47.54860", *LON_TXT = "11.38000";
	// Launch point ~350m south-west: on the map at the wider zooms, off the edge
	// at the closest one, so both the home marker and its edge arrow appear
	// across the three scenes.
	const double HOME_LAT = 47.54638, HOME_LON = 11.37671;

	const scene_t scenes[] = {
		// Cruise: everything healthy, moving fast, so the map is zoomed out and
		// running ahead of the aircraft.
		{"cruise", "166", "81", "14.80", "41", "423", "95", "12", "01:33", "ACRO", 1, NULL,
			25.0f, 60.0f, 62.0f},
		// Loitering: barely moving, so the map closes right in and re-centres.
		{"loiter", "84", "38", "14.10", "12", "612", "88", "14", "04:12", "PH", 2, NULL,
			3.0f, 0.0f, 15.0f},
		// Getting home on what is left: red battery, weak link, RTH.
		{"lowbatt", "121", "64", "13.28", "36", "1480", "28", "7", "09:47", "RTH", 5,
			"LOW BATTERY", 22.0f, 240.0f, 236.0f},
	};
	const int nscenes = (int)(sizeof(scenes) / sizeof(scenes[0]));

	uint8_t *buf = malloc((size_t)W * H * 4);
	uint8_t *rgba = malloc((size_t)W * H * 4);

	for (int i = 0; i < nscenes; i++) {
		const scene_t *sc = &scenes[i];
		build_grid(sc, LAT_TXT, LON_TXT);

		osd_element_t els[32];
		int n = osd_elements_scan(getter, NULL, COLS, ROWS, "INAV", els, 32);

		osd_widget_state_t st;
		osd_widgets_state_init(&st);
		osd_widgets_update_arm(&st, true);
		st.heading_deg = sc->heading_deg;
		st.ground_speed_mps = sc->speed_mps;
		st.course_deg = sc->course_deg;
		// Home is captured at arming in flight; set it here so the launch point
		// is somewhere other than under the aircraft.
		st.home_valid = true;
		st.home_lat = HOME_LAT;
		st.home_lon = HOME_LON;
		// A peak from earlier in the flight, so the current bar has a scale.
		st.current_peak = 47.0f;
		// Pack size is normally inferred from the first reading after arming,
		// when the pack is full. Seeding it keeps the low-battery scene honest:
		// 13.28V read cold divides out to 3S, which no 4S pack ever is.
		st.cell_count = 4;

		osd_surface_t s;
		osd_grid_t g = {CELL_W, CELL_H, (W - COLS * CELL_W) / 2, 0, NULL, NULL};

		// 40s of simulated time so the map view settles, with real time in
		// between so background tile fetches land before the last frame.
		for (int f = 0; f < 400; f++) {
			memset(buf, 0, (size_t)W * H * 4);
			osd_surface_init(&s, buf, W, H, W * 4);
			osd_widgets_draw_all(&s, &th, font, &st, els, n, &g, 1000 + (uint64_t)f * 100);
			usleep(15000);
		}

		// Composite over the backdrop with real alpha, as the hardware does.
		for (size_t p = 0; p < (size_t)W * H; p++) {
			float a = buf[p * 4 + 3] / 255.0f;
			rgba[p * 4 + 0] = (uint8_t)(buf[p * 4 + 2] * a + back[p * 4 + 0] * (1 - a));
			rgba[p * 4 + 1] = (uint8_t)(buf[p * 4 + 1] * a + back[p * 4 + 1] * (1 - a));
			rgba[p * 4 + 2] = (uint8_t)(buf[p * 4 + 0] * a + back[p * 4 + 2] * (1 - a));
			rgba[p * 4 + 3] = 255;
		}

		char path[512];
		snprintf(path, sizeof(path), "%s/osd-%s.png", outdir, sc->name);
		printf("  %-8s %2d elements, z%d, %.0fm/s -> %s\n", sc->name, n, st.map_view.zoom,
			(double)sc->speed_mps, path);
		if (lodepng_encode32_file(path, rgba, (unsigned)W, (unsigned)H))
			printf("  !! png write failed\n");
	}

	osd_tiles_shutdown();
	return 0;
}

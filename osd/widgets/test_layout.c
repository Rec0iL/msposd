// Panel placement must not depend on how many digits a reading happens to have.
//
// Flight controllers right-align their readouts, so "99" and "100" share a
// right edge and differ on the left. Placing a widget against the run's
// leftmost cell therefore walked it one cell left every time a value crossed
// 99 -> 100 or 999 -> 1000, which is exactly what this checks for.
#include "osd_widgets.h"

#include <stdio.h>
#include <string.h>

#define COLS 53
#define ROWS 20
#define CELL_W 36
#define CELL_H 54
#define SURF_W (COLS * CELL_W)
#define SURF_H 860

// INAV symbol glyphs, from src/main/drivers/osd_symbols.h.
#define SYM_MAH 0x99
#define SYM_THR 0x95
#define SYM_VOLT 0x1F
#define SYM_RSSI 0x01
#define SYM_LAT 0x03
#define SYM_LON 0x04

static uint16_t grid[ROWS][COLS];

// A blank cell is the glyph 0x20, not the byte 0x20: memset() over a uint16_t
// grid would fill it with 0x2020, which no blank check recognises.
static void clear_grid(void) {
	for (int r = 0; r < ROWS; r++)
		for (int c = 0; c < COLS; c++)
			grid[r][c] = 0x20;
}

static uint16_t getter(int col, int row, void *ctx) {
	(void)ctx;
	if (col < 0 || col >= COLS || row < 0 || row >= ROWS)
		return 0;
	return grid[row][col];
}

static int fails = 0;

static void check(const char *name, int cond) {
	printf("  %-52s %s\n", name, cond ? "PASS" : "FAIL");
	if (!cond)
		fails++;
}

// Writes a right-aligned reading whose symbol sits at `sym_col`, the way a
// flight controller lays the field out: digits grow leftwards from the symbol.
static void put_trailing(int row, int sym_col, const char *digits, uint16_t sym) {
	clear_grid();
	int n = (int)strlen(digits);
	for (int i = 0; i < n; i++)
		grid[row][sym_col - n + i] = (uint16_t)digits[i];
	grid[row][sym_col] = sym;
}

// The other direction: symbol first, value right-aligned in the field after it.
static void put_leading(int row, int sym_col, const char *digits, uint16_t sym, int field) {
	clear_grid();
	grid[row][sym_col] = sym;
	int n = (int)strlen(digits);
	for (int i = 0; i < n; i++)
		grid[row][sym_col + field - n + 1 + i] = (uint16_t)digits[i];
}

// Same as put_trailing/put_leading, but without clearing the grid first, so a
// scene can hold more than one element.
static void put_str(int row, int col, const char *t) {
	for (int i = 0; t[i]; i++)
		grid[row][col + i] = (uint16_t)t[i];
}

static void put_trailing_at(int row, int sym_col, const char *digits, uint16_t sym) {
	int n = (int)strlen(digits);
	for (int i = 0; i < n; i++)
		grid[row][sym_col - n + i] = (uint16_t)digits[i];
	grid[row][sym_col] = sym;
}

static void put_leading_at(int row, int sym_col, const char *digits, uint16_t sym, int field) {
	grid[row][sym_col] = sym;
	int n = (int)strlen(digits);
	for (int i = 0; i < n; i++)
		grid[row][sym_col + field - n + 1 + i] = (uint16_t)digits[i];
}

typedef struct {
	float x, w;
} placed_t;

// Runs one frame through the real recogniser and the real renderer, and reports
// where the panel for `type` ended up.
static bool render(osd_theme_t *th, osd_font_t *font, osd_widget_state_t *st, uint8_t *buf,
	uint64_t now_ms, osd_element_type_t type, placed_t *out) {
	osd_element_t els[32];
	int n = osd_elements_scan(getter, NULL, COLS, ROWS, "INAV", els, 32);
	if (n == 0)
		return false;

	memset(buf, 0, (size_t)SURF_W * SURF_H * 4);
	osd_surface_t s;
	osd_surface_init(&s, buf, SURF_W, SURF_H, SURF_W * 4);
	osd_grid_t g = {CELL_W, CELL_H, 8, 0, NULL, NULL};
	osd_widgets_draw_all(&s, th, font, st, els, n, &g, now_ms);

	if (!st->layout[type].valid)
		return false;
	out->x = st->layout[type].x;
	out->w = st->layout[type].w;
	return true;
}

int main(void) {
	osd_theme_t th;
	osd_theme_defaults(&th);
	th.map_enabled = false; // no lat/lon here, but keep the tile store out of it

	osd_font_t *font = osd_font_load("fonts/UbuntuMono-Regular.ttf");
	if (!font) {
		printf("!! font load failed - run from the repo root\n");
		return 1;
	}
	static uint8_t buf[(size_t)SURF_W * SURF_H * 4];

	// --- cold placement: a fresh layout must land in the same place whatever
	// the current reading happens to be.
	placed_t cold[3];
	const char *runs[3] = {"99", "100", "1000"};
	for (int i = 0; i < 3; i++) {
		osd_widget_state_t st;
		osd_widgets_state_init(&st);
		put_trailing(6, 20, runs[i], SYM_MAH);
		if (!render(&th, font, &st, buf, 1000, OSD_ELEM_MAH, &cold[i])) {
			printf("!! mAh element not recognised for \"%s\"\n", runs[i]);
			return 1;
		}
	}
	check("cold layout: 99 and 100 place the panel identically", cold[0].x == cold[1].x);
	check("cold layout: 100 and 1000 place the panel identically", cold[1].x == cold[2].x);

	// --- live transition: the case actually reported. One long-lived state,
	// the reading climbing past both digit boundaries.
	{
		osd_widget_state_t st;
		osd_widgets_state_init(&st);
		placed_t p[3];
		for (int i = 0; i < 3; i++) {
			put_trailing(6, 20, runs[i], SYM_MAH);
			if (!render(&th, font, &st, buf, 1000 + (uint64_t)i * 100, OSD_ELEM_MAH, &p[i])) {
				printf("!! mAh element vanished at \"%s\"\n", runs[i]);
				return 1;
			}
		}
		check("in flight: 99 -> 100 does not move the panel", p[0].x == p[1].x);
		check("in flight: 999 -> 1000 does not move the panel", p[1].x == p[2].x);
		check("in flight: the panel does not shuffle other panels either",
			st.overlap_warnings == 0);
	}

	// --- the same, with the panel narrow enough that the text really does
	// drive its width. Growing must extend the panel leftwards so the
	// right-aligned value stays where the flight controller put it.
	{
		osd_theme_t narrow = th;
		narrow.panel_min_width = 40.0f;
		osd_widget_state_t st;
		osd_widgets_state_init(&st);
		placed_t p[3];
		for (int i = 0; i < 3; i++) {
			put_trailing(6, 20, runs[i], SYM_MAH);
			if (!render(&narrow, font, &st, buf, 1000 + (uint64_t)i * 100, OSD_ELEM_MAH, &p[i])) {
				printf("!! mAh element vanished at \"%s\"\n", runs[i]);
				return 1;
			}
		}
		check("text-sized panel: width grows with the reading", p[2].w > p[0].w);
		check("text-sized panel: right edge stays put 99 -> 100",
			p[0].x + p[0].w == p[1].x + p[1].w);
		check("text-sized panel: right edge stays put 999 -> 1000",
			p[1].x + p[1].w == p[2].x + p[2].w);
	}

	// --- a leading symbol anchors on its left instead, and must be unaffected.
	{
		placed_t a, b;
		osd_widget_state_t st1, st2;
		osd_widgets_state_init(&st1);
		osd_widgets_state_init(&st2);
		put_leading(6, 4, "7", SYM_THR, 3);
		if (!render(&th, font, &st1, buf, 1000, OSD_ELEM_THROTTLE, &a)) {
			printf("!! throttle element not recognised\n");
			return 1;
		}
		put_leading(6, 4, "100", SYM_THR, 3);
		if (!render(&th, font, &st2, buf, 1000, OSD_ELEM_THROTTLE, &b)) {
			printf("!! throttle element not recognised\n");
			return 1;
		}
		check("leading symbol: panel stays on the symbol column", a.x == b.x);
		check("leading symbol: panel starts at the symbol's cell", a.x == 8 + 4 * CELL_W);
	}

	// --- an OSD message carries no number, so it must not be filtered out with
	// the elements whose value failed to parse. A dropped failsafe banner is the
	// worst possible thing for this OSD to lose.
	{
		osd_widget_state_t st;
		osd_widgets_state_init(&st);
		clear_grid();
		put_str(9, 20, "FAILSAFE");
		osd_element_t els[32];
		int n = osd_elements_scan(getter, NULL, COLS, ROWS, "INAV", els, 32);
		bool recognised = false;
		for (int i = 0; i < n; i++)
			if (els[i].type == OSD_ELEM_WARNING)
				recognised = true;
		check("message: recognised in the grid", recognised);

		memset(buf, 0, (size_t)SURF_W * SURF_H * 4);
		osd_surface_t s;
		osd_surface_init(&s, buf, SURF_W, SURF_H, SURF_W * 4);
		osd_grid_t g = {CELL_W, CELL_H, 8, 0, NULL, NULL};
		int drawn = osd_widgets_draw_all(&s, &th, font, &st, els, n, &g, 1000);
		check("message: actually drawn", drawn > 0 && st.layout[OSD_ELEM_WARNING].valid);
		check("message: banner is wider than it is tall",
			st.layout[OSD_ELEM_WARNING].w > st.layout[OSD_ELEM_WARNING].h);
	}

	// --- a panel pulled left to fit on screen must not land on one already
	// placed. The clamp and the collision pass have to run in that order.
	{
		osd_widget_state_t st;
		osd_widgets_state_init(&st);
		clear_grid();
		// Two readings side by side near the right edge, both on row 0: the
		// second one's panel is far wider than the gap left for it.
		grid[0][40] = 0x63; // battery icon, full
		put_trailing_at(0, 47, "14.80", SYM_VOLT);
		grid[0][40] = 0x63;
		put_leading_at(0, 49, "95", SYM_RSSI, 3);

		osd_element_t els[32];
		int n = osd_elements_scan(getter, NULL, COLS, ROWS, "INAV", els, 32);
		memset(buf, 0, (size_t)SURF_W * SURF_H * 4);
		osd_surface_t s;
		osd_surface_init(&s, buf, SURF_W, SURF_H, SURF_W * 4);
		osd_grid_t g = {CELL_W, CELL_H, 8, 0, NULL, NULL};
		osd_widgets_draw_all(&s, &th, font, &st, els, n, &g, 1000);

		bool have = st.layout[OSD_ELEM_VOLTAGE].valid && st.layout[OSD_ELEM_RSSI].valid;
		check("clamped panel: both panels placed", have);
		if (have) {
			float ax = st.layout[OSD_ELEM_VOLTAGE].x, ay = st.layout[OSD_ELEM_VOLTAGE].y;
			float aw = st.layout[OSD_ELEM_VOLTAGE].w, ah = st.layout[OSD_ELEM_VOLTAGE].h;
			float bx = st.layout[OSD_ELEM_RSSI].x, by = st.layout[OSD_ELEM_RSSI].y;
			float bw = st.layout[OSD_ELEM_RSSI].w, bh = st.layout[OSD_ELEM_RSSI].h;
			check("clamped panel: stays inside the viewport",
				bx >= 0.0f && bx + bw <= (float)SURF_W);
			check("clamped panel: does not cover the one beside it",
				!(ax < bx + bw && bx < ax + aw && ay < by + bh && by < ay + ah));
		}
	}

	// --- lat/lon that span no usable rectangle must fall back to value panels
	// rather than vanishing behind a map that never gets drawn.
	{
		struct {
			const char *name;
			int lat_row, lat_col, lon_row, lon_col;
			bool expect_map;
		} cases[] = {
			{"same row", 6, 1, 6, 20, false},
			{"stacked in one column", 6, 1, 8, 1, false},
			// Overlapping columns are how a *narrow* map is asked for: the two
			// readouts are 10 cells wide, so requiring them fully side by side
			// would put a 20-cell floor under the map's width. Only an exact
			// column match reads as a list.
			{"columns overlapping, offset", 6, 1, 9, 3, true},
			{"diagonal, far apart", 4, 1, 12, 20, true},
		};
		for (size_t c = 0; c < sizeof(cases) / sizeof(cases[0]); c++) {
			osd_widget_state_t st;
			osd_widgets_state_init(&st);
			clear_grid();
			grid[cases[c].lat_row][cases[c].lat_col] = SYM_LAT;
			put_str(cases[c].lat_row, cases[c].lat_col + 1, "52.47890");
			grid[cases[c].lon_row][cases[c].lon_col] = SYM_LON;
			put_str(cases[c].lon_row, cases[c].lon_col + 1, "13.65127");

			osd_element_t els[32];
			int n = osd_elements_scan(getter, NULL, COLS, ROWS, "INAV", els, 32);
			memset(buf, 0, (size_t)SURF_W * SURF_H * 4);
			osd_surface_t s;
			osd_surface_init(&s, buf, SURF_W, SURF_H, SURF_W * 4);
			osd_grid_t g = {CELL_W, CELL_H, 8, 0, NULL, NULL};
			osd_theme_t map_th = th;
			map_th.map_enabled = true;
			// The defaults reserve lat/lon for the map; the shipped theme turns
			// them on, and the fallback is only reachable when they are.
			map_th.elem_enabled[OSD_ELEM_LATITUDE] = true;
			map_th.elem_enabled[OSD_ELEM_LONGITUDE] = true;
			osd_widgets_draw_all(&s, &map_th, font, &st, els, n, &g, 1000);

			const bool panels = st.layout[OSD_ELEM_LATITUDE].valid &&
								st.layout[OSD_ELEM_LONGITUDE].valid;
			char label[96];
			snprintf(label, sizeof(label), "%s: %s", cases[c].name,
				cases[c].expect_map ? "draws a map" : "draws value panels");
			check(label, cases[c].expect_map ? !panels : panels);
		}
	}

	// --- a half-drawn reading must not evict a good cached one. A flight
	// controller redraws a cell at a time, so a scan catches a field mid-write
	// perhaps 5% of the time; if that flushes the cache the widget blinks out
	// for a frame, and for lat/lon the map goes with it.
	{
		osd_widget_state_t st;
		osd_widgets_state_init(&st);
		osd_theme_t map_th = th;
		map_th.map_enabled = true;
		map_th.elem_enabled[OSD_ELEM_LATITUDE] = true;
		map_th.elem_enabled[OSD_ELEM_LONGITUDE] = true;
		osd_grid_t g = {CELL_W, CELL_H, 8, 0, NULL, NULL};
		osd_surface_t s;

		// A good frame: both coordinates parse.
		clear_grid();
		grid[6][1] = SYM_LAT;
		put_str(6, 2, "52.47890");
		grid[14][4] = SYM_LON;
		put_str(14, 5, "13.65127");
		osd_element_t els[32];
		int n = osd_elements_scan(getter, NULL, COLS, ROWS, "INAV", els, 32);
		memset(buf, 0, (size_t)SURF_W * SURF_H * 4);
		osd_surface_init(&s, buf, SURF_W, SURF_H, SURF_W * 4);
		osd_widgets_draw_all(&s, &map_th, font, &st, els, n, &g, 1000);
		const bool map_first = !st.layout[OSD_ELEM_LATITUDE].valid;
		check("half-drawn: a good frame draws the map", map_first);

		// The next frame catches latitude mid-redraw: the symbol is there and a
		// stray character follows, so it is recognised but parses to nothing.
		clear_grid();
		grid[6][1] = SYM_LAT;
		put_str(6, 2, "-");
		grid[14][4] = SYM_LON;
		put_str(14, 5, "13.65127");
		n = osd_elements_scan(getter, NULL, COLS, ROWS, "INAV", els, 32);
		memset(buf, 0, (size_t)SURF_W * SURF_H * 4);
		osd_surface_init(&s, buf, SURF_W, SURF_H, SURF_W * 4);
		osd_widgets_draw_all(&s, &map_th, font, &st, els, n, &g, 1100);
		check("half-drawn: the map survives the bad frame",
			!st.layout[OSD_ELEM_LATITUDE].valid);
		check("half-drawn: the good reading is kept",
			st.last[OSD_ELEM_LATITUDE].value > 52.47f && st.last[OSD_ELEM_LATITUDE].value < 52.48f);
	}

	printf("\n%s (%d failures)\n", fails ? "FAILURES" : "ALL PASS", fails);
	return fails ? 1 : 0;
}

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
// The compass bar: a run out of the heading-arrow block, which is what makes it
// recognisable without knowing anything about the field's format.
#define SYM_HEADING_FIRST 0xC8

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

// The layout lives in position-keyed slots, so a test asks for a placement
// rather than indexing by type.
#define PLACE_x(t) (place_of(&st, t).x)
#define PLACE_y(t) (place_of(&st, t).y)
#define PLACE_w(t) (place_of(&st, t).w)
#define PLACE_h(t) (place_of(&st, t).h)

typedef struct {
	float x, y, w, h;
} place4_t;

static place4_t place_of(const osd_widget_state_t *st, osd_element_type_t type) {
	place4_t p = {0, 0, 0, 0};
	osd_widgets_placement(st, type, &p.x, &p.y, &p.w, &p.h);
	return p;
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

	return osd_widgets_placement(st, type, &out->x, NULL, &out->w, NULL);
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
		// The label has to be out of the way for the value to drive the width at
		// all: a panel is sized to fit both rows, and "CAPACITY USED" is wider
		// than any mAh reading. Without this the width is label-driven and
		// constant, and the check below would be asserting nothing.
		narrow.label_size = 4.0f;
		narrow.label_tracking = 0.0f;
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
		check("message: actually drawn", drawn > 0 && osd_widgets_placement(&st, OSD_ELEM_WARNING, NULL, NULL, NULL, NULL));
		check("message: banner is wider than it is tall",
			PLACE_w(OSD_ELEM_WARNING) > PLACE_h(OSD_ELEM_WARNING));
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

		bool have = osd_widgets_placement(&st, OSD_ELEM_VOLTAGE, NULL, NULL, NULL, NULL) && osd_widgets_placement(&st, OSD_ELEM_RSSI, NULL, NULL, NULL, NULL);
		check("clamped panel: both panels placed", have);
		if (have) {
			float ax = PLACE_x(OSD_ELEM_VOLTAGE), ay = PLACE_y(OSD_ELEM_VOLTAGE);
			float aw = PLACE_w(OSD_ELEM_VOLTAGE), ah = PLACE_h(OSD_ELEM_VOLTAGE);
			float bx = PLACE_x(OSD_ELEM_RSSI), by = PLACE_y(OSD_ELEM_RSSI);
			float bw = PLACE_w(OSD_ELEM_RSSI), bh = PLACE_h(OSD_ELEM_RSSI);
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

			const bool panels = osd_widgets_placement(&st, OSD_ELEM_LATITUDE, NULL, NULL, NULL, NULL) &&
								osd_widgets_placement(&st, OSD_ELEM_LONGITUDE, NULL, NULL, NULL, NULL);
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
		const bool map_first = !osd_widgets_placement(&st, OSD_ELEM_LATITUDE, NULL, NULL, NULL, NULL);
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
			!osd_widgets_placement(&st, OSD_ELEM_LATITUDE, NULL, NULL, NULL, NULL));
		check("half-drawn: the good reading is kept",
			osd_widgets_cached(&st, OSD_ELEM_LATITUDE)->value > 52.47f &&
				osd_widgets_cached(&st, OSD_ELEM_LATITUDE)->value < 52.48f);
	}

	// --- the compass bar. It carries no value, only a position, so the two
	// filters that drop valueless elements have to let it through - and it must
	// be drawn as the compass rather than as a panel with nothing in it.
	{
		osd_widget_state_t st;
		osd_widgets_state_init(&st);
		osd_grid_t g = {CELL_W, CELL_H, 8, 0, NULL, NULL};
		osd_surface_t s;

		clear_grid();
		// A nine-cell bar centred at the top, as both firmwares draw it.
		for (int i = 0; i < 9; i++)
			grid[1][22 + i] = (uint16_t)(SYM_HEADING_FIRST + (i % 6));
		// Something below it that a wide compass would otherwise sit on.
		put_trailing_at(2, 26, "1250", SYM_MAH);

		osd_element_t els[32];
		int n = osd_elements_scan(getter, NULL, COLS, ROWS, "INAV", els, 32);
		bool found = false;
		for (int i = 0; i < n; i++)
			if (els[i].type == OSD_ELEM_HEADING_BAR)
				found = true;
		check("compass: the bar is recognised", found);

		osd_theme_t hd = th;
		hd.heading_style = 0; // band
		hd.heading_size = 480.0f;
		st.heading_deg = 274.0f;
		st.course_deg = 289.0f;
		st.ground_speed_mps = 18.0f;
		memset(buf, 0, (size_t)SURF_W * SURF_H * 4);
		osd_surface_init(&s, buf, SURF_W, SURF_H, SURF_W * 4);
		osd_widgets_draw_all(&s, &hd, font, &st, els, n, &g, 1000);

		// Drawn, but not as a panel: no layout slot is claimed for it.
		check("compass: not laid out as a panel", !osd_widgets_placement(&st, OSD_ELEM_HEADING_BAR, NULL, NULL, NULL, NULL));

		// Pixels landed somewhere across the bar's own row band.
		int lit = 0;
		for (int y = 0; y < 200; y++)
			for (int x = 0; x < SURF_W; x++)
				if (buf[((size_t)y * SURF_W + x) * 4 + 3] != 0)
					lit++;
		check("compass: something was actually drawn", lit > 2000);

		// And the panel beneath it was pushed clear rather than buried.
		check("compass: the panel below it is placed", osd_widgets_placement(&st, OSD_ELEM_MAH, NULL, NULL, NULL, NULL));
		{
			float bx, by, bw, bh;
			osd_widgets_heading_box(&hd, (float)(8 + 22 * CELL_W) + (float)(9 * CELL_W) * 0.5f,
				(float)CELL_H + (float)CELL_H * 0.5f, &bx, &by, &bw, &bh);
			const float px = PLACE_x(OSD_ELEM_MAH), py = PLACE_y(OSD_ELEM_MAH);
			const float pw = PLACE_w(OSD_ELEM_MAH), ph = PLACE_h(OSD_ELEM_MAH);
			const bool clear = px + pw <= bx || bx + bw <= px || py + ph <= by || by + bh <= py;
			check("compass: the panel below is pushed clear of it", clear);
		}
	}

	// --- two of the same kind on screen at once.
	//
	// Betaflight draws core and ESC temperature with the same symbol, so a pilot
	// who places both gets two temperature elements. Keyed on type alone they
	// shared one cache slot and one layout: each frame overwrote the other, so
	// both flickered and neither held still.
	{
		osd_widget_state_t st;
		osd_widgets_state_init(&st);
		osd_grid_t g = {CELL_W, CELL_H, 8, 0, NULL, NULL};
		osd_surface_t s;

		clear_grid();
		put_str(4, 2, "C");
		grid[4][3] = 0x7A;
		put_str(4, 4, " 42");
		grid[4][7] = 0x0E;
		put_str(10, 2, "E");
		grid[10][3] = 0x7A;
		put_str(10, 4, " 68");
		grid[10][7] = 0x0E;

		osd_element_t els[32];
		int n = osd_elements_scan(getter, NULL, COLS, ROWS, "BTFL", els, 32);
		int temps = 0;
		for (int i = 0; i < n; i++)
			if (els[i].type == OSD_ELEM_TEMPERATURE)
				temps++;
		check("duplicates: both temperatures recognised", temps == 2);

		memset(buf, 0, (size_t)SURF_W * SURF_H * 4);
		osd_surface_init(&s, buf, SURF_W, SURF_H, SURF_W * 4);
		const int drawn = osd_widgets_draw_all(&s, &th, font, &st, els, n, &g, 1000);
		check("duplicates: both drawn", drawn == 2);

		// Two live slots, not one overwriting the other.
		int used = 0;
		for (int i = 0; i < OSD_WIDGET_SLOTS; i++)
			if (st.slots[i].used && st.slots[i].el.type == OSD_ELEM_TEMPERATURE)
				used++;
		check("duplicates: a slot each", used == 2);

		// And they keep their own positions rather than converging.
		float ys[2];
		int k = 0;
		for (int i = 0; i < OSD_WIDGET_SLOTS && k < 2; i++)
			if (st.slots[i].used && st.slots[i].el.type == OSD_ELEM_TEMPERATURE &&
				st.slots[i].layout_valid)
				ys[k++] = st.slots[i].y;
		check("duplicates: placed apart", k == 2 && ys[0] != ys[1]);

		// Values do not cross over: each slot keeps the reading from its own row.
		bool got42 = false, got68 = false;
		for (int i = 0; i < OSD_WIDGET_SLOTS; i++) {
			if (!st.slots[i].used || st.slots[i].el.type != OSD_ELEM_TEMPERATURE)
				continue;
			if (st.slots[i].el.row == 4 && st.slots[i].el.value > 41.9f &&
				st.slots[i].el.value < 42.1f)
				got42 = true;
			if (st.slots[i].el.row == 10 && st.slots[i].el.value > 67.9f &&
				st.slots[i].el.value < 68.1f)
				got68 = true;
		}
		check("duplicates: each keeps its own reading", got42 && got68);

		// A second frame must reuse the same two slots, not add more.
		osd_widgets_draw_all(&s, &th, font, &st, els, n, &g, 1100);
		used = 0;
		for (int i = 0; i < OSD_WIDGET_SLOTS; i++)
			if (st.slots[i].used && st.slots[i].el.type == OSD_ELEM_TEMPERATURE)
				used++;
		check("duplicates: slots are reused, not grown", used == 2);
	}

	// --- link statistics: not a flight controller element at all.
	//
	// The numbers come from a file the ground station writes, and the widget is
	// placed from the theme because there is nothing on the glyph grid to anchor
	// to - wfb-ng and APFPV live on this side of the link.
	{
		char path[] = "/tmp/_msposd_link_test.ini";
		FILE *f = fopen(path, "w");
		fprintf(f, "; written by the ground station\n"
				   "source = WFB-NG\n"
				   "channel = 149\n"
				   "freq_mhz = 5745\n"
				   "bandwidth_mhz = 20\n"
				   "quality = 97\n"
				   "loss = 0.4\n"
				   "bitrate_mbps = 12.4\n"
				   "ant0_rssi = -58   ; first antenna\n"
				   "ant0_snr = 18\n"
				   "ant1_rssi = -71\n"
				   "ant1_snr = 11\n");
		fclose(f);

		osd_link_stats_t ls;
		memset(&ls, 0, sizeof(ls));
		check("link: stats file read", osd_link_stats_load(path, &ls, 1000));
		check("link: source read", strcmp(ls.source, "WFB-NG") == 0);
		check("link: two antennas", ls.antennas == 2);
		check("link: rssi read past its comment", ls.rssi_valid[0] && ls.rssi_dbm[0] == -58);
		check("link: snr read", ls.snr_valid[1] && ls.snr_db[1] == 11);
		check("link: quality read", ls.quality_pct > 96.9f && ls.quality_pct < 97.1f);
		check("link: tuning read", ls.channel == 149 && ls.freq_mhz == 5745 &&
									   ls.bandwidth_mhz == 20);

		// A reader polling ten times a second will catch a write in progress. That
		// must keep the last good numbers rather than blanking the widget.
		osd_link_stats_t before = ls;
		f = fopen(path, "w");
		fclose(f); // truncated to nothing, mid-rewrite
		check("link: an empty file is refused", !osd_link_stats_load(path, &ls, 1100));
		check("link: the last good stats survive it",
			ls.rssi_dbm[0] == before.rssi_dbm[0] && ls.antennas == before.antennas);

		// Missing entirely - a ground station that does not write the file.
		remove(path);
		check("link: a missing file is refused", !osd_link_stats_load(path, &ls, 1200));

		// Stale once the writer stops.
		check("link: fresh stats are not stale", !osd_link_stats_stale(&before, 1200, 3000.0f));
		check("link: old stats go stale", osd_link_stats_stale(&before, 9000, 3000.0f));

		// Both styles have to measure to something that fits on a 1080p screen
		// with every antenna slot filled.
		osd_link_params_t lp = {0};
		lp.scale = 1.0f;
		lp.pad_x = 13;
		lp.pad_y = 11;
		lp.chamfer = 14;
		lp.bar_height = 16;
		lp.value_size = 25;
		lp.label_size = 11;
		lp.label_tracking = 2.5f;
		osd_link_stats_t full = before;
		full.antennas = OSD_LINK_MAX_ANTENNAS;
		float lw, lh;
		lp.style = OSD_LINK_VERTICAL;
		osd_link_measure(&lp, &full, font, &lw, &lh);
		check("link: vertical fits at six antennas", lw < 1920.0f && lh < 1080.0f);
		lp.style = OSD_LINK_HORIZONTAL;
		osd_link_measure(&lp, &full, font, &lw, &lh);
		check("link: horizontal fits at six antennas", lw < 1920.0f && lh < 1080.0f);

		// Turning the aerials off has to actually shrink the panel, in both
		// directions for the horizontal style - it sizes its width to the
		// number of columns, and with no columns there is nothing to size to.
		float on_w, on_h, off_w, off_h;
		lp.show_antennas = true;
		osd_link_measure(&lp, &full, font, &on_w, &on_h);
		lp.show_antennas = false;
		osd_link_measure(&lp, &full, font, &off_w, &off_h);
		check("link: aerials off is shorter", off_h < on_h);
		check("link: aerials off is narrower", off_w < on_w);

		lp.style = OSD_LINK_VERTICAL;
		lp.show_antennas = true;
		osd_link_measure(&lp, &full, font, &on_w, &on_h);
		lp.show_antennas = false;
		osd_link_measure(&lp, &full, font, &off_w, &off_h);
		check("link: vertical loses its aerial rows", off_h < on_h);
		// Six rows gone, so it should be a lot shorter rather than a little.
		check("link: vertical drops six rows' worth", on_h - off_h > 100.0f);

		// Ultrawide is the widest and shallowest of the three, and it carries no
		// footer at all: the loss and the throughput share its header line.
		{
			osd_link_params_t up = lp;
			up.show_antennas = true;
			up.style = OSD_LINK_HORIZONTAL;
			float hw, hh2;
			osd_link_measure(&up, &full, font, &hw, &hh2);
			up.style = OSD_LINK_ULTRAWIDE;
			float uw, uh;
			osd_link_measure(&up, &full, font, &uw, &uh);
			// Not wider than horizontal: with the aerials shown both are driven
			// by the same columns, and the point of this style is that it takes
			// no more room than its contents need.
			check("link: ultrawide is no wider than horizontal", uw <= hw + 1.0f);
			check("link: ultrawide is shallower", uh < hh2);

			// Losing the footer must not depend on there being nothing to put
			// in it - full stats, and it is still shorter.
			osd_link_stats_t rich = full;
			rich.loss_pct = 0.4f;
			rich.bitrate_mbps = 12.4f;
			up.style = OSD_LINK_HORIZONTAL;
			osd_link_measure(&up, &rich, font, &hw, &hh2);
			up.style = OSD_LINK_ULTRAWIDE;
			osd_link_measure(&up, &rich, font, &uw, &uh);
			check("link: ultrawide sheds the footer row", uh < hh2);

			// And with no aerials it sizes to its header line, so a station
			// reporting little takes a shorter strip than one reporting a lot.
			up.show_antennas = false;
			osd_link_stats_t bare = rich;
			bare.freq_mhz = 0;
			bare.bandwidth_mhz = 0;
			bare.loss_pct = -1.0f;
			bare.bitrate_mbps = -1.0f;
			float rich_w, bare_w, ignored;
			osd_link_measure(&up, &rich, font, &rich_w, &ignored);
			osd_link_measure(&up, &bare, font, &bare_w, &ignored);
			check("link: a fuller header makes a wider strip", rich_w > bare_w);
			// The floor still applies, or the quality bar stops reading as one.
			check("link: but never narrower than the floor", bare_w > 300.0f);
		}

		// Zero is a reading, not an absence: a ground station saying the link is
		// down has to get the row, drawn empty and red, rather than no row.
		{
			osd_link_params_t zp = lp;
			zp.show_antennas = false;
			osd_link_stats_t down = full;
			down.quality_pct = 0.0f;
			osd_link_stats_t none = full;
			none.quality_pct = -1.0f;
			float zw, zh, nw, nh;
			osd_link_measure(&zp, &down, font, &zw, &zh);
			osd_link_measure(&zp, &none, font, &nw, &nh);
			check("link: a zero quality still gets its row", zh > nh);
		}

		// The quality row exists only when quality is reported: a ground station
		// that does not count packets must not get an empty bar.
		lp.show_antennas = false;
		osd_link_stats_t noq = full;
		noq.quality_pct = -1.0f;
		float noq_w, noq_h;
		osd_link_measure(&lp, &noq, font, &noq_w, &noq_h);
		check("link: no quality, no quality row", noq_h < off_h);
	}

	// The widget is placed from the theme, clamped into the viewport, and panels
	// are pushed clear of it just as they are of the map.
	{
		char path[] = "/tmp/_msposd_link_place.ini";
		FILE *f = fopen(path, "w");
		fprintf(f, "source = WFB-NG\nant0_rssi = -58\nant0_snr = 18\n");
		fclose(f);

		osd_widget_state_t st;
		osd_widgets_state_init(&st);
		osd_theme_t lt = th;
		lt.link_enabled = true;
		lt.link_style = 0;
		lt.link_scale = 1.0f;
		lt.link_opacity = 1.0f;
		snprintf(lt.link_source, sizeof(lt.link_source), "%s", path);
		// Hard against the right edge, which must be pulled back rather than drawn
		// off the side of the screen.
		lt.link_x = 100.0f;
		lt.link_y = 0.0f;

		clear_grid();
		put_trailing_at(1, 50, "1250", SYM_MAH);
		osd_element_t els[32];
		int n = osd_elements_scan(getter, NULL, COLS, ROWS, "INAV", els, 32);
		osd_grid_t g = {CELL_W, CELL_H, 8, 0, NULL, NULL};
		osd_surface_t s;
		memset(buf, 0, (size_t)SURF_W * SURF_H * 4);
		osd_surface_init(&s, buf, SURF_W, SURF_H, SURF_W * 4);
		osd_widgets_draw_all(&s, &lt, font, &st, els, n, &g, 1000);

		check("link: the stats were picked up", st.link.valid);

		// Something was painted in the top-right corner.
		int lit = 0;
		for (int yy = 0; yy < 220; yy++)
			for (int xx = SURF_W - 300; xx < SURF_W; xx++)
				if (buf[((size_t)yy * SURF_W + xx) * 4 + 3] != 0)
					lit++;
		check("link: drawn inside the viewport", lit > 3000);

		// And the panel that shares that corner was pushed clear.
		float pxx, pyy, pww, phh;
		const bool placed = osd_widgets_placement(&st, OSD_ELEM_MAH, &pxx, &pyy, &pww, &phh);
		check("link: the panel beside it is placed", placed);
		osd_link_params_t lp = {0};
		lp.style = OSD_LINK_VERTICAL;
		lp.scale = 1.0f;
		float lw, lh;
		osd_link_measure(&lp, &st.link, font, &lw, &lh);
		const float lx = (float)SURF_W - lw;
		const bool clear_of_it =
			pxx + pww <= lx || lx + lw <= pxx || pyy + phh <= 0.0f || lh <= pyy;
		check("link: the panel is pushed clear of it", placed && clear_of_it);
		remove(path);
	}

	printf("\n%s (%d failures)\n", fails ? "FAILURES" : "ALL PASS", fails);
	return fails ? 1 : 0;
}

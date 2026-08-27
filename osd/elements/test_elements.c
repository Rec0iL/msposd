#include "osd_elements.h"
#include <stdio.h>
#include <string.h>

#define COLS 53
#define ROWS 20
static uint16_t grid[ROWS][COLS];

static uint16_t getter(int col, int row, void *ctx) {
    (void)ctx;
    if (col < 0 || col >= COLS || row < 0 || row >= ROWS) return 0;
    return grid[row][col];
}
static void put(int row, int col, const uint16_t *g, int n) {
    for (int i = 0; i < n; i++) grid[row][col + i] = g[i];
}
static int fails = 0;
static void check(const char *name, int cond) {
    printf("  %-46s %s\n", name, cond ? "PASS" : "FAIL");
    if (!cond) fails++;
}

int main(void) {
    osd_element_t els[32];

    // --- INAV, plain ASCII digits: "4.14" + SYM_VOLT
    memset(grid, 0x20, sizeof(grid));
    { uint16_t g[] = {'4','.','1','4',0x1F}; put(2, 10, g, 5); }
    int n = osd_elements_scan(getter, NULL, COLS, ROWS, "INAV", els, 32);
    check("INAV ascii voltage: exactly one element", n == 1);
    check("INAV ascii voltage: type", n && els[0].type == OSD_ELEM_VOLTAGE);
    check("INAV ascii voltage: value 4.14", n && els[0].value > 4.139 && els[0].value < 4.141);
    check("INAV ascii voltage: flagged per-cell", n && els[0].is_per_cell);
    check("INAV ascii voltage: col == 10", n && els[0].col == 10);
    check("INAV ascii voltage: row == 2", n && els[0].row == 2);

    // --- INAV packed decimal glyph: 0xA1+4 is "4." then "14"
    memset(grid, 0x20, sizeof(grid));
    { uint16_t g[] = {(uint16_t)(0xA1+4),'1','4',0x1F}; put(5, 3, g, 4); }
    n = osd_elements_scan(getter, NULL, COLS, ROWS, "INAV", els, 32);
    check("INAV packed-dot voltage: value 4.14", n == 1 && els[0].value > 4.139 && els[0].value < 4.141);

    // --- real INAV encoding: split half-dot pair must collapse to one point
    memset(grid, 0x20, sizeof(grid));
    { uint16_t g[] = {'1',(uint16_t)(0xA1+6),(uint16_t)(0xB1+7),0x1F}; put(0, 13, g, 4); }
    n = osd_elements_scan(getter, NULL, COLS, ROWS, "INAV", els, 32);
    check("INAV half-dot pair '16.7' parses as 16.7", n == 1 && els[0].value > 16.69 && els[0].value < 16.71);
    check("INAV half-dot pair text is '16.7'", n == 1 && strcmp(els[0].text, "16.7") == 0);

    memset(grid, 0x20, sizeof(grid));
    { uint16_t g[] = {(uint16_t)(0xA1+0),(uint16_t)(0xB1+0),'0',0x6A}; put(3, 2, g, 4); }
    n = osd_elements_scan(getter, NULL, COLS, ROWS, "INAV", els, 32);
    check("INAV half-dot current '0.00' parses as 0.0", n == 1 && els[0].value >= 0.0 && els[0].value < 0.001 && strcmp(els[0].text, "0.00") == 0);

    // --- pack voltage must NOT be flagged per-cell
    memset(grid, 0x20, sizeof(grid));
    { uint16_t g[] = {'1','6','.','2',0x1F}; put(1, 1, g, 5); }
    n = osd_elements_scan(getter, NULL, COLS, ROWS, "INAV", els, 32);
    check("INAV pack voltage 16.2: not per-cell", n == 1 && !els[0].is_per_cell);

    // --- current, trailing SYM_AMP
    memset(grid, 0x20, sizeof(grid));
    { uint16_t g[] = {'3','4','.','1',0x6A}; put(7, 20, g, 5); }
    n = osd_elements_scan(getter, NULL, COLS, ROWS, "INAV", els, 32);
    check("INAV current 34.1A", n == 1 && els[0].type == OSD_ELEM_CURRENT && els[0].value > 34.09 && els[0].value < 34.11);

    // --- latitude, LEADING symbol
    memset(grid, 0x20, sizeof(grid));
    { uint16_t g[] = {0x03,'5','2','.','4','7','9','6'}; put(18, 0, g, 8); }
    n = osd_elements_scan(getter, NULL, COLS, ROWS, "INAV", els, 32);
    check("INAV latitude: type", n == 1 && els[0].type == OSD_ELEM_LATITUDE);
    check("INAV latitude: value 52.4796", n == 1 && els[0].value > 52.479 && els[0].value < 52.480);
    check("INAV latitude: col is symbol cell 0", n == 1 && els[0].col == 0);

    // --- negative longitude
    memset(grid, 0x20, sizeof(grid));
    { uint16_t g[] = {0x04,'-','1','3','.','6'}; put(19, 30, g, 6); }
    n = osd_elements_scan(getter, NULL, COLS, ROWS, "INAV", els, 32);
    check("INAV longitude -13.6", n == 1 && els[0].value < -13.59 && els[0].value > -13.61);

    // --- Betaflight uses different codes: 0x06 volt, 0x9A amp
    memset(grid, 0x20, sizeof(grid));
    { uint16_t g[] = {'3','.','8','5',0x06}; put(4, 8, g, 5); }
    n = osd_elements_scan(getter, NULL, COLS, ROWS, "BTFL", els, 32);
    check("BTFL voltage 3.85 via 0x06", n == 1 && els[0].type == OSD_ELEM_VOLTAGE && els[0].value > 3.84 && els[0].value < 3.86);

    // INAV's volt code must NOT match under BTFL
    memset(grid, 0x20, sizeof(grid));
    { uint16_t g[] = {'4','.','1',0x1F}; put(4, 8, g, 4); }
    n = osd_elements_scan(getter, NULL, COLS, ROWS, "BTFL", els, 32);
    check("BTFL ignores INAV volt code 0x1F", n == 0);

    // --- bare symbol with no digits is not an element
    memset(grid, 0x20, sizeof(grid));
    { uint16_t g[] = {0x1F}; put(9, 9, g, 1); }
    n = osd_elements_scan(getter, NULL, COLS, ROWS, "INAV", els, 32);
    check("bare symbol, no number: ignored", n == 0);

    // --- several elements on one row
    memset(grid, 0x20, sizeof(grid));
    { uint16_t a[] = {'4','.','1',0x1F}; put(3, 0, a, 4); }
    { uint16_t b[] = {'1','2','.','5',0x6A}; put(3, 20, b, 5); }
    n = osd_elements_scan(getter, NULL, COLS, ROWS, "INAV", els, 32);
    check("two elements on one row", n == 2 && els[0].type == OSD_ELEM_VOLTAGE && els[1].type == OSD_ELEM_CURRENT);

    printf("\n%s (%d failure%s)\n", fails ? "FAILURES" : "ALL PASS", fails, fails == 1 ? "" : "s");
    return fails != 0;
}

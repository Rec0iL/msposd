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

    // --- battery icon before a voltage marks it as battery voltage and is absorbed
    memset(grid, 0x20, sizeof(grid));
    { uint16_t g[] = {0x63,'1','6','.','7',0x1F}; put(0, 12, g, 6); }   // SYM_BATT_FULL
    n = osd_elements_scan(getter, NULL, COLS, ROWS, "INAV", els, 32);
    check("battery icon detected", n == 1 && els[0].has_battery_icon);
    check("battery level 0 = full", n == 1 && els[0].battery_level == 0);
    check("battery icon absorbed into col", n == 1 && els[0].col == 12);
    check("battery voltage value 16.7", n == 1 && els[0].value > 16.69 && els[0].value < 16.71);

    memset(grid, 0x20, sizeof(grid));
    { uint16_t g[] = {0x69,'1','4','.','0',0x1F}; put(0, 12, g, 6); }   // SYM_BATT_EMPTY
    n = osd_elements_scan(getter, NULL, COLS, ROWS, "INAV", els, 32);
    check("battery level 6 = empty", n == 1 && els[0].battery_level == 6);

    // a voltage with no icon must not claim one
    memset(grid, 0x20, sizeof(grid));
    { uint16_t g[] = {'4','.','1','4',0x1F}; put(2, 10, g, 5); }
    n = osd_elements_scan(getter, NULL, COLS, ROWS, "INAV", els, 32);
    check("no battery icon when absent", n == 1 && !els[0].has_battery_icon);

    // --- satellites: two-cell icon, both halves absorbed
    memset(grid, 0x20, sizeof(grid));
    { uint16_t g[] = {0x08,0x09,'1','7'}; put(18, 4, g, 4); }
    n = osd_elements_scan(getter, NULL, COLS, ROWS, "INAV", els, 32);
    check("sats detected", n == 1 && els[0].type == OSD_ELEM_SATS);
    check("sats value 17", n == 1 && els[0].value > 16.9 && els[0].value < 17.1);
    check("both icon halves absorbed", n == 1 && els[0].col == 4 && els[0].width == 4);

    // --- throttle
    memset(grid, 0x20, sizeof(grid));
    { uint16_t g[] = {0x95,'4','5'}; put(6, 2, g, 3); }
    n = osd_elements_scan(getter, NULL, COLS, ROWS, "INAV", els, 32);
    check("throttle detected 45", n == 1 && els[0].type == OSD_ELEM_THROTTLE
          && els[0].value > 44.9 && els[0].value < 45.1);

    // --- flight time keeps the colon and is text, not a number
    memset(grid, 0x20, sizeof(grid));
    { uint16_t g[] = {0x9F,'0','5',':','3','6'}; put(7, 2, g, 6); }
    n = osd_elements_scan(getter, NULL, COLS, ROWS, "INAV", els, 32);
    check("flight time detected", n == 1 && els[0].type == OSD_ELEM_FLIGHT_TIME);
    check("flight time text '05:36'", n == 1 && strcmp(els[0].text, "05:36") == 0);
    check("flight time is not numeric", n == 1 && !els[0].value_valid);

    // --- flight mode word, no symbol involved
    memset(grid, 0x20, sizeof(grid));
    { uint16_t g[] = {'A','C','R','O'}; put(14, 8, g, 4); }
    n = osd_elements_scan(getter, NULL, COLS, ROWS, "INAV", els, 32);
    check("flight mode ACRO detected", n == 1 && els[0].type == OSD_ELEM_FLIGHT_MODE
          && strcmp(els[0].text, "ACRO") == 0);
    check("flight mode position", n == 1 && els[0].col == 8 && els[0].width == 4);

    memset(grid, 0x20, sizeof(grid));
    { uint16_t g[] = {'B','A','N','A','N','A'}; put(14, 8, g, 6); }
    n = osd_elements_scan(getter, NULL, COLS, ROWS, "INAV", els, 32);
    check("unknown word is not a flight mode", n == 0);

    // BTFL uses different sat/throttle codes
    memset(grid, 0x20, sizeof(grid));
    { uint16_t g[] = {0x1E,0x1F,'0','9'}; put(3, 3, g, 4); }
    n = osd_elements_scan(getter, NULL, COLS, ROWS, "BTFL", els, 32);
    check("BTFL sats via 0x1E/0x1F", n == 1 && els[0].type == OSD_ELEM_SATS && els[0].value > 8.9);

    // --- right-aligned fields put blanks between symbol and value.
    // These are the low/blinking readings, so losing them is worst-case.
    memset(grid, 0x20, sizeof(grid));
    { uint16_t g[] = {0x01,0x20,'0'}; put(0, 23, g, 3); }        // RSSI "<sym> 0"
    n = osd_elements_scan(getter, NULL, COLS, ROWS, "INAV", els, 32);
    check("RSSI 0 with one leading blank", n == 1 && els[0].type == OSD_ELEM_RSSI
          && els[0].value >= 0.0f && els[0].value < 0.01f);
    check("RSSI element starts at its symbol", n == 1 && els[0].col == 23);

    memset(grid, 0x20, sizeof(grid));
    { uint16_t g[] = {0x95,0x20,'5','3'}; put(2, 2, g, 4); }     // THR "<sym> 53"
    n = osd_elements_scan(getter, NULL, COLS, ROWS, "INAV", els, 32);
    check("throttle with leading blank", n == 1 && els[0].type == OSD_ELEM_THROTTLE
          && els[0].value > 52.9 && els[0].value < 53.1);
    check("throttle spans symbol to value", n == 1 && els[0].col == 2 && els[0].width == 4);

    memset(grid, 0x20, sizeof(grid));
    { uint16_t g[] = {0x08,0x09,0x20,'8'}; put(11, 0, g, 4); }   // sats "<icon> 8"
    n = osd_elements_scan(getter, NULL, COLS, ROWS, "INAV", els, 32);
    check("single-digit sats with blank", n == 1 && els[0].type == OSD_ELEM_SATS
          && els[0].value > 7.9 && els[0].value < 8.1);

    // throttle is a fixed 4-cell field: symbol + 3 digit slots, right aligned.
    // 100 fills it, 7 leaves two blanks.
    memset(grid, 0x20, sizeof(grid));
    { uint16_t g[] = {0x95,'1','0','0'}; put(2, 2, g, 4); }
    n = osd_elements_scan(getter, NULL, COLS, ROWS, "INAV", els, 32);
    check("throttle 100 fills the field", n == 1 && els[0].value > 99.9 && els[0].value < 100.1);

    memset(grid, 0x20, sizeof(grid));
    { uint16_t g[] = {0x95,0x20,0x20,'7'}; put(2, 2, g, 4); }
    n = osd_elements_scan(getter, NULL, COLS, ROWS, "INAV", els, 32);
    check("throttle 7 with two leading blanks", n == 1 && els[0].value > 6.9 && els[0].value < 7.1);
    check("throttle 7 still spans the whole field", n == 1 && els[0].col == 2 && els[0].width == 4);

    // a symbol with only blanks after it is still not an element
    memset(grid, 0x20, sizeof(grid));
    { uint16_t g[] = {0x95}; put(4, 4, g, 1); }
    n = osd_elements_scan(getter, NULL, COLS, ROWS, "INAV", els, 32);
    check("symbol with no value anywhere: ignored", n == 0);

    // --- OSD messages: severity must come from the message, not the theme
    #define PUTSTR(r,c,str) do{ const char*_s=(str); for(int _i=0;_s[_i];_i++) grid[r][(c)+_i]=(uint16_t)_s[_i]; }while(0)

    memset(grid, 0x20, sizeof(grid));
    PUTSTR(15, 10, "FAILSAFE MODE ENABLED");
    n = osd_elements_scan(getter, NULL, COLS, ROWS, "INAV", els, 32);
    check("failsafe recognised", n == 1 && els[0].type == OSD_ELEM_WARNING);
    check("failsafe is critical", n == 1 && els[0].severity == OSD_SEV_CRIT);
    check("message text preserved whole", n == 1 && strcmp(els[0].text, "FAILSAFE MODE ENABLED") == 0);
    check("message spans its text", n == 1 && els[0].col == 10 && els[0].width == 21);

    memset(grid, 0x20, sizeof(grid));
    PUTSTR(15, 4, "ACCELEROMETER NOT CALIBRATED");
    n = osd_elements_scan(getter, NULL, COLS, ROWS, "INAV", els, 32);
    check("28-char message not truncated", n == 1
          && strcmp(els[0].text, "ACCELEROMETER NOT CALIBRATED") == 0);
    check("not-calibrated is a warning", n == 1 && els[0].severity == OSD_SEV_WARN);

    memset(grid, 0x20, sizeof(grid));
    PUTSTR(15, 4, "LANDED");
    n = osd_elements_scan(getter, NULL, COLS, ROWS, "INAV", els, 32);
    check("LANDED is info, not alarming", n == 1 && els[0].severity == OSD_SEV_INFO);

    memset(grid, 0x20, sizeof(grid));
    PUTSTR(15, 4, "LOW BATTERY");
    n = osd_elements_scan(getter, NULL, COLS, ROWS, "BTFL", els, 32);
    check("BTFL LOW BATTERY is critical", n == 1 && els[0].severity == OSD_SEV_CRIT);

    // messages formatted with a trailing value still match
    memset(grid, 0x20, sizeof(grid));
    PUTSTR(15, 4, "ENTERING NFZ IN 30 S");
    n = osd_elements_scan(getter, NULL, COLS, ROWS, "INAV", els, 32);
    check("message with trailing value matches", n == 1 && els[0].type == OSD_ELEM_WARNING);

    // a word that merely starts like a message must not match
    memset(grid, 0x20, sizeof(grid));
    PUTSTR(15, 4, "LANDEDXYZ");
    n = osd_elements_scan(getter, NULL, COLS, ROWS, "INAV", els, 32);
    check("prefix of a longer word rejected", n == 0);

    // a message row must not also yield a flight mode
    memset(grid, 0x20, sizeof(grid));
    PUTSTR(15, 4, "AUTOLAUNCH");
    n = osd_elements_scan(getter, NULL, COLS, ROWS, "INAV", els, 32);
    check("message row not double-claimed as mode", n == 1 && els[0].type == OSD_ELEM_WARNING);

    // --- compass bar: found as a run of graphic glyphs, not by a leading symbol.
    // Only its position matters; the heading itself comes from MSP_ATTITUDE.
    memset(grid, 0x20, sizeof(grid));
    { uint16_t g[] = {0xCD,0xCC,0xC8,0xCC,0xCD,0xCC,0xCA,0xCC,0xCD}; put(3, 20, g, 9); }
    n = osd_elements_scan(getter, NULL, COLS, ROWS, "INAV", els, 32);
    check("INAV heading bar recognised", n == 1 && els[0].type == OSD_ELEM_HEADING_BAR);
    check("INAV heading bar spans the run", n == 1 && els[0].col == 20 && els[0].width == 9);
    check("INAV heading bar carries no value", n == 1 && !els[0].value_valid);

    memset(grid, 0x20, sizeof(grid));
    { uint16_t g[] = {0x1D,0x1C,0x18,0x1C,0x1D,0x1C,0x1A,0x1C,0x1D}; put(5, 8, g, 9); }
    n = osd_elements_scan(getter, NULL, COLS, ROWS, "BTFL", els, 32);
    check("BTFL heading bar recognised", n == 1 && els[0].type == OSD_ELEM_HEADING_BAR);
    check("BTFL heading bar spans the run", n == 1 && els[0].col == 8 && els[0].width == 9);

    // The two firmwares use different ranges, and Betaflight's sits in what is
    // ordinary text territory for INAV - the tables must not be crossed.
    memset(grid, 0x20, sizeof(grid));
    { uint16_t g[] = {0x1D,0x1C,0x18,0x1C,0x1D,0x1C,0x1A,0x1C,0x1D}; put(5, 8, g, 9); }
    n = osd_elements_scan(getter, NULL, COLS, ROWS, "INAV", els, 32);
    check("BTFL bar glyphs are not an INAV bar", n == 0);

    // A stray glyph or two must not become a bar.
    memset(grid, 0x20, sizeof(grid));
    { uint16_t g[] = {0xCD,0xCC}; put(7, 30, g, 2); }
    n = osd_elements_scan(getter, NULL, COLS, ROWS, "INAV", els, 32);
    check("a two-glyph run is not a bar", n == 0);

    printf("\n%s (%d failure%s)\n", fails ? "FAILURES" : "ALL PASS", fails, fails == 1 ? "" : "s");
    return fails != 0;
}

#include "osd_elements.h"
#include <stdio.h>
#include <string.h>

#define COLS 53
#define ROWS 20
static uint16_t grid[ROWS][COLS];

// A blank cell is the glyph 0x20, not the byte 0x20: memset() over a uint16_t
// grid fills it with 0x2020, which no blank check recognises. Every test here
// used to "clear" the grid that way, so nothing in this file had a genuinely
// blank cell in it - and anything that looks at the cells *around* a field, as
// the literal-field scan does, was untestable.
static void clear_grid(void) {
	for (int r = 0; r < ROWS; r++)
		for (int c = 0; c < COLS; c++)
			grid[r][c] = 0x20;
}

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
    clear_grid();
    { uint16_t g[] = {'4','.','1','4',0x1F}; put(2, 10, g, 5); }
    int n = osd_elements_scan(getter, NULL, COLS, ROWS, "INAV", els, 32);
    check("INAV ascii voltage: exactly one element", n == 1);
    check("INAV ascii voltage: type", n && els[0].type == OSD_ELEM_VOLTAGE);
    check("INAV ascii voltage: value 4.14", n && els[0].value > 4.139 && els[0].value < 4.141);
    check("INAV ascii voltage: flagged per-cell", n && els[0].is_per_cell);
    check("INAV ascii voltage: col == 10", n && els[0].col == 10);
    check("INAV ascii voltage: row == 2", n && els[0].row == 2);

    // --- INAV packed decimal glyph: 0xA1+4 is "4." then "14"
    clear_grid();
    { uint16_t g[] = {(uint16_t)(0xA1+4),'1','4',0x1F}; put(5, 3, g, 4); }
    n = osd_elements_scan(getter, NULL, COLS, ROWS, "INAV", els, 32);
    check("INAV packed-dot voltage: value 4.14", n == 1 && els[0].value > 4.139 && els[0].value < 4.141);

    // --- real INAV encoding: split half-dot pair must collapse to one point
    clear_grid();
    { uint16_t g[] = {'1',(uint16_t)(0xA1+6),(uint16_t)(0xB1+7),0x1F}; put(0, 13, g, 4); }
    n = osd_elements_scan(getter, NULL, COLS, ROWS, "INAV", els, 32);
    check("INAV half-dot pair '16.7' parses as 16.7", n == 1 && els[0].value > 16.69 && els[0].value < 16.71);
    check("INAV half-dot pair text is '16.7'", n == 1 && strcmp(els[0].text, "16.7") == 0);

    clear_grid();
    { uint16_t g[] = {(uint16_t)(0xA1+0),(uint16_t)(0xB1+0),'0',0x6A}; put(3, 2, g, 4); }
    n = osd_elements_scan(getter, NULL, COLS, ROWS, "INAV", els, 32);
    check("INAV half-dot current '0.00' parses as 0.0", n == 1 && els[0].value >= 0.0 && els[0].value < 0.001 && strcmp(els[0].text, "0.00") == 0);

    // --- pack voltage must NOT be flagged per-cell
    clear_grid();
    { uint16_t g[] = {'1','6','.','2',0x1F}; put(1, 1, g, 5); }
    n = osd_elements_scan(getter, NULL, COLS, ROWS, "INAV", els, 32);
    check("INAV pack voltage 16.2: not per-cell", n == 1 && !els[0].is_per_cell);

    // --- current, trailing SYM_AMP
    clear_grid();
    { uint16_t g[] = {'3','4','.','1',0x6A}; put(7, 20, g, 5); }
    n = osd_elements_scan(getter, NULL, COLS, ROWS, "INAV", els, 32);
    check("INAV current 34.1A", n == 1 && els[0].type == OSD_ELEM_CURRENT && els[0].value > 34.09 && els[0].value < 34.11);

    // --- latitude, LEADING symbol
    clear_grid();
    { uint16_t g[] = {0x03,'5','2','.','4','7','9','6'}; put(18, 0, g, 8); }
    n = osd_elements_scan(getter, NULL, COLS, ROWS, "INAV", els, 32);
    check("INAV latitude: type", n == 1 && els[0].type == OSD_ELEM_LATITUDE);
    check("INAV latitude: value 52.4796", n == 1 && els[0].value > 52.479 && els[0].value < 52.480);
    check("INAV latitude: col is symbol cell 0", n == 1 && els[0].col == 0);

    // --- negative longitude
    clear_grid();
    { uint16_t g[] = {0x04,'-','1','3','.','6'}; put(19, 30, g, 6); }
    n = osd_elements_scan(getter, NULL, COLS, ROWS, "INAV", els, 32);
    check("INAV longitude -13.6", n == 1 && els[0].value < -13.59 && els[0].value > -13.61);

    // --- Betaflight uses different codes: 0x06 volt, 0x9A amp
    clear_grid();
    { uint16_t g[] = {'3','.','8','5',0x06}; put(4, 8, g, 5); }
    n = osd_elements_scan(getter, NULL, COLS, ROWS, "BTFL", els, 32);
    check("BTFL voltage 3.85 via 0x06", n == 1 && els[0].type == OSD_ELEM_VOLTAGE && els[0].value > 3.84 && els[0].value < 3.86);

    // INAV's volt code must NOT match under BTFL
    clear_grid();
    { uint16_t g[] = {'4','.','1',0x1F}; put(4, 8, g, 4); }
    n = osd_elements_scan(getter, NULL, COLS, ROWS, "BTFL", els, 32);
    check("BTFL ignores INAV volt code 0x1F", n == 0);

    // --- bare symbol with no digits is not an element
    clear_grid();
    { uint16_t g[] = {0x1F}; put(9, 9, g, 1); }
    n = osd_elements_scan(getter, NULL, COLS, ROWS, "INAV", els, 32);
    check("bare symbol, no number: ignored", n == 0);

    // --- several elements on one row
    clear_grid();
    { uint16_t a[] = {'4','.','1',0x1F}; put(3, 0, a, 4); }
    { uint16_t b[] = {'1','2','.','5',0x6A}; put(3, 20, b, 5); }
    n = osd_elements_scan(getter, NULL, COLS, ROWS, "INAV", els, 32);
    check("two elements on one row", n == 2 && els[0].type == OSD_ELEM_VOLTAGE && els[1].type == OSD_ELEM_CURRENT);

    // --- battery icon before a voltage marks it as battery voltage and is absorbed
    clear_grid();
    { uint16_t g[] = {0x63,'1','6','.','7',0x1F}; put(0, 12, g, 6); }   // SYM_BATT_FULL
    n = osd_elements_scan(getter, NULL, COLS, ROWS, "INAV", els, 32);
    check("battery icon detected", n == 1 && els[0].has_battery_icon);
    check("battery level 0 = full", n == 1 && els[0].battery_level == 0);
    check("battery icon absorbed into col", n == 1 && els[0].col == 12);
    check("battery voltage value 16.7", n == 1 && els[0].value > 16.69 && els[0].value < 16.71);

    clear_grid();
    { uint16_t g[] = {0x69,'1','4','.','0',0x1F}; put(0, 12, g, 6); }   // SYM_BATT_EMPTY
    n = osd_elements_scan(getter, NULL, COLS, ROWS, "INAV", els, 32);
    check("battery level 6 = empty", n == 1 && els[0].battery_level == 6);

    // a voltage with no icon must not claim one
    clear_grid();
    { uint16_t g[] = {'4','.','1','4',0x1F}; put(2, 10, g, 5); }
    n = osd_elements_scan(getter, NULL, COLS, ROWS, "INAV", els, 32);
    check("no battery icon when absent", n == 1 && !els[0].has_battery_icon);

    // --- satellites: two-cell icon, both halves absorbed
    clear_grid();
    { uint16_t g[] = {0x08,0x09,'1','7'}; put(18, 4, g, 4); }
    n = osd_elements_scan(getter, NULL, COLS, ROWS, "INAV", els, 32);
    check("sats detected", n == 1 && els[0].type == OSD_ELEM_SATS);
    check("sats value 17", n == 1 && els[0].value > 16.9 && els[0].value < 17.1);
    check("both icon halves absorbed", n == 1 && els[0].col == 4 && els[0].width == 4);

    // --- throttle
    clear_grid();
    { uint16_t g[] = {0x95,'4','5'}; put(6, 2, g, 3); }
    n = osd_elements_scan(getter, NULL, COLS, ROWS, "INAV", els, 32);
    check("throttle detected 45", n == 1 && els[0].type == OSD_ELEM_THROTTLE
          && els[0].value > 44.9 && els[0].value < 45.1);

    // --- flight time keeps the colon and is text, not a number
    clear_grid();
    { uint16_t g[] = {0x9F,'0','5',':','3','6'}; put(7, 2, g, 6); }
    n = osd_elements_scan(getter, NULL, COLS, ROWS, "INAV", els, 32);
    check("flight time detected", n == 1 && els[0].type == OSD_ELEM_FLIGHT_TIME);
    check("flight time text '05:36'", n == 1 && strcmp(els[0].text, "05:36") == 0);
    check("flight time is not numeric", n == 1 && !els[0].value_valid);

    // --- flight mode word, no symbol involved
    clear_grid();
    { uint16_t g[] = {'A','C','R','O'}; put(14, 8, g, 4); }
    n = osd_elements_scan(getter, NULL, COLS, ROWS, "INAV", els, 32);
    check("flight mode ACRO detected", n == 1 && els[0].type == OSD_ELEM_FLIGHT_MODE
          && strcmp(els[0].text, "ACRO") == 0);
    check("flight mode position", n == 1 && els[0].col == 8 && els[0].width == 4);

    clear_grid();
    { uint16_t g[] = {'B','A','N','A','N','A'}; put(14, 8, g, 6); }
    n = osd_elements_scan(getter, NULL, COLS, ROWS, "INAV", els, 32);
    check("unknown word is not a flight mode", n == 0);

    // BTFL uses different sat/throttle codes
    clear_grid();
    { uint16_t g[] = {0x1E,0x1F,'0','9'}; put(3, 3, g, 4); }
    n = osd_elements_scan(getter, NULL, COLS, ROWS, "BTFL", els, 32);
    check("BTFL sats via 0x1E/0x1F", n == 1 && els[0].type == OSD_ELEM_SATS && els[0].value > 8.9);

    // --- right-aligned fields put blanks between symbol and value.
    // These are the low/blinking readings, so losing them is worst-case.
    clear_grid();
    { uint16_t g[] = {0x01,0x20,'0'}; put(0, 23, g, 3); }        // RSSI "<sym> 0"
    n = osd_elements_scan(getter, NULL, COLS, ROWS, "INAV", els, 32);
    check("RSSI 0 with one leading blank", n == 1 && els[0].type == OSD_ELEM_RSSI
          && els[0].value >= 0.0f && els[0].value < 0.01f);
    check("RSSI element starts at its symbol", n == 1 && els[0].col == 23);

    clear_grid();
    { uint16_t g[] = {0x95,0x20,'5','3'}; put(2, 2, g, 4); }     // THR "<sym> 53"
    n = osd_elements_scan(getter, NULL, COLS, ROWS, "INAV", els, 32);
    check("throttle with leading blank", n == 1 && els[0].type == OSD_ELEM_THROTTLE
          && els[0].value > 52.9 && els[0].value < 53.1);
    check("throttle spans symbol to value", n == 1 && els[0].col == 2 && els[0].width == 4);

    clear_grid();
    { uint16_t g[] = {0x08,0x09,0x20,'8'}; put(11, 0, g, 4); }   // sats "<icon> 8"
    n = osd_elements_scan(getter, NULL, COLS, ROWS, "INAV", els, 32);
    check("single-digit sats with blank", n == 1 && els[0].type == OSD_ELEM_SATS
          && els[0].value > 7.9 && els[0].value < 8.1);

    // throttle is a fixed 4-cell field: symbol + 3 digit slots, right aligned.
    // 100 fills it, 7 leaves two blanks.
    clear_grid();
    { uint16_t g[] = {0x95,'1','0','0'}; put(2, 2, g, 4); }
    n = osd_elements_scan(getter, NULL, COLS, ROWS, "INAV", els, 32);
    check("throttle 100 fills the field", n == 1 && els[0].value > 99.9 && els[0].value < 100.1);

    clear_grid();
    { uint16_t g[] = {0x95,0x20,0x20,'7'}; put(2, 2, g, 4); }
    n = osd_elements_scan(getter, NULL, COLS, ROWS, "INAV", els, 32);
    check("throttle 7 with two leading blanks", n == 1 && els[0].value > 6.9 && els[0].value < 7.1);
    check("throttle 7 still spans the whole field", n == 1 && els[0].col == 2 && els[0].width == 4);

    // a symbol with only blanks after it is still not an element
    clear_grid();
    { uint16_t g[] = {0x95}; put(4, 4, g, 1); }
    n = osd_elements_scan(getter, NULL, COLS, ROWS, "INAV", els, 32);
    check("symbol with no value anywhere: ignored", n == 0);

    // --- OSD messages: severity must come from the message, not the theme
    #define PUTSTR(r,c,str) do{ const char*_s=(str); for(int _i=0;_s[_i];_i++) grid[r][(c)+_i]=(uint16_t)_s[_i]; }while(0)

    clear_grid();
    PUTSTR(15, 10, "FAILSAFE MODE ENABLED");
    n = osd_elements_scan(getter, NULL, COLS, ROWS, "INAV", els, 32);
    check("failsafe recognised", n == 1 && els[0].type == OSD_ELEM_WARNING);
    check("failsafe is critical", n == 1 && els[0].severity == OSD_SEV_CRIT);
    check("message text preserved whole", n == 1 && strcmp(els[0].text, "FAILSAFE MODE ENABLED") == 0);
    check("message spans its text", n == 1 && els[0].col == 10 && els[0].width == 21);

    clear_grid();
    PUTSTR(15, 4, "ACCELEROMETER NOT CALIBRATED");
    n = osd_elements_scan(getter, NULL, COLS, ROWS, "INAV", els, 32);
    check("28-char message not truncated", n == 1
          && strcmp(els[0].text, "ACCELEROMETER NOT CALIBRATED") == 0);
    check("not-calibrated is a warning", n == 1 && els[0].severity == OSD_SEV_WARN);

    clear_grid();
    PUTSTR(15, 4, "LANDED");
    n = osd_elements_scan(getter, NULL, COLS, ROWS, "INAV", els, 32);
    check("LANDED is info, not alarming", n == 1 && els[0].severity == OSD_SEV_INFO);

    clear_grid();
    PUTSTR(15, 4, "LOW BATTERY");
    n = osd_elements_scan(getter, NULL, COLS, ROWS, "BTFL", els, 32);
    check("BTFL LOW BATTERY is critical", n == 1 && els[0].severity == OSD_SEV_CRIT);

    // messages formatted with a trailing value still match
    clear_grid();
    PUTSTR(15, 4, "ENTERING NFZ IN 30 S");
    n = osd_elements_scan(getter, NULL, COLS, ROWS, "INAV", els, 32);
    check("message with trailing value matches", n == 1 && els[0].type == OSD_ELEM_WARNING);

    // a word that merely starts like a message must not match
    clear_grid();
    PUTSTR(15, 4, "LANDEDXYZ");
    n = osd_elements_scan(getter, NULL, COLS, ROWS, "INAV", els, 32);
    check("prefix of a longer word rejected", n == 0);

    // a message row must not also yield a flight mode
    clear_grid();
    PUTSTR(15, 4, "AUTOLAUNCH");
    n = osd_elements_scan(getter, NULL, COLS, ROWS, "INAV", els, 32);
    check("message row not double-claimed as mode", n == 1 && els[0].type == OSD_ELEM_WARNING);

    // --- compass bar: found as a run of graphic glyphs, not by a leading symbol.
    // Only its position matters; the heading itself comes from MSP_ATTITUDE.
    clear_grid();
    { uint16_t g[] = {0xCD,0xCC,0xC8,0xCC,0xCD,0xCC,0xCA,0xCC,0xCD}; put(3, 20, g, 9); }
    n = osd_elements_scan(getter, NULL, COLS, ROWS, "INAV", els, 32);
    check("INAV heading bar recognised", n == 1 && els[0].type == OSD_ELEM_HEADING_BAR);
    check("INAV heading bar spans the run", n == 1 && els[0].col == 20 && els[0].width == 9);
    check("INAV heading bar carries no value", n == 1 && !els[0].value_valid);

    clear_grid();
    { uint16_t g[] = {0x1D,0x1C,0x18,0x1C,0x1D,0x1C,0x1A,0x1C,0x1D}; put(5, 8, g, 9); }
    n = osd_elements_scan(getter, NULL, COLS, ROWS, "BTFL", els, 32);
    check("BTFL heading bar recognised", n == 1 && els[0].type == OSD_ELEM_HEADING_BAR);
    check("BTFL heading bar spans the run", n == 1 && els[0].col == 8 && els[0].width == 9);

    // The two firmwares use different ranges, and Betaflight's sits in what is
    // ordinary text territory for INAV - the tables must not be crossed.
    clear_grid();
    { uint16_t g[] = {0x1D,0x1C,0x18,0x1C,0x1D,0x1C,0x1A,0x1C,0x1D}; put(5, 8, g, 9); }
    n = osd_elements_scan(getter, NULL, COLS, ROWS, "INAV", els, 32);
    check("BTFL bar glyphs are not an INAV bar", n == 0);

    // A stray glyph or two must not become a bar.
    clear_grid();
    { uint16_t g[] = {0xCD,0xCC}; put(7, 30, g, 2); }
    n = osd_elements_scan(getter, NULL, COLS, ROWS, "INAV", els, 32);
    check("a two-glyph run is not a bar", n == 0);

    // --- Betaflight fields the recogniser used to walk straight past.
    //
    // Every layout below is the exact byte sequence the firmware writes, taken
    // from the tfp_sprintf and osdPrintFloat calls in src/main/osd/osd_elements.c,
    // so a format change upstream fails here rather than silently on the video.

    // Altitude: osdFormatAltitudeString passes SYM_ALTITUDE as the *leading*
    // symbol with the unit trailing. The table had it as a trailing symbol, so
    // this element was never recognised on Betaflight at all.
    clear_grid();
    { uint16_t g[] = {0x7F,'1','2','.','4',0x0C}; put(4, 10, g, 6); }
    n = osd_elements_scan(getter, NULL, COLS, ROWS, "BTFL", els, 32);
    check("BTFL altitude recognised", n == 1 && els[0].type == OSD_ELEM_ALTITUDE);
    check("BTFL altitude value", n == 1 && els[0].value > 12.3f && els[0].value < 12.5f);
    check("BTFL altitude unit read", n == 1 && els[0].unit == OSD_UNIT_METRES);
    check("BTFL altitude absorbs the unit glyph", n == 1 && els[0].width == 6);

    // The unit glyph is what says metric or imperial - the number alone does not.
    clear_grid();
    { uint16_t g[] = {0x7F,'4','0','7',0x0F}; put(4, 10, g, 5); }
    n = osd_elements_scan(getter, NULL, COLS, ROWS, "BTFL", els, 32);
    check("BTFL altitude in feet", n == 1 && els[0].unit == OSD_UNIT_FEET);

    // Ground speed: SYM_SPEED, three digits, unit.
    clear_grid();
    { uint16_t g[] = {0x70,' ','8','7',0x9E}; put(6, 2, g, 5); }
    n = osd_elements_scan(getter, NULL, COLS, ROWS, "BTFL", els, 32);
    check("BTFL ground speed", n == 1 && els[0].type == OSD_ELEM_SPEED &&
        els[0].value > 86.9f && els[0].value < 87.1f);
    check("BTFL speed unit", n == 1 && els[0].unit == OSD_UNIT_KPH);
    check("BTFL speed is not airspeed", n == 1 && !els[0].is_airspeed);

    // Airspeed is the same field with an 'a' wedged in. Without handling that
    // letter the digits start one cell late and the reading is wrong.
    clear_grid();
    { uint16_t g[] = {0x70,'a',' ','9','2',0x9E}; put(6, 2, g, 6); }
    n = osd_elements_scan(getter, NULL, COLS, ROWS, "BTFL", els, 32);
    check("BTFL airspeed recognised", n == 1 && els[0].type == OSD_ELEM_SPEED);
    check("BTFL airspeed flagged", n == 1 && els[0].is_airspeed);
    check("BTFL airspeed value", n == 1 && els[0].value > 91.9f && els[0].value < 92.1f);

    // Vario: the number is unsigned and the arrow carries the sign.
    clear_grid();
    { uint16_t g[] = {0x75,'2','.','4',0x9F}; put(8, 40, g, 5); }
    n = osd_elements_scan(getter, NULL, COLS, ROWS, "BTFL", els, 32);
    check("BTFL vario climb", n == 1 && els[0].type == OSD_ELEM_VARIO && els[0].value > 2.3f);

    clear_grid();
    { uint16_t g[] = {0x76,'2','.','4',0x9F}; put(8, 40, g, 5); }
    n = osd_elements_scan(getter, NULL, COLS, ROWS, "BTFL", els, 32);
    check("BTFL vario sink is negative", n == 1 && els[0].value < -2.3f && els[0].value > -2.5f);

    // Distances.
    clear_grid();
    { uint16_t g[] = {0x11,'4','2','0',0x0C}; put(10, 2, g, 5); }
    n = osd_elements_scan(getter, NULL, COLS, ROWS, "BTFL", els, 32);
    check("BTFL home distance", n == 1 && els[0].type == OSD_ELEM_HOME_DISTANCE &&
        els[0].value > 419.0f);

    clear_grid();
    { uint16_t g[] = {0x71,'1','.','2','4',0x7D}; put(11, 2, g, 6); }
    n = osd_elements_scan(getter, NULL, COLS, ROWS, "BTFL", els, 32);
    check("BTFL total distance in km", n == 1 && els[0].type == OSD_ELEM_TOTAL_DISTANCE &&
        els[0].unit == OSD_UNIT_KM);

    // Temperature: the letter before the symbol says core or ESC, and both must
    // be recognised rather than one swallowing the other.
    clear_grid();
    { uint16_t g[] = {'C',0x7A,' ','4','2',0x0E}; put(12, 2, g, 6); }
    n = osd_elements_scan(getter, NULL, COLS, ROWS, "BTFL", els, 32);
    check("BTFL core temperature", n == 1 && els[0].type == OSD_ELEM_TEMPERATURE &&
        els[0].value > 41.9f && els[0].value < 42.1f);
    check("BTFL temperature unit", n == 1 && els[0].unit == OSD_UNIT_CELSIUS);

    clear_grid();
    { uint16_t g[] = {'E',0x7A,' ','6','8',0x0E}; put(12, 2, g, 6); }
    n = osd_elements_scan(getter, NULL, COLS, ROWS, "BTFL", els, 32);
    check("BTFL ESC temperature", n == 1 && els[0].value > 67.9f && els[0].value < 68.1f);

    // Link quality has its own symbol and is not RSSI.
    clear_grid();
    { uint16_t g[] = {0x7B,'9','9'}; put(13, 2, g, 3); }
    n = osd_elements_scan(getter, NULL, COLS, ROWS, "BTFL", els, 32);
    check("BTFL link quality", n == 1 && els[0].type == OSD_ELEM_LINK_QUALITY &&
        els[0].value > 98.9f);

    // The three readings that share SYM_RSSI, told apart by magnitude. Drawing
    // -94dBm as a percentage would show a nearly full signal bar.
    clear_grid();
    { uint16_t g[] = {0x01,'8','5'}; put(14, 2, g, 3); }
    n = osd_elements_scan(getter, NULL, COLS, ROWS, "BTFL", els, 32);
    check("SYM_RSSI with 85 is a percentage", n == 1 && els[0].type == OSD_ELEM_RSSI);

    clear_grid();
    { uint16_t g[] = {0x01,'-','9','4'}; put(14, 2, g, 4); }
    n = osd_elements_scan(getter, NULL, COLS, ROWS, "BTFL", els, 32);
    check("SYM_RSSI with -94 is dBm", n == 1 && els[0].type == OSD_ELEM_RSSI_DBM &&
        els[0].value < -93.9f);

    clear_grid();
    { uint16_t g[] = {0x01,'-','6'}; put(14, 2, g, 3); }
    n = osd_elements_scan(getter, NULL, COLS, ROWS, "BTFL", els, 32);
    check("SYM_RSSI with -6 is SNR", n == 1 && els[0].type == OSD_ELEM_SNR);

    // The arrow rose serves two elements; the three digits are what separate them.
    clear_grid();
    { uint16_t g[] = {0x68}; put(16, 25, g, 1); }
    n = osd_elements_scan(getter, NULL, COLS, ROWS, "BTFL", els, 32);
    check("a lone arrow is the home marker", n == 1 && els[0].type == OSD_ELEM_HOME_ARROW &&
        els[0].width == 1);

    clear_grid();
    { uint16_t g[] = {0x6C,'2','7','4'}; put(16, 25, g, 4); }
    n = osd_elements_scan(getter, NULL, COLS, ROWS, "BTFL", els, 32);
    check("an arrow with digits is the heading", n == 1 && els[0].type == OSD_ELEM_HEADING &&
        els[0].value > 273.9f && els[0].value < 274.1f);
    check("heading spans arrow and digits", n == 1 && els[0].width == 4);

    // INAV must not pick up Betaflight's arrow block, which lies in ordinary
    // text territory for it.
    clear_grid();
    { uint16_t g[] = {0x68}; put(16, 25, g, 1); }
    n = osd_elements_scan(getter, NULL, COLS, ROWS, "INAV", els, 32);
    check("INAV ignores the BTFL arrow block", n == 0);

    // --- fields with no symbol glyph, found by the literal around the number.

    clear_grid();
    PUTSTR(9, 40, "1.8G");
    n = osd_elements_scan(getter, NULL, COLS, ROWS, "BTFL", els, 32);
    check("g-force by its trailing G", n == 1 && els[0].type == OSD_ELEM_GFORCE &&
        els[0].value > 1.79f && els[0].value < 1.81f);

    clear_grid();
    PUTSTR(9, 40, "1234W");
    n = osd_elements_scan(getter, NULL, COLS, ROWS, "BTFL", els, 32);
    check("power by its trailing W", n == 1 && els[0].type == OSD_ELEM_POWER &&
        els[0].value > 1233.0f);

    // Longest suffix first, or "12WH" reads as 12 watts with a stray H.
    clear_grid();
    PUTSTR(9, 40, "12.50WH");
    n = osd_elements_scan(getter, NULL, COLS, ROWS, "BTFL", els, 32);
    check("watt-hours beat watts", n == 1 && els[0].type == OSD_ELEM_WATT_HOURS &&
        els[0].value > 12.4f && els[0].value < 12.6f);

    clear_grid();
    PUTSTR(9, 40, "850MWH");
    n = osd_elements_scan(getter, NULL, COLS, ROWS, "BTFL", els, 32);
    check("milliwatt-hours scaled to Wh", n == 1 && els[0].type == OSD_ELEM_WATT_HOURS &&
        els[0].value > 0.84f && els[0].value < 0.86f);

    clear_grid();
    PUTSTR(9, 40, "RF:  32");
    n = osd_elements_scan(getter, NULL, COLS, ROWS, "BTFL", els, 32);
    check("rangefinder by its RF: prefix", n == 1 && els[0].type == OSD_ELEM_RANGEFINDER &&
        els[0].value > 31.9f);

    // A number that happens to sit before a word is not a reading.
    clear_grid();
    PUTSTR(9, 40, "12WHATEVER");
    n = osd_elements_scan(getter, NULL, COLS, ROWS, "BTFL", els, 32);
    check("a word after the suffix is rejected", n == 0);

    // Nor is a number with nothing attached - that is ESC RPM, the RC channel
    // readout, the ETA and half a dozen others, and guessing between them would
    // put a confident wrong number on the screen.
    clear_grid();
    PUTSTR(9, 40, "1450");
    n = osd_elements_scan(getter, NULL, COLS, ROWS, "BTFL", els, 32);
    check("a bare number is left alone", n == 0);

    // --- the two fields that were being read as something else entirely.

    // Efficiency carries SYM_MAH, so it was reported as capacity used: an
    // efficiency of 180 mAh/km showed up as 180mAh consumed.
    clear_grid();
    { uint16_t g[] = {'1','8','0',0x07,'/',0x7D}; put(9, 30, g, 6); }
    n = osd_elements_scan(getter, NULL, COLS, ROWS, "BTFL", els, 32);
    check("mAh followed by / is efficiency", n == 1 && els[0].type == OSD_ELEM_EFFICIENCY);
    check("efficiency absorbs the whole field", n == 1 && els[0].width == 6);
    check("efficiency keeps its unit", n == 1 && els[0].unit == OSD_UNIT_KM);

    // Plain capacity used must still be capacity used.
    clear_grid();
    { uint16_t g[] = {'1','8','0',0x07}; put(9, 30, g, 4); }
    n = osd_elements_scan(getter, NULL, COLS, ROWS, "BTFL", els, 32);
    check("mAh alone is still capacity used", n == 1 && els[0].type == OSD_ELEM_MAH);

    // Uplink power carries SYM_RSSI, so "25MW" was a 25% signal reading.
    clear_grid();
    { uint16_t g[] = {0x01,' ','2','5','M','W'}; put(9, 30, g, 6); }
    n = osd_elements_scan(getter, NULL, COLS, ROWS, "BTFL", els, 32);
    check("SYM_RSSI with MW is transmit power", n == 1 && els[0].type == OSD_ELEM_TX_POWER &&
        els[0].value > 24.9f && els[0].value < 25.1f);

    // Above a watt it is printed in watts and has to be scaled to match.
    clear_grid();
    { uint16_t g[] = {0x01,'1','.','5','W'}; put(9, 30, g, 5); }
    n = osd_elements_scan(getter, NULL, COLS, ROWS, "BTFL", els, 32);
    check("SYM_RSSI with W is transmit power in mW", n == 1 &&
        els[0].type == OSD_ELEM_TX_POWER && els[0].value > 1499.0f && els[0].value < 1501.0f);

    // And plain RSSI must survive all of that.
    clear_grid();
    { uint16_t g[] = {0x01,'8','5'}; put(9, 30, g, 3); }
    n = osd_elements_scan(getter, NULL, COLS, ROWS, "BTFL", els, 32);
    check("plain RSSI still reads as RSSI", n == 1 && els[0].type == OSD_ELEM_RSSI);

    // INAV shares none of this - its font puts other things at these codes.
    clear_grid();
    PUTSTR(9, 40, "1.8G");
    n = osd_elements_scan(getter, NULL, COLS, ROWS, "INAV", els, 32);
    check("INAV is not scanned for literal fields", n == 0);

    // --- INAV, against src/main/io/osd.c and src/main/drivers/osd_symbols.h.
    //
    // INAV encodes the unit in the symbol rather than appending one, and several
    // of its fields have no symbol of their own at all - osdFormatVelocityStr
    // writes "%3d%c", so the unit glyph is the only anchor there is.

    clear_grid();
    { uint16_t g[] = {' ','8','7',0x90}; put(6, 2, g, 4); }
    n = osd_elements_scan(getter, NULL, COLS, ROWS, "INAV", els, 32);
    check("INAV ground speed off its unit glyph", n == 1 && els[0].type == OSD_ELEM_SPEED &&
        els[0].value > 86.9f && els[0].value < 87.1f && els[0].unit == OSD_UNIT_KPH);
    check("INAV ground speed is not airspeed", n == 1 && !els[0].is_airspeed);

    // SYM_AIR in front of an otherwise identical field is what makes it airspeed.
    clear_grid();
    { uint16_t g[] = {0x8C,' ','9','2',0x90}; put(6, 2, g, 5); }
    n = osd_elements_scan(getter, NULL, COLS, ROWS, "INAV", els, 32);
    check("INAV airspeed by its SYM_AIR", n == 1 && els[0].type == OSD_ELEM_SPEED &&
        els[0].is_airspeed);
    check("INAV airspeed absorbs SYM_AIR", n == 1 && els[0].width == 5);

    // Wind speed ends identically - digits then the same unit glyph - and is
    // only told apart by what sits in front of it.
    clear_grid();
    { uint16_t g[] = {0x86,0x18,'4','.','2',0x90}; put(6, 2, g, 6); }
    n = osd_elements_scan(getter, NULL, COLS, ROWS, "INAV", els, 32);
    check("INAV wind is not read as ground speed", n == 0);

    // And with the padding a short reading leaves, which puts a blank between
    // the wind symbols and the digits - looking only at the cell next door
    // would see that blank and let it through.
    clear_grid();
    { uint16_t g[] = {0x86,0x18,' ','4',0x90}; put(6, 2, g, 5); }
    n = osd_elements_scan(getter, NULL, COLS, ROWS, "INAV", els, 32);
    check("INAV padded wind is still not ground speed", n == 0);

    clear_grid();
    { uint16_t g[] = {'2','.','4',0x8F}; put(7, 2, g, 4); }
    n = osd_elements_scan(getter, NULL, COLS, ROWS, "INAV", els, 32);
    check("INAV climb rate", n == 1 && els[0].type == OSD_ELEM_VARIO &&
        els[0].unit == OSD_UNIT_MPS);

    clear_grid();
    { uint16_t g[] = {'4','1','2',0x71}; put(8, 2, g, 4); }
    n = osd_elements_scan(getter, NULL, COLS, ROWS, "INAV", els, 32);
    check("INAV power in watts", n == 1 && els[0].type == OSD_ELEM_POWER &&
        els[0].value > 411.0f);

    // Above a kilowatt INAV swaps the glyph and rescales the number with it.
    clear_grid();
    { uint16_t g[] = {'1','.','2',0x73}; put(8, 2, g, 4); }
    n = osd_elements_scan(getter, NULL, COLS, ROWS, "INAV", els, 32);
    check("INAV kilowatts scaled to watts", n == 1 && els[0].type == OSD_ELEM_POWER &&
        els[0].value > 1199.0f && els[0].value < 1201.0f);

    clear_grid();
    { uint16_t g[] = {0x10,'4','2','0',0x7A}; put(9, 2, g, 5); }
    n = osd_elements_scan(getter, NULL, COLS, ROWS, "INAV", els, 32);
    check("INAV home distance", n == 1 && els[0].type == OSD_ELEM_HOME_DISTANCE &&
        els[0].unit == OSD_UNIT_METRES && els[0].width == 5);

    clear_grid();
    { uint16_t g[] = {0x75,'1','.','2','4',0x7E}; put(10, 2, g, 6); }
    n = osd_elements_scan(getter, NULL, COLS, ROWS, "INAV", els, 32);
    check("INAV trip distance in km", n == 1 && els[0].type == OSD_ELEM_TOTAL_DISTANCE &&
        els[0].unit == OSD_UNIT_KM);

    // Heading and ground course are the same shape and differ only in the
    // leading symbol. Both close with SYM_DEGREES, which has to be absorbed.
    clear_grid();
    { uint16_t g[] = {0x0C,'2','7','4',0x0B}; put(11, 2, g, 5); }
    n = osd_elements_scan(getter, NULL, COLS, ROWS, "INAV", els, 32);
    check("INAV heading", n == 1 && els[0].type == OSD_ELEM_HEADING &&
        els[0].value > 273.9f && els[0].value < 274.1f);
    check("INAV heading absorbs SYM_DEGREES", n == 1 && els[0].width == 5);

    clear_grid();
    { uint16_t g[] = {0xDC,'2','8','9',0x0B}; put(11, 2, g, 5); }
    n = osd_elements_scan(getter, NULL, COLS, ROWS, "INAV", els, 32);
    check("INAV ground course", n == 1 && els[0].type == OSD_ELEM_HEADING &&
        els[0].value > 288.9f);

    clear_grid();
    { uint16_t g[] = {0x02,'9','9'}; put(12, 2, g, 3); }
    n = osd_elements_scan(getter, NULL, COLS, ROWS, "INAV", els, 32);
    check("INAV link quality", n == 1 && els[0].type == OSD_ELEM_LINK_QUALITY);

    // dBm is wrapped in two symbols, so unlike Betaflight nothing has to be
    // guessed from the magnitude: SYM_RSSI in front, SYM_DBM behind.
    clear_grid();
    { uint16_t g[] = {0x01,' ','-','9','4',0x13}; put(13, 2, g, 6); }
    n = osd_elements_scan(getter, NULL, COLS, ROWS, "INAV", els, 32);
    check("INAV dBm is not a percentage", n == 1 && els[0].type == OSD_ELEM_RSSI_DBM &&
        els[0].value < -93.9f);
    check("INAV dBm is one element, not two", n == 1 && els[0].width == 6);

    // The same symbol without the unit really is a percentage.
    clear_grid();
    { uint16_t g[] = {0x01,'8','5'}; put(13, 2, g, 3); }
    n = osd_elements_scan(getter, NULL, COLS, ROWS, "INAV", els, 32);
    check("INAV RSSI percent", n == 1 && els[0].type == OSD_ELEM_RSSI);

    clear_grid();
    { uint16_t g[] = {0x14,'-','6',0x12}; put(14, 2, g, 4); }
    n = osd_elements_scan(getter, NULL, COLS, ROWS, "INAV", els, 32);
    check("INAV SNR", n == 1 && els[0].type == OSD_ELEM_SNR && els[0].width == 4);

    clear_grid();
    { uint16_t g[] = {0xBC,'1','.','8'}; put(15, 2, g, 4); }
    n = osd_elements_scan(getter, NULL, COLS, ROWS, "INAV", els, 32);
    check("INAV g-force", n == 1 && els[0].type == OSD_ELEM_GFORCE &&
        els[0].value > 1.79f && els[0].value < 1.81f);

    clear_grid();
    { uint16_t g[] = {0xC2,' ','4','2',0x97}; put(16, 2, g, 5); }
    n = osd_elements_scan(getter, NULL, COLS, ROWS, "INAV", els, 32);
    check("INAV temperature", n == 1 && els[0].type == OSD_ELEM_TEMPERATURE &&
        els[0].unit == OSD_UNIT_CELSIUS);

    clear_grid();
    { uint16_t g[] = {0xC3,' ','6','8',0x97}; put(16, 2, g, 5); }
    n = osd_elements_scan(getter, NULL, COLS, ROWS, "INAV", els, 32);
    check("INAV ESC temperature", n == 1 && els[0].type == OSD_ELEM_TEMPERATURE &&
        els[0].value > 67.9f);

    clear_grid();
    { uint16_t g[] = {'1','8','0',0x22}; put(17, 2, g, 4); }
    n = osd_elements_scan(getter, NULL, COLS, ROWS, "INAV", els, 32);
    check("INAV efficiency", n == 1 && els[0].type == OSD_ELEM_EFFICIENCY &&
        els[0].unit == OSD_UNIT_KM);

    // INAV's altitude glyphs say the unit outright.
    clear_grid();
    { uint16_t g[] = {' ','1','3','9',0x76}; put(18, 2, g, 5); }
    n = osd_elements_scan(getter, NULL, COLS, ROWS, "INAV", els, 32);
    check("INAV altitude in metres", n == 1 && els[0].type == OSD_ELEM_ALTITUDE &&
        els[0].unit == OSD_UNIT_METRES);

    clear_grid();
    { uint16_t g[] = {'1','.','2',0x77}; put(18, 2, g, 4); }
    n = osd_elements_scan(getter, NULL, COLS, ROWS, "INAV", els, 32);
    check("INAV altitude scaled to km", n == 1 && els[0].type == OSD_ELEM_ALTITUDE &&
        els[0].unit == OSD_UNIT_KM);

    // INAV's direction arrows live above 0xFF, well clear of Betaflight's.
    clear_grid();
    { uint16_t g[] = {0x140}; put(19, 25, g, 1); }
    n = osd_elements_scan(getter, NULL, COLS, ROWS, "INAV", els, 32);
    check("INAV home arrow", n == 1 && els[0].type == OSD_ELEM_HOME_ARROW);

    clear_grid();
    { uint16_t g[] = {0x140}; put(19, 25, g, 1); }
    n = osd_elements_scan(getter, NULL, COLS, ROWS, "BTFL", els, 32);
    check("BTFL ignores the INAV arrow block", n == 0);

    printf("\n%s (%d failure%s)\n", fails ? "FAILURES" : "ALL PASS", fails, fails == 1 ? "" : "s");
    return fails != 0;
}

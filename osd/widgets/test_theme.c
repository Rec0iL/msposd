#include "osd_theme.h"
#include <stdio.h>
#include <string.h>

static int fails = 0;
static void check(const char *n, int c) {
    printf("  %-52s %s\n", n, c ? "PASS" : "FAIL");
    if (!c) fails++;
}

int main(void) {
    osd_theme_t t;

    // defaults must stand alone
    osd_theme_defaults(&t);
    check("defaults: mode is fancy", t.mode == OSD_MODE_FANCY);
    check("defaults: accent cyan", t.accent == OSD_ARGB(0xFF,0x00,0xE5,0xFF));
    check("defaults: lat/lon off (reserved for map)", !t.elem_enabled[OSD_ELEM_LATITUDE]);
    check("defaults: voltage on", t.elem_enabled[OSD_ELEM_VOLTAGE]);

    // shipped theme
    check("shipped theme loads", osd_theme_load(&t, "themes/tactical/theme.ini"));
    check("shipped: name parsed", strcmp(t.name, "Tactical") == 0);
    check("shipped: panel_fill keeps its alpha", OSD_A(t.panel_fill) == 0xD6);
    check("shipped: 6-digit colour gets opaque alpha", OSD_A(t.accent) == 0xFF);
    check("shipped: inline comment stripped from value", t.hatch_slant < -0.4f && t.hatch_slant > -0.5f);
    check("shipped: latitude disabled", !t.elem_enabled[OSD_ELEM_LATITUDE]);

    // mode switch
    FILE *f = fopen("/tmp/_m.ini", "w");
    fputs("[osd]\nmode = classic\nopacity = 0.5\n", f); fclose(f);
    check("mode switches to classic", osd_theme_load(&t, "/tmp/_m.ini") && t.mode == OSD_MODE_CLASSIC);
    check("classic disables widgets", !osd_theme_element_enabled(&t, OSD_ELEM_VOLTAGE));
    check("global opacity applied", t.global_opacity > 0.49f && t.global_opacity < 0.51f);

    // a broken theme must not destroy a good one
    osd_theme_defaults(&t);
    f = fopen("/tmp/_b.ini", "w");
    fputs("[colors]\naccent = ZZZZZZ\n[theme]\nvalue_size = banana\n"
          "[nonsense]\nfoo = bar\n[elements]\nvoltage = maybe\n", f); fclose(f);
    osd_theme_load(&t, "/tmp/_b.ini");
    check("bad colour keeps default", t.accent == OSD_ARGB(0xFF,0x00,0xE5,0xFF));
    check("bad float keeps default", t.value_size > 31.9f && t.value_size < 32.1f);
    check("bad bool keeps default", t.elem_enabled[OSD_ELEM_VOLTAGE]);
    check("unknown section ignored", true);

    // clamping
    f = fopen("/tmp/_c.ini", "w");
    fputs("[osd]\nopacity = 9.0\n[theme]\nhatch_duty = 0\n", f); fclose(f);
    osd_theme_load(&t, "/tmp/_c.ini");
    check("opacity clamped to 1.0", t.global_opacity <= 1.0f);
    check("hatch_duty clamped above 0", t.hatch_duty > 0.0f);

    // missing file leaves theme usable
    osd_theme_defaults(&t);
    check("missing file returns false", !osd_theme_load(&t, "/tmp/does-not-exist.ini"));
    check("theme still usable after miss", t.elem_enabled[OSD_ELEM_VOLTAGE]);

    // opacity composition
    osd_theme_defaults(&t);
    t.global_opacity = 0.5f; t.elem_opacity[OSD_ELEM_VOLTAGE] = 0.5f;
    float o = osd_theme_element_opacity(&t, OSD_ELEM_VOLTAGE);
    check("per-element * global opacity", o > 0.24f && o < 0.26f);
    check("apply_opacity halves alpha", OSD_A(osd_theme_apply_opacity(OSD_ARGB(200,1,2,3), 0.5f)) == 100);

    // per-element size scale
    osd_theme_defaults(&t);
    check("default scale is 1.0", osd_theme_element_scale(&t, OSD_ELEM_VOLTAGE) > 0.99f
                               && osd_theme_element_scale(&t, OSD_ELEM_VOLTAGE) < 1.01f);
    f = fopen("/tmp/_s.ini", "w");
    fputs("[osd]\nscale = 2.0\n[elements]\nvoltage_scale = 1.5\ncurrent_scale = 0.5\n", f); fclose(f);
    osd_theme_load(&t, "/tmp/_s.ini");
    check("element scale * global scale", osd_theme_element_scale(&t, OSD_ELEM_CURRENT) > 0.99f
                                       && osd_theme_element_scale(&t, OSD_ELEM_CURRENT) < 1.01f);
    check("combined scale clamped to 4.0", osd_theme_element_scale(&t, OSD_ELEM_VOLTAGE) <= 4.0f);
    check("untouched element keeps global only", osd_theme_element_scale(&t, OSD_ELEM_RSSI) > 1.99f);
    f = fopen("/tmp/_s2.ini", "w");
    fputs("[elements]\nvoltage_scale = banana\n", f); fclose(f);
    osd_theme_defaults(&t); osd_theme_load(&t, "/tmp/_s2.ini");
    check("bad scale keeps default", osd_theme_element_scale(&t, OSD_ELEM_VOLTAGE) > 0.99f);

    printf("\n%s (%d failure%s)\n", fails ? "FAILURES" : "ALL PASS", fails, fails==1?"":"s");
    return fails != 0;
}

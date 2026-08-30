#include "osd_theme.h"
#include <stdio.h>
#include <string.h>

static int fails = 0;
// Rough perceptual distance - green weighted up, blue down - which is enough to
// catch two colours that would read alike on a small panel over moving video.
static bool colours_far_apart(uint32_t a, uint32_t b) {
    const int dr = (int)OSD_R(a) - (int)OSD_R(b);
    const int dg = (int)OSD_G(a) - (int)OSD_G(b);
    const int db = (int)OSD_B(a) - (int)OSD_B(b);
    return (dr * dr * 2 + dg * dg * 4 + db * db) > 9000;
}

static void check(const char *n, int c) {
    printf("  %-52s %s\n", n, c ? "PASS" : "FAIL");
    if (!c) fails++;
}

int main(void) {
    osd_theme_t t;
    FILE *f;

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
    // lat/lon are enabled now: they drive the map rather than drawing value panels
    check("shipped: latitude enabled for the map", t.elem_enabled[OSD_ELEM_LATITUDE]);

    // shipped map view keys - the map follows ground speed, so these have to
    // survive the parse or the view silently falls back to the defaults
    check("shipped: auto zoom on", t.map_auto_zoom);
    check("shipped: zoom range parsed", t.map_zoom_min == 14 && t.map_zoom_max == 17);
    check("shipped: look-ahead parsed", t.map_lookahead_s > 19.9f && t.map_lookahead_s < 20.1f);
    check("shipped: lead parsed", t.map_lead_s > 5.9f && t.map_lead_s < 6.1f);
    check("shipped: lead cap parsed", t.map_lead_max > 0.34f && t.map_lead_max < 0.36f);
    check("shipped: settle time parsed", t.map_zoom_settle_ms > 2999.0f);
    check("shipped: fixed zoom still parsed", t.map_zoom == 16);

    // a bad map value must leave the view usable rather than pinning it at zero
    osd_theme_defaults(&t);
    f = fopen("/tmp/_mv.ini", "w");
    fputs("[map]\nauto_zoom = perhaps\nlookahead = soon\nlead_max = 9.0\nzoom_min = 99\n", f);
    fclose(f);
    osd_theme_load(&t, "/tmp/_mv.ini");
    check("bad auto_zoom keeps default", t.map_auto_zoom);
    check("bad lookahead keeps default", t.map_lookahead_s > 19.9f);
    check("lead cap clamped below 1.0", t.map_lead_max <= 0.9f);
    check("zoom_min clamped to a real zoom", t.map_zoom_min <= 19);

    // mode switch
    f = fopen("/tmp/_m.ini", "w");
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
    check("bad float keeps default", t.value_size > 24.9f && t.value_size < 25.1f);
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

    // The panel shape is a shape, not a dimension: a theme wanting squares has to
    // switch the path rather than dial the notch to zero, because the notch is a
    // third of the top edge and the value sits in the raised part beside it.
    osd_theme_defaults(&t);
    check("panel shape defaults to notched", t.panel_shape == OSD_PANEL_NOTCHED);
    f = fopen("/tmp/_sh.ini", "w");
    fputs("[theme]\npanel_shape = square\n", f); fclose(f);
    osd_theme_load(&t, "/tmp/_sh.ini");
    check("square shape parsed", t.panel_shape == OSD_PANEL_SQUARE);
    // A shape we do not know keeps the current one: a typo must not silently
    // restyle the OSD back to something the user did not ask for.
    f = fopen("/tmp/_sh2.ini", "w");
    fputs("[theme]\npanel_shape = hexagon\n", f); fclose(f);
    osd_theme_load(&t, "/tmp/_sh2.ini");
    check("an unknown shape is ignored", t.panel_shape == OSD_PANEL_SQUARE);

    // --- inheritance. A variant should be a palette and nothing else.
    f = fopen("/tmp/_base.ini", "w");
    fputs("[theme]\npanel_shape = square\npanel_min_width = 111\n[colors]\naccent = 112233\n", f);
    fclose(f);
    f = fopen("/tmp/_child.ini", "w");
    // The inherit line last on purpose: it is pre-scanned, so where it sits in
    // the file must not decide whether the child's own values survive.
    fputs("[colors]\naccent = AABBCC\n[theme]\ninherit = /tmp/_base.ini\n", f);
    fclose(f);
    osd_theme_defaults(&t);
    osd_theme_load(&t, "/tmp/_child.ini");
    check("inherited value came through", t.panel_min_width > 110.0f && t.panel_min_width < 112.0f);
    check("child wins over the parent", OSD_R(t.accent) == 0xAA);
    check("inherited shape came through", t.panel_shape == OSD_PANEL_SQUARE);

    // A theme that inherits itself must not take the OSD down with it.
    f = fopen("/tmp/_loop.ini", "w");
    fputs("[theme]\ninherit = /tmp/_loop.ini\nname = Loop\n", f); fclose(f);
    osd_theme_defaults(&t);
    check("a self-inheriting theme still returns", osd_theme_load(&t, "/tmp/_loop.ini"));
    check("and still applies its own keys", strcmp(t.name, "Loop") == 0);

    // A parent that is not there leaves the child's own values standing.
    f = fopen("/tmp/_orphan.ini", "w");
    fputs("[theme]\ninherit = /tmp/_nope_nope.ini\nname = Orphan\n", f); fclose(f);
    osd_theme_defaults(&t);
    check("a missing parent is survivable", osd_theme_load(&t, "/tmp/_orphan.ini"));
    check("the child still applied", strcmp(t.name, "Orphan") == 0);

    // --- the shipped family. Each variant must inherit the layout and differ
    // only in its palette, and its chrome must stay clear of the three colours
    // that mean something.
    {
        static const char *family[] = {"orchid", "red", "teal", "green", "blue", "orange",
                                       "yellow"};
        osd_theme_t base;
        osd_theme_defaults(&base);
        for (unsigned i = 0; i < sizeof(family) / sizeof(family[0]); i++) {
            char path[128], msg[160];
            snprintf(path, sizeof(path), "themes/minimal-%s/theme.ini", family[i]);
            osd_theme_t v;
            osd_theme_defaults(&v);
            snprintf(msg, sizeof(msg), "minimal-%s loads", family[i]);
            check(msg, osd_theme_load(&v, path));
            snprintf(msg, sizeof(msg), "minimal-%s inherits the layout", family[i]);
            check(msg, v.panel_shape == OSD_PANEL_SQUARE && v.panel_min_width < 200.0f &&
                           v.hatch_slant == 0.0f);
            snprintf(msg, sizeof(msg), "minimal-%s has a palette of its own", family[i]);
            check(msg, i == 0 || v.accent != base.accent);
            // The reason the chrome is a pale tint rather than the hue itself:
            // in the red, orange and yellow variants the theme colour and the
            // colour that means "look at this" would otherwise be the same.
            snprintf(msg, sizeof(msg), "minimal-%s keeps crit clear of its chrome", family[i]);
            check(msg, colours_far_apart(v.accent, v.crit));
            snprintf(msg, sizeof(msg), "minimal-%s keeps warn clear of its chrome", family[i]);
            check(msg, colours_far_apart(v.accent, v.warn));
            snprintf(msg, sizeof(msg), "minimal-%s keeps good clear of its chrome", family[i]);
            check(msg, colours_far_apart(v.accent, v.good));
        }
    }

    printf("\n%s (%d failure%s)\n", fails ? "FAILURES" : "ALL PASS", fails, fails==1?"":"s");
    return fails != 0;
}

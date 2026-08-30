// Visual check for the heading display: every style, over a still frame.
//
//   ./preview_heading <backdrop.png> [out.png]
//
// Nose 274, ground track 289, home 122 - so the crab angle and the way home are
// both visible on any style that can show a bearing.
#include "osd/widgets/osd_heading.h"
#include "libpng/lodepng.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv) {
	if (argc < 2) {
		printf("usage: %s <backdrop.png> [out.png]\n", argv[0]);
		return 2;
	}
	const char *out_path = argc > 2 ? argv[2] : "heading-preview.png";

	unsigned bw = 0, bh = 0;
	unsigned char *back = NULL;
	if (lodepng_decode32_file(&back, &bw, &bh, argv[1])) {
		printf("!! cannot read %s\n", argv[1]);
		return 1;
	}
	const int W = (int)bw, H = (int)bh;

	osd_font_t *font = osd_font_load("fonts/UbuntuMono-Regular.ttf");
	if (!font) {
		printf("!! font load failed - run from the repo root\n");
		return 1;
	}

	const osd_heading_style_t styles[] = {OSD_HEADING_BAND, OSD_HEADING_ROSE, OSD_HEADING_RING,
		OSD_HEADING_RING, OSD_HEADING_NAVBALL, OSD_HEADING_NUMERIC};
	const char *names[] = {"band", "rose", "ring", "ring flipped", "navball", "numeric"};
	const int n = (int)(sizeof(styles) / sizeof(styles[0]));

	uint8_t *sheet = calloc((size_t)W * H * n * 4, 1);

	for (int i = 0; i < n; i++) {
		uint8_t *buf = calloc((size_t)W * H * 4, 1);
		osd_surface_t s;
		osd_surface_init(&s, buf, W, H, W * 4);

		osd_heading_params_t p = {0};
		p.style = styles[i];
		p.flip = (i == 3);
		p.size = (styles[i] == OSD_HEADING_ROSE || styles[i] == OSD_HEADING_NAVBALL) ? 260.0f
				 : (styles[i] == OSD_HEADING_NUMERIC)                               ? 200.0f
																					: 620.0f;
		p.show_track = true;
		p.span_deg = 90.0f;
		p.lens = 0.62f;
		p.accent = OSD_ARGB(0xFF, 0x00, 0xE5, 0xFF);
		p.label = OSD_ARGB(0xFF, 0x4F, 0xA8, 0xC4);
		p.track = OSD_ARGB(0xFF, 0x00, 0xFF, 0x9C);
		p.home = OSD_ARGB(0xFF, 0xFF, 0xB3, 0x00);
		p.fill = OSD_ARGB(0xD6, 0x0A, 0x1A, 0x26);
		p.edge = OSD_ARGB(0xFF, 0x0E, 0x3D, 0x52);
		p.heading_deg = 274.0f;
		p.track_deg = 289.0f;
		p.track_valid = true;
		p.home_deg = 122.0f;
		p.home_valid = true;
		p.pitch_deg = -6.0f;
		p.roll_deg = 14.0f;

		osd_text_set_outline(i % 2 == 1, OSD_ARGB(0xC0, 0, 0, 0), 1);
		osd_heading_draw(&s, font, W * 0.5f, H * 0.34f, &p);
		char lbl[64];
		snprintf(lbl, sizeof(lbl), "%s%s", names[i], (i % 2 == 1) ? "  (text outline on)" : "");
		osd_text_draw(&s, font, 22.0f, 24, 40, lbl, p.accent);

		// Composite over the backdrop, as the hardware does.
		uint8_t *dst = sheet + (size_t)i * W * H * 4;
		for (size_t q = 0; q < (size_t)W * H; q++) {
			const float a = buf[q * 4 + 3] / 255.0f;
			dst[q * 4 + 0] = (uint8_t)(buf[q * 4 + 2] * a + back[q * 4 + 0] * (1 - a));
			dst[q * 4 + 1] = (uint8_t)(buf[q * 4 + 1] * a + back[q * 4 + 1] * (1 - a));
			dst[q * 4 + 2] = (uint8_t)(buf[q * 4 + 0] * a + back[q * 4 + 2] * (1 - a));
			dst[q * 4 + 3] = 255;
		}
		free(buf);
	}

	printf(lodepng_encode32_file(out_path, sheet, (unsigned)W, (unsigned)(H * n)) ? "png err\n"
																				 : "wrote it\n");
	return 0;
}

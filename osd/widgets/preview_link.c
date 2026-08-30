// Visual check for the link widget: both styles, over a still frame.
#include "osd/widgets/osd_link.h"
#include "libpng/lodepng.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv) {
	unsigned bw, bh; unsigned char *back = NULL;
	if (argc < 2 || lodepng_decode32_file(&back, &bw, &bh, argv[1])) { printf("need backdrop\n"); return 1; }
	const int W = (int)bw, H = (int)bh;
	osd_font_t *font = osd_font_load("fonts/UbuntuMono-Regular.ttf");
	if (!font) { printf("!! font\n"); return 1; }

	const bool wfb = (argc > 3 && argv[3][0] == 'w');
	osd_link_stats_t st; memset(&st, 0, sizeof(st));
	st.valid = true;
	st.antennas = 4;
	if (wfb) {
		// wfb-ng: real dBm and a real SNR per aerial, plus packet counters.
		snprintf(st.source, sizeof(st.source), "WFB-NG");
		const int r[4] = {-58, -71, -84, -63};
		const int n[4] = {18, 11, 3, 14};
		for (int i = 0; i < 4; i++) {
			st.rssi_dbm[i]=r[i]; st.rssi_valid[i]=true;
			st.snr_db[i]=n[i];   st.snr_valid[i]=true;
		}
		st.quality_pct = 97.0f; st.loss_pct = 0.4f; st.bitrate_mbps = 12.4f;
		st.channel = 149; st.freq_mhz = 5745; st.bandwidth_mhz = 20;
	} else {
		// APFPV: two adapters, two aerials each, and no SNR at all - the WiFi
		// driver does not report it. These dBm values are what PixelPilot's
		// writer produces from driver percentages of 70/55 and 41/12.
		snprintf(st.source, sizeof(st.source), "APFPV");
		const int r[4] = {-53, -62, -70, -88};
		for (int i = 0; i < 4; i++) { st.rssi_dbm[i]=r[i]; st.rssi_valid[i]=true; }
		// The driver counts no packets, so quality and loss stay absent - but the
		// throughput belongs to the stream rather than the radio, so it is
		// reported here too.
		st.quality_pct = -1.0f; st.loss_pct = -1.0f; st.bitrate_mbps = 11.4f;
		// The channel comes from the Realtek driver's rf_info. These are the
		// numbers off a real APFPV station: channel 140 at 40MHz.
		st.channel = 140; st.freq_mhz = 5700; st.bandwidth_mhz = 40;
	}

	// Three styles, with and without the per-aerial rows.
	const int N = 6;
	uint8_t *sheet = calloc((size_t)W*H*N*4, 1);
	for (int i = 0; i < N; i++) {
		uint8_t *buf = calloc((size_t)W*H*4, 1);
		osd_surface_t s; osd_surface_init(&s, buf, W, H, W*4);
		osd_link_params_t p = {0};
		const osd_link_style_t styles[3] = {
			OSD_LINK_VERTICAL, OSD_LINK_HORIZONTAL, OSD_LINK_ULTRAWIDE};
		const char *names[3] = {"vertical", "horizontal", "ultrawide"};
		p.style = styles[i % 3];
		p.scale = 1.15f;
		p.show_antennas = (i < 3);
		p.accent = OSD_ARGB(0xFF,0x00,0xE5,0xFF);
		p.label  = OSD_ARGB(0xFF,0x4F,0xA8,0xC4);
		p.good   = OSD_ARGB(0xFF,0x00,0xFF,0x9C);
		p.warn   = OSD_ARGB(0xFF,0xFF,0xB3,0x00);
		p.crit   = OSD_ARGB(0xFF,0xFF,0x3B,0x30);
		p.fill   = OSD_ARGB(0xD6,0x0A,0x1A,0x26);
		p.edge   = OSD_ARGB(0xFF,0x0E,0x3D,0x52);
		p.track  = OSD_ARGB(0xCC,0x06,0x22,0x2E);
		p.opacity = 1.0f;
		p.outline = true; p.outline_color = OSD_ARGB(0xC0,0,0,0); p.outline_px = 2.0f;
		p.pad_x = 13; p.pad_y = 11; p.chamfer = 14; p.bar_height = 16;
		p.value_size = 25; p.label_size = 11; p.label_tracking = 2.5f;

		float ww, hh; osd_link_measure(&p, &st, &ww, &hh);
		osd_link_draw(&s, font, 60.0f, 60.0f, &p, &st, false);
		char lbl[64];
		snprintf(lbl, sizeof(lbl), "%s  %s  %s  (%.0fx%.0f)", names[i % 3], st.source,
			p.show_antennas ? "aerials on" : "aerials off", ww, hh);
		osd_text_draw(&s, font, 20.0f, 60, 40, lbl, p.accent);

		uint8_t *dst = sheet + (size_t)i*W*H*4;
		for (size_t q = 0; q < (size_t)W*H; q++) {
			const float a = buf[q*4+3]/255.0f;
			dst[q*4+0]=(uint8_t)(buf[q*4+2]*a+back[q*4+0]*(1-a));
			dst[q*4+1]=(uint8_t)(buf[q*4+1]*a+back[q*4+1]*(1-a));
			dst[q*4+2]=(uint8_t)(buf[q*4+0]*a+back[q*4+2]*(1-a));
			dst[q*4+3]=255;
		}
		free(buf);
	}
	printf(lodepng_encode32_file(argc>2?argv[2]:"apfpv.png", sheet, (unsigned)W, (unsigned)(H*N)) ? "png err\n" : "ok\n");
	return 0;
}

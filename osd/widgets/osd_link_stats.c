// The stats file, on its own.
//
// Split from the drawing because the two share nothing but the struct between
// them, and because a ground station checking that it writes the format msposd
// expects should be able to link this and nothing else - not the paint layer,
// and not a font rasteriser behind it.
#include "osd_link.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------------------
// The stats file
// ---------------------------------------------------------------------------
//
// Deliberately the same shape as the theme: a plain ini, forgiving about
// unknown keys. The ground station rewrites it several times a second, so the
// parser has to tolerate reading one mid-write - it fills a local copy and only
// commits it if the file held enough to be worth showing.

const char *osd_link_default_path(void) {
	const char *env = getenv("MSPOSD_LINK_STATS");
	return (env && env[0]) ? env : "/tmp/msposd-link.ini";
}

static void trim(char *s) {
	char *p = s;
	while (*p == ' ' || *p == '\t')
		p++;
	if (p != s)
		memmove(s, p, strlen(p) + 1);
	size_t n = strlen(s);
	while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r' || s[n - 1] == '\n'))
		s[--n] = '\0';
}

bool osd_link_stats_load(const char *path, osd_link_stats_t *out, uint64_t now_ms) {
	if (!path || !out)
		return false;
	FILE *f = fopen(path, "r");
	if (!f)
		return false;

	osd_link_stats_t s;
	memset(&s, 0, sizeof(s));
	s.quality_pct = -1.0f;
	s.loss_pct = -1.0f;
	s.bitrate_mbps = -1.0f;
	snprintf(s.source, sizeof(s.source), "%s", "LINK");

	char line[160];
	bool saw_anything = false;
	while (fgets(line, sizeof(line), f)) {
		char *hash = strchr(line, ';');
		if (hash)
			*hash = '\0';
		char *eq = strchr(line, '=');
		if (!eq)
			continue;
		*eq = '\0';
		// Anything longer than these is not a key or value we know, so a
		// truncating copy is the right answer rather than a diagnostic.
		char key[64], val[80];
		strncpy(key, line, sizeof(key) - 1);
		key[sizeof(key) - 1] = '\0';
		strncpy(val, eq + 1, sizeof(val) - 1);
		val[sizeof(val) - 1] = '\0';
		trim(key);
		trim(val);
		if (!key[0])
			continue;

		if (!strcasecmp(key, "source")) {
			strncpy(s.source, val, sizeof(s.source) - 1);
			s.source[sizeof(s.source) - 1] = '\0';
			saw_anything = true;
		} else if (!strcasecmp(key, "quality")) {
			s.quality_pct = strtof(val, NULL);
			saw_anything = true;
		} else if (!strcasecmp(key, "loss")) {
			s.loss_pct = strtof(val, NULL);
			saw_anything = true;
		} else if (!strcasecmp(key, "channel")) {
			s.channel = atoi(val);
			saw_anything = true;
		} else if (!strcasecmp(key, "freq_mhz")) {
			s.freq_mhz = atoi(val);
			saw_anything = true;
		} else if (!strcasecmp(key, "bandwidth_mhz")) {
			s.bandwidth_mhz = atoi(val);
			saw_anything = true;
		} else if (!strcasecmp(key, "bitrate_mbps")) {
			s.bitrate_mbps = strtof(val, NULL);
			saw_anything = true;
		} else if (!strncasecmp(key, "ant", 3)) {
			// ant<N>_rssi / ant<N>_snr. Indices are the writer's own, so a
			// station with a gap in its antenna numbering still lands in order.
			const int idx = atoi(key + 3);
			const char *field = strchr(key, '_');
			if (!field || idx < 0 || idx >= OSD_LINK_MAX_ANTENNAS)
				continue;
			if (!strcasecmp(field, "_rssi")) {
				s.rssi_dbm[idx] = atoi(val);
				s.rssi_valid[idx] = true;
			} else if (!strcasecmp(field, "_snr")) {
				s.snr_db[idx] = atoi(val);
				s.snr_valid[idx] = true;
			} else {
				continue;
			}
			if (idx + 1 > s.antennas)
				s.antennas = idx + 1;
			saw_anything = true;
		}
	}
	fclose(f);

	if (!saw_anything)
		return false; // an empty or half-written file keeps the last good one

	s.valid = true;
	s.updated_ms = now_ms;
	*out = s;
	return true;
}

bool osd_link_stats_stale(const osd_link_stats_t *s, uint64_t now_ms, float hold_ms) {
	if (!s || !s->valid)
		return true;
	return (float)(now_ms - s->updated_ms) > hold_ms;
}


#include "osd_tiles.h"

#include "../../libpng/lodepng.h"

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifdef OSD_MAP_HTTP
	#include <curl/curl.h>
#endif

#ifdef OSD_MAP_JPEG
	#include <jpeglib.h>
	#include <setjmp.h>
#endif

#define MAX_CACHED 96 // ~24MB of RGBA at 256px; plenty for one viewport plus slack
#define MAX_QUEUE 32
#define KEY_LEN 64

typedef struct {
	char key[KEY_LEN];
	osd_tile_bitmap_t bmp;
	uint64_t last_used;
	bool valid;
} tile_slot_t;

typedef struct {
	osd_map_style_t style;
	int layer, zoom, x, y;
	char key[KEY_LEN];
} fetch_job_t;

static tile_slot_t g_cache[MAX_CACHED];
static fetch_job_t g_queue[MAX_QUEUE];
static int g_queue_len = 0;
static uint64_t g_tick = 0;

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_wake = PTHREAD_COND_INITIALIZER;
static pthread_t g_worker;
static bool g_running = false;
static char g_cache_dir[512] = "";

static int g_stat_fetched = 0, g_stat_failed = 0;
static bool g_had_error = false;

static int find_slot(const char *key) {
	for (int i = 0; i < MAX_CACHED; i++)
		if (g_cache[i].valid && strcmp(g_cache[i].key, key) == 0)
			return i;
	return -1;
}

// Least recently used, so a viewport's own tiles are never evicted by the
// tiles just outside it.
static int evict_slot(void) {
	int oldest = 0;
	uint64_t best = UINT64_MAX;
	for (int i = 0; i < MAX_CACHED; i++) {
		if (!g_cache[i].valid)
			return i;
		if (g_cache[i].last_used < best) {
			best = g_cache[i].last_used;
			oldest = i;
		}
	}
	free(g_cache[oldest].bmp.pixels);
	memset(&g_cache[oldest], 0, sizeof(g_cache[oldest]));
	return oldest;
}

// lodepng gives RGBA; the overlay is BGRA.
static void rgba_to_bgra(uint8_t *p, size_t px) {
	for (size_t i = 0; i < px; i++) {
		uint8_t r = p[i * 4 + 0];
		p[i * 4 + 0] = p[i * 4 + 2];
		p[i * 4 + 2] = r;
	}
}

#ifdef OSD_MAP_JPEG
// libjpeg's default error handler calls exit(). A corrupt tile must not take
// the OSD down, so longjmp out instead.
struct jpeg_bail {
	struct jpeg_error_mgr mgr;
	jmp_buf jump;
};

static void jpeg_on_error(j_common_ptr cinfo) {
	struct jpeg_bail *b = (struct jpeg_bail *)cinfo->err;
	longjmp(b->jump, 1);
}

// Decodes JPEG to RGBA, matching lodepng's output layout so callers do not care
// which format arrived.
static bool decode_jpeg(const uint8_t *data, size_t len, uint8_t **out, unsigned *w, unsigned *h) {
	struct jpeg_decompress_struct cinfo;
	struct jpeg_bail err;
	uint8_t *rgba = NULL;
	uint8_t *row = NULL;

	cinfo.err = jpeg_std_error(&err.mgr);
	err.mgr.error_exit = jpeg_on_error;
	if (setjmp(err.jump)) {
		jpeg_destroy_decompress(&cinfo);
		free(rgba);
		free(row);
		return false;
	}

	jpeg_create_decompress(&cinfo);
	jpeg_mem_src(&cinfo, data, (unsigned long)len);
	if (jpeg_read_header(&cinfo, TRUE) != JPEG_HEADER_OK) {
		jpeg_destroy_decompress(&cinfo);
		return false;
	}
	cinfo.out_color_space = JCS_RGB;
	jpeg_start_decompress(&cinfo);

	*w = cinfo.output_width;
	*h = cinfo.output_height;
	rgba = (uint8_t *)malloc((size_t)*w * *h * 4);
	row = (uint8_t *)malloc((size_t)*w * cinfo.output_components);
	if (!rgba || !row) {
		jpeg_destroy_decompress(&cinfo);
		free(rgba);
		free(row);
		return false;
	}

	while (cinfo.output_scanline < cinfo.output_height) {
		unsigned y = cinfo.output_scanline;
		jpeg_read_scanlines(&cinfo, &row, 1);
		for (unsigned x = 0; x < *w; x++) {
			uint8_t *d = rgba + ((size_t)y * *w + x) * 4;
			d[0] = row[x * 3 + 0];
			d[1] = row[x * 3 + 1];
			d[2] = row[x * 3 + 2];
			d[3] = 255; // imagery is opaque
		}
	}
	jpeg_finish_decompress(&cinfo);
	jpeg_destroy_decompress(&cinfo);
	free(row);
	*out = rgba;
	return true;
}
#endif

// Tile servers do not all serve PNG: OpenStreetMap does, but Esri's World
// Imagery serves JPEG. Dispatch on the magic bytes rather than trusting the
// URL or the Content-Type header.
static bool decode_tile(const uint8_t *data, size_t len, uint8_t **rgba, unsigned *w, unsigned *h) {
	if (len < 4)
		return false;

	if (data[0] == 0x89 && data[1] == 'P' && data[2] == 'N' && data[3] == 'G')
		return lodepng_decode32(rgba, w, h, data, len) == 0;

	if (data[0] == 0xFF && data[1] == 0xD8) {
#ifdef OSD_MAP_JPEG
		return decode_jpeg(data, len, rgba, w, h);
#else
		return false; // built without JPEG support: satellite imagery unavailable
#endif
	}
	return false;
}

static bool store_decoded(const char *key, uint8_t *rgba, unsigned w, unsigned h) {
	rgba_to_bgra(rgba, (size_t)w * h);
	pthread_mutex_lock(&g_lock);
	int slot = find_slot(key);
	if (slot < 0)
		slot = evict_slot();
	else
		free(g_cache[slot].bmp.pixels);
	snprintf(g_cache[slot].key, KEY_LEN, "%s", key);
	g_cache[slot].bmp.pixels = rgba;
	g_cache[slot].bmp.width = (int)w;
	g_cache[slot].bmp.height = (int)h;
	g_cache[slot].last_used = ++g_tick;
	g_cache[slot].valid = true;
	pthread_mutex_unlock(&g_lock);
	return true;
}

static void cache_path(const char *key, char *out, size_t n) {
	snprintf(out, n, "%s/%s", g_cache_dir, key);
}

static bool load_from_disk(const char *key) {
	if (!g_cache_dir[0])
		return false;
	char path[640];
	cache_path(key, path, sizeof(path));

	FILE *f = fopen(path, "rb");
	if (!f)
		return false;
	fseek(f, 0, SEEK_END);
	long sz = ftell(f);
	fseek(f, 0, SEEK_SET);
	if (sz <= 0) {
		fclose(f);
		return false;
	}
	uint8_t *raw = (uint8_t *)malloc((size_t)sz);
	if (!raw || fread(raw, 1, (size_t)sz, f) != (size_t)sz) {
		fclose(f);
		free(raw);
		return false;
	}
	fclose(f);

	unsigned char *rgba = NULL;
	unsigned w = 0, h = 0;
	bool ok = decode_tile(raw, (size_t)sz, &rgba, &w, &h);
	free(raw);
	if (!ok)
		return false;
	return store_decoded(key, rgba, w, h);
}

#ifdef OSD_MAP_HTTP
typedef struct {
	uint8_t *data;
	size_t len;
} buffer_t;

static size_t on_data(void *ptr, size_t size, size_t nmemb, void *userp) {
	buffer_t *b = (buffer_t *)userp;
	size_t add = size * nmemb;
	uint8_t *grown = realloc(b->data, b->len + add);
	if (!grown)
		return 0;
	b->data = grown;
	memcpy(b->data + b->len, ptr, add);
	b->len += add;
	return add;
}

static bool fetch_tile(const fetch_job_t *job) {
	char url[512];
	if (!osd_map_tile_url(job->style, job->layer, job->zoom, job->x, job->y, url, sizeof(url)))
		return false;

	CURL *curl = curl_easy_init();
	if (!curl)
		return false;

	buffer_t buf = {0};
	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, on_data);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 8L);
	// OpenStreetMap's tile usage policy requires an identifying User-Agent and
	// refuses generic ones. Being a good citizen here is what keeps access.
	curl_easy_setopt(curl, CURLOPT_USERAGENT, "msposd-osd-map/1.0 (+https://github.com/OpenIPC/msposd)");

	CURLcode rc = curl_easy_perform(curl);
	long status = 0;
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
	curl_easy_cleanup(curl);

	if (rc != CURLE_OK || status != 200 || buf.len == 0) {
		free(buf.data);
		return false;
	}

	unsigned char *rgba = NULL;
	unsigned w = 0, h = 0;
	bool decoded = decode_tile(buf.data, buf.len, &rgba, &w, &h);
	if (decoded && g_cache_dir[0]) {
		// Cache the original bytes, not our decode, so the file stays a valid
		// PNG that other tools can read.
		char path[640];
		cache_path(job->key, path, sizeof(path));
		FILE *f = fopen(path, "wb");
		if (f) {
			fwrite(buf.data, 1, buf.len, f);
			fclose(f);
		}
	}
	free(buf.data);
	if (!decoded) {
		free(rgba);
		return false;
	}
	return store_decoded(job->key, rgba, w, h);
}
#else
static bool fetch_tile(const fetch_job_t *job) {
	(void)job;
	return false; // built without HTTP support: disk cache only
}
#endif

static void *worker_main(void *arg) {
	(void)arg;
	for (;;) {
		pthread_mutex_lock(&g_lock);
		while (g_running && g_queue_len == 0)
			pthread_cond_wait(&g_wake, &g_lock);
		if (!g_running) {
			pthread_mutex_unlock(&g_lock);
			return NULL;
		}
		fetch_job_t job = g_queue[0];
		memmove(&g_queue[0], &g_queue[1], sizeof(fetch_job_t) * (size_t)(g_queue_len - 1));
		g_queue_len--;
		pthread_mutex_unlock(&g_lock);

		bool ok = load_from_disk(job.key) || fetch_tile(&job);

		pthread_mutex_lock(&g_lock);
		if (ok) {
			g_stat_fetched++;
		} else {
			g_stat_failed++;
			g_had_error = true;
		}
		pthread_mutex_unlock(&g_lock);
	}
}

bool osd_tiles_init(const char *cache_dir) {
	if (g_running)
		return true;

	if (cache_dir && *cache_dir) {
		snprintf(g_cache_dir, sizeof(g_cache_dir), "%s", cache_dir);
		if (mkdir(g_cache_dir, 0755) != 0 && errno != EEXIST) {
			printf("Map tile cache unavailable at %s, continuing without it\n", g_cache_dir);
			g_cache_dir[0] = '\0';
		}
	}

#ifdef OSD_MAP_HTTP
	curl_global_init(CURL_GLOBAL_DEFAULT);
#endif

	g_running = true;
	if (pthread_create(&g_worker, NULL, worker_main, NULL) != 0) {
		g_running = false;
		return false;
	}
	return true;
}

void osd_tiles_shutdown(void) {
	if (!g_running)
		return;
	pthread_mutex_lock(&g_lock);
	g_running = false;
	pthread_cond_broadcast(&g_wake);
	pthread_mutex_unlock(&g_lock);
	pthread_join(g_worker, NULL);

	for (int i = 0; i < MAX_CACHED; i++) {
		free(g_cache[i].bmp.pixels);
		memset(&g_cache[i], 0, sizeof(g_cache[i]));
	}
	g_queue_len = 0;
#ifdef OSD_MAP_HTTP
	curl_global_cleanup();
#endif
}

const osd_tile_bitmap_t *osd_tiles_get(osd_map_style_t style, int layer, int zoom, int x, int y) {
	char key[KEY_LEN];
	if (!osd_map_tile_key(style, layer, zoom, x, y, key, sizeof(key)))
		return NULL;

	pthread_mutex_lock(&g_lock);
	int slot = find_slot(key);
	if (slot >= 0) {
		g_cache[slot].last_used = ++g_tick;
		const osd_tile_bitmap_t *bmp = &g_cache[slot].bmp;
		pthread_mutex_unlock(&g_lock);
		return bmp;
	}

	// Not cached. Queue it unless it is already queued or the queue is full;
	// dropping a request is fine, the next frame will ask again.
	bool queued = false;
	for (int i = 0; i < g_queue_len; i++)
		if (strcmp(g_queue[i].key, key) == 0)
			queued = true;
	if (!queued && g_queue_len < MAX_QUEUE && g_running) {
		fetch_job_t *job = &g_queue[g_queue_len++];
		job->style = style;
		job->layer = layer;
		job->zoom = zoom;
		job->x = x;
		job->y = y;
		snprintf(job->key, KEY_LEN, "%s", key);
		pthread_cond_signal(&g_wake);
	}
	pthread_mutex_unlock(&g_lock);
	return NULL;
}

bool osd_tiles_had_error(void) {
	pthread_mutex_lock(&g_lock);
	bool e = g_had_error;
	pthread_mutex_unlock(&g_lock);
	return e;
}

void osd_tiles_stats(int *cached, int *queued, int *fetched, int *failed) {
	pthread_mutex_lock(&g_lock);
	if (cached) {
		int n = 0;
		for (int i = 0; i < MAX_CACHED; i++)
			if (g_cache[i].valid)
				n++;
		*cached = n;
	}
	if (queued)
		*queued = g_queue_len;
	if (fetched)
		*fetched = g_stat_fetched;
	if (failed)
		*failed = g_stat_failed;
	pthread_mutex_unlock(&g_lock);
}

#define MAX_STATUS_MSG_LEN 500

extern char current_fc_identifier[4];

uint64_t get_time_ms();

/// Overrides where the widget theme is read from. Pass an absolute path when
/// something other than msposd owns the file - a settings front-end editing it
/// live, for instance. Takes effect on the next frame.
void osd_set_theme_path(const char *path);

/// Writes the composed RGBA overlay to `path` as raw bgra frames at `fps`, for
/// compositing onto recorded video. `path` is normally a fifo with an encoder
/// on the other end - the frames are uncompressed and large.
void osd_record_overlay(const char *path, int fps);

/// Called by the render loop once a frame is fully composed, so the recorder
/// never captures one mid-compose.
void osd_record_publish_frame(void);

/// Mirrors one artificial-horizon line into the recording. On this platform the
/// AHI is drawn with cairo onto the window and never enters the overlay buffer,
/// so a capture would otherwise have an empty centre.
void osd_record_mirror_line(int x0, int y0, int x1, int y1, uint32_t rgba, double thickness);

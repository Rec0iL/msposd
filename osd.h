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

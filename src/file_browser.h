#pragma once
#include <stdbool.h>

// Portable file browser: blocking on desktop, async on web.
//
// Load flow (both platforms):
//   file_browser_open(title, filter)          -- trigger
//   const char *p = file_browser_poll()       -- check each frame; non-NULL = path ready
//
// Save flow:
//   file_browser_save_as(title, default_name) -- desktop: opens save dialog; web: returns default immediately
//   const char *p = file_browser_poll()       -- get chosen path
//   tracker_save(song, p)                     -- caller writes the file
//   file_browser_download(p, "song.rpt")      -- web: triggers browser download; desktop: no-op
//
// filter: space-separated glob patterns e.g. "*.sf2 *.SF2"

void        file_browser_open(const char *title, const char *filter);
void        file_browser_save_as(const char *title, const char *default_name);
void        file_browser_download(const char *fs_path, const char *suggested_name);
const char *file_browser_poll(void);
void        file_browser_tick(void);
void        file_browser_draw(void);
bool        file_browser_active(void);

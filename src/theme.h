#pragma once
#include "raylib.h"
#include "tracker.h"

// M8-inspired default color palette. All of these are mutable globals (not
// compile-time constants) so a theme file loaded at runtime via `--theme`
// can override them. See theme.c for the loader and the on-disk format.
extern Color C_BG;
extern Color C_BG_ALT;
extern Color C_CURSOR;
extern Color C_CURSOR2;
extern Color C_SEP;
extern Color C_TEXT;
extern Color C_DIM;
extern Color C_HEADER;
extern Color C_NOTE;
extern Color C_NOTE_OFF;
extern Color C_VEL;
extern Color C_INST;
extern Color C_FX;
extern Color C_PLAY;
extern Color C_STATUS;
extern Color C_TITLE;
extern Color C_EDIT_TAG;

// File browser accents that don't share a value with any color above.
extern Color C_FB_HEADER;
extern Color C_FB_DIM;
extern Color C_FB_INPUT_BG;

// Indexed by track (0-F) in the pattern screen and by lane (0..SONG_CHANNELS-1) in the song screen.
extern Color CH_COLORS[PATTERN_TRACKS];

// Loads a theme file (key=RRGGBB lines, see theme.c for the format) and
// overwrites the matching globals above. Returns false (leaving all colors
// untouched) if the file can't be opened.
bool theme_load(const char* path);

// Like theme_load, but silent (no stderr warning) if the file is missing.
// Used for the implicit default theme.ptt lookup in the start directory.
bool theme_load_default(const char* path);

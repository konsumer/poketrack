#pragma once
#include "input.h"
#include "raylib.h"

// Draws a small icon for btn in a size×size box at (x,y): a colored gamepad
// glyph if a gamepad is connected, otherwise a keyboard key-cap. Returns the
// width consumed (always `size`).
int icon_draw(TrackerButton btn, int x, int y, int size, Color fg);

// Draws a hint line containing inline {TOKEN} icon references (token names
// match the TrackerButton enum, e.g. "{OK}+{UP}/{DOWN}: set pattern#") at
// (x,y). Plain text runs are drawn with DrawText at `font_size`; icons are
// sized to font_size and baseline-aligned with the text.
void hint_draw(const char* fmt, int x, int y, int font_size, Color color);

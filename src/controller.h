#pragma once
#include "raylib.h"

// Optional on-screen SNES pad, drawn in a band under the tracker view so a
// screen recording shows which buttons were pressed. Enabled with
// `--controller`.

// Band height for a window `win_w` logical units wide. The band lives in the
// same logical space as WIN_W/WIN_H, so the whole layout scales as one.
int controller_height(int win_w);

// Decodes the embedded artwork. Call once, after InitWindow.
void controller_init(void);
void controller_unload(void);

// Draws the pad centered in `band` (window space, not the render texture),
// lighting whichever buttons input.c reports as held this frame.
void controller_draw(Rectangle band);

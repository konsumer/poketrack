#pragma once
#include <stdbool.h>

// All physical buttons mapped to logical (semantic) tracker actions.
// Keyboard defaults match RetroArch SNES layout. Gamepad face buttons (OK/NO/
// ALL/NONE) are remapped per-controller so the *label* stays fixed (e.g. OK is
// always "A") while the *physical position* follows the pad's own brand
// convention — Nintendo pads put A east, Xbox/PlayStation-style pads
// (including Steam Deck) put A south. See input.c raw_down().
typedef enum {
  BTN_UP = 0,
  BTN_DOWN,
  BTN_LEFT,
  BTN_RIGHT,
  BTN_OK,      // X key / gamepad "A" — confirm, enter
  BTN_NO,      // Z key / gamepad "B" — back, delete
  BTN_ALL,     // S key / gamepad "X" — apply to all / fill
  BTN_NONE,    // A key / gamepad "Y" — clear / alt
  BTN_PREV,    // Q key / left shoulder
  BTN_NEXT,    // W key / right shoulder
  BTN_PLAY,    // Enter / gamepad middle-right (start)
  BTN_SCREEN,  // RShift / gamepad middle-left (select) — held to switch context
  BTN_COUNT,
} TrackerButton;

void input_update(void);
bool input_pressed(TrackerButton btn);     // went down this frame
bool input_released(TrackerButton btn);    // went up this frame
bool input_held(TrackerButton btn);        // currently down
int input_held_frames(TrackerButton btn);  // frames held (0 if not held)

// True if a gamepad is connected — UI uses this to choose gamepad vs keyboard icons.
bool input_gamepad_connected(void);

// Raylib KEY_* code bound to btn (for drawing keyboard key-cap icons).
int input_kb_key(TrackerButton btn);

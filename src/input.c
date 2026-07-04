#include "input.h"

#include <ctype.h>
#include <string.h>

#include "raylib.h"

// RetroArch SNES keyboard defaults
static const int KB_MAP[BTN_COUNT] = {
    [BTN_UP] = KEY_UP,
    [BTN_DOWN] = KEY_DOWN,
    [BTN_LEFT] = KEY_LEFT,
    [BTN_RIGHT] = KEY_RIGHT,
    [BTN_OK] = KEY_X,
    [BTN_NO] = KEY_Z,
    [BTN_ALL] = KEY_S,
    [BTN_NONE] = KEY_A,
    [BTN_PREV] = KEY_Q,
    [BTN_NEXT] = KEY_W,
    [BTN_PLAY] = KEY_ENTER,
    [BTN_SCREEN] = KEY_RIGHT_SHIFT,
};

// Non-face gamepad buttons: physical position is the same across brands.
static const int GP_MAP[BTN_COUNT] = {
    [BTN_UP] = GAMEPAD_BUTTON_LEFT_FACE_UP,
    [BTN_DOWN] = GAMEPAD_BUTTON_LEFT_FACE_DOWN,
    [BTN_LEFT] = GAMEPAD_BUTTON_LEFT_FACE_LEFT,
    [BTN_RIGHT] = GAMEPAD_BUTTON_LEFT_FACE_RIGHT,
    [BTN_PREV] = GAMEPAD_BUTTON_LEFT_TRIGGER_1,
    [BTN_NEXT] = GAMEPAD_BUTTON_RIGHT_TRIGGER_1,
    [BTN_PLAY] = GAMEPAD_BUTTON_MIDDLE_RIGHT,
    [BTN_SCREEN] = GAMEPAD_BUTTON_MIDDLE_LEFT,
};

// Face buttons (OK/NO/ALL/NONE): physical position swaps by brand so the
// printed label stays correct. Xbox/PlayStation/Steam Deck put "A" south;
// Nintendo puts "A" east. Indexed [BTN_OK-BTN_OK .. BTN_NONE-BTN_OK].
static const int GP_FACE_XBOX[4] = {
    GAMEPAD_BUTTON_RIGHT_FACE_DOWN,   // OK  (A)
    GAMEPAD_BUTTON_RIGHT_FACE_RIGHT,  // NO  (B)
    GAMEPAD_BUTTON_RIGHT_FACE_LEFT,   // ALL (X)
    GAMEPAD_BUTTON_RIGHT_FACE_UP,     // NONE(Y)
};
static const int GP_FACE_NINTENDO[4] = {
    GAMEPAD_BUTTON_RIGHT_FACE_RIGHT,  // OK  (A)
    GAMEPAD_BUTTON_RIGHT_FACE_DOWN,   // NO  (B)
    GAMEPAD_BUTTON_RIGHT_FACE_UP,     // ALL (X)
    GAMEPAD_BUTTON_RIGHT_FACE_LEFT,   // NONE(Y)
};

static bool prev[BTN_COUNT];
static bool curr[BTN_COUNT];
static int hframes[BTN_COUNT];

// Case-insensitive substring search (no strcasestr on MSVC).
static bool name_has(const char *name, const char *needle) {
  if (!name)
    return false;
  size_t nlen = strlen(needle);
  for (const char *p = name; *p; p++) {
    size_t i = 0;
    while (i < nlen && p[i] && (tolower((unsigned char)p[i]) == tolower((unsigned char)needle[i])))
      i++;
    if (i == nlen)
      return true;
  }
  return false;
}

static bool is_nintendo_layout(int gp) {
  const char *name = GetGamepadName(gp);
  return name_has(name, "nintendo") || name_has(name, "switch") || name_has(name, "joy-con") ||
         name_has(name, "joycon");
}

static bool raw_down(TrackerButton btn) {
  if (IsKeyDown(KB_MAP[btn]))
    return true;
  if (btn == BTN_SCREEN && IsKeyDown(KEY_LEFT_SHIFT))
    return true;
  if (!IsGamepadAvailable(0))
    return false;
  if (btn >= BTN_OK && btn <= BTN_NONE) {
    const int *face = is_nintendo_layout(0) ? GP_FACE_NINTENDO : GP_FACE_XBOX;
    return IsGamepadButtonDown(0, face[btn - BTN_OK]);
  }
  return IsGamepadButtonDown(0, GP_MAP[btn]);
}

void input_update(void) {
  memcpy(prev, curr, sizeof(curr));
  for (int i = 0; i < BTN_COUNT; i++) {
    curr[i] = raw_down((TrackerButton)i);
    hframes[i] = curr[i] ? hframes[i] + 1 : 0;
  }
}

bool input_pressed(TrackerButton btn) { return curr[btn] && !prev[btn]; }
bool input_released(TrackerButton btn) { return !curr[btn] && prev[btn]; }
bool input_held(TrackerButton btn) { return curr[btn]; }
int input_held_frames(TrackerButton btn) { return hframes[btn]; }

bool input_gamepad_connected(void) {
#ifdef FORCE_GAMEPAD_ICONS
  return true;  // test gamepad icon rendering without a physical controller
#else
  return IsGamepadAvailable(0);
#endif
}
int input_kb_key(TrackerButton btn) { return KB_MAP[btn]; }

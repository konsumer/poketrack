#include "controller.h"

#include "controller_png.h"
#include "input.h"
#include "theme.h"

// Every shape below is in pixels of the embedded artwork, measured off it
// directly, and scaled to wherever the pad lands on screen.
#define ART_W 848.0f
#define ART_H 371.0f

// Band height as a fraction of window width, and how much of the band the pad
// fills. Together these set how big the pad reads in a recording — the pad is
// letterboxed inside the band, so bumping BAND_FRAC alone makes it larger.
#define BAND_FRAC 0.32f
#define PAD_FILL 0.94f

// Idle brightness of the artwork. Everything sits dimmed so a pressed button,
// drawn at full strength, pops without needing an outline that would fight
// with the art.
static const Color C_IDLE = {0xC4, 0xC4, 0xC4, 0xFF};
// Buttons the artwork draws in dark gray (d-pad, start/select, shoulders) have
// no color of their own to brighten, so they light up to near-white.
static const Color C_LIT = {0xF6, 0xF6, 0xFC, 0xFF};
static const Color C_RING = {0xFF, 0xFF, 0xFF, 0xFF};  // ring around a lit face button

static Texture2D g_tex;
static bool g_loaded;

// Artwork pixels -> window pixels.
typedef struct {
  float ox, oy, s;
} Xf;

static Rectangle rr(Xf t, float x, float y, float w, float h) {
  return (Rectangle){t.ox + x * t.s, t.oy + y * t.s, w * t.s, h * t.s};
}

static Vector2 vv(Xf t, float x, float y) {
  return (Vector2){t.ox + x * t.s, t.oy + y * t.s};
}

// The d-pad cross, split at the hub so each arm lights on its own. The hub
// itself lights whenever any direction is held, which keeps a single press
// looking like one continuous rocker rather than a detached stub.
static const struct {
  TrackerButton btn;
  float x, y, w, h;
} DPAD[4] = {
    {BTN_UP, 140, 118, 50, 52},
    {BTN_DOWN, 140, 220, 50, 52},
    {BTN_LEFT, 89, 170, 51, 50},
    {BTN_RIGHT, 190, 170, 52, 50},
};
#define DPAD_HUB_X 140
#define DPAD_HUB_Y 170
#define DPAD_HUB_S 50

// Face buttons light up to the artwork's own cap colors, at full strength.
// The colors are spelled out here rather than kept in named `static const
// Color` constants: a const variable is not a constant expression in C, so
// MSVC rejects one inside this file-scope initializer (C2099) where gcc and
// clang quietly fold it — which is how the Windows build broke.
static const struct {
  TrackerButton btn;
  float cx, cy, r;
  Color lit;
} FACE[4] = {
    {BTN_ALL, 670, 120, 27, {0x1E, 0x7B, 0xE8, 0xFF}},   // X — north, blue
    {BTN_NONE, 601, 189, 27, {0x00, 0xD1, 0x8A, 0xFF}},  // Y — west, green
    {BTN_OK, 753, 197, 26, {0xF0, 0x37, 0x4F, 0xFF}},    // A — east, red
    {BTN_NO, 684, 267, 26, {0xFB, 0xB0, 0x3B, 0xFF}},    // B — south, orange
};

// Start/select are slanted capsules in the artwork — a thick line with round
// caps traces them closely enough at this size.
static const struct {
  TrackerButton btn;
  float x0, y0, x1, y1;
} BARS[2] = {
    {BTN_SCREEN, 343, 246, 378, 204},  // SELECT
    {BTN_PLAY, 441, 246, 476, 204},    // START
};
#define BAR_R 8.5f

// Only a thin curved sliver of each shoulder clears the shell, so these cover
// the straight part of that sliver and stop short of where it arcs away —
// a wider box would spill highlight onto the transparent background.
static const struct {
  TrackerButton btn;
  float x, y, w, h;
} SHOULDERS[2] = {
    {BTN_PREV, 150, 1, 113, 17},
    {BTN_NEXT, 585, 1, 113, 17},
};

int controller_height(int win_w) {
  int h = (int)(win_w * BAND_FRAC);
  return h < 48 ? 48 : h;
}

void controller_init(void) {
  if (g_loaded)
    return;
  Image img = LoadImageFromMemory(".png", CONTROLLER_PNG, CONTROLLER_PNG_LEN);
  g_tex = LoadTextureFromImage(img);
  UnloadImage(img);
  // The pad is always drawn smaller than the artwork, so mipmaps (not just
  // bilinear) are what keep the thin outlines from shimmering.
  GenTextureMipmaps(&g_tex);
  SetTextureFilter(g_tex, TEXTURE_FILTER_TRILINEAR);
  g_loaded = true;
}

void controller_unload(void) {
  if (!g_loaded)
    return;
  UnloadTexture(g_tex);
  g_loaded = false;
}

void controller_draw(Rectangle band) {
  DrawRectangleRec(band, C_BG);
  DrawLineV((Vector2){band.x, band.y}, (Vector2){band.x + band.width, band.y}, C_SEP);
  if (!g_loaded)
    return;

  float sx = band.width * PAD_FILL / ART_W;
  float sy = band.height * PAD_FILL / ART_H;
  Xf t = {0, 0, sx < sy ? sx : sy};
  t.ox = band.x + (band.width - ART_W * t.s) / 2.0f;
  t.oy = band.y + (band.height - ART_H * t.s) / 2.0f;

  DrawTexturePro(g_tex, (Rectangle){0, 0, ART_W, ART_H}, rr(t, 0, 0, ART_W, ART_H),
                 (Vector2){0, 0}, 0.0f, C_IDLE);

  bool any_dir = false;
  for (int i = 0; i < 4; i++) {
    if (!input_held(DPAD[i].btn))
      continue;
    any_dir = true;
    DrawRectangleRounded(rr(t, DPAD[i].x, DPAD[i].y, DPAD[i].w, DPAD[i].h), 0.25f, 8, C_LIT);
  }
  if (any_dir)
    DrawRectangleRec(rr(t, DPAD_HUB_X, DPAD_HUB_Y, DPAD_HUB_S, DPAD_HUB_S), C_LIT);

  for (int i = 0; i < 4; i++) {
    if (!input_held(FACE[i].btn))
      continue;
    Vector2 c = vv(t, FACE[i].cx, FACE[i].cy);
    DrawCircleV(c, FACE[i].r * t.s, FACE[i].lit);
    DrawRing(c, FACE[i].r * t.s, (FACE[i].r + 3.0f) * t.s, 0.0f, 360.0f, 32, C_RING);
  }

  for (int i = 0; i < 2; i++) {
    if (!input_held(BARS[i].btn))
      continue;
    Vector2 a = vv(t, BARS[i].x0, BARS[i].y0), b = vv(t, BARS[i].x1, BARS[i].y1);
    DrawLineEx(a, b, 2.0f * BAR_R * t.s, C_LIT);
    DrawCircleV(a, BAR_R * t.s, C_LIT);
    DrawCircleV(b, BAR_R * t.s, C_LIT);
  }

  for (int i = 0; i < 2; i++) {
    if (!input_held(SHOULDERS[i].btn))
      continue;
    DrawRectangleRounded(
        rr(t, SHOULDERS[i].x, SHOULDERS[i].y, SHOULDERS[i].w, SHOULDERS[i].h), 0.7f, 8, C_LIT);
  }
}

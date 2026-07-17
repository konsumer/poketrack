#include "icons.h"

#include <string.h>

// Face-button colors match the physical cap color on most pads.
static const Color C_A = {0x30, 0xC0, 0x50, 0xFF};       // OK    — green
static const Color C_B = {0xE0, 0x40, 0x40, 0xFF};       // NO    — red
static const Color C_X = {0x40, 0x90, 0xE0, 0xFF};       // ALL   — blue
static const Color C_Y = {0xE0, 0xC0, 0x30, 0xFF};       // NONE  — yellow
static const Color C_CHROME = {0x50, 0x50, 0x64, 0xFF};  // shoulder/start/select fill
static const Color C_KEYCAP = {0x20, 0x20, 0x2c, 0xFF};  // keyboard key-cap fill
// Labels/glyphs are always drawn at fixed brightness — legible regardless of
// the (often dim) color the surrounding hint text uses.
static const Color C_GLYPH_LIGHT = {0xE8, 0xE8, 0xF4, 0xFF};  // on dark chrome
static const Color C_GLYPH_DARK = {0x10, 0x10, 0x14, 0xFF};   // on bright face color

static const char* face_label(TrackerButton btn) {
  switch (btn) {
    case BTN_OK:
      return "A";
    case BTN_NO:
      return "B";
    case BTN_ALL:
      return "X";
    case BTN_NONE:
      return "Y";
    default:
      return "?";
  }
}

static Color face_color(TrackerButton btn) {
  switch (btn) {
    case BTN_OK:
      return C_A;
    case BTN_NO:
      return C_B;
    case BTN_ALL:
      return C_X;
    case BTN_NONE:
      return C_Y;
    default:
      return GRAY;
  }
}

// Hand-tune these against a real controller (nobody on this end has one) and
// rebuild — positive X moves the glyph right, positive Y moves it down.
#define FACE_LABEL_DX 0
#define FACE_LABEL_DY 0

// Extra size for every gamepad glyph (circles/shoulder/play/select) — they
// read as too small/cramped at the plain hint-text row height.
#define GAMEPAD_SIZE_BOOST 4

// DrawText's y is the top of the glyph box, and that box is taller than the
// visible ink (the default font pads above/below) — so centering by `fs`
// alone lands low. MeasureTextEx reports the font's real render box, which is
// what actually needs to straddle (cx, cy).
static void draw_centered_text_ex(const char* s, int cx, int cy, int fs, Color c, int dx, int dy) {
  Vector2 dim = MeasureTextEx(GetFontDefault(), s, (float)fs, 1.0f);
  DrawText(s, cx - (int)(dim.x / 2) + dx, cy - (int)(dim.y / 2) + dy, fs, c);
}

static void draw_centered_text(const char* s, int cx, int cy, int fs, Color c) {
  draw_centered_text_ex(s, cx, cy, fs, c, 0, 0);
}

static void draw_arrow(TrackerButton btn, int x, int y, int size) {
  DrawRectangle(x, y, size, size, C_KEYCAP);
  Color c = C_GLYPH_LIGHT;
  int cx = x + size / 2, cy = y + size / 2;
  int r = size / 2 - 2;
  if (r < 2)
    r = 2;
  // DrawTriangle culls clockwise-wound triangles, so every direction below
  // is ordered apex, then the two base points, to keep the same winding.
  Vector2 p1, p2, p3;
  switch (btn) {
    case BTN_UP:
      p1 = (Vector2){cx, cy - r};
      p2 = (Vector2){cx - r, cy + r};
      p3 = (Vector2){cx + r, cy + r};
      break;
    case BTN_DOWN:
      p1 = (Vector2){cx, cy + r};
      p2 = (Vector2){cx + r, cy - r};
      p3 = (Vector2){cx - r, cy - r};
      break;
    case BTN_LEFT:
      p1 = (Vector2){cx - r, cy};
      p2 = (Vector2){cx + r, cy + r};
      p3 = (Vector2){cx + r, cy - r};
      break;
    default:  // BTN_RIGHT
      p1 = (Vector2){cx + r, cy};
      p2 = (Vector2){cx - r, cy - r};
      p3 = (Vector2){cx - r, cy + r};
      break;
  }
  DrawTriangle(p1, p2, p3, c);
}

// Bent-arrow "return" glyph — the default raylib font has no U+23CE (⏎), so
// draw it: horizontal stroke down to a short vertical, capped with an
// arrowhead pointing back left. Reads as "Enter" at any size, one glyph wide.
static void draw_return_glyph(int cx, int cy, int r, Color c) {
  float x0 = cx + r * 0.55f, y0 = cy - r * 0.45f;
  float y1 = cy + r * 0.15f;
  float x1 = cx - r * 0.15f;
  DrawLineEx((Vector2){x0, y0}, (Vector2){x0, y1}, 1.1f, c);
  DrawLineEx((Vector2){x0, y1}, (Vector2){x1, y1}, 1.1f, c);
  DrawTriangle((Vector2){x1, y1 - r * 0.35f}, (Vector2){x1, y1 + r * 0.35f},
               (Vector2){x1 - r * 0.5f, y1}, c);
}

// Short label for a raylib KEY_* code, for keyboard key-cap icons.
static const char* key_label(int key) {
  switch (key) {
    case KEY_X:
      return "X";
    case KEY_Z:
      return "Z";
    case KEY_S:
      return "S";
    case KEY_A:
      return "A";
    case KEY_Q:
      return "Q";
    case KEY_W:
      return "W";
    case KEY_RIGHT_SHIFT:
      return "SH";
    default:
      return "?";
  }
}

static void draw_keycap(TrackerButton btn, int x, int y, int size) {
  DrawRectangle(x, y, size, size, C_KEYCAP);
  int key = input_kb_key(btn);
  if (key == KEY_ENTER) {
    draw_return_glyph(x + size / 2, y + size / 2 + 1, size / 2, C_GLYPH_LIGHT);
    return;
  }
  const char* lbl = key_label(key);
  int fs = strlen(lbl) > 1 ? size - 4 : size - 2;
  if (fs < 5)
    fs = 5;
  draw_centered_text(lbl, x + size / 2, y + size / 2, fs, C_GLYPH_LIGHT);
}

static void draw_face(TrackerButton btn, int x, int y, int size) {
  int cx = x + size / 2, cy = y + size / 2;
  DrawCircle(cx, cy, (float)(size / 2), face_color(btn));
  draw_centered_text_ex(face_label(btn), cx, cy, size - 3, C_GLYPH_DARK, FACE_LABEL_DX,
                        FACE_LABEL_DY);
}

static void draw_shoulder(TrackerButton btn, int x, int y, int size) {
  DrawRectangle(x, y, size, size, C_CHROME);
  draw_centered_text(btn == BTN_PREV ? "L" : "R", x + size / 2, y + size / 2, size - 2,
                     C_GLYPH_LIGHT);
}

static void draw_play(int x, int y, int size) {
  DrawRectangle(x, y, size, size, C_CHROME);
  int cx = x + size / 2, cy = y + size / 2, tr = size / 3;
  DrawTriangle((Vector2){cx - tr / 2, cy - tr}, (Vector2){cx - tr / 2, cy + tr},
               (Vector2){cx + tr, cy}, C_GLYPH_LIGHT);
}

static void draw_screen(int x, int y, int size) {
  DrawRectangle(x, y, size, size, C_CHROME);
  int bw = size - 6;
  if (bw < 3)
    bw = 3;
  int bx = x + (size - bw) / 2;
  for (int i = 0; i < 3; i++)
    DrawRectangle(bx, y + size / 2 - 3 + i * 3, bw, 1, C_GLYPH_LIGHT);
}

int icon_draw(TrackerButton btn, int x, int y, int size, Color fg) {
  (void)fg;  // icon chrome/glyphs use fixed high-contrast colors, not the (often dim) hint text color
  if (btn == BTN_UP || btn == BTN_DOWN || btn == BTN_LEFT || btn == BTN_RIGHT) {
    draw_arrow(btn, x, y, size);
    return size;
  }
  bool gp = input_gamepad_connected();
  // Gamepad glyphs (circles/shoulder boxes/play/select) all read poorly at
  // exactly the text row height, so every one of them gets the same size
  // boost and the same upward shift to stay centered on the text baseline —
  // otherwise buttons that skip the boost sit lower than the ones that don't.
  int gsize = size + GAMEPAD_SIZE_BOOST;
  int gy = y - GAMEPAD_SIZE_BOOST / 2;
  switch (btn) {
    case BTN_OK:
    case BTN_NO:
    case BTN_ALL:
    case BTN_NONE:
      if (gp) {
        draw_face(btn, x, gy, gsize);
        return gsize;
      }
      draw_keycap(btn, x, y, size);
      break;
    case BTN_PREV:
    case BTN_NEXT:
      if (gp) {
        draw_shoulder(btn, x, gy, gsize);
        return gsize;
      }
      draw_keycap(btn, x, y, size);
      break;
    case BTN_PLAY:
      if (gp) {
        draw_play(x, gy, gsize);
        return gsize;
      }
      draw_keycap(btn, x, y, size);
      break;
    case BTN_SCREEN:
      if (gp) {
        draw_screen(x, gy, gsize);
        return gsize;
      }
      draw_keycap(btn, x, y, size);
      break;
    default:
      break;
  }
  return size;
}

static bool token_to_btn(const char* name, int len, TrackerButton* out) {
  static const struct {
    const char* n;
    TrackerButton b;
  } table[] = {
      {"UP", BTN_UP},
      {"DOWN", BTN_DOWN},
      {"LEFT", BTN_LEFT},
      {"RIGHT", BTN_RIGHT},
      {"OK", BTN_OK},
      {"NO", BTN_NO},
      {"ALL", BTN_ALL},
      {"NONE", BTN_NONE},
      {"PREV", BTN_PREV},
      {"NEXT", BTN_NEXT},
      {"PLAY", BTN_PLAY},
      {"SCREEN", BTN_SCREEN},
  };
  for (size_t i = 0; i < sizeof(table) / sizeof(table[0]); i++) {
    if ((int)strlen(table[i].n) == len && strncmp(table[i].n, name, len) == 0) {
      *out = table[i].b;
      return true;
    }
  }
  return false;
}

void hint_draw(const char* fmt, int x, int y, int font_size, Color color) {
  int size = font_size;  // icon box matches text height — no extra padding/blur
  char run[128];
  int run_len = 0;
  const char* p = fmt;

  while (*p) {
    if (*p == '{') {
      const char* close = strchr(p, '}');
      TrackerButton btn;
      if (close && token_to_btn(p + 1, (int)(close - p - 1), &btn)) {
        if (run_len > 0) {
          run[run_len] = '\0';
          DrawText(run, x, y, font_size, color);
          x += MeasureText(run, font_size);
          run_len = 0;
        }
        x += icon_draw(btn, x, y, size, color) + 1;
        p = close + 1;
        continue;
      }
    }
    if (run_len < (int)sizeof(run) - 1)
      run[run_len++] = *p;
    p++;
  }
  if (run_len > 0) {
    run[run_len] = '\0';
    DrawText(run, x, y, font_size, color);
  }
}

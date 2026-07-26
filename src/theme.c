#include "theme.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

Color C_BG = {0x00, 0x00, 0x08, 0xFF};
Color C_BG_ALT = {0x06, 0x06, 0x10, 0xFF};
Color C_CURSOR = {0x18, 0x28, 0x68, 0xFF};
Color C_CURSOR2 = {0x28, 0x10, 0x40, 0xFF};
Color C_SEP = {0x20, 0x20, 0x30, 0xFF};
Color C_TEXT = {0xB8, 0xB8, 0xC8, 0xFF};
Color C_DIM = {0x28, 0x28, 0x38, 0xFF};
Color C_HEADER = {0x50, 0x50, 0x70, 0xFF};
Color C_NOTE = {0x40, 0xFF, 0xC0, 0xFF};
Color C_NOTE_OFF = {0xFF, 0x50, 0x50, 0xFF};
Color C_VEL = {0xA0, 0xFF, 0x60, 0xFF};
Color C_INST = {0xFF, 0xA0, 0x30, 0xFF};
Color C_FX = {0x80, 0xA0, 0xFF, 0xFF};
Color C_PLAY = {0x00, 0xFF, 0x60, 0xFF};
Color C_STATUS = {0xE0, 0xFF, 0x00, 0xFF};
Color C_TITLE = {0xFF, 0xFF, 0xFF, 0xFF};
Color C_EDIT_TAG = {0xFF, 0xC0, 0x00, 0xFF};

Color C_FB_HEADER = {0x0A, 0x0A, 0x28, 0xFF};
Color C_FB_DIM = {0x40, 0x40, 0x60, 0xFF};
Color C_FB_INPUT_BG = {0x08, 0x08, 0x20, 0xFF};

bool THEME_HIDE_HELP = false;

Color CH_COLORS[PATTERN_TRACKS] = {
    {0xFF, 0x40, 0x40, 0xFF},
    {0xFF, 0x90, 0x20, 0xFF},
    {0xFF, 0xFF, 0x20, 0xFF},
    {0x40, 0xFF, 0x60, 0xFF},
    {0x20, 0xD0, 0xFF, 0xFF},
    {0x60, 0x60, 0xFF, 0xFF},
    {0xC0, 0x40, 0xFF, 0xFF},
    {0xFF, 0x40, 0xC0, 0xFF},
    {0xFF, 0x80, 0x80, 0xFF},
    {0x80, 0xFF, 0x80, 0xFF},
    {0x80, 0x80, 0xFF, 0xFF},
    {0xFF, 0xFF, 0x80, 0xFF},
    {0x80, 0xFF, 0xFF, 0xFF},
    {0xFF, 0x80, 0xFF, 0xFF},
    {0xC0, 0xC0, 0x40, 0xFF},
    {0x40, 0xC0, 0xC0, 0xFF},
};

typedef struct {
  const char* key;
  Color* target;
} ThemeField;

static const ThemeField THEME_FIELDS[] = {
    {"bg", &C_BG},
    {"bg_alt", &C_BG_ALT},
    {"cursor", &C_CURSOR},
    {"cursor2", &C_CURSOR2},
    {"sep", &C_SEP},
    {"text", &C_TEXT},
    {"dim", &C_DIM},
    {"header", &C_HEADER},
    {"note", &C_NOTE},
    {"note_off", &C_NOTE_OFF},
    {"vel", &C_VEL},
    {"inst", &C_INST},
    {"fx", &C_FX},
    {"play", &C_PLAY},
    {"status", &C_STATUS},
    {"title", &C_TITLE},
    {"edit_tag", &C_EDIT_TAG},
    {"fb_header", &C_FB_HEADER},
    {"fb_dim", &C_FB_DIM},
    {"fb_input_bg", &C_FB_INPUT_BG},
};

static char* trim(char* s) {
  while (isspace((unsigned char)*s)) s++;
  if (*s == '\0')
    return s;
  char* end = s + strlen(s) - 1;
  while (end > s && isspace((unsigned char)*end)) *end-- = '\0';
  return s;
}

// "RRGGBB" or "#RRGGBB" → opaque Color, via raylib's 0xRRGGBBAA constructor.
static bool parse_hex_color(const char* s, Color* out) {
  if (s[0] == '#')
    s++;
  // Exactly six hex digits — strtoul alone would also swallow a leading sign.
  if (strspn(s, "0123456789abcdefABCDEF") != 6 || s[6] != '\0')
    return false;
  *out = GetColor((unsigned int)(strtoul(s, NULL, 16) << 8 | 0xFF));
  return true;
}

static bool parse_bool(const char* s) {
  const char* l = TextToLower(s);
  return TextIsEqual(l, "1") || TextIsEqual(l, "true") || TextIsEqual(l, "yes");
}

static Color* find_target(const char* key) {
  for (size_t i = 0; i < sizeof(THEME_FIELDS) / sizeof(THEME_FIELDS[0]); i++)
    if (strcmp(key, THEME_FIELDS[i].key) == 0)
      return THEME_FIELDS[i].target;
  if (strncmp(key, "track", 5) == 0 && isdigit((unsigned char)key[5])) {
    int idx = atoi(key + 5);
    if (idx >= 0 && idx < PATTERN_TRACKS)
      return &CH_COLORS[idx];
  }
  return NULL;
}

bool theme_load(const char* path) {
  char* text = LoadFileText(path);
  if (!text) {
    fprintf(stderr, "theme: could not open '%s'\n", path);
    return false;
  }

  char line[256];
  int lineno = 0;
  for (char* cur = text; *cur;) {
    lineno++;
    char* nl = strchr(cur, '\n');
    size_t n = nl ? (size_t)(nl - cur) : strlen(cur);
    if (n >= sizeof(line))
      n = sizeof(line) - 1;
    memcpy(line, cur, n);
    line[n] = '\0';
    cur = nl ? nl + 1 : cur + strlen(cur);

    char* l = trim(line);
    if (l[0] == '\0' || l[0] == '#')
      continue;
    char* eq = strchr(l, '=');
    if (!eq) {
      fprintf(stderr, "theme: %s:%d: missing '=', skipping\n", path, lineno);
      continue;
    }
    *eq = '\0';
    char* key = trim(l);
    char* val = trim(eq + 1);

    if (strcmp(key, "hide_help") == 0) {
      THEME_HIDE_HELP = parse_bool(val);
      continue;
    }

    Color* target = find_target(key);
    if (!target) {
      fprintf(stderr, "theme: %s:%d: unknown key '%s', skipping\n", path, lineno, key);
      continue;
    }
    if (!parse_hex_color(val, target))
      fprintf(stderr, "theme: %s:%d: bad color '%s' for '%s', skipping\n", path, lineno, val, key);
  }

  UnloadFileText(text);
  return true;
}

bool theme_load_default(const char* path) {
  if (!FileExists(path))
    return false;
  return theme_load(path);
}

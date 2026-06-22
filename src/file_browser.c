#include "file_browser.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static char g_result[512] = {0};
static int g_ready = 0;

#ifdef __EMSCRIPTEN__
#include <emscripten.h>

EMSCRIPTEN_KEEPALIVE void c_file_browser_ready(const char *path) {
  strncpy(g_result, path, sizeof(g_result) - 1);
  g_result[sizeof(g_result) - 1] = '\0';
  g_ready = 1;
}

EM_JS(void, js_file_open, (const char *filter_c), {
  var filterStr = UTF8ToString(filter_c);
  var accept = filterStr.split(' ').map(function(p) {
                                     return p.replace("*", "");
                                   })
                   .join(',');
  var input = document.createElement('input');
  input.type = 'file';
  if (accept)
    input.accept = accept;
  input.style.display = 'none';
  input.onchange = function(e) {
    var file = e.target.files[0];
    if (!file)
      return;
    var reader = new FileReader();
    reader.onload = function(ev) {
      var data = new Uint8Array(ev.target.result);
      var path = '/uploads/' + file.name;
      try {
        FS.mkdir('/uploads');
      } catch (err) {
        void err;
      }
      FS.writeFile(path, data);
      Module.ccall('c_file_browser_ready', null, ['string'], [path]);
    };
    reader.readAsArrayBuffer(file);
    document.body.removeChild(input);
  };
  document.body.appendChild(input);
  input.click();
});

EM_JS(void, js_file_download, (const char *fs_path_c, const char *name_c), {
  var path = UTF8ToString(fs_path_c);
  var name = UTF8ToString(name_c);
  try {
    var data = FS.readFile(path);
    var blob = new Blob([data], {type: 'application/octet-stream' });
    var url = URL.createObjectURL(blob);
    var a = document.createElement('a');
    a.href = url;
    a.download = name;
    a.style.display = 'none';
    document.body.appendChild(a);
    a.click();
    document.body.removeChild(a);
    URL.revokeObjectURL(url);
  } catch (err) {
    console.error('download:', err);
  }
});

void file_browser_open(const char *title, const char *filter) {
  (void)title;
  g_ready = 0;
  js_file_open(filter ? filter : "");
}

void file_browser_save_as(const char *title, const char *default_name) {
  (void)title;
  const char *p = default_name ? default_name : "song.rpt";
  strncpy(g_result, p, sizeof(g_result) - 1);
  g_result[sizeof(g_result) - 1] = '\0';
  g_ready = 1;
}

void file_browser_download(const char *fs_path, const char *suggested_name) {
  js_file_download(fs_path, suggested_name ? suggested_name : "download");
}

void file_browser_tick(void) {}
void file_browser_draw(void) {}
bool file_browser_active(void) { return false; }

#else  // ---- DESKTOP: inline raylib file browser ----

#include <dirent.h>
#include <sys/stat.h>

#include "bip39_en.h"
#include "input.h"
#include "raylib.h"

#define FB_W 480
#define FB_H 320
#define FB_FS 10
#define FB_RH 14

static const Color C_HDR = {0x0A, 0x0A, 0x28, 0xFF};
static const Color C_ROW0 = {0x00, 0x00, 0x08, 0xFF};
static const Color C_ROW1 = {0x06, 0x06, 0x10, 0xFF};
static const Color C_SEL = {0x18, 0x28, 0x68, 0xFF};
static const Color C_SEP = {0x20, 0x20, 0x30, 0xFF};
static const Color C_TXT = {0xB8, 0xB8, 0xC8, 0xFF};
static const Color C_DIM = {0x40, 0x40, 0x60, 0xFF};
static const Color C_DIR = {0xFF, 0xA0, 0x30, 0xFF};
static const Color C_FILE = {0x40, 0xFF, 0xC0, 0xFF};
static const Color C_WHT = {0xFF, 0xFF, 0xFF, 0xFF};
static const Color C_INP = {0x08, 0x08, 0x20, 0xFF};

#define MAX_ENT 1024
#define MAX_PATH 512

typedef enum { FB_NONE,
               FB_OPEN,
               FB_SAVE } FBMode;
typedef struct {
  char name[256];
  bool is_dir;
} Ent;

static FBMode g_mode = FB_NONE;
static char g_dir[MAX_PATH] = {0};
static char g_filt[128] = {0};
static char g_ext[16] = "rpt";
static Ent g_ents[MAX_ENT];
static int g_cnt = 0;
static int g_cur = 0;
static int g_scr = 0;
static char g_fname[256] = {0};
static bool g_fname_ed = false;

// On-screen keyboard for filename entry
#define FB_KB_KEY_W     42
#define FB_KB_KEY_H     22
#define FB_KB_GAP       2
#define FB_KB_CHAR_ROWS 4
static const char *FB_KB_CHARS[FB_KB_CHAR_ROWS] = {
    "1234567890",
    "QWERTYUIOP",
    "ASDFGHJKL-",
    "ZXCVBNM._",
};
static const int FB_KB_CHAR_COLS[FB_KB_CHAR_ROWS] = {10, 10, 10, 9};
#define FB_KB_SPECIAL 4
#define FB_KB_ROWS    5
// Special row cols: 0=SHIFT 1=SPACE 2=DEL 3=SUGGEST 4=OK
#define FB_KB_SPECIAL_COLS 5
static int  g_kb_row   = FB_KB_SPECIAL;
static int  g_kb_col   = 3;
static bool g_kb_shift = false;

static int fb_kb_max_col(int row) {
  return (row < FB_KB_CHAR_ROWS) ? FB_KB_CHAR_COLS[row] : FB_KB_SPECIAL_COLS;
}

static void fb_strip_ext(char *name) {
  size_t l = strlen(name), el = strlen(g_ext);
  if (el && l > el + 1 && name[l - el - 1] == '.' && strcasecmp(name + l - el, g_ext) == 0)
    name[l - el - 1] = '\0';
}
static void fb_fname_append(char c) {
  size_t l = strlen(g_fname);
  if (l < sizeof(g_fname) - 2) { g_fname[l] = c; g_fname[l + 1] = '\0'; }
}
static void fb_fname_backspace(void) {
  size_t l = strlen(g_fname);
  if (l) g_fname[l - 1] = '\0';
}
static void fb_fname_confirm(void) {
  if (!g_fname[0]) return;
  snprintf(g_result, sizeof(g_result), "%s/%s.%s", g_dir, g_fname, g_ext);
  g_ready = 1;
  g_mode = FB_NONE;
  g_fname_ed = false;
}
static void fb_suggest(void) {
  static bool seeded = false;
  if (!seeded) { srand((unsigned int)time(NULL)); seeded = true; }
  int a = rand() % 2048, b = rand() % 2048, c = rand() % 2048;
  snprintf(g_fname, sizeof(g_fname), "%s-%s-%s", bip39_en[a], bip39_en[b], bip39_en[c]);
}
static void fb_enter_kb(void) {
  g_fname_ed = true;
  g_kb_row   = FB_KB_SPECIAL;
  g_kb_col   = 3;  // SUGGEST preselected
}

bool file_browser_active(void) { return g_mode != FB_NONE; }

static bool fmatch(const char *name) {
  if (!g_filt[0])
    return true;
  char buf[128];
  strncpy(buf, g_filt, sizeof(buf) - 1);
  char *tok = strtok(buf, " ");
  while (tok) {
    if (tok[0] == '*' && tok[1] == '.') {
      const char *ext = strrchr(name, '.');
      if (ext && strcasecmp(ext, tok + 1) == 0)
        return true;
    }
    tok = strtok(NULL, " ");
  }
  return false;
}

static int ecmp(const void *a, const void *b) {
  const Ent *ea = a, *eb = b;
  if (!strcmp(ea->name, ".."))
    return -1;
  if (!strcmp(eb->name, ".."))
    return 1;
  if (ea->is_dir != eb->is_dir)
    return ea->is_dir ? -1 : 1;
  return strcmp(ea->name, eb->name);
}

static void scan(void) {
  g_cnt = g_cur = g_scr = 0;
  DIR *d = opendir(g_dir);
  if (!d)
    return;
  struct dirent *de;
  while ((de = readdir(d)) && g_cnt < MAX_ENT) {
    const char *n = de->d_name;
    if (n[0] == '.' && strcmp(n, "..") != 0)
      continue;
    char full[MAX_PATH];
    snprintf(full, sizeof(full), "%s/%s", g_dir, n);
    struct stat st;
    if (stat(full, &st))
      continue;
    bool isdir = S_ISDIR(st.st_mode);
    if (isdir) {
      const char *ext = strrchr(n, '.');
      if (ext && strcasecmp(ext, ".clap") == 0) {
        if (!strstr(g_filt, ".clap"))
          continue;
        isdir = false;
      }
    }
    if (!isdir) {
      if (!fmatch(n))
        continue;
    }
    strncpy(g_ents[g_cnt].name, n, 255);
    g_ents[g_cnt].is_dir = isdir;
    g_cnt++;
  }
  closedir(d);
  qsort(g_ents, g_cnt, sizeof(Ent), ecmp);
}

static void go_up(void) {
  char *last = strrchr(g_dir, '/');
  if (!last) {
    g_mode = FB_NONE;
    g_ready = 0;
    return;
  }
  if (last == g_dir) {
    if (!strcmp(g_dir, "/")) {
      g_mode = FB_NONE;
      g_ready = 0;
      return;
    }
    g_dir[1] = '\0';
  } else {
    *last = '\0';
  }
  scan();
}

static int vis_rows(void) {
  int bot_h = (g_mode == FB_SAVE) ? 48 : 20;
  return (FB_H - 22 - bot_h) / FB_RH;
}

static bool fb_rep(TrackerButton b) {
  if (input_pressed(b))
    return true;
  int f = input_held_frames(b);
  return (f > 20) && (f % 4 == 0);
}

void file_browser_open(const char *title, const char *filter) {
  (void)title;
  g_mode = FB_OPEN;
  g_ready = 0;
  g_fname[0] = '\0';
  g_fname_ed = false;
  if (!g_dir[0])
    strncpy(g_dir, GetWorkingDirectory(), MAX_PATH - 1);
  strncpy(g_filt, filter ? filter : "", sizeof(g_filt) - 1);
  scan();
}

void file_browser_save_as(const char *title, const char *def_name) {
  (void)title;
  g_mode = FB_SAVE;
  g_ready = 0;
  g_fname_ed = false;
  if (!g_dir[0])
    strncpy(g_dir, GetWorkingDirectory(), MAX_PATH - 1);
  // Derive extension from def_name (e.g. "song.rpt" → "rpt", "inst.rpti" → "rpti")
  const char *dot = def_name ? strrchr(def_name, '.') : NULL;
  strncpy(g_ext, (dot && dot[1]) ? dot + 1 : "rpt", sizeof(g_ext) - 1);
  snprintf(g_filt, sizeof(g_filt), "*.%s", g_ext);
  strncpy(g_fname, def_name ? def_name : "song", sizeof(g_fname) - 1);
  fb_strip_ext(g_fname);
  scan();
}

void file_browser_download(const char *p, const char *n) {
  (void)p;
  (void)n;
}

void file_browser_tick(void) {
  if (g_mode == FB_NONE)
    return;
  int vis = vis_rows();

  // SELECT+B always cancels
  if (input_held(BTN_SELECT) && input_pressed(BTN_B)) {
    g_mode = FB_NONE;
    g_ready = 0;
    return;
  }

  if (g_fname_ed) {
    while (GetCharPressed() > 0) {}  // drain queue — on-screen keyboard handles input
    if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER)) { fb_fname_confirm(); return; }

    // On-screen keyboard navigation
    if (fb_rep(BTN_LEFT)) {
      if (--g_kb_col < 0) g_kb_col = fb_kb_max_col(g_kb_row) - 1;
    }
    if (fb_rep(BTN_RIGHT)) {
      if (++g_kb_col >= fb_kb_max_col(g_kb_row)) g_kb_col = 0;
    }
    if (fb_rep(BTN_UP)) {
      if (--g_kb_row < 0) g_kb_row = FB_KB_ROWS - 1;
      if (g_kb_col >= fb_kb_max_col(g_kb_row)) g_kb_col = fb_kb_max_col(g_kb_row) - 1;
    }
    if (fb_rep(BTN_DOWN)) {
      if (++g_kb_row >= FB_KB_ROWS) g_kb_row = 0;
      if (g_kb_col >= fb_kb_max_col(g_kb_row)) g_kb_col = fb_kb_max_col(g_kb_row) - 1;
    }

    if (input_pressed(BTN_A)) {
      if (g_kb_row < FB_KB_CHAR_ROWS) {
        char c = FB_KB_CHARS[g_kb_row][g_kb_col];
        fb_fname_append(g_kb_shift ? (c >= 'A' && c <= 'Z' ? c + 32 : c) : c);
      } else {
        switch (g_kb_col) {
          case 0: g_kb_shift = !g_kb_shift; break;
          case 1: fb_fname_append(' ');     break;
          case 2: fb_fname_backspace();     break;
          case 3: fb_suggest();             break;
          case 4: fb_fname_confirm();       break;
        }
      }
    }
    if (input_pressed(BTN_B))
      g_fname_ed = false;
    return;
  }

  if (fb_rep(BTN_UP) && g_cur > 0) {
    g_cur--;
    if (g_cur < g_scr)
      g_scr = g_cur;
  }
  if (fb_rep(BTN_DOWN) && g_cur < g_cnt - 1) {
    g_cur++;
    if (g_cur >= g_scr + vis)
      g_scr = g_cur - vis + 1;
  }

  if (input_pressed(BTN_A)) {
    if (g_cnt == 0) {
      if (g_mode == FB_SAVE) fb_fname_confirm();
      return;
    }
    Ent *e = &g_ents[g_cur];
    if (e->is_dir) {
      if (!strcmp(e->name, "..")) {
        go_up();
      } else {
        char np[MAX_PATH];
        snprintf(np, sizeof(np), "%s/%s", g_dir, e->name);
        strncpy(g_dir, np, MAX_PATH - 1);
        scan();
      }
    } else {
      // Both open and save: selecting existing file sets result directly
      snprintf(g_result, sizeof(g_result), "%s/%s", g_dir, e->name);
      g_ready = 1;
      g_mode = FB_NONE;
    }
  }

  if (input_pressed(BTN_B))
    go_up();

  if (g_mode == FB_SAVE) {
    if (input_pressed(BTN_Y))
      fb_enter_kb();
    if (input_pressed(BTN_START)) {
      if (g_fname[0]) fb_fname_confirm();
      else fb_enter_kb();
    }
  }
}

void file_browser_draw(void) {
  if (g_mode == FB_NONE)
    return;

  int title_h = 22;
  DrawRectangle(0, 0, FB_W, FB_H, C_ROW0);

  // Title bar (always)
  DrawRectangle(0, 0, FB_W, title_h, C_HDR);
  const char *mstr = (g_mode == FB_SAVE) ? "SAVE" : "LOAD";
  char hdr[MAX_PATH + 16];
  snprintf(hdr, sizeof(hdr), "%s  %s", mstr, g_dir);
  if (MeasureText(hdr, FB_FS) > FB_W - 8) {
    const char *tail = g_dir + strlen(g_dir);
    while (tail > g_dir && *(tail - 1) != '/') tail--;
    snprintf(hdr, sizeof(hdr), "%s  .../%s", mstr, tail);
  }
  DrawText(hdr, 4, (title_h - FB_FS) / 2, FB_FS, C_WHT);
  DrawLine(0, title_h, FB_W, title_h, C_SEP);

  // On-screen keyboard mode
  if (g_fname_ed) {
    // Name field
    int fn_y = title_h + 2, fn_h = FB_RH + 2;
    DrawRectangle(0, fn_y, FB_W, fn_h, C_HDR);
    DrawText("NAME", 4, fn_y + (fn_h - FB_FS) / 2, FB_FS, C_DIM);
    int ix = 44, iw = FB_W - ix - 4;
    DrawRectangle(ix, fn_y + 1, iw, fn_h - 2, C_SEL);
    DrawText(g_fname, ix + 3, fn_y + (fn_h - FB_FS) / 2, FB_FS, C_WHT);
    double t = GetTime();
    if ((t - (int)t) < 0.5) {
      int cx = ix + 3 + MeasureText(g_fname, FB_FS);
      DrawRectangle(cx, fn_y + 2, 1, FB_FS + 1, C_WHT);
    }
    DrawLine(0, fn_y + fn_h, FB_W, fn_y + fn_h, C_SEP);

    // Keyboard grid
    int kb_y = fn_y + fn_h + 6;
    Color kb_key = {0x28, 0x28, 0x50, 0xFF};
    Color kb_cur = {0x20, 0x60, 0xC0, 0xFF};

    for (int r = 0; r < FB_KB_CHAR_ROWS; r++) {
      int ncols   = FB_KB_CHAR_COLS[r];
      int total_w = ncols * FB_KB_KEY_W + (ncols - 1) * FB_KB_GAP;
      int sx      = (FB_W - total_w) / 2;
      int y       = kb_y + r * (FB_KB_KEY_H + FB_KB_GAP);
      for (int c = 0; c < ncols; c++) {
        int  x   = sx + c * (FB_KB_KEY_W + FB_KB_GAP);
        bool cur = (g_kb_row == r && g_kb_col == c);
        DrawRectangle(x, y, FB_KB_KEY_W, FB_KB_KEY_H, cur ? kb_cur : kb_key);
        char raw = FB_KB_CHARS[r][c];
        char label[2] = {(g_kb_shift && raw >= 'A' && raw <= 'Z') ? raw + 32 : raw, 0};
        int  tw       = MeasureText(label, FB_FS);
        DrawText(label, x + (FB_KB_KEY_W - tw) / 2, y + (FB_KB_KEY_H - FB_FS) / 2,
                 FB_FS, cur ? C_WHT : C_TXT);
      }
    }

    // Special row: SHIFT | SPACE | DEL | SUGGEST | OK
    int sy      = kb_y + FB_KB_CHAR_ROWS * (FB_KB_KEY_H + FB_KB_GAP);
    int sh_x    = 8,           sh_w  = 72;
    int sp_x    = sh_x + sh_w + 3, sp_w = 100;
    int del_x   = sp_x + sp_w + 3, del_w = 66;
    int sug_x   = del_x + del_w + 3, sug_w = 112;
    int ok_x    = sug_x + sug_w + 3, ok_w = FB_W - ok_x - 8;

    bool sh_cur  = (g_kb_row == FB_KB_SPECIAL && g_kb_col == 0);
    bool sp_cur  = (g_kb_row == FB_KB_SPECIAL && g_kb_col == 1);
    bool del_cur = (g_kb_row == FB_KB_SPECIAL && g_kb_col == 2);
    bool sug_cur = (g_kb_row == FB_KB_SPECIAL && g_kb_col == 3);
    bool ok_cur  = (g_kb_row == FB_KB_SPECIAL && g_kb_col == 4);

    Color sh_bg = g_kb_shift ? (Color){0x60, 0x40, 0x00, 0xFF} : kb_key;
    DrawRectangle(sh_x,  sy, sh_w,  FB_KB_KEY_H, sh_cur  ? kb_cur : sh_bg);
    DrawRectangle(sp_x,  sy, sp_w,  FB_KB_KEY_H, sp_cur  ? kb_cur : kb_key);
    DrawRectangle(del_x, sy, del_w, FB_KB_KEY_H, del_cur ? kb_cur : kb_key);
    DrawRectangle(sug_x, sy, sug_w, FB_KB_KEY_H, sug_cur ? kb_cur : (Color){0x00,0x28,0x40,0xFF});
    DrawRectangle(ok_x,  sy, ok_w,  FB_KB_KEY_H, ok_cur  ? kb_cur : kb_key);

    DrawText("SHIFT",   sh_x  + (sh_w  - MeasureText("SHIFT",   FB_FS)) / 2,
             sy + (FB_KB_KEY_H - FB_FS) / 2, FB_FS, sh_cur  ? C_WHT : (g_kb_shift ? (Color){0xFF,0xC0,0x00,0xFF} : C_TXT));
    DrawText("SPACE",   sp_x  + (sp_w  - MeasureText("SPACE",   FB_FS)) / 2,
             sy + (FB_KB_KEY_H - FB_FS) / 2, FB_FS, sp_cur  ? C_WHT : C_TXT);
    DrawText("DEL",     del_x + (del_w - MeasureText("DEL",     FB_FS)) / 2,
             sy + (FB_KB_KEY_H - FB_FS) / 2, FB_FS, del_cur ? C_WHT : (Color){0xFF, 0x50, 0x50, 0xFF});
    DrawText("SUGGEST", sug_x + (sug_w - MeasureText("SUGGEST", FB_FS)) / 2,
             sy + (FB_KB_KEY_H - FB_FS) / 2, FB_FS, sug_cur ? C_WHT : (Color){0x40, 0xC0, 0xFF, 0xFF});
    DrawText("OK",      ok_x  + (ok_w  - MeasureText("OK",      FB_FS)) / 2,
             sy + (FB_KB_KEY_H - FB_FS) / 2, FB_FS, ok_cur  ? C_WHT : (Color){0x00, 0xFF, 0x60, 0xFF});

    DrawText("DPAD=navigate   A=type   B=back to list   Enter=save",
             4, FB_H - FB_FS - 4, FB_FS - 1, C_DIM);
    return;
  }

  // File list
  int bot_h = (g_mode == FB_SAVE) ? 48 : 20;
  int list_y = title_h;
  int bot_y  = FB_H - bot_h;
  int list_h = bot_y - list_y;
  int vis    = list_h / FB_RH;

  for (int i = 0; i < vis && (g_scr + i) < g_cnt; i++) {
    int idx = g_scr + i;
    int y   = list_y + i * FB_RH;
    bool cur = (idx == g_cur);
    DrawRectangle(0, y, FB_W, FB_RH, cur ? C_SEL : (i % 2 == 0 ? C_ROW1 : C_ROW0));
    Ent *e = &g_ents[idx];
    char label[260];
    if (!strcmp(e->name, ".."))
      snprintf(label, sizeof(label), "[ .. ]");
    else if (e->is_dir)
      snprintf(label, sizeof(label), "[%s]", e->name);
    else
      strncpy(label, e->name, sizeof(label) - 1);
    DrawText(label, 6, y + (FB_RH - FB_FS) / 2, FB_FS, cur ? C_WHT : (e->is_dir ? C_DIR : C_FILE));
  }

  // Scrollbar
  if (g_cnt > vis && vis > 0) {
    int sx = FB_W - 5, sh = list_h, denom = g_cnt - vis;
    DrawRectangle(sx, list_y, 5, sh, C_DIM);
    int th = (vis * sh) / g_cnt;
    if (th < 6) th = 6;
    int ty = list_y + (denom > 0 ? (g_scr * (sh - th)) / denom : 0);
    DrawRectangle(sx, ty, 5, th, C_TXT);
  }

  // Bottom bar
  DrawLine(0, bot_y, FB_W, bot_y, C_SEP);
  DrawRectangle(0, bot_y, FB_W, bot_h, C_HDR);

  if (g_mode == FB_SAVE) {
    int fn_y = bot_y + 4, fn_h = FB_RH;
    DrawText("NAME", 4, fn_y + (fn_h - FB_FS) / 2, FB_FS, C_DIM);
    int ix = 44, iw = FB_W - ix - 4;
    DrawRectangle(ix, fn_y, iw, fn_h, C_INP);
    DrawText(g_fname, ix + 3, fn_y + (fn_h - FB_FS) / 2, FB_FS, C_TXT);
    DrawText("A=pick/enter   B=up   Y=edit name   START=save here   SEL+B=cancel",
             4, fn_y + fn_h + 6, FB_FS - 1, C_DIM);
  } else {
    DrawText("A=open/enter dir   B=up   SELECT+B=cancel",
             4, bot_y + (bot_h - (FB_FS - 1)) / 2, FB_FS - 1, C_DIM);
  }
}

#endif  // __EMSCRIPTEN__

const char *file_browser_poll(void) {
  if (!g_ready)
    return NULL;
  g_ready = 0;
  return g_result;
}

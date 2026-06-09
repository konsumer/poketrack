#include "file_browser.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
                                     return p.replace('*', '');
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
static Ent g_ents[MAX_ENT];
static int g_cnt = 0;
static int g_cur = 0;
static int g_scr = 0;
static char g_fname[256] = {0};
static bool g_fname_ed = false;

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
  strncpy(g_filt, "*.rpt", sizeof(g_filt) - 1);
  strncpy(g_fname, def_name ? def_name : "song.rpt", sizeof(g_fname) - 1);
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
    int ch;
    while ((ch = GetCharPressed()) > 0) {
      size_t l = strlen(g_fname);
      if (ch >= 32 && l < sizeof(g_fname) - 2) {
        g_fname[l] = (char)ch;
        g_fname[l + 1] = '\0';
      }
    }
    if (IsKeyPressed(KEY_BACKSPACE)) {
      size_t l = strlen(g_fname);
      if (l)
        g_fname[l - 1] = '\0';
    }
    if (input_pressed(BTN_A) || IsKeyPressed(KEY_ENTER)) {
      if (g_fname[0]) {
        snprintf(g_result, sizeof(g_result), "%s/%s", g_dir, g_fname);
        g_ready = 1;
        g_mode = FB_NONE;
        g_fname_ed = false;
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
      if (g_mode == FB_SAVE && g_fname[0]) {
        snprintf(g_result, sizeof(g_result), "%s/%s", g_dir, g_fname);
        g_ready = 1;
        g_mode = FB_NONE;
      }
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
      if (g_mode == FB_OPEN) {
        snprintf(g_result, sizeof(g_result), "%s/%s", g_dir, e->name);
        g_ready = 1;
        g_mode = FB_NONE;
      } else {
        // Save: use this file's name, confirm in filename editor
        strncpy(g_fname, e->name, sizeof(g_fname) - 1);
        g_fname_ed = true;
      }
    }
  }

  if (input_pressed(BTN_B))
    go_up();

  if (g_mode == FB_SAVE) {
    if (input_pressed(BTN_Y))
      g_fname_ed = true;
    if (input_pressed(BTN_START)) {
      if (g_fname[0]) {
        snprintf(g_result, sizeof(g_result), "%s/%s", g_dir, g_fname);
        g_ready = 1;
        g_mode = FB_NONE;
        g_fname_ed = false;
      } else {
        g_fname_ed = true;
      }
    }
  }
}

void file_browser_draw(void) {
  if (g_mode == FB_NONE)
    return;

  int title_h = 22;
  int bot_h = (g_mode == FB_SAVE) ? 48 : 20;
  int list_y = title_h;
  int bot_y = FB_H - bot_h;
  int list_h = bot_y - list_y;
  int vis = list_h / FB_RH;

  DrawRectangle(0, 0, FB_W, FB_H, C_ROW0);

  // Title bar
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

  // File rows
  for (int i = 0; i < vis && (g_scr + i) < g_cnt; i++) {
    int idx = g_scr + i;
    int y = list_y + i * FB_RH;
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
    Color fc = e->is_dir ? C_DIR : C_FILE;
    DrawText(label, 6, y + (FB_RH - FB_FS) / 2, FB_FS, cur ? C_WHT : fc);
  }

  // Scrollbar
  if (g_cnt > vis && vis > 0) {
    int sx = FB_W - 5, sh = list_h, denom = g_cnt - vis;
    DrawRectangle(sx, list_y, 5, sh, C_DIM);
    int th = (vis * sh) / g_cnt;
    if (th < 6)
      th = 6;
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
    DrawRectangle(ix, fn_y, iw, fn_h, g_fname_ed ? C_SEL : C_INP);
    DrawText(g_fname, ix + 3, fn_y + (fn_h - FB_FS) / 2, FB_FS, g_fname_ed ? C_WHT : C_TXT);
    if (g_fname_ed) {
      double t = GetTime();
      if ((t - (int)t) < 0.5) {
        int cx = ix + 3 + MeasureText(g_fname, FB_FS);
        DrawRectangle(cx, fn_y + 2, 1, FB_FS + 1, C_WHT);
      }
    }
    const char *hint = g_fname_ed
                           ? "type name   A/Enter=save   B=back to list"
                           : "A=pick/enter   B=up   Y=edit name   START=save here   SEL+B=cancel";
    DrawText(hint, 4, fn_y + fn_h + 6, FB_FS - 1, C_DIM);
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

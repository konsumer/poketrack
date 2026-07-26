#include "file_browser.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char g_result[512] = {0};
static int g_ready = 0;

#ifdef __EMSCRIPTEN__
#include <emscripten.h>

EMSCRIPTEN_KEEPALIVE void c_file_browser_ready(const char* path) {
  strncpy(g_result, path, sizeof(g_result) - 1);
  g_result[sizeof(g_result) - 1] = '\0';
  g_ready = 1;
}

// EM_JS bodies are JavaScript; C formatting corrupts JS-only tokens
// (=== becomes "== =") and only the web build catches it.
// The directive below must be the comment's exact text to take effect.
// clang-format off
EM_JS(void, js_file_open, (const char* filter_c), {
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
      }
      catch(err) {
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

EM_JS(void, js_file_download, (const char* fs_path_c, const char* name_c), {
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
  }
  catch(err) {
    console.error('download:', err);
  }
});
// clang-format on

void file_browser_open(const char* title, const char* filter) {
  (void)title;
  g_ready = 0;
  js_file_open(filter ? filter : "");
}

void file_browser_save_as(const char* title, const char* default_name) {
  (void)title;
  const char* p = default_name ? default_name : "song.rpt";
  strncpy(g_result, p, sizeof(g_result) - 1);
  g_result[sizeof(g_result) - 1] = '\0';
  g_ready = 1;
}

void file_browser_download(const char* fs_path, const char* suggested_name) {
  js_file_download(fs_path, suggested_name ? suggested_name : "download");
}

void file_browser_tick(void) {}
void file_browser_draw(void) {}
bool file_browser_active(void) { return false; }

#else  // ---- DESKTOP: inline raylib file browser ----

#include "dir.h"
#include "input.h"
#include "raylib.h"
#include "theme.h"
#include "ui.h"

#define FB_W WIN_W
#define FB_H WIN_H
#define FB_FS 10
#define FB_RH 14

// Aliases onto the shared theme globals (see theme.h) so the file browser
// stays in sync with `--theme`. C_HDR/C_DIM have no equivalent in the
// main palette, so they get their own themeable fields (C_FB_*).
#define C_HDR C_FB_HEADER
#define C_ROW0 C_BG
#define C_ROW1 C_BG_ALT
#define C_SEL C_CURSOR
#define C_TXT C_TEXT
#define C_DIM C_FB_DIM
#define C_DIR C_INST
#define C_FILE C_NOTE
#define C_WHT C_TITLE

#define MAX_ENT 1024
// Not MAX_PATH: windows.h (via dir.h) already defines that as 260, and
// redefining it both warns and silently changes it for the Win32 calls in
// this translation unit.
#define FB_PATH 512

typedef enum { FB_NONE,
               FB_OPEN,
               FB_SAVE } FBMode;
typedef struct {
  char name[256];
  bool is_dir;
} Ent;

static FBMode g_mode = FB_NONE;
static char g_dir[FB_PATH] = {0};
static char g_filt[128] = {0};
static char g_ext[16] = ".rpt";  // includes the dot, matching raylib's GetFileExtension
static Ent g_ents[MAX_ENT];
static int g_cnt = 0;
static int g_cur = 0;
static int g_scr = 0;
static char g_fname[256] = {0};
static bool g_fname_ed = false;
static KBModal g_kb;

static void fb_fname_confirm(void) {
  if (!g_fname[0])
    return;
  snprintf(g_result, sizeof(g_result), "%s/%s%s", g_dir, g_fname, g_ext);
  g_ready = 1;
  g_mode = FB_NONE;
  g_fname_ed = false;
}
static void fb_enter_kb(void) {
  g_fname_ed = true;
  kb_modal_open(&g_kb, g_fname, sizeof(g_fname), 3);
}

bool file_browser_active(void) { return g_mode != FB_NONE; }

// g_filt is a space-separated glob list, e.g. "*.sf2 *.SF2". IsFileExtension
// already matches case-insensitively, so only the ".ext" part matters.
static bool fmatch(const char* name) {
  if (!g_filt[0])
    return true;
  char buf[128];
  snprintf(buf, sizeof(buf), "%s", g_filt);
  for (char* tok = strtok(buf, " "); tok; tok = strtok(NULL, " "))
    if (tok[0] == '*' && IsFileExtension(name, tok + 1))
      return true;
  return false;
}

static int ecmp(const void* a, const void* b) {
  const Ent *ea = a, *eb = b;
  if (!strcmp(ea->name, ".."))
    return -1;
  if (!strcmp(eb->name, ".."))
    return 1;
  if (ea->is_dir != eb->is_dir)
    return ea->is_dir ? -1 : 1;
  return strcmp(ea->name, eb->name);
}

// Streams entries straight into the fixed g_ents array — see dir.h for why
// this doesn't use raylib's LoadDirectoryFilesEx.
static void scan(void) {
  g_cnt = g_cur = g_scr = 0;

  // Synthesize ".." unless we're at the filesystem root. Skipping every name
  // starting with '.' below hides dotfiles, and drops the real "." / ".."
  // along with them, so this is the only way back up.
  if (strcmp(g_dir, "/") != 0) {
    strncpy(g_ents[g_cnt].name, "..", sizeof(g_ents[g_cnt].name) - 1);
    g_ents[g_cnt].is_dir = true;
    g_cnt++;
  }

  DirIter it;
  if (dir_open(&it, g_dir)) {
    const char* name;
    bool is_dir;
    while (g_cnt < MAX_ENT && dir_next(&it, &name, &is_dir)) {
      if (name[0] == '.')
        continue;  // dotfiles, plus "." and ".."
      if (!is_dir && !fmatch(name))
        continue;
      strncpy(g_ents[g_cnt].name, name, sizeof(g_ents[g_cnt].name) - 1);
      g_ents[g_cnt].name[sizeof(g_ents[g_cnt].name) - 1] = '\0';
      g_ents[g_cnt].is_dir = is_dir;
      g_cnt++;
    }
    dir_close(&it);
  }

  qsort(g_ents, g_cnt, sizeof(Ent), ecmp);
}

static void go_up(void) {
  char* last = strrchr(g_dir, '/');
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
  return (FB_H - 22 - 20) / FB_RH;
}

void file_browser_open(const char* title, const char* filter) {
  (void)title;
  g_mode = FB_OPEN;
  g_ready = 0;
  g_fname[0] = '\0';
  g_fname_ed = false;
  if (!g_dir[0])
    strncpy(g_dir, GetWorkingDirectory(), FB_PATH - 1);
  strncpy(g_filt, filter ? filter : "", sizeof(g_filt) - 1);
  scan();
}

void file_browser_save_as(const char* title, const char* def_name) {
  (void)title;
  g_mode = FB_SAVE;
  g_ready = 0;
  g_fname_ed = false;
  if (!g_dir[0])
    strncpy(g_dir, GetWorkingDirectory(), FB_PATH - 1);
  // Derive extension from def_name (e.g. "song.rpt" → ".rpt", "inst.rpti" → ".rpti")
  const char* ext = def_name ? GetFileExtension(def_name) : NULL;
  snprintf(g_ext, sizeof(g_ext), "%s", (ext && ext[1]) ? ext : ".rpt");
  snprintf(g_filt, sizeof(g_filt), "*%s", g_ext);
  strncpy(g_fname, def_name ? def_name : "song", sizeof(g_fname) - 1);
  strip_ext(g_fname, g_ext);
  scan();
}

void file_browser_download(const char* p, const char* n) {
  (void)p;
  (void)n;
}

void file_browser_tick(void) {
  if (g_mode == FB_NONE)
    return;
  int vis = vis_rows();

  // SELECT+B always cancels
  if (input_held(BTN_SCREEN) && input_pressed(BTN_NO)) {
    g_mode = FB_NONE;
    g_ready = 0;
    return;
  }

  if (g_fname_ed) {
    if (kb_modal_update(&g_kb)) {
      g_fname_ed = false;
      if (g_kb.confirmed)
        fb_fname_confirm();
    }
    return;
  }

  // In save mode, a synthetic "[ SAVE HERE ]" row sits at display index 0,
  // ahead of the real entries (which then sit at g_cur - save_off).
  int save_off = (g_mode == FB_SAVE) ? 1 : 0;
  int total = g_cnt + save_off;

  if (ui_repeat(BTN_UP) && g_cur > 0) {
    g_cur--;
    if (g_cur < g_scr)
      g_scr = g_cur;
  }
  if (ui_repeat(BTN_DOWN) && g_cur < total - 1) {
    g_cur++;
    if (g_cur >= g_scr + vis)
      g_scr = g_cur - vis + 1;
  }

  if (input_pressed(BTN_OK)) {
    if (g_mode == FB_SAVE && g_cur == 0) {
      fb_enter_kb();
    } else if (total > 0) {
      Ent* e = &g_ents[g_cur - save_off];
      if (e->is_dir) {
        if (!strcmp(e->name, "..")) {
          go_up();
        } else {
          char np[FB_PATH];
          snprintf(np, sizeof(np), "%s/%s", g_dir, e->name);
          strncpy(g_dir, np, FB_PATH - 1);
          scan();
        }
      } else {
        // Both open and save: selecting an existing file sets result directly
        snprintf(g_result, sizeof(g_result), "%s/%s", g_dir, e->name);
        g_ready = 1;
        g_mode = FB_NONE;
      }
    }
  }

  if (input_pressed(BTN_NO))
    go_up();
}

void file_browser_draw(void) {
  if (g_mode == FB_NONE)
    return;

  // On-screen keyboard mode: shared modal, drawn full-screen in place of the browser
  if (g_fname_ed) {
    kb_modal_draw(&g_kb, "NAME:");
    return;
  }

  int title_h = 22;
  DrawRectangle(0, 0, FB_W, FB_H, C_ROW0);

  // Title bar (always)
  DrawRectangle(0, 0, FB_W, title_h, C_HDR);
  const char* mstr = (g_mode == FB_SAVE) ? "SAVE" : "LOAD";
  char hdr[FB_PATH + 16];
  snprintf(hdr, sizeof(hdr), "%s  %s", mstr, g_dir);
  if (MeasureText(hdr, FB_FS) > FB_W - 8) {
    const char* tail = g_dir + strlen(g_dir);
    while (tail > g_dir && *(tail - 1) != '/') tail--;
    snprintf(hdr, sizeof(hdr), "%s  .../%s", mstr, tail);
  }
  DrawText(hdr, 4, (title_h - FB_FS) / 2, FB_FS, C_WHT);
  DrawLine(0, title_h, FB_W, title_h, C_SEP);

  // File list — save mode prepends a synthetic "[ SAVE HERE ]" row at index 0
  int save_off = (g_mode == FB_SAVE) ? 1 : 0;
  int total = g_cnt + save_off;
  int bot_h = 20;
  int list_y = title_h;
  int bot_y = FB_H - bot_h;
  int list_h = bot_y - list_y;
  int vis = list_h / FB_RH;

  for (int i = 0; i < vis && (g_scr + i) < total; i++) {
    int idx = g_scr + i;
    int y = list_y + i * FB_RH;
    bool cur = (idx == g_cur);
    DrawRectangle(0, y, FB_W, FB_RH, cur ? C_SEL : (i % 2 == 0 ? C_ROW1 : C_ROW0));
    if (g_mode == FB_SAVE && idx == 0) {
      DrawText("[ SAVE HERE ]", 6, y + (FB_RH - FB_FS) / 2, FB_FS, cur ? C_WHT : C_FILE);
      continue;
    }
    Ent* e = &g_ents[idx - save_off];
    char label[260];
    if (!strcmp(e->name, ".."))
      snprintf(label, sizeof(label), "[ .. ]");
    else if (e->is_dir)
      snprintf(label, sizeof(label), "[%s]", e->name);
    else
      strncpy(label, e->name, sizeof(label) - 1);
    DrawText(label, 6, y + (FB_RH - FB_FS) / 2, FB_FS, cur ? C_WHT : (e->is_dir ? C_DIR : C_FILE));
  }

  draw_scrollbar(FB_W - 5, list_y, 5, list_h, g_scr, vis, total);

  // Bottom bar
  DrawLine(0, bot_y, FB_W, bot_y, C_SEP);
  DrawRectangle(0, bot_y, FB_W, bot_h, C_HDR);
  DrawText(g_mode == FB_SAVE ? "A=select   B=up   SELECT+B=cancel"
                             : "A=open/enter dir   B=up   SELECT+B=cancel",
           4, bot_y + (bot_h - (FB_FS - 1)) / 2, FB_FS - 1, C_DIM);
}

#endif  // __EMSCRIPTEN__

const char* file_browser_poll(void) {
  if (!g_ready)
    return NULL;
  g_ready = 0;
  return g_result;
}

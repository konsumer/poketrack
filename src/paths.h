#pragma once
// Path helpers shared by the save format (tracker.c) and by units that load
// external files (sf2/sfz/sampler/gran/clap). raylib covers the parsing side
// (GetDirectoryPath, GetFileName, IsFileExtension); what it has no equivalent
// for is "is this absolute", "make this relative to that", and a directory
// string that always ends in a separator — so those three live here, once.
#include <raylib.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

// True for "/x", "\x" and "C:\x". The only place a Windows/POSIX difference
// survives in this codebase — everything else goes through raylib.
static inline bool path_is_absolute(const char* p) {
  return p && (p[0] == '/' || p[0] == '\\' || (p[0] && p[1] == ':'));
}

// Directory containing file_path, always absolute and with a trailing '/' so
// callers can concatenate directly. GetDirectoryPath drops the separator (and
// answers "." for a bare filename), so put one back — and anchor a relative
// answer on the working directory, because callers use this dir in both
// directions: to resolve a song's stored data paths into absolute ones on
// load, and to relativise them again on save. A relative dir breaks both --
// the "resolved" path stays relative and gets resolved a second time against
// save_dir (sf2 then looks for songs/./songs/font.sf2 and finds nothing), and
// path_make_relative shares no prefix with an absolute path, so it gives up
// and writes the absolute one into the file.
static inline void path_dir_of(const char* file_path, char* out, int sz) {
  char dir[512];
  snprintf(dir, sizeof(dir), "%s", GetDirectoryPath(file_path));
  const char* d = dir;
  while (d[0] == '.' && d[1] == '/')  // "./x" is just "x"
    d += 2;
  if (path_is_absolute(d)) {
    snprintf(out, sz, "%s/", d);
    return;
  }
  char cwd[512];
  snprintf(cwd, sizeof(cwd), "%s", GetWorkingDirectory());
  size_t cl = strlen(cwd);
  while (cl > 0 && (cwd[cl - 1] == '/' || cwd[cl - 1] == '\\'))
    cwd[--cl] = '\0';  // trailing separator (and root "/") is re-added below
  if (!d[0] || (d[0] == '.' && !d[1]))
    snprintf(out, sz, "%s/", cwd);
  else
    snprintf(out, sz, "%s/%s/", cwd, d);
}

// Resolve path against base_dir. Absolute paths (and an empty base_dir) pass
// through unchanged; base_dir is expected to end in a separator.
static inline void unit_resolve_path(const char* base_dir, const char* path,
                                     char* out, int sz) {
  if (!path || !path[0])
    out[0] = '\0';
  else if (path_is_absolute(path) || !base_dir || !base_dir[0])
    snprintf(out, sz, "%s", path);
  else
    snprintf(out, sz, "%s%s", base_dir, path);
}

// Inverse of unit_resolve_path: express abs_path relative to base_dir, using
// "../" hops for the parts of base_dir they don't share. Falls back to the
// absolute path when there's no common prefix at all (e.g. another drive).
static inline void path_make_relative(const char* base_dir, const char* abs_path,
                                      char* out, int out_sz) {
  int last_sep = 0;
  for (int i = 0; base_dir[i] && abs_path[i]; i++) {
    if (base_dir[i] != abs_path[i])
      break;
    if (base_dir[i] == '/')
      last_sep = i + 1;
  }
  if (last_sep == 0) {
    snprintf(out, out_sz, "%s", abs_path);
    return;
  }
  int ups = 0;
  for (int i = last_sep; base_dir[i]; i++)
    if (base_dir[i] == '/')
      ups++;
  out[0] = '\0';
  for (int i = 0; i < ups; i++) strncat(out, "../", out_sz - strlen(out) - 1);
  strncat(out, abs_path + last_sep, out_sz - strlen(out) - 1);
}

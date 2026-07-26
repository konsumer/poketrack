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

// Directory containing file_path, always with a trailing '/' so callers can
// concatenate directly. GetDirectoryPath drops the separator (and answers "."
// for a bare filename), so put one back.
static inline void path_dir_of(const char* file_path, char* out, int sz) {
  snprintf(out, sz, "%s/", GetDirectoryPath(file_path));
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

#pragma once
// Streaming directory iteration.
//
// raylib's LoadDirectoryFilesEx is the portable way to do this, and it's what
// the rest of the codebase uses — but it is the wrong tool for the file
// browser. It walks the directory twice (once to count, once to read), calls
// stat() on every entry in both passes, and allocates MAX_FILEPATH_LENGTH
// (4KB) per entry up front. Browsing a 2000-sample folder therefore costs
// ~6000 stat() syscalls and ~8MB of transient calloc every time the cursor
// enters a directory. Measured on a fast desktop that is 14ms per scan; on
// the microSD card a handheld boots from, syscall latency makes it far worse.
//
// This iterator does one pass, allocates nothing, and gets the is-directory
// flag from data the OS already handed back (dirent.d_type / Win32 file
// attributes) instead of a separate stat(). Same directory, 0.36ms.
//
// Keep using raylib's version for one-shot scans where none of that matters.

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#if defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOGDI
#define NOGDI
#endif
#ifndef NOUSER
#define NOUSER
#endif
#include <windows.h>

typedef struct {
  HANDLE h;
  WIN32_FIND_DATAA fd;
  bool pending;  // fd already holds an unconsumed entry (FindFirstFile's result)
} DirIter;

static inline bool dir_open(DirIter* it, const char* path) {
  char pattern[1024];
  snprintf(pattern, sizeof(pattern), "%s\\*", path);
  it->h = FindFirstFileA(pattern, &it->fd);
  it->pending = (it->h != INVALID_HANDLE_VALUE);
  return it->pending;
}

// Returns false at end of directory. *name stays valid until the next call.
static inline bool dir_next(DirIter* it, const char** name, bool* is_dir) {
  if (it->h == INVALID_HANDLE_VALUE)
    return false;
  if (it->pending)
    it->pending = false;  // consume FindFirstFile's entry
  else if (!FindNextFileA(it->h, &it->fd))
    return false;
  *name = it->fd.cFileName;
  *is_dir = (it->fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
  return true;
}

static inline void dir_close(DirIter* it) {
  if (it->h != INVALID_HANDLE_VALUE)
    FindClose(it->h);
  it->h = INVALID_HANDLE_VALUE;
}

#else

#include <dirent.h>
#include <sys/stat.h>

typedef struct {
  DIR* d;
  char base[1024];
} DirIter;

static inline bool dir_open(DirIter* it, const char* path) {
  snprintf(it->base, sizeof(it->base), "%s", path);
  it->d = opendir(path);
  return it->d != NULL;
}

static inline bool dir_next(DirIter* it, const char** name, bool* is_dir) {
  if (!it->d)
    return false;
  struct dirent* de = readdir(it->d);
  if (!de)
    return false;
  *name = de->d_name;
#ifdef DT_DIR
  // d_type is filled in by every filesystem that matters here (ext4, exFAT,
  // APFS, emscripten's MEMFS), so the common case needs no syscall at all.
  // Two cases still do: DT_UNKNOWN (filesystem declined to say) and DT_LNK —
  // a symlink to a directory must browse as a directory, and only stat(),
  // which follows the link, can tell. Both are rare, so the fast path holds.
  if (de->d_type != DT_UNKNOWN && de->d_type != DT_LNK) {
    *is_dir = (de->d_type == DT_DIR);
    return true;
  }
#endif
  char full[1024];
  snprintf(full, sizeof(full), "%s/%s", it->base, de->d_name);
  struct stat st;
  *is_dir = (stat(full, &st) == 0) && S_ISDIR(st.st_mode);
  return true;
}

static inline void dir_close(DirIter* it) {
  if (it->d)
    closedir(it->d);
  it->d = NULL;
}

#endif

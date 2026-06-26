#!/bin/bash

# this dir is where file-browser starts
WORK_DIR="${ROM_DIR:-$HOME}/raypoketrack"

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BIN="${SCRIPT_DIR}/raypoketrack/raypoketrack"
LOG="$SCRIPT_DIR/launch.log"

# Use framebuffer console for errors if no terminal
if [ ! -t 1 ] && [ -w /dev/tty1 ]; then
  export TERM=linux
  exec > /dev/tty1 2>&1
fi

echo "=== Launch started $(date) =>" >> "$LOG"

if [ ! -x "${BIN}" ]; then
  MSG="RayPokeTrack not found. Run 'Update RayPokeTrack' first."
  echo "ERROR: $MSG" >> "$LOG"
  CURR_TTY=$(tty 2>/dev/null)
  if [ -z "$CURR_TTY" ] || [ "$CURR_TTY" = "not a tty" ]; then
    CURR_TTY=/dev/tty1
  fi
  if [ -x /opt/inttools/gptokeyb ]; then
    /opt/inttools/gptokeyb -1 "raypoketrack_err" -c "/opt/inttools/keys.gptk" >/dev/null 2>&1 &
    GPTOKEYB_PID=$!
  fi
  dialog --msgbox "$MSG" 7 50 2>&1 > "$CURR_TTY"
  [ -n "$GPTOKEYB_PID" ] && kill "$GPTOKEYB_PID" 2>/dev/null
  exit 1
fi

mkdir -p "${WORK_DIR}"
cd "${WORK_DIR}"

if [ -z "$DISPLAY" ] && [ -z "$WAYLAND_DISPLAY" ]; then
  export SDL_VIDEODRIVER=kmsdrm
  export SDL_VIDEO_EGL_DRIVER=libEGL.so
fi

exec "${BIN}" --fullscreen >> "$LOG" 2>&1

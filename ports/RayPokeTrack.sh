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

CURR_TTY=$(tty 2>/dev/null)
if [ -z "$CURR_TTY" ] || [ "$CURR_TTY" = "not a tty" ]; then
  CURR_TTY=/dev/tty1
fi

if [ -x /opt/inttools/gptokeyb ]; then
  /opt/inttools/gptokeyb -1 "RayPokeTrack.sh" -c "/opt/inttools/keys.gptk" >/dev/null 2>&1 &
  GPTOKEYB_PID=$!
fi

cleanup() { [ -n "$GPTOKEYB_PID" ] && kill "$GPTOKEYB_PID" 2>/dev/null; }
trap cleanup EXIT

echo "=== Launch started $(date) =>" >> "$LOG"

if [ ! -x "${BIN}" ]; then
  MSG="RayPokeTrack not found. Run 'Update RayPokeTrack' first."
  echo "ERROR: $MSG" >> "$LOG"
  dialog --msgbox "$MSG" 7 50 2>&1 > "$CURR_TTY"
  exit 1
fi

mkdir -p "${WORK_DIR}"
cd "${WORK_DIR}"

if [ -z "$DISPLAY" ] && [ -z "$WAYLAND_DISPLAY" ]; then
  export SDL_VIDEODRIVER=kmsdrm
  export SDL_VIDEO_EGL_DRIVER=libEGL.so
fi

if [ -f /roms/tools/PortMaster/gamecontrollerdb.txt ]; then
  export SDL_GAMECONTROLLERCONFIG_FILE=/roms/tools/PortMaster/gamecontrollerdb.txt
fi
# GO-Super Gamepad (R36Max / odroidgo3) — corrected face button layout
export SDL_GAMECONTROLLERCONFIG="190000004b4800000011000000010000,GO-Super Gamepad,a:b0,b:b1,back:b12,dpdown:b9,dpleft:b10,dpright:b11,dpup:b8,guide:b16,leftshoulder:b4,leftstick:b14,lefttrigger:b6,leftx:a0,lefty:a1,rightshoulder:b5,rightstick:b15,righttrigger:b7,rightx:a2,righty:a3,start:b13,x:b3,y:b2,platform:Linux,"

exec "${BIN}" --fullscreen >> "$LOG" 2>&1

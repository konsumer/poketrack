#!/bin/bash

WORK_DIR="${HOME}/raypoketrack"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BIN="${SCRIPT_DIR}/raypoketrack/raypoketrack"
LOG="$SCRIPT_DIR/launch.log"

# Use framebuffer console for errors if no terminal
if [ ! -t 1 ] && [ -w /dev/tty1 ]; then
  export TERM=linux
  exec > /dev/tty1 2>&1
fi

echo "=== Launch started $(date) =>" >> "$LOG"

if [ "$(id -u)" != "0" ]; then
  exec sudo "$0" "$@"
fi

if [ ! -x "${BIN}" ]; then
  MSG="RayPokeTrack not found. Run 'Update RayPokeTrack' first."
  echo "ERROR: $MSG" >> "$LOG"
  dialog --msgbox "$MSG" 7 50
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

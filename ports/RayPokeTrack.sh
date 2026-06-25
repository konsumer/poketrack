#!/bin/bash

WORK_DIR="${HOME}/raypoketrack"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BIN="${SCRIPT_DIR}/raypoketrack/raypoketrack"
LOG="$SCRIPT_DIR/launch.log"

exec >> "$LOG" 2>&1
echo "=== Launch started $(date) ==="

if [ "$(id -u)" != "0" ]; then
  exec sudo "$0" "$@"
fi

if [ ! -x "${BIN}" ]; then
  echo "ERROR: binary not found: ${BIN}"
  exit 1
fi

mkdir -p "${WORK_DIR}"
cd "${WORK_DIR}"

if [ -z "$DISPLAY" ] && [ -z "$WAYLAND_DISPLAY" ]; then
  export SDL_VIDEODRIVER=kmsdrm
  export SDL_VIDEO_EGL_DRIVER=libEGL.so
fi

# Load controller mappings from PortMaster DB if available, then override with known working mappings
if [ -f /roms/tools/PortMaster/gamecontrollerdb.txt ]; then
  export SDL_GAMECONTROLLERCONFIG_FILE=/roms/tools/PortMaster/gamecontrollerdb.txt
fi
# Ensure GO-Super Gamepad (R36Max / odroidgo3) buttons all work
export SDL_GAMECONTROLLERCONFIG="190000004b4800000011000000010000,GO-Super Gamepad,a:b0,b:b1,back:b12,dpdown:b9,dpleft:b10,dpright:b11,dpup:b8,guide:b16,leftshoulder:b4,leftstick:b14,lefttrigger:b6,leftx:a0,lefty:a1,rightshoulder:b5,rightstick:b15,righttrigger:b7,rightx:a2,righty:a3,start:b13,x:b3,y:b2,platform:Linux,"

exec "${BIN}" --fullscreen

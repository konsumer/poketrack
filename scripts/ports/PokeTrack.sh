#!/bin/bash

# this dir is where file-browser starts. it can get large, so reuse an existing
# poketrack dir on the ROM tree if there is one (roms2 first, then roms).
# on first run none exist, so default to HOME rather than risk creating it in an
# unused ROM mount-point. users can move it onto the SD card and it'll be found.
for WORK_DIR in /roms2/poketrack /roms/poketrack "$HOME/poketrack"; do
  [ -d "$WORK_DIR" ] && break
done

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BIN="${SCRIPT_DIR}/poketrack/poketrack"
LOG="$SCRIPT_DIR/launch.log"

echo "=== Launch started $(date) =>" >> "$LOG"

if [ ! -x "${BIN}" ]; then
  MSG="PokeTrack not found. Run 'Update PokeTrack' first."
  echo "ERROR: $MSG" >> "$LOG"

  CURR_TTY="/dev/tty1"
  export TERM=linux
  pkill -f "gptokeyb -1 poketrack_err" || true
  sleep 0.1
  /opt/inttools/gptokeyb -1 "poketrack_err" -c "/opt/inttools/keys.gptk" >/dev/null 2>&1 &
  GPTOKEYB_PID=$!
  sleep 0.2

  dialog --msgbox "$MSG" 7 50 2>&1 > "$CURR_TTY"
  kill "$GPTOKEYB_PID" 2>/dev/null
  exit 1
fi

mkdir -p "${WORK_DIR}"
cd "${WORK_DIR}"

if [ -z "$DISPLAY" ] && [ -z "$WAYLAND_DISPLAY" ]; then
  export SDL_VIDEODRIVER=kmsdrm
  export SDL_VIDEO_EGL_DRIVER=libEGL.so
fi

export SDL_GAMECONTROLLERCONFIG_FILE="/opt/inttools/gamecontrollerdb.txt"

# You can also set override like this
# export SDL_GAMECONTROLLERCONFIG="190000004b4800000011000000010000,GO-Super Gamepad,a:b1,b:b0,back:b12,dpdown:b9,dpleft:b10,dpright:b11,dpup:b8,guide:b16,leftshoulder:b4,leftstick:b14,lefttrigger:b6,leftx:a0,lefty:a1,rightshoulder:b5,rightstick:b15,righttrigger:b7,rightx:a2,righty:a3,start:b13,x:b2,y:b3,platform:Linux,"

# you might want to add --width and --height here
exec "${BIN}" --fullscreen >> "$LOG" 2>&1

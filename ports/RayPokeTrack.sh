#!/bin/bash

# Start raypoketrack

# set this to your "start" directory
WORK_DIR="${HOME}/raypoketrack"

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BIN="${SCRIPT_DIR}/raypoketrack/raypoketrack"
LOG="$SCRIPT_DIR/launch.log"

exec >> "$LOG" 2>&1
echo "=== Launch started $(date) ==="
echo "OS=$(uname -s) ARCH=$(uname -m) DISPLAY=$DISPLAY WAYLAND_DISPLAY=$WAYLAND_DISPLAY"

if [ "$(id -u)" != "0" ]; then
  echo "not root, re-running with sudo..."
  exec sudo "$0" "$@"
fi
echo "user: $(id)"
echo "dri: $(ls -la /dev/dri/ 2>&1)"

if [ ! -x "${BIN}" ]; then
  echo "ERROR: binary not found or not executable: ${BIN}"
  exit 1
fi

mkdir -p "${WORK_DIR}"
cd "${WORK_DIR}"

echo "user: $(id)"
echo "dri devices:"
ls -la /dev/dri/ 2>&1

exec "${BIN}" --fullscreen

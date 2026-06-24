#!/bin/bash

# Start raypoketrack

# set this to your "start" directory
WORK_DIR="${HOME}/raypoketrack"

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BIN="${SCRIPT_DIR}/raypoketrack/raypoketrack"

if [ ! -x "${BIN}" ]; then
  dialog --msgbox "RayPokeTrack not found.\nRun 'Update RayPokeTrack.sh' first." 7 50
  exit 1
fi

mkdir -p "${WORK_DIR}"
cd "${WORK_DIR}"
exec "${BIN}" --fullscreen

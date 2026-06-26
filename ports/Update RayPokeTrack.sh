#!/bin/bash

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
DEST="$SCRIPT_DIR/raypoketrack"
REPO="konsumer/raypoketrack"
API="https://api.github.com/repos/$REPO/releases/latest"
RAW="https://raw.githubusercontent.com/$REPO/refs/tags"
LOG="$SCRIPT_DIR/update.log"

# Use framebuffer console if no terminal (ES launches without TTY)
if [ ! -t 1 ] && [ -w /dev/tty1 ]; then
  export TERM=linux
  exec > /dev/tty1 2>&1
fi

CURR_TTY=$(tty 2>/dev/null)
if [ -z "$CURR_TTY" ] || [ "$CURR_TTY" = "not a tty" ]; then
  CURR_TTY=/dev/tty1
fi

if [ -x /opt/inttools/gptokeyb ]; then
  /opt/inttools/gptokeyb -1 "Update RayPokeTrack.sh" -c "/opt/inttools/keys.gptk" >/dev/null 2>&1 &
  GPTOKEYB_PID=$!
fi

cleanup() { [ -n "$GPTOKEYB_PID" ] && kill "$GPTOKEYB_PID" 2>/dev/null; }
trap cleanup EXIT

log()    { echo "$1"; echo "$1" >> "$LOG"; }
notify() { log "[INFO] $1"; dialog --infobox "$1" 5 50 2>&1 > "$CURR_TTY"; }
error()  { log "[ERROR] $1"; dialog --msgbox "Error: $1" 7 50 2>&1 > "$CURR_TTY"; exit 1; }

log "=== Update started $(date) ==="

OS="$(uname -s)"
ARCH="$(uname -m)"

case "$OS" in
  Linux)
    case "$ARCH" in
      aarch64|arm64)
        if [ -z "$DISPLAY" ] && [ -z "$WAYLAND_DISPLAY" ]; then
          ASSET="raypoketrack-linux-arm64-sdl.zip"
        else
          ASSET="raypoketrack-linux-arm64.zip"
        fi
        ;;
      *) ASSET="raypoketrack-linux.zip" ;;
    esac
    ;;
  Darwin) ASSET="raypoketrack-macos.zip" ;;
  *) error "Unsupported platform: $OS" ;;
esac
log "Platform: $OS/$ARCH -> $ASSET"

notify "Fetching latest release..."
RELEASE="$(curl -sf "$API" 2>>"$LOG")"
[ -z "$RELEASE" ] && error "Could not reach GitHub API"

TAG="$(echo "$RELEASE" | grep -o '"tag_name": *"[^"]*"' | grep -o '"[^"]*"$' | tr -d '"')"
[ -z "$TAG" ] && error "Could not determine latest tag"
log "Latest: $TAG"

URL="$(echo "$RELEASE" | grep -o "\"browser_download_url\": *\"[^\"]*$ASSET\"" | grep -o 'https://[^"]*')"
[ -z "$URL" ] && error "Could not find asset: $ASSET"

notify "Updating port scripts..."
for SCRIPT in "RayPokeTrack.sh" "Update RayPokeTrack.sh"; do
  ENCODED="$(echo "$SCRIPT" | sed 's/ /%20/g')"
  curl -sf "$RAW/$TAG/ports/$ENCODED" -o "$SCRIPT_DIR/$SCRIPT" 2>>"$LOG" \
    && log "  updated: $SCRIPT" || log "  warning: could not update $SCRIPT"
  chmod +x "$SCRIPT_DIR/$SCRIPT" 2>/dev/null
done

notify "Downloading $ASSET..."
TMP="$(mktemp -d)"
curl -L "$URL" -o "$TMP/$ASSET" 2>>"$LOG" || error "Download failed"

notify "Extracting..."
rm -rf "$DEST"
mkdir -p "$DEST"
unzip -q "$TMP/$ASSET" -d "$DEST" 2>>"$LOG" || error "Extract failed"
chmod +x "$DEST"/raypoketrack* 2>/dev/null
rm -rf "$TMP"

dialog --msgbox "RayPokeTrack updated to $TAG." 5 50 2>&1 > "$CURR_TTY"

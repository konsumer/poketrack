#!/bin/bash

# Download & extract latest RayPokeTrack release for current platform

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
DEST="$SCRIPT_DIR/raypoketrack"
REPO="konsumer/raypoketrack"
API="https://api.github.com/repos/$REPO/releases/latest"
LOG="$SCRIPT_DIR/update.log"

exec > >(tee -a "$LOG") 2>&1
echo "=== Update started $(date) ==="
echo "OS=$(uname -s) ARCH=$(uname -m) DISPLAY=$DISPLAY WAYLAND_DISPLAY=$WAYLAND_DISPLAY"

notify() { echo "[INFO] $1"; dialog --infobox "$1" 5 50 2>/dev/null || true; }
error()  { echo "[ERROR] $1"; dialog --msgbox "Error: $1" 7 50 2>/dev/null || true; exit 1; }

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

notify "Fetching latest release..."
URL="$(curl -sf "$API" | grep -o "\"browser_download_url\": *\"[^\"]*$ASSET\"" | grep -o 'https://[^"]*')"

[ -z "$URL" ] && error "Could not find asset: $ASSET"

notify "Downloading $ASSET..."
TMP="$(mktemp -d)"
curl -L "$URL" -o "$TMP/$ASSET" || error "Download failed"

notify "Extracting..."
rm -rf "$DEST"
mkdir -p "$DEST"
unzip -q "$TMP/$ASSET" -d "$DEST" || error "Extract failed"
chmod +x "$DEST"/raypoketrack* 2>/dev/null

rm -rf "$TMP"
dialog --msgbox "RayPokeTrack updated." 5 40

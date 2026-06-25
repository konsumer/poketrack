#!/bin/bash

# Download & extract latest RayPokeTrack release for current platform

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
DEST="$SCRIPT_DIR/raypoketrack"
REPO="konsumer/raypoketrack"
API="https://api.github.com/repos/$REPO/releases/latest"
RAW="https://raw.githubusercontent.com/$REPO/refs/tags"
LOG="$SCRIPT_DIR/update.log"

exec >> "$LOG" 2>&1
echo "=== Update started $(date) ==="

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
RELEASE="$(curl -sf "$API")"
[ -z "$RELEASE" ] && error "Could not reach GitHub API"

TAG="$(echo "$RELEASE" | grep -o '"tag_name": *"[^"]*"' | grep -o '"[^"]*"$' | tr -d '"')"
[ -z "$TAG" ] && error "Could not determine latest tag"
echo "Latest tag: $TAG"

URL="$(echo "$RELEASE" | grep -o "\"browser_download_url\": *\"[^\"]*$ASSET\"" | grep -o 'https://[^"]*')"
[ -z "$URL" ] && error "Could not find asset: $ASSET"

notify "Updating port scripts..."
for SCRIPT in "RayPokeTrack.sh" "Update RayPokeTrack.sh" "Enable SSH.sh"; do
  ENCODED="$(python3 -c "import urllib.parse; print(urllib.parse.quote('$SCRIPT'))" 2>/dev/null || echo "$SCRIPT" | sed 's/ /%20/g')"
  curl -sf "$RAW/$TAG/ports/$ENCODED" -o "$SCRIPT_DIR/$SCRIPT" && echo "updated: $SCRIPT" || echo "warning: could not update $SCRIPT"
  chmod +x "$SCRIPT_DIR/$SCRIPT" 2>/dev/null
done

notify "Downloading $ASSET..."
TMP="$(mktemp -d)"
curl -L "$URL" -o "$TMP/$ASSET" || error "Download failed"

notify "Extracting..."
rm -rf "$DEST"
mkdir -p "$DEST"
unzip -q "$TMP/$ASSET" -d "$DEST" || error "Extract failed"
chmod +x "$DEST"/raypoketrack* 2>/dev/null

rm -rf "$TMP"
dialog --msgbox "RayPokeTrack updated to $TAG." 5 50 2>/dev/null || echo "Done: $TAG"

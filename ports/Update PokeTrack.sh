#!/bin/bash

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

main() {
  DEST="$SCRIPT_DIR/poketrack"
  REPO="konsumer/poketrack"
  API="https://api.github.com/repos/$REPO/releases/latest"
  RAW="https://raw.githubusercontent.com/$REPO/refs/tags"
  LOG="$SCRIPT_DIR/update.log"

  # Same working-dir selection as PokeTrack.sh: reuse an existing poketrack
  # dir on the ROM tree if there is one (roms2 first, then roms), otherwise
  # default to HOME. Examples get extracted here, alongside the song data.
  for WORK_DIR in /roms2/poketrack /roms/poketrack "$HOME/poketrack"; do
    [ -d "$WORK_DIR" ] && break
  done

  CURR_TTY="/dev/tty1"
  export TERM=linux

  log()     { echo "$1"; echo "$1" >> "$LOG"; }
  notify()  { log "[INFO] $1"; dialog --infobox "$1" 5 50 2>&1 > "$CURR_TTY"; }
  error()   { log "[ERROR] $1"; notify "Error: $1"; sleep 3; exit 1; }
  confirm() { dialog --yesno "$1" 7 50 2>&1 > "$CURR_TTY"; }

  log "=== Update started $(date) ==="

  OS="$(uname -s)"
  ARCH="$(uname -m)"

  case "$OS" in
    Linux)
      case "$ARCH" in
        aarch64|arm64)
          if [ -z "$DISPLAY" ] && [ -z "$WAYLAND_DISPLAY" ]; then
            ASSET="poketrack-linux-arm64-sdl.zip"
          else
            ASSET="poketrack-linux-arm64.zip"
          fi
          ;;
        *) ASSET="poketrack-linux.zip" ;;
      esac
      ;;
    Darwin) ASSET="poketrack-macos.zip" ;;
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
  for SCRIPT in "PokeTrack.sh" "Update PokeTrack.sh"; do
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
  chmod +x "$DEST"/poketrack* 2>/dev/null
  rm -rf "$TMP"

  if confirm "Download example songs, instruments, and plugins too?"; then
    EX_URL="$(echo "$RELEASE" | grep -o '"browser_download_url": *"[^"]*examples\.zip"' | grep -o 'https://[^"]*')"
    if [ -z "$EX_URL" ]; then
      log "  warning: could not find examples.zip in release $TAG"
    else
      notify "Downloading examples..."
      mkdir -p "$WORK_DIR"
      EX_TMP="$(mktemp -d)"
      if curl -L "$EX_URL" -o "$EX_TMP/examples.zip" 2>>"$LOG"; then
        unzip -oq "$EX_TMP/examples.zip" -d "$WORK_DIR" 2>>"$LOG" \
          && log "  examples extracted to: $WORK_DIR" \
          || log "  warning: could not extract examples.zip"
      else
        log "  warning: could not download examples.zip"
      fi
      rm -rf "$EX_TMP"
    fi
  fi

  notify "PokeTrack updated to $TAG."
  sleep 3
}

main "$@"

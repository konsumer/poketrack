#!/usr/bin/env bash
# build.sh [output-dir]
#
# Compiles plugin.c into a WCLAP plugin (.wasm) using wasi-sdk's -pthread
# (wasm32-wasip1-threads) toolchain — see README.md for what this
# demonstrates and how to get wasi-sdk.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUT_DIR="$(cd "${1:-$SCRIPT_DIR/build}" 2>/dev/null && pwd || (mkdir -p "${1:-$SCRIPT_DIR/build}" && cd "${1:-$SCRIPT_DIR/build}" && pwd))"

: "${WASI_SDK_PATH:=/opt/wasi-sdk}"
if [ ! -x "$WASI_SDK_PATH/bin/clang" ]; then
  echo "wasi-sdk not found at $WASI_SDK_PATH (set WASI_SDK_PATH) — see README.md#install" >&2
  exit 1
fi

# CLAP headers: reuse poketrack's own CMake-fetched copy if present, else a
# cached shallow clone under vendor/ (gitignored), else fetch one now.
POKETRACK_FETCHED="$SCRIPT_DIR/../../build/_deps/wclap-bridge-src/modules/clap/include"
VENDORED="$SCRIPT_DIR/vendor/clap/include"
if [ -n "${CLAP_INCLUDE:-}" ]; then
  : # explicit override
elif [ -d "$POKETRACK_FETCHED" ]; then
  CLAP_INCLUDE="$POKETRACK_FETCHED"
elif [ -d "$VENDORED" ]; then
  CLAP_INCLUDE="$VENDORED"
else
  echo "Fetching CLAP headers into $SCRIPT_DIR/vendor/clap ..." >&2
  git clone --depth 1 https://github.com/free-audio/clap "$SCRIPT_DIR/vendor/clap" >&2
  CLAP_INCLUDE="$VENDORED"
fi

"$WASI_SDK_PATH/bin/clang" --target=wasm32-wasip1-threads -pthread -mexec-model=reactor -O2 \
  -Wl,--import-memory -Wl,--export-table \
  -Wl,--initial-memory=1310720 -Wl,--max-memory=1073741824 \
  -Wl,--export=malloc -Wl,--export=clap_entry -Wl,--growable-table \
  -I"$CLAP_INCLUDE" \
  "$SCRIPT_DIR/plugin.c" \
  -lm \
  -o "$OUT_DIR/pthread-synth.wasm"

echo "Built $OUT_DIR/pthread-synth.wasm"

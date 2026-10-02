#!/usr/bin/env bash
# build.sh [output-dir]
#
# Builds the trommelsynthesizer WCLAP instrument (trommelsynthesizer.wasm) with wasi-sdk. Plain C, no
# threads, exported memory, so it loads on every target (see
# plugins/README.md). src/samples.c #embeds the blobs in src/samples/, which
# scripts/prep_samples.py generates (see README.md#samples); without it
# src/samples_stub.c is used and the sampled machines are silent.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUT_DIR="$(cd "${1:-$SCRIPT_DIR/build}" 2>/dev/null && pwd || (mkdir -p "${1:-$SCRIPT_DIR/build}" && cd "${1:-$SCRIPT_DIR/build}" && pwd))"

: "${WASI_SDK_PATH:=/opt/wasi-sdk}"
if [ ! -x "$WASI_SDK_PATH/bin/clang" ]; then
  echo "wasi-sdk not found at $WASI_SDK_PATH (set WASI_SDK_PATH) — see README.md#building" >&2
  exit 1
fi

SAMPLES="$SCRIPT_DIR/src/samples.c"
if [ ! -f "$SAMPLES" ]; then
  echo "src/samples.c not found, so TR-707/505 will be silent: run scripts/prep_samples.py WAV_ROOT (see README.md#samples)" >&2
  SAMPLES="$SCRIPT_DIR/src/samples_stub.c"
fi

# CLAP headers: reuse poketrack's own CMake-fetched copy if present, else a
# cached shallow clone under vendor/ (gitignored), else fetch one now.
POKETRACK_FETCHED="$SCRIPT_DIR/../../build/_deps/clap-src/include"
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

# -std=c23 for #embed.
"$WASI_SDK_PATH/bin/clang" --target=wasm32-wasip1 -mexec-model=reactor -std=c23 -O2 \
  -Wall -Wextra \
  -Wl,--export-memory -Wl,--export-table \
  -Wl,--initial-memory=16777216 -Wl,--max-memory=268435456 \
  -Wl,--export=malloc -Wl,--export=clap_entry -Wl,--growable-table \
  -I"$CLAP_INCLUDE" -I"$SCRIPT_DIR/src" \
  "$SCRIPT_DIR/src/plugin.c" \
  "$SCRIPT_DIR/src/trommelsynthesizer.c" \
  "$SAMPLES" \
  -lm \
  -o "$OUT_DIR/trommelsynthesizer.wasm"

echo "Built $OUT_DIR/trommelsynthesizer.wasm"

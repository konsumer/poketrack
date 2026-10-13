#!/usr/bin/env bash
# build.sh [output-dir]
#
# Builds the syntakt WCLAP *template* (syntakt-template.wasm) with wasi-sdk:
# the plugin with an empty engine slot. It contains nothing of Elektron's and
# is silent until you patch your own Syntakt OS into it (scripts/make-plugin.mjs,
# or the generator page in web/). Plain C, no threads, exported memory.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUT_DIR="$(cd "${1:-$SCRIPT_DIR/build}" 2>/dev/null && pwd || (mkdir -p "${1:-$SCRIPT_DIR/build}" && cd "${1:-$SCRIPT_DIR/build}" && pwd))"

: "${WASI_SDK_PATH:=/opt/wasi-sdk}"
if [ ! -x "$WASI_SDK_PATH/bin/clang" ]; then
  echo "wasi-sdk not found at $WASI_SDK_PATH (set WASI_SDK_PATH) — see README.md#building" >&2
  exit 1
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

# No wasm-opt: its memory packing could split the engine slot, which the
# generator expects to find in one piece. clang runs wasm-opt by itself when
# it's on PATH, hence the minimal PATH.
# -fno-jump-tables: the interpreter's dispatch switch must not become a wasm
# br_table. wasmtime 39 (what poketrack bundles) compiles br_table and
# call_indirect several times slower on arm64, and per emulated instruction
# that made the engine ~5x slower.
PATH=/usr/bin:/bin "$WASI_SDK_PATH/bin/clang" --target=wasm32-wasip1 -mexec-model=reactor -std=gnu17 -O2 -fno-jump-tables \
  -Wall -Wextra \
  -Wl,--export-memory -Wl,--export-table \
  -Wl,--initial-memory=16777216 -Wl,--max-memory=268435456 \
  -Wl,--export=malloc -Wl,--export=clap_entry -Wl,--growable-table \
  -I"$CLAP_INCLUDE" -I"$SCRIPT_DIR/src" \
  "$SCRIPT_DIR/src/plugin.c" \
  "$SCRIPT_DIR/src/synth.c" \
  "$SCRIPT_DIR/src/machines.c" \
  "$SCRIPT_DIR/src/slot.c" \
  "$SCRIPT_DIR/src/engine.c" \
  "$SCRIPT_DIR/src/cf.c" \
  -lm \
  -o "$OUT_DIR/syntakt-template.wasm"

# the slot must be in the file exactly once
count=$(grep -c -a -o 'SYNTAKT-ENGINE-SLOT-v1' "$OUT_DIR/syntakt-template.wasm" || true)
if [ "$count" != "1" ]; then
  echo "engine slot marker found $count times in syntakt-template.wasm (expected 1)" >&2
  exit 1
fi

echo "Built $OUT_DIR/syntakt-template.wasm"

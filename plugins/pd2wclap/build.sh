#!/usr/bin/env bash
# build.sh <patch.pd> <name> [output-dir]
#
# Compiles a Pure Data patch into a self-contained WCLAP plugin (.wasm):
#   patch.pd -> pd2ast -> pdast2wclap -> <name>.c -> wasi-sdk clang -> <name>.wasm
#
# See README.md for how to install pd2ast/pdast2wclap and wasi-sdk.
set -euo pipefail

if [ $# -lt 2 ]; then
  echo "usage: $0 <patch.pd> <name> [output-dir]" >&2
  exit 1
fi

PATCH="$1"
NAME="$2"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUT_DIR="$(cd "${3:-$SCRIPT_DIR/build}" 2>/dev/null && pwd || (mkdir -p "${3:-$SCRIPT_DIR/build}" && cd "${3:-$SCRIPT_DIR/build}" && pwd))"

PD2AST="${PD2AST:-pd2ast}"
PDAST2WCLAP="${PDAST2WCLAP:-pdast2wclap}"
command -v "$PD2AST" >/dev/null || { echo "pd2ast not found — see README.md#install" >&2; exit 1; }
command -v "$PDAST2WCLAP" >/dev/null || { echo "pdast2wclap not found — see README.md#install" >&2; exit 1; }

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

GEN_C="$OUT_DIR/$NAME.c"
"$PD2AST" "$PATCH" | "$PDAST2WCLAP" - -o "$GEN_C"

# The AUR wasi-sdk-git package's bundled wasm-ld can be linked against a
# libxml2 soname the rest of the system no longer ships — fall back to the
# system linker (any reasonably recent lld build works fine here) if so.
# clang wants a real path (or a bare "flavor" name it resolves itself) here,
# not a bare binary name found only via $PATH.
WASM_LD="${WASM_LD:-$(command -v wasm-ld || echo "$WASI_SDK_PATH/bin/wasm-ld")}"

"$WASI_SDK_PATH/bin/clang" --target=wasm32-wasi -mexec-model=reactor -O2 \
  -fuse-ld="$WASM_LD" \
  -I"$CLAP_INCLUDE" -I"$SCRIPT_DIR" \
  -DPD_PLUGIN_ID="\"com.poketrack.clap.$NAME\"" -DPD_PLUGIN_NAME="\"$NAME (PD)\"" \
  -Wl,--export=clap_entry -Wl,--export=malloc -Wl,--export-table -Wl,--growable-table \
  "$GEN_C" "$SCRIPT_DIR/runtime-shim.c" \
  -o "$OUT_DIR/$NAME.wasm"

echo "Built $OUT_DIR/$NAME.wasm"

#!/usr/bin/env bash
# build.sh [output-dir]
#
# Builds the Dexed WCLAP instrument (dexed.wclap.wasm) with wasi-sdk.
#
# Unlike the AssemblyScript/Rust plugin examples, this one vendors dexed's
# actual C++ engine sources (vendor/dexed) and compiles them unmodified with
# wasi-sdk's clang++ — that's the whole point: same source, same arithmetic,
# no reimplementation to drift. The plugin's own sources (src/) are the CLAP
# wrapper and the port of dexed's audio-path class around them.
#
# See README.md for what's vendored and what was adapted.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUT_DIR="$(cd "${1:-$SCRIPT_DIR/build}" 2>/dev/null && pwd || (mkdir -p "${1:-$SCRIPT_DIR/build}" && cd "${1:-$SCRIPT_DIR/build}" && pwd))"

: "${WASI_SDK_PATH:=/opt/wasi-sdk}"
if [ ! -x "$WASI_SDK_PATH/bin/clang++" ]; then
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

# The preset table is generated from vendor/syx; regenerate it whenever the
# banks are newer than the table so a hand-edited table can't drift from them.
if [ ! -f "$SCRIPT_DIR/src/presets-data.cpp" ] || [ -n "$(find "$SCRIPT_DIR/vendor/syx" -name '*.syx' -newer "$SCRIPT_DIR/src/presets-data.cpp" -print -quit)" ]; then
  echo "Regenerating src/presets-data.{h,cpp} from vendor/syx ..." >&2
  node "$SCRIPT_DIR/scripts/syx2presets.mjs" --out "$SCRIPT_DIR/src/presets-data" \
    "$SCRIPT_DIR/vendor/syx/Dexed_01.syx" $(ls "$SCRIPT_DIR"/vendor/syx/SynprezFM_*.syx | sort)
fi

# Memory: exported, not imported. poketrack's WCLAP loader accepts either, but
# an imported memory has to be a *shared* one (that's the only reason
# pthread-synth builds with -pthread), and shared memory only works in a
# browser page that's cross-origin isolated — see plugins/README.md. Exporting
# a plain memory, like the AssemblyScript plugins do, keeps this loadable
# everywhere with no isolation requirement. This plugin spawns no threads.
#
# 8 MiB initial: ~135 KB of cartridge SysEx is compiled in as data, and a
# 16-voice DX7 engine's tables/voices sit on top of that.
"$WASI_SDK_PATH/bin/clang++" --target=wasm32-wasip1 -mexec-model=reactor -std=c++17 -O2 \
  -fno-exceptions -fno-rtti \
  -Wl,--export-memory -Wl,--export-table \
  -Wl,--initial-memory=8388608 -Wl,--max-memory=268435456 \
  -Wl,--export=malloc -Wl,--export=clap_entry -Wl,--growable-table \
  -I"$CLAP_INCLUDE" -I"$SCRIPT_DIR/vendor/dexed" -I"$SCRIPT_DIR/vendor/dexed/compat" -I"$SCRIPT_DIR/src" \
  "$SCRIPT_DIR/src/plugin.cpp" \
  "$SCRIPT_DIR/src/engine.cpp" \
  "$SCRIPT_DIR/src/params.cpp" \
  "$SCRIPT_DIR/src/fx.cpp" \
  "$SCRIPT_DIR/src/cartridge.cpp" \
  "$SCRIPT_DIR/src/presets-data.cpp" \
  "$SCRIPT_DIR/vendor/dexed/msfa/"*.cc \
  "$SCRIPT_DIR/vendor/dexed/msfa/porta.cpp" \
  "$SCRIPT_DIR/vendor/dexed/EngineMkI.cpp" \
  "$SCRIPT_DIR/vendor/dexed/EngineOpl.cpp" \
  "$SCRIPT_DIR/vendor/dexed/compat/tuning.cc" \
  -lm \
  -o "$OUT_DIR/dexed.wasm"

echo "Built $OUT_DIR/dexed.wasm"

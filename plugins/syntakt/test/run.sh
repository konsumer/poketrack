#!/usr/bin/env bash
# test/run.sh [image.bin]
#
# Builds the native bench and renders every scenario in test/. Where a
# reference render from ref_bench.py exists (build/<name>.ref), the output
# must match it exactly.
set -euo pipefail

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
IMAGE="${1:-$DIR/build/section7.bin}"
if [ ! -f "$IMAGE" ]; then
  echo "no engine image at $IMAGE: run scripts/extract.mjs Syntakt_OS1.42.syx first" >&2
  exit 1
fi

mkdir -p "$DIR/build"
cc -std=gnu17 -O2 -Wall -Wextra -I"$DIR/src" -o "$DIR/build/bench" "$DIR/test/bench.c" "$DIR/src/engine.c" "$DIR/src/cf.c" -lm

fail=0
for s in "$DIR"/test/*.txt; do
  name="$(basename "$s" .txt)"
  ref="$DIR/build/$name.ref"
  printf '%s: ' "$name"
  if [ -f "$ref" ]; then
    "$DIR/build/bench" "$IMAGE" "$s" "$DIR/build/$name.raw" "$ref" | tr '\n' ' ' || fail=1
  else
    "$DIR/build/bench" "$IMAGE" "$s" "$DIR/build/$name.raw" | tr '\n' ' '
    printf '(no reference)'
  fi
  echo
done
exit $fail

#!/usr/bin/env bash
# fetch_samples.sh [dest]
#
# Fetches the drum-machine recordings and generates src/samples.c plus the
# blobs it embeds (scripts/prep_samples.py), so the sampled machines work.
#
# Only the machine folders prep_samples.py reads are downloaded (a sparse
# checkout), at a pinned commit so CI builds are reproducible. Re-run with a
# new SAMPLES_REF to move to a newer snapshot. See README.md#samples for the
# licensing caveat.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DEST="${1:-$SCRIPT_DIR/../vendor/tidal-drum-machines}"
SAMPLES_REPO="${SAMPLES_REPO:-https://github.com/geikha/tidal-drum-machines}"
SAMPLES_REF="${SAMPLES_REF:-6577395a4d05031728ced2ec3e5637fa89d8be48}"

mkdir -p "$DEST"
if [ ! -d "$DEST/.git" ]; then
  git -C "$DEST" init -q
  git -C "$DEST" remote add origin "$SAMPLES_REPO"
fi

folders=()
while IFS= read -r f; do folders+=("machines/$f"); done < <(python3 "$SCRIPT_DIR/prep_samples.py" --folders)

git -C "$DEST" sparse-checkout set --cone "${folders[@]}"
git -C "$DEST" fetch -q --depth 1 --filter=blob:none origin "$SAMPLES_REF"
git -C "$DEST" checkout -q FETCH_HEAD

python3 "$SCRIPT_DIR/prep_samples.py" "$DEST/machines"

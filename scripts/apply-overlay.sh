#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Apply Spear overlay onto a Mint root (or work/edit). Only Spear-owned files.
# Usage: apply-overlay.sh [DEST_ROOT]
# DEST defaults to SPEAR_MINT_ROOT or ../NewLatest/Spear/work/edit if present.
set -euo pipefail
SPEAR="$(cd "$(dirname "$0")/.." && pwd)"
OVER="$SPEAR/overlay"
DEST="${1:-${SPEAR_MINT_ROOT:-}}"
if [[ -z "$DEST" ]]; then
  for c in \
    "$SPEAR/work/edit" \
    /home/zachary/Desktop/SG/NewLatest/Spear/work/edit
  do
    [[ -d "$c/usr" ]] && DEST="$c" && break
  done
fi
[[ -d "$DEST" ]] || { echo "usage: $0 /path/to/mint-root"; exit 1; }
[[ -d "$OVER" ]] || { echo "no overlay"; exit 1; }

echo "Spear overlay → $DEST"
# Copy every file under overlay/ preserving relative paths (Mint replacements)
( cd "$OVER" && find . -type f ) | while read -r rel; do
  rel=${rel#./}
  mkdir -p "$DEST/$(dirname "$rel")"
  cp -a "$OVER/$rel" "$DEST/$rel"
  echo "  + /$rel"
done
# Install C++ spear into dest
if [[ -x "$SPEAR/src/spear" ]] || make -C "$SPEAR/src" -q spear 2>/dev/null || make -C "$SPEAR/src" spear; then
  install -m 0755 "$SPEAR/src/spear" "$DEST/usr/local/bin/spear"
  echo "  + /usr/local/bin/spear"
fi
echo "OK overlay applied"

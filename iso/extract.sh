#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Extract upstream ISO → work/{iso-extract,edit}
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
WORK="${SPEAR_WORK:-$ROOT/work}"
ISOX="$WORK/iso-extract"
EDIT="$WORK/edit"
ISO=$(readlink -f "${SPEAR_UPSTREAM_ISO:-$ROOT/iso/upstream-mint-cinnamon.iso}")
[[ -e "$ISO" ]] || { echo "missing $ISO — run iso/fetch-upstream.sh"; exit 1; }
command -v xorriso >/dev/null
command -v unsquashfs >/dev/null

rm -rf "$ISOX" "$EDIT"
mkdir -p "$ISOX"
echo "extract ISO tree…"
xorriso -osirrox on -indev "$ISO" -extract / "$ISOX" 2>&1 | tail -8
chmod -R u+w "$ISOX" 2>/dev/null || true
SFS=$(find "$ISOX" -name filesystem.squashfs | head -1)
[[ -n "$SFS" ]]
echo "unsquashfs (long)…"
set +e
unsquashfs -f -no-xattrs -d "$EDIT" "$SFS"
set -e
chmod -R u+w "$EDIT" 2>/dev/null || true
[[ -d "$EDIT/usr" ]]
du -sh "$EDIT" "$ISOX"
echo "extract OK · WORK=$WORK"

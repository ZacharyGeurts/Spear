#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Upstream Mint Cinnamon ISO = source material only. Product identity is Spear.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ISO_DIR="$ROOT/iso"
mkdir -p "$ISO_DIR"
TARGET="$ISO_DIR/upstream-mint-cinnamon.iso"
URL="${SPEAR_MINT_ISO_URL:-https://mirrors.kernel.org/linuxmint/stable/22.3/linuxmint-22.3-cinnamon-64bit.iso}"

if [[ -L "$TARGET" || -f "$TARGET" ]]; then
  if [[ -s "$TARGET" ]] || [[ -L "$TARGET" && -s "$(readlink -f "$TARGET" 2>/dev/null || true)" ]]; then
    echo "upstream ISO present: $TARGET"
    ls -lh "$TARGET"
    exit 0
  fi
fi

for CAND in \
  "/home/zachary/Desktop/SG/AmmoMint/iso/linuxmint-22.3-cinnamon-64bit.iso" \
  "$HOME/Desktop/SG/AmmoMint/iso/linuxmint-22.3-cinnamon-64bit.iso"
do
  if [[ -s "$CAND" ]]; then
    ln -sfn "$CAND" "$TARGET"
    echo "linked $CAND → $TARGET"
    ls -lh "$TARGET"
    exit 0
  fi
done

echo "Downloading upstream Mint (source material)…"
wget -c -O "$TARGET.partial" "$URL"
mv -f "$TARGET.partial" "$TARGET"
ls -lh "$TARGET"

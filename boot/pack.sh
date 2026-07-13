#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Pack Spear product boot image (self-contained — no KILROY tree required).
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
OUT="$ROOT/out"
STAGE="$OUT/boot-staging"
IMG="${SPEAR_IMG:-$OUT/spear-boot.img}"
OFF=1048576

bash "$HERE/build-initrd.sh" "$OUT/initramfs.cpio.gz"

mkdir -p "$STAGE/boot/spear" "$STAGE/boot/face/themes/field"
cp -f "$HERE/limine.conf" "$STAGE/limine.conf"
cp -f "$OUT/initramfs.cpio.gz" "$STAGE/boot/spear/initramfs.cpio.gz"

# kernel: prefer out/vmlinuz, else running kernel
if [[ -f "$OUT/vmlinuz" ]]; then
  cp -f "$OUT/vmlinuz" "$STAGE/boot/spear/vmlinuz"
elif [[ -f "/boot/vmlinuz-$(uname -r)" ]]; then
  cp -f "/boot/vmlinuz-$(uname -r)" "$STAGE/boot/spear/vmlinuz"
else
  K=$(ls -1 /boot/vmlinuz-* 2>/dev/null | head -1 || true)
  [[ -n "$K" ]] && cp -f "$K" "$STAGE/boot/spear/vmlinuz"
fi
[[ -f "$STAGE/boot/spear/vmlinuz" ]] || { echo "no vmlinuz — place out/vmlinuz"; exit 1; }

# optional face wallpaper from overlay
if [[ -f "$ROOT/overlay/usr/share/backgrounds/spear/spear-default.png" ]]; then
  cp -f "$ROOT/overlay/usr/share/backgrounds/spear/spear-default.png" \
    "$STAGE/boot/face/themes/field/wallpaper.png" 2>/dev/null || true
fi

# Build sparse raw image with ESP-like partition for limine if mtools available
if command -v mformat >/dev/null && command -v mcopy >/dev/null; then
  # 512 MiB image
  dd if=/dev/zero of="$IMG" bs=1M count=512 status=none
  # simple FAT at offset 1MiB
  if command -v parted >/dev/null; then
    parted -s "$IMG" mklabel msdos mkpart primary fat32 1MiB 100% set 1 boot on 2>/dev/null || true
  fi
  export MTOOLS_SKIP_CHECK=1
  mformat -i "${IMG}@@${OFF}" -F :: 2>/dev/null || mformat -i "${IMG}@@${OFF}" ::
  for d in boot boot/spear boot/face boot/face/themes/field; do
    mmd -i "${IMG}@@${OFF}" "::/$d" 2>/dev/null || true
  done
  mcopy -o -i "${IMG}@@${OFF}" "$STAGE/limine.conf" ::/limine.conf
  mcopy -o -i "${IMG}@@${OFF}" "$STAGE/boot/spear/initramfs.cpio.gz" ::/boot/spear/initramfs.cpio.gz
  mcopy -o -i "${IMG}@@${OFF}" "$STAGE/boot/spear/vmlinuz" ::/boot/spear/vmlinuz
  echo "OK packed $IMG"
else
  echo "mtools missing — staging only at $STAGE"
fi

cp -f "$ROOT/src/spear" "$OUT/spear" 2>/dev/null || true
echo "initrd=$OUT/initramfs.cpio.gz staging=$STAGE"

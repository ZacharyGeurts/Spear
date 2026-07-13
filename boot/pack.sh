#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
BUILD="${KILROY_BUILD:-/home/zachary/Desktop/SG/zpics/kilroy-qemu-build}"
IMG="${KILROY_IMG:-$BUILD/grok-kilroy.img}"
FACE="${KILROY_FACE:-/home/zachary/Desktop/SG/NewLatest/KILROY/boot/face}"
OFF=1048576

bash "$HERE/build-initrd.sh" "$BUILD/initramfs.cpio.gz"
cp -f "$HERE/limine.conf" "$FACE/limine.conf"
cp -f "$HERE/limine.conf" /home/zachary/Desktop/SG/NewLatest/KILROY/boot/limine.conf 2>/dev/null || true
mkdir -p "$BUILD/grok-staging/boot/kilroy"
cp -f "$HERE/limine.conf" "$BUILD/grok-staging/limine.conf"
cp -f "$BUILD/initramfs.cpio.gz" "$BUILD/grok-staging/boot/kilroy/initramfs.cpio.gz"
[[ -f "$BUILD/grok-staging/boot/kilroy/vmlinuz-lab" ]] || \
  cp -f "/boot/vmlinuz-$(uname -r)" "$BUILD/grok-staging/boot/kilroy/vmlinuz-lab" 2>/dev/null || true

export MTOOLS_SKIP_CHECK=1
for d in boot boot/kilroy boot/face boot/face/fonts boot/face/themes/field; do
  mmd -i "${IMG}@@${OFF}" "::/$d" 2>/dev/null || true
done
mcopy -o -i "${IMG}@@${OFF}" "$HERE/limine.conf" ::/limine.conf
mcopy -o -i "${IMG}@@${OFF}" "$BUILD/initramfs.cpio.gz" ::/boot/kilroy/initramfs.cpio.gz
mcopy -o -i "${IMG}@@${OFF}" "$BUILD/grok-staging/boot/kilroy/vmlinuz-lab" ::/boot/kilroy/vmlinuz-lab
[[ -f "$FACE/themes/field/wallpaper.bmp" ]] && \
  mcopy -o -i "${IMG}@@${OFF}" "$FACE/themes/field/wallpaper.bmp" ::/boot/face/themes/field/wallpaper.bmp || true

mkdir -p "$ROOT/out"
cp -f "$BUILD/initramfs.cpio.gz" "$ROOT/out/initramfs.cpio.gz"
cp -f "$ROOT/src/spear" "$ROOT/out/spear"
echo "OK packed $IMG"

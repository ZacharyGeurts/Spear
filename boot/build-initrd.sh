#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Build initramfs: busybox + C++ spear + field_drive.sh + init
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
OUT="${1:-$ROOT/out/initramfs.cpio.gz}"
STAGE="$ROOT/out/initrd-root"
BB="$(command -v busybox)"

echo "== build C++ spear (prefer static) =="
SRCS="spear_main.cpp spear_ffat.cpp spear_elevate.cpp spear_field1.cpp spear_chip.cpp spear_fieldram.cpp spear_fieldmem.cpp spear_patdict.cpp"
if (cd "$ROOT/src" && g++ -O2 -Wall -std=c++17 -static -o spear $SRCS -lm) 2>/dev/null; then
  echo "static spear OK"
else
  make -C "$ROOT/src" all
  echo "dynamic spear"
fi

rm -rf "$STAGE"
mkdir -p "$STAGE"/{bin,sbin,dev,proc,sys,tmp,etc,mnt/field,usr/local/bin,lib,lib64,usr/lib}

cp -a "$BB" "$STAGE/bin/busybox"
while read -r a; do
  [[ -z "$a" || "$a" == busybox ]] && continue
  ln -sf busybox "$STAGE/bin/$a"
done < <("$BB" --list)
ln -sf ../bin/busybox "$STAGE/sbin/fdisk"
ln -sf ../bin/busybox "$STAGE/sbin/mke2fs"

install -m 0755 "$ROOT/src/spear" "$STAGE/usr/local/bin/spear"
install -m 0755 "$HERE/field_drive.sh" "$STAGE/usr/local/bin/spear-field-drive"
install -m 0755 "$HERE/init" "$STAGE/init"
rm -f "$STAGE/bin/init" "$STAGE/sbin/init"
ln -sf /init "$STAGE/bin/init"
ln -sf /init "$STAGE/sbin/init"
ln -sf busybox "$STAGE/bin/sh"

if ldd "$STAGE/usr/local/bin/spear" >/dev/null 2>&1; then
  while read -r lib; do
    [[ -z "$lib" || ! -f "$lib" ]] && continue
    mkdir -p "$STAGE$(dirname "$lib")"
    cp -aL "$lib" "$STAGE$lib" 2>/dev/null || cp -a "$lib" "$STAGE$lib"
  done < <(ldd "$STAGE/usr/local/bin/spear" | sed -n 's/.*=> \(\/[^ ]*\).*/\1/p; s/^\t\(\/[^ ]*\).*/\1/p')
  for ld in /lib64/ld-linux-x86-64.so.2 /lib/x86_64-linux-gnu/ld-linux-x86-64.so.2; do
    [[ -f "$ld" ]] || continue
    mkdir -p "$STAGE$(dirname "$ld")"
    cp -aL "$ld" "$STAGE$ld" 2>/dev/null || true
  done
fi

mknod -m 666 "$STAGE/dev/null" c 1 3 2>/dev/null || true
mknod -m 600 "$STAGE/dev/console" c 5 1 2>/dev/null || true

mkdir -p "$(dirname "$OUT")"
( cd "$STAGE" && find . | cpio -H newc -o 2>/dev/null | gzip -9 >"$OUT" )
ls -lh "$OUT"
echo "OK $OUT"

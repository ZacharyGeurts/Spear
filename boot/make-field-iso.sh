#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Product Field ISO — stack like the test boot drive.
# NO casper · NO Mint squash underlay · NO QEMU-required underlay.
# Kernel + Field initrd + isolinux · boots to SPEAR FIELD shell (fieldbox/spear).
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
OUT="$ROOT/out"
STAGE="$OUT/field-iso-root"
VER="$(cat "$ROOT/VERSION" 2>/dev/null || echo field)"
STAMP=$(date +%Y%m%d)
ISO_OUT="${SPEAR_FIELD_ISO:-$OUT/spear-field-${VER//\//-}-${STAMP}.iso}"

ISOLINUX_BIN=""
for c in /usr/lib/ISOLINUX/isolinux.bin /usr/lib/syslinux/isolinux.bin; do
  [[ -f "$c" ]] && ISOLINUX_BIN="$c" && break
done
[[ -n "$ISOLINUX_BIN" ]] || { echo "need isolinux.bin"; exit 1; }
ISOHDPFX=""
for c in /usr/lib/ISOLINUX/isohdpfx.bin /usr/lib/syslinux/isohdpfx.bin; do
  [[ -f "$c" ]] && ISOHDPFX="$c" && break
done

echo "== Field product ISO (no underlay) =="
bash "$HERE/build-initrd.sh" "$OUT/initramfs.cpio.gz"

KIMG=""
if [[ -f "$OUT/vmlinuz" ]]; then
  KIMG="$OUT/vmlinuz"
elif [[ -f "/boot/vmlinuz-$(uname -r)" ]]; then
  KIMG="/boot/vmlinuz-$(uname -r)"
else
  KIMG=$(ls -1 /boot/vmlinuz-* 2>/dev/null | head -1 || true)
fi
[[ -n "$KIMG" && -f "$KIMG" ]] || { echo "no vmlinuz"; exit 1; }

rm -rf "$STAGE"
mkdir -p "$STAGE/boot/spear" "$STAGE/isolinux" "$STAGE/EFI/BOOT"

cp -f "$KIMG" "$STAGE/boot/spear/vmlinuz"
cp -f "$OUT/initramfs.cpio.gz" "$STAGE/boot/spear/initramfs.cpio.gz"
cp -f "$HERE/limine.conf" "$STAGE/boot/spear/limine.conf" 2>/dev/null || true

# Shared Field cmdline (test-boot-drive doctrine)
CMN="field=1 spear_harden=1 root=/dev/ram0 rw init=/init console=tty0 console=ttyS0,115200n8"
CMN="$CMN quiet loglevel=3 tsc=reliable pti=on page_alloc.shuffle=1 randomize_kstack_offset=on slab_nomerge"
CMN="$CMN fsck.mode=skip raid=noautodetect noresume"

cat >"$STAGE/isolinux/isolinux.cfg" <<EOF
SERIAL 0 115200
DEFAULT field
TIMEOUT 0
PROMPT 0
UI menu.c32
MENU TITLE Spear Field — product stack (no Mint underlay)
MENU COLOR title  1;36;44  #ffffffff #00000000 std
MENU COLOR sel    7;37;40  #e0ffffff #20ffffff all

LABEL field
  MENU LABEL ^Field — fast stack (default · test-drive path)
  KERNEL /boot/spear/vmlinuz
  APPEND initrd=/boot/spear/initramfs.cpio.gz $CMN spear_mode=field spear_fast=1

LABEL normal
  MENU LABEL ^Normal — full Field path
  KERNEL /boot/spear/vmlinuz
  APPEND initrd=/boot/spear/initramfs.cpio.gz $CMN spear_mode=normal

LABEL fielddrive
  MENU LABEL ^Create Field Drive — FFAT claim
  KERNEL /boot/spear/vmlinuz
  APPEND initrd=/boot/spear/initramfs.cpio.gz $CMN spear_mode=fielddrive spear_fast=1

LABEL war
  MENU LABEL ^War — max harden
  KERNEL /boot/spear/vmlinuz
  APPEND initrd=/boot/spear/initramfs.cpio.gz $CMN spear_mode=normal spear_war=1 spectre_v2=on mds=full tsx=off

LABEL debug
  MENU LABEL ^Debug — verbose serial
  KERNEL /boot/spear/vmlinuz
  APPEND initrd=/boot/spear/initramfs.cpio.gz field=1 spear_mode=debug spear_debug=1 spear_harden=1 root=/dev/ram0 rw init=/init console=tty0 console=ttyS0,115200n8 earlyprintk=serial,ttyS0,115200 loglevel=8 debug ignore_loglevel pti=on

LABEL local
  MENU LABEL Boot first hard disk
  LOCALBOOT 0
EOF

cp -f "$ISOLINUX_BIN" "$STAGE/isolinux/isolinux.bin"
# menu modules
for m in menu.c32 libutil.c32 libcom32.c32 ldlinux.c32; do
  for d in /usr/lib/syslinux/modules/bios /usr/share/syslinux; do
    [[ -f "$d/$m" ]] && cp -f "$d/$m" "$STAGE/isolinux/" && break
  done
done

# Identity
cat >"$STAGE/SPEAR_FIELD.txt" <<EOF
Spear Field product ISO
version=$VER
stack=Field boot drive path
underlay=NONE
casper=NO
mint=NO
qemu_required=NO
field1=CLAIMED or claim via Create Field Drive
control_plane=C++
god_bless=1
EOF
echo "$VER" >"$STAGE/VERSION"

rm -f "$ISO_OUT"
XORRISO_ARGS=(
  -as mkisofs
  -r -V "Spear_Field"
  -J -joliet-long -l
  -iso-level 3
  -c isolinux/boot.cat
  -b isolinux/isolinux.bin
  -no-emul-boot -boot-load-size 4 -boot-info-table
  -o "$ISO_OUT"
  "$STAGE"
)
if [[ -n "$ISOHDPFX" ]]; then
  xorriso "${XORRISO_ARGS[@]}" -isohybrid-mbr "$ISOHDPFX" 2>&1 | tail -8
else
  xorriso "${XORRISO_ARGS[@]}" 2>&1 | tail -8
fi

ln -sfn "$(basename "$ISO_OUT")" "$OUT/spear-field-latest.iso"
# Prefer product ISO as spear-latest when building field product
ln -sfn "$(basename "$ISO_OUT")" "$OUT/spear-latest.iso"

BYTES=$(/usr/bin/stat -c%s "$ISO_OUT" 2>/dev/null || wc -c <"$ISO_OUT")
SHA=$(sha256sum "$ISO_OUT" | awk '{print $1}')
echo "$SHA  $(basename "$ISO_OUT")" >"$OUT/spear-field.iso.sha256"
python3 - "$OUT/field-product-receipt.json" "$VER" "$ISO_OUT" "$BYTES" "$SHA" "$KIMG" "$OUT/initramfs.cpio.gz" <<'PY'
import json,sys,time
path,ver,iso,byt,sha,k,ird=sys.argv[1:8]
rec={
  "schema":"spear-field-product/v1",
  "product":"Spear Field",
  "version":ver,
  "kind":"field_boot_iso",
  "underlay":"none",
  "casper":False,
  "mint":False,
  "qemu_required":False,
  "like_test_boot_drive":True,
  "field1":"attach CLAIMED FIELD1 or Create Field Drive entry",
  "iso":iso,
  "iso_bytes":int(byt),
  "iso_sha256":sha,
  "kernel":k,
  "initrd":ird,
  "entries":["Field","Normal","Create Field Drive","War","Debug"],
  "built_at":time.strftime("%Y-%m-%dT%H:%MZ",time.gmtime()),
  "motto":"Stack is Field boot · not Mint underlay · God Bless",
}
open(path,"w").write(json.dumps(rec,indent=2)+"\n")
print(json.dumps(rec,indent=2))
PY

ls -lh "$ISO_OUT" "$OUT/spear-field-latest.iso"
echo "FIELD PRODUCT ISO OK · $BYTES bytes · sha256 $SHA"
echo "Boot like test drive: Field entry → spear init → Field1 / shell"

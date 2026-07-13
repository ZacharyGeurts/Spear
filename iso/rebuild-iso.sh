#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# work/edit → squashfs → out/spear-*.iso
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
WORK="${SPEAR_WORK:-$ROOT/work}"
if [[ ! -d "$WORK/edit/usr" && -d /home/zachary/Desktop/SG/NewLatest/Spear/work/edit/usr ]]; then
  WORK=/home/zachary/Desktop/SG/NewLatest/Spear/work
fi
EDIT="$WORK/edit"
ISOX="$WORK/iso-extract"
OUT="$ROOT/out"
VER="$(cat "$ROOT/VERSION" 2>/dev/null || echo 22.3.1-field)"
UPSTREAM=$(readlink -f "${SPEAR_UPSTREAM_ISO:-$ROOT/iso/upstream-mint-cinnamon.iso}" 2>/dev/null || true)
[[ -e "${UPSTREAM:-}" ]] || UPSTREAM=$(readlink -f /home/zachary/Desktop/SG/AmmoMint/iso/linuxmint-22.3-cinnamon-64bit.iso 2>/dev/null || true)

[[ -d "$EDIT/usr" ]] || { echo "No edit root"; exit 1; }
[[ -d "$ISOX" ]] || { echo "No iso-extract"; exit 1; }
mkdir -p "$OUT"
chmod -R u+w "$ISOX" 2>/dev/null || true

SFS_PATH=$(find "$ISOX" -name 'filesystem.squashfs' | head -1)
[[ -n "$SFS_PATH" ]] || { echo "filesystem.squashfs path missing"; exit 1; }
SFS_DIR=$(dirname "$SFS_PATH")

echo "Rebuilding squashfs (SPEAR_FORCE_SQUASH=${SPEAR_FORCE_SQUASH:-1})…"
export SPEAR_FORCE_SQUASH="${SPEAR_FORCE_SQUASH:-1}"
rm -f "$SFS_PATH"
mksquashfs "$EDIT" "$SFS_PATH" -comp xz -b 1M -Xdict-size 100% -noappend -processors "${SPEAR_JOBS:-$(nproc)}"
du -sx --block-size=1 "$EDIT" | cut -f1 >"$SFS_DIR/filesystem.size" || true

if [[ -f "$ISOX/md5sum.txt" ]]; then
  (cd "$ISOX" && find . -type f ! -name md5sum.txt -print0 | xargs -0 md5sum | grep -v isolinux/boot.cat >md5sum.txt) || true
fi

STAMP=$(date +%Y%m%d)
SAFE_VER=$(echo "$VER" | tr '/ ' '__')
OUT_ISO="$OUT/spear-${SAFE_VER}-cinnamon-64bit-${STAMP}.iso"
echo "Building ISO → $OUT_ISO"
rm -f "$OUT_ISO"

set +e
if [[ -n "${UPSTREAM:-}" && -e "$UPSTREAM" ]]; then
  xorriso -as mkisofs \
    -r -V "Spear_Field" \
    -J -joliet-long -l \
    -iso-level 3 \
    -partition_offset 16 \
    --grub2-mbr --interval:local_fs:0s-15s:zero_mbrpt,zero_gpt:"$UPSTREAM" \
    -append_partition 2 0xef --interval:local_fs:appended_part_2_start_sector_from_iso_end_with_padding_as_gap_before_this_is_ignored:"$UPSTREAM" \
    -appended_part_as_gpt \
    -c isolinux/boot.cat \
    -b isolinux/isolinux.bin \
    -no-emul-boot -boot-load-size 4 -boot-info-table \
    -eltorito-alt-boot \
    -e boot/grub/efi.img \
    -no-emul-boot \
    -isohybrid-gpt-basdat \
    -o "$OUT_ISO" \
    "$ISOX"
  RC=$?
else
  RC=1
fi

if [[ $RC -ne 0 || ! -s "$OUT_ISO" ]]; then
  echo "hybrid path fallback…"
  xorriso -as mkisofs \
    -r -V "Spear_Field" \
    -J -joliet-long -l \
    -iso-level 3 \
    -c isolinux/boot.cat \
    -b isolinux/isolinux.bin \
    -no-emul-boot -boot-load-size 4 -boot-info-table \
    -eltorito-alt-boot \
    -e boot/grub/efi.img \
    -no-emul-boot \
    -isohybrid-gpt-basdat \
    -o "$OUT_ISO" \
    "$ISOX"
  RC=$?
fi
set -e
[[ $RC -eq 0 && -s "$OUT_ISO" ]] || { echo "ISO build failed"; exit 1; }

if command -v isohybrid >/dev/null; then
  isohybrid --uefi "$OUT_ISO" 2>/dev/null || isohybrid "$OUT_ISO" 2>/dev/null || true
fi

ln -sfn "$(basename "$OUT_ISO")" "$OUT/spear-latest.iso"
# also mirror to NewLatest out if present
if [[ -d /home/zachary/Desktop/SG/NewLatest/Spear/out ]]; then
  cp -f "$OUT_ISO" /home/zachary/Desktop/SG/NewLatest/Spear/out/ 2>/dev/null || true
  ln -sfn "$(basename "$OUT_ISO")" /home/zachary/Desktop/SG/NewLatest/Spear/out/spear-latest.iso 2>/dev/null || true
fi

ls -lh "$OUT_ISO" "$OUT/spear-latest.iso"
xorriso -indev "$OUT_ISO" -report_el_torito plain 2>&1 | head -20 || true
echo "DONE: $OUT_ISO"
bash "$ROOT/scripts/release-receipt.sh" "$OUT_ISO" || true

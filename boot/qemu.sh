#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Optional QEMU harness for Field product boot — NOT required for product.
# Uses out/spear-boot.img from pack.sh (self-contained). No KILROY underlay.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
SPEAR="$(cd "$HERE/.." && pwd)"
IMG="${SPEAR_IMG:-$SPEAR/out/spear-boot.img}"
FIELD_DISK="${SPEAR_FIELD_DISK:-$SPEAR/out/field-disk.img}"
# Prefer real Field1 if operator sets SPEAR_FIELD1=auto
if [[ "${SPEAR_FIELD1:-}" == "auto" && -b /dev/disk/by-label/FIELD1 ]]; then
  FIELD_DISK=$(readlink -f /dev/disk/by-label/FIELD1)
fi
LOGDIR="${SPEAR_QEMU_LOG:-$SPEAR/out/qemu-logs}"
mkdir -p "$LOGDIR" "$SPEAR/out"

bash "$HERE/pack.sh"
[[ -f "$IMG" ]] || { echo "missing $IMG — pack failed"; exit 1; }
[[ -e "$FIELD_DISK" ]] || dd if=/dev/zero of="$FIELD_DISK" bs=1M count=512 status=none

KVM=()
[[ -r /dev/kvm && -w /dev/kvm ]] && KVM=(-enable-kvm -cpu host)
SERIAL="$LOGDIR/spear-field-$(date +%Y%m%d-%H%M%S).log"

DISPLAY_ARGS=(-nographic)
if [[ -n "${DISPLAY:-}" || -n "${WAYLAND_DISPLAY:-}" ]]; then
  DISPLAY_ARGS=(-display gtk -device virtio-vga)
fi

echo "OPTIONAL qemu · product is still make product / Field ISO"
echo "disk=$IMG field=$FIELD_DISK serial=$SERIAL"

exec /usr/bin/qemu-system-x86_64 \
  "${KVM[@]}" -m "${SPEAR_MEM:-2048}" -smp "${SPEAR_SMP:-2}" \
  -name Spear-Field-product \
  "${DISPLAY_ARGS[@]}" \
  -drive "file=${IMG},format=raw,if=virtio" \
  -drive "file=${FIELD_DISK},format=raw,if=virtio" \
  -boot order=c \
  -serial "file:${SERIAL}" \
  -netdev user,id=net0 -device virtio-net-pci,netdev=net0

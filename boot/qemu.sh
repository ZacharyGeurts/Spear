#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# QEMU Spear-KILROY (no casper) — pack then boot
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
SPEAR="$(cd "$HERE/.." && pwd)"
BUILD="${KILROY_BUILD:-/home/zachary/Desktop/SG/zpics/kilroy-qemu-build}"
IMG="${KILROY_IMG:-$BUILD/grok-kilroy.img}"
FIELD_DISK="${SPEAR_FIELD_DISK:-$SPEAR/out/field-disk.img}"
LOGDIR="${SPEAR_QEMU_LOG:-$SPEAR/out/qemu-logs}"
mkdir -p "$LOGDIR" "$SPEAR/out"

# Pack latest limine + initrd (spear + field native init)
bash "$HERE/pack.sh"

[[ -f "$FIELD_DISK" ]] || dd if=/dev/zero of="$FIELD_DISK" bs=1M count=2048 status=none

# stop prior Spear/KILROY qemu only
ps -eo pid,args | awk '/qemu-system-x86_64/ && /Spear-KILROY|grok-kilroy/ && !/awk/ {print $1}' | while read -r p; do
  kill "$p" 2>/dev/null || true
done
sleep 1

KVM=()
[[ -r /dev/kvm && -w /dev/kvm ]] && KVM=(-enable-kvm -cpu host)
SERIAL="$LOGDIR/spear-kilroy-$(date +%Y%m%d-%H%M%S).log"

# SPEAR_QEMU_DEBUG=1 → default limine Debug entry already packed; extra serial
MODE_NOTE="menu: Debug | Normal | Create Field Drive | Compat"
if [[ "${SPEAR_QEMU_DEBUG:-1}" == "1" ]]; then
  MODE_NOTE="DEBUG boot (limine default=Debug) · serial loglevel=8"
fi

# Prefer GUI; fall back to serial-only if no display
DISPLAY_ARGS=(-display gtk,zoom-to-fit=off,show-cursor=on -device virtio-vga,xres=1920,yres=1080)
if [[ -z "${DISPLAY:-}" && -z "${WAYLAND_DISPLAY:-}" ]]; then
  DISPLAY_ARGS=(-nographic)
fi

# Dual serial: file log + optional mon:stdio when nographic
SERIAL_ARGS=(-serial "file:${SERIAL}")
if [[ "${DISPLAY_ARGS[*]}" == *nographic* ]]; then
  SERIAL_ARGS=(-serial "file:${SERIAL}" -serial mon:stdio)
fi

setsid /usr/bin/qemu-system-x86_64 \
  "${KVM[@]}" -m "${KILROY_MEM:-4096}" -smp "${KILROY_SMP:-4}" \
  -name Spear-KILROY-DEBUG \
  "${DISPLAY_ARGS[@]}" \
  -drive "file=${IMG},format=raw,if=virtio" \
  -drive "file=${FIELD_DISK},format=raw,if=virtio" \
  -boot order=c \
  "${SERIAL_ARGS[@]}" \
  -netdev user,id=net0 -device virtio-net-pci,netdev=net0 \
  </dev/null >/dev/null 2>&1 &

echo "pid=$!  Spear-KILROY-DEBUG"
echo "serial=$SERIAL"
echo "disk=$IMG"
echo "field=$FIELD_DISK"
echo "$MODE_NOTE"
echo "tail serial: tail -f $SERIAL"

#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Boot Spear Cinnamon live ISO in QEMU (GUI + serial capture).
#
# Field1: prefers host claimed Field1 (SPEARMBR /dev/sdb via by-label/FIELD1).
# Set SPEAR_FIELD1=img to use out/field-disk.img instead of the real disk.
# Set SPEAR_FIELD1=0 to omit a second disk.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ISO_DEFAULT="/home/zachary/Desktop/SG/NewLatest/Spear/out/spear-latest.iso"
ISO="${SPEAR_ISO:-$ISO_DEFAULT}"
OUT="$ROOT/out"
mkdir -p "$OUT/qemu-logs"
STAMP=$(date +%Y%m%d-%H%M%S)
SERIAL="$OUT/qemu-logs/spear-gui-${STAMP}.log"
echo "$SERIAL" >"$OUT/qemu-logs/current-serial.path"
MEM="${SPEAR_QEMU_MEM:-6G}"
NAME="${SPEAR_QEMU_NAME:-Spear-OS-GUI}"
DIRECT="${SPEAR_DIRECT:-1}"
UUID="${SPEAR_CASPER_UUID:-6e72f523-dc09-4880-8910-93ffa64401c5}"
FIELD1_MODE="${SPEAR_FIELD1:-auto}"  # auto|host|img|0
# field (default speed) | normal (desktop) | fielddrive | war | debug
SPEAR_BOOT_MODE="${SPEAR_BOOT_MODE:-field}"

[[ -f "$ISO" ]] || { echo "missing ISO: $ISO"; exit 1; }

resolve_field1() {
  local d
  for d in /dev/disk/by-label/FIELD1 /dev/spear/field1 \
           /dev/disk/by-id/ata-T-FORCE_1TB_TPBF2411190010300627; do
    if [[ -e "$d" ]]; then
      # resolve to real block device
      readlink -f "$d" 2>/dev/null || echo "$d"
      return 0
    fi
  done
  return 1
}

EXTRA_DISK=()
FIELD1_DEV=""
case "$FIELD1_MODE" in
  0|off|none) ;;
  img)
    FDISK="$OUT/field-disk.img"
    if [[ ! -f "$FDISK" ]]; then
      qemu-img create -f raw "$FDISK" 8G >/dev/null
    fi
    FIELD1_DEV="$FDISK"
    # serial via device, not raw drive option
    EXTRA_DISK=(
      -drive "file=$FDISK,format=raw,if=none,id=field1,cache=writeback"
      -device "virtio-blk-pci,drive=field1,serial=FIELD1"
    )
    ;;
  host|auto|*)
    if FIELD1_DEV=$(resolve_field1); then
      # Real Field1 whole disk — exclusive; do not mount on host while guest runs
      EXTRA_DISK=(
        -drive "file=$FIELD1_DEV,format=raw,if=none,id=field1,cache=none,aio=native"
        -device "virtio-blk-pci,drive=field1,serial=FIELD1"
      )
    elif [[ "$FIELD1_MODE" == "host" ]]; then
      echo "SPEAR_FIELD1=host but no Field1 device found" >&2
      exit 1
    else
      FDISK="$OUT/field-disk.img"
      [[ -f "$FDISK" ]] || qemu-img create -f raw "$FDISK" 8G >/dev/null
      FIELD1_DEV="$FDISK"
      EXTRA_DISK=(
        -drive "file=$FDISK,format=raw,if=none,id=field1,cache=writeback"
        -device "virtio-blk-pci,drive=field1,serial=FIELD1"
      )
    fi
    ;;
esac

COMMON=(
  -name "$NAME"
  -enable-kvm
  -m "$MEM"
  -smp 4
  -cpu host
  -machine q35,accel=kvm
  -display gtk,gl=off,window-close=on
  -vga std
  -device virtio-net-pci,netdev=n0
  -netdev user,id=n0
  -cdrom "$ISO"
  -serial "file:$SERIAL"
  -monitor none
  "${EXTRA_DISK[@]}"
)

echo "ISO=$ISO"
echo "SERIAL=$SERIAL"
echo "name=$NAME mem=$MEM direct=$DIRECT"
echo "FIELD1=$FIELD1_DEV mode=$FIELD1_MODE"

if [[ "$DIRECT" == "1" ]]; then
  CACHE="${SPEAR_KERNEL_CACHE:-/tmp/spear-direct}"
  mkdir -p "$CACHE"
  if [[ ! -f "$CACHE/vmlinuz" || ! -f "$CACHE/initrd.lz" ]]; then
    echo "extracting casper kernel/initrd from ISO..."
    xorriso -indev "$ISO" -osirrox on \
      -extract /casper/vmlinuz "$CACHE/vmlinuz" \
      -extract /casper/initrd.lz "$CACHE/initrd.lz"
  fi
  # Field is the speed path; Normal is full desktop (heavier).
  case "$SPEAR_BOOT_MODE" in
    field|fast)
      APPEND="boot=casper uuid=${UUID} username=spear hostname=spear spear_mode=field spear_harden=1 spear_fast=1 systemd.unit=multi-user.target quiet loglevel=3 systemd.show_status=false fsck.mode=skip raid=noautodetect noresume tsc=reliable apparmor=1 security=apparmor pti=on systemd.mask=lightdm.service,accounts-daemon.service,ModemManager.service,cups.service,cups-browsed.service,avahi-daemon.service,blueman-mechanism.service,openvpn.service,touchegg.service,kerneloops.service,casper-md5check.service,ubiquity.service,mintsystem.service,ssl-cert.service,switcheroo-control.service,apport.service,power-profiles-daemon.service,thermald.service,bluetooth.service,rsyslog.service,cron.service,anacron.service,whoopsie.service console=tty0 console=ttyS0,115200n8 vga=normal ---"
      ;;
    normal|desktop)
      APPEND="boot=casper uuid=${UUID} username=spear hostname=spear spear_mode=normal spear_harden=1 quiet loglevel=3 systemd.show_status=error fsck.mode=skip raid=noautodetect noresume apparmor=1 security=apparmor page_alloc.shuffle=1 randomize_kstack_offset=on slab_nomerge pti=on systemd.mask=ModemManager.service,cups.service,cups-browsed.service,avahi-daemon.service,blueman-mechanism.service,openvpn.service,touchegg.service,kerneloops.service,casper-md5check.service,ubiquity.service,mintsystem.service,ssl-cert.service,switcheroo-control.service,apport.service console=tty0 console=ttyS0,115200n8 vga=normal ---"
      ;;
    *)
      APPEND="boot=casper uuid=${UUID} username=spear hostname=spear spear_mode=${SPEAR_BOOT_MODE} spear_harden=1 quiet loglevel=3 fsck.mode=skip raid=noautodetect noresume apparmor=1 security=apparmor pti=on console=tty0 console=ttyS0,115200n8 vga=normal ---"
      ;;
  esac
  echo "SPEAR_BOOT_MODE=$SPEAR_BOOT_MODE"
  exec qemu-system-x86_64 \
    "${COMMON[@]}" \
    -kernel "$CACHE/vmlinuz" \
    -initrd "$CACHE/initrd.lz" \
    -append "$APPEND" \
    "$@"
else
  exec qemu-system-x86_64 \
    "${COMMON[@]}" \
    -boot d \
    "$@"
fi

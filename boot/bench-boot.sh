#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Time Field vs Normal to ready. Field must be strictly faster.
# Usage: ./boot/bench-boot.sh
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ISO="${SPEAR_ISO:-/home/zachary/Desktop/SG/NewLatest/Spear/out/spear-latest.iso}"
CACHE="${SPEAR_KERNEL_CACHE:-/tmp/spear-direct}"
UUID="${SPEAR_CASPER_UUID:-6e72f523-dc09-4880-8910-93ffa64401c5}"
OUT="$ROOT/out/qemu-logs"
mkdir -p "$OUT" "$CACHE"
RESULT_FILE="$OUT/bench-field-vs-normal.txt"

kill_bench_qemu() {
  # Kill by window/name substring in full cmdline
  local p
  for p in $(ps -eo pid=,args= | awk '/qemu-system-x86_64/ && /Spear-Bench/ {print $1}'); do
    kill -TERM "$p" 2>/dev/null || true
  done
  sleep 1
  for p in $(ps -eo pid=,args= | awk '/qemu-system-x86_64/ && /Spear-Bench/ {print $1}'); do
    kill -KILL "$p" 2>/dev/null || true
  done
  sleep 1
}

[[ -f "$ISO" ]] || { echo "missing ISO: $ISO"; exit 1; }
[[ -f "$CACHE/vmlinuz" && -f "$CACHE/initrd.lz" ]] || {
  echo "extracting kernel/initrd..."
  xorriso -indev "$ISO" -osirrox on \
    -extract /casper/vmlinuz "$CACHE/vmlinuz" \
    -extract /casper/initrd.lz "$CACHE/initrd.lz" >/dev/null 2>&1
}

# Pure boot-time compare: no Field1 disk (avoids exclusive /dev/sdb races)
BASE="boot=casper uuid=${UUID} username=spear hostname=spear spear_harden=1 quiet loglevel=3 fsck.mode=skip raid=noautodetect noresume tsc=reliable apparmor=1 security=apparmor pti=on console=tty0 console=ttyS0,115200n8 vga=normal"

FIELD_APPEND="${BASE} spear_mode=field spear_fast=1 systemd.unit=multi-user.target systemd.show_status=false systemd.mask=lightdm.service,accounts-daemon.service,ModemManager.service,cups.service,cups-browsed.service,avahi-daemon.service,blueman-mechanism.service,openvpn.service,touchegg.service,kerneloops.service,casper-md5check.service,ubiquity.service,mintsystem.service,ssl-cert.service,switcheroo-control.service,apport.service,power-profiles-daemon.service,thermald.service,bluetooth.service,rsyslog.service,cron.service,anacron.service,whoopsie.service"

NORMAL_APPEND="${BASE} spear_mode=normal systemd.show_status=error page_alloc.shuffle=1 randomize_kstack_offset=on slab_nomerge systemd.mask=ModemManager.service,cups.service,cups-browsed.service,avahi-daemon.service,blueman-mechanism.service,openvpn.service,touchegg.service,kerneloops.service,casper-md5check.service,ubiquity.service,mintsystem.service,ssl-cert.service,switcheroo-control.service,apport.service"

run_one() {
  local mode="$1" append="$2" ready_re="$3"
  local serial="$OUT/bench-${mode}-$(date +%Y%m%d-%H%M%S).log"
  local name="Spear-Bench-${mode}"
  kill_bench_qemu
  : >"$serial"

  local t0 t1 elapsed
  t0=$(date +%s.%N)

  # Process group so we can kill the whole tree
  set -m
  qemu-system-x86_64 -name "$name" -enable-kvm -m 6G -smp 4 -cpu host \
    -machine q35,accel=kvm -display none -vga std \
    -device virtio-net-pci,netdev=n0 -netdev user,id=n0 \
    -cdrom "$ISO" -serial "file:$serial" \
    -kernel "$CACHE/vmlinuz" -initrd "$CACHE/initrd.lz" \
    -append "$append" >/dev/null 2>&1 &
  local qpid=$!
  set +m

  local i=0 ok=0
  while (( i < 150 )); do
    if grep -qE "$ready_re" "$serial" 2>/dev/null; then
      t1=$(date +%s.%N)
      elapsed=$(awk -v a="$t0" -v b="$t1" 'BEGIN{printf "%.2f", b-a}')
      echo "$mode ready_s=$elapsed serial=$serial"
      grep -E 'login:|multi-user.target|graphical.target|spear_ready' "$serial" 2>/dev/null | tail -6 || true
      ok=1
      break
    fi
    # died early?
    if ! kill -0 "$qpid" 2>/dev/null; then
      echo "$mode qemu_exited_early serial=$serial bytes=$(wc -c <"$serial")"
      break
    fi
    sleep 1
    i=$((i+1))
  done

  if [[ "$ok" -ne 1 ]]; then
    t1=$(date +%s.%N)
    elapsed=$(awk -v a="$t0" -v b="$t1" 'BEGIN{printf "%.2f", b-a}')
    echo "$mode TIMEOUT_OR_FAIL after ${elapsed}s serial=$serial bytes=$(wc -c <"$serial")"
    tail -c 800 "$serial" 2>/dev/null | tr -cd '\11\12\15\40-\176' | tail -15 || true
    elapsed="fail"
  fi

  kill -TERM -"$qpid" 2>/dev/null || kill -TERM "$qpid" 2>/dev/null || true
  sleep 1
  kill -KILL -"$qpid" 2>/dev/null || kill -KILL "$qpid" 2>/dev/null || true
  wait "$qpid" 2>/dev/null || true
  kill_bench_qemu

  echo "$elapsed" >"$OUT/bench-${mode}.last"
  [[ "$elapsed" != "fail" ]]
}

{
  echo "=== Spear boot bench: Field vs Normal $(date -u +%Y-%m-%dT%H:%MZ) ==="
  echo "ISO=$ISO"
  kill_bench_qemu

  field_ok=0
  normal_ok=0
  if run_one field "$FIELD_APPEND" 'login:|Reached target multi-user|multi-user.target'; then
    field_ok=1
  fi
  if run_one normal "$NORMAL_APPEND" 'login:|Reached target graphical|graphical.target|Reached target multi-user|multi-user.target'; then
    normal_ok=1
  fi

  F=$(cat "$OUT/bench-field.last" 2>/dev/null || echo fail)
  N=$(cat "$OUT/bench-normal.last" 2>/dev/null || echo fail)
  echo "=== RESULT field=${F}s normal=${N}s field_ok=$field_ok normal_ok=$normal_ok ==="

  if [[ "$field_ok" -eq 1 && "$normal_ok" -eq 1 ]]; then
    if awk -v f="$F" -v n="$N" 'BEGIN{exit !(f+0 > 0 && n+0 > 0 && f+0 < n+0)}'; then
      echo "PASS: Field faster than Normal (${F}s < ${N}s)"
      exit 0
    fi
    echo "FAIL: Field not faster than Normal (${F}s vs ${N}s)"
    exit 1
  fi
  echo "FAIL: incomplete bench (need both real ready times)"
  exit 1
} 2>&1 | tee "$RESULT_FILE"
exit "${PIPESTATUS[0]}"

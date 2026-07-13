#!/bin/sh
# SPDX-License-Identifier: MIT
# Interactive Field Drive (ash). Uses C++ `spear` when available for format.
set -eu
PATH=/bin:/sbin:/usr/bin:/usr/sbin:/usr/local/bin
red() { printf '\033[31m%s\033[0m\n' "$*"; }
grn() { printf '\033[32m%s\033[0m\n' "$*"; }
ylw() { printf '\033[33m%s\033[0m\n' "$*"; }
bold() { printf '\033[1m%s\033[0m\n' "$*"; }

human() {
  b=$1
  if [ "$b" -ge 1073741824 ] 2>/dev/null; then echo "$((b/1073741824)) GiB"
  elif [ "$b" -ge 1048576 ] 2>/dev/null; then echo "$((b/1048576)) MiB"
  else echo "${b} B"; fi
}

disk_bytes() {
  n=$(basename "$1")
  [ -r "/sys/block/$n/size" ] && echo $(($(cat "/sys/block/$n/size")*512)) && return
  [ -f "$1" ] && wc -c <"$1" | tr -d ' ' && return
  echo 0
}

has_fd() {
  if command -v spear >/dev/null; then
    spear ffat-probe "$1" 2>/dev/null | grep -q 'ffat=yes' || return 1
    return 0
  fi
  dd if="$1" bs=512 count=1 2>/dev/null | dd bs=1 skip=3 count=8 2>/dev/null | grep -q SPEARMBR || return 1
  return 0
}

list_disks() {
  bold "SPEAR disks · guaranteed=pool (probe for address_space)"
  i=0; DISKS=""
  for d in /sys/block/*; do
    n=$(basename "$d")
    case "$n" in loop*|ram*|sr*|dm-*) continue ;; esac
    dev=/dev/$n; [ -b "$dev" ] || continue
    i=$((i+1)); DISKS="$DISKS $dev"
    sz=$(disk_bytes "$dev")
    star=""; has_fd "$dev" && star=" ★ FIELD1"
    bold "[$i] $dev phys=$(human $sz)$star"
    [ -r "$d/device/model" ] && echo "    model=$(tr -s ' ' <"$d/device/model")"
    if command -v spear >/dev/null && has_fd "$dev"; then
      spear ffat-probe "$dev" 2>/dev/null | tr ' ' '\n' | grep -E 'guaranteed=|address_space=' | tr '\n' ' '
      echo
    fi
  done
  [ "$i" -gt 0 ] || { red "no disks"; return 1; }
  export DISK_LIST="$DISKS"
  command -v spear >/dev/null && spear fieldram-status 2>/dev/null || true
}

do_ensure() {
  dev=$1; force=${2:-0}
  if command -v spear >/dev/null; then
    if [ "$force" = "1" ]; then spear ffat-force "$dev"; else spear ffat-ensure "$dev"; fi
    return $?
  fi
  red "spear C++ binary missing — rebuild with: make -C src && pack initrd"
  return 1
}

wipe_create() {
  dev=$1
  sz=$(disk_bytes "$dev")
  bold "WIPE $dev → secure MBR + FFAT v4 entropy/PAK"
  echo "  phys=$(human $sz) · guaranteed/address_space after format (not fake ×N)"
  red "Type path: $dev"; printf '> '; read -r c
  [ "$c" = "$dev" ] || { red abort; return 1; }
  red "Type YES:"; printf '> '; read -r y
  [ "$y" = "YES" ] || { red abort; return 1; }
  for p in ${dev}1 ${dev}p1 ${dev}2; do [ -b "$p" ] && umount "$p" 2>/dev/null || true; done
  dd if=/dev/zero of="$dev" bs=1M count=4 conv=notrunc 2>/dev/null || true
  sync
  do_ensure "$dev" 1
  command -v spear >/dev/null && spear fieldram-ensure 2>/dev/null || true
  command -v spear >/dev/null && spear storage-status 2>/dev/null || true
}

cmd_status() {
  f=0
  for d in /sys/block/*; do
    n=$(basename "$d"); case "$n" in loop*|ram*|sr*) continue ;; esac
    dev=/dev/$n; [ -b "$dev" ] || continue
    if has_fd "$dev"; then grn "FIELD $dev"; f=1; fi
  done
  [ "$f" -eq 1 ]
}

pick() {
  list_disks || return 1
  printf 'number or path: '; read -r pick
  case "$pick" in
    [0-9]*) n=0; for d in $DISK_LIST; do n=$((n+1)); [ "$n" -eq "$pick" ] && { echo $d; return; }; done ;;
    *) echo "$pick" ;;
  esac
  return 1
}

mount_all() {
  for d in /sys/block/*; do
    n=$(basename "$d"); case "$n" in loop*|ram*|sr*) continue ;; esac
    dev=/dev/$n; has_fd "$dev" || continue
    for p in ${dev}1 ${dev}p1; do
      [ -b "$p" ] || continue
      mkdir -p /mnt/field
      mount -t vfat -o ro "$p" /mnt/field 2>/dev/null && { grn "mounted $p"; return 0; }
      mount -o ro "$p" /mnt/field 2>/dev/null && { grn "mounted $p"; return 0; }
    done
  done
  return 1
}

case "${1:-menu}" in
  status) cmd_status ;;
  list) list_disks ;;
  has-field-drive) cmd_status ;;
  mount-all) mount_all ;;
  ensure)
    if [ -n "${2:-}" ]; then do_ensure "$2" 0
    else d=$(pick) && do_ensure "$d" 0; fi
    ;;
  create) d=$(pick) && wipe_create "$d" ;;
  menu)
    clear 2>/dev/null || true
    bold "SPEAR Field Drive · C++ format · ×$FACTOR"
    if cmd_status; then
      printf 'recreate? [y/N] '; read -r a
      case "$a" in y|Y) d=$(pick) && wipe_create "$d" ;; esac
    else
      d=$(pick) && wipe_create "$d"
    fi
    ;;
  *) echo "status|list|create|ensure|mount-all|menu|has-field-drive" ;;
esac

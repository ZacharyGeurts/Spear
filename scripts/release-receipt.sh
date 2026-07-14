#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/out"
VER="$(cat "$ROOT/VERSION" 2>/dev/null || echo unknown)"
ISO="${1:-}"
[[ -n "$ISO" && -f "$ISO" ]] || ISO=$(readlink -f "$OUT/spear-latest.iso" 2>/dev/null || true)
[[ -n "${ISO:-}" && -f "$ISO" ]] || ISO=$(readlink -f /home/zachary/Desktop/SG/NewLatest/Spear/out/spear-latest.iso 2>/dev/null || true)

mkdir -p "$OUT"
SHA=""
SIZE=0
if [[ -n "${ISO:-}" && -f "$ISO" ]]; then
  SHA=$(sha256sum "$ISO" | awk '{print $1}')
  SIZE=$(/usr/bin/stat -c%s "$ISO" 2>/dev/null || wc -c <"$ISO")
fi

BINS=()
for b in spear spear-wartime spear-fleet-link spear-www spear-planet spear-export \
         spear-hard-dispose spear-kill-copilot spear-copilot-monitor; do
  if [[ -x "$ROOT/src/$b" ]]; then
    BINS+=("\"$b\"")
  fi
done
BINS_JSON=$(IFS=,; echo "${BINS[*]}")

cat >"$OUT/release-receipt.json" <<EOF
{
  "schema": "spear-release-receipt/v1",
  "product": "Spear",
  "version": "$VER",
  "stack_of_record": true,
  "no_archives": true,
  "control_plane": "cpp_only",
  "wartime": {
    "hard_signal": "FIELD_UDP_WAR_BLASTERS",
    "cooked": true,
    "pipeline": ["SPOT","VECTOR_SOURCE","COOK_FAT","QUEUE_REBURN","BURN","SCRUB","OUTLET_DESTROY","SEAL"]
  },
  "binaries": [${BINS_JSON}],
  "paths": {
    "overlay": "overlay/",
    "iso_pipeline": "iso/",
    "boot": "boot/",
    "initrd": "out/initramfs.cpio.gz",
    "iso": "${ISO:-}",
    "iso_sha256": "$SHA",
    "iso_bytes": $SIZE
  },
  "boot_menu": ["Field","Normal","War","Debug","Create Field Drive","Compat"],
  "services": [
    "spear-boot-harden.service",
    "spear-wartime.service",
    "spear-fleet-link.service",
    "spear-www.service",
    "spear-planet.service"
  ],
  "related": {
    "hostess7": "https://github.com/ZacharyGeurts/Hostess7",
    "big_grin": "https://github.com/ZacharyGeurts/Big_Grin_Terrorist_Hunter"
  },
  "built_at": "$(date -u +%Y-%m-%dT%H:%MZ)",
  "motto": "Spear full stack to the OS · COOKED · God Bless"
}
EOF
echo "receipt → $OUT/release-receipt.json"
cat "$OUT/release-receipt.json"

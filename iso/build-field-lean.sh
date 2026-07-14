#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Full lean Field ISO: strip + secure apt + apply stack + rebuild (<2 GiB target)
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
export SPEAR_FORCE_SQUASH=1
export SPEAR_WORK="${SPEAR_WORK:-}"
if [[ -z "${SPEAR_WORK}" && -d /home/zachary/Desktop/SG/NewLatest/Spear/work/edit/usr ]]; then
  export SPEAR_WORK=/home/zachary/Desktop/SG/NewLatest/Spear/work
fi

bash "$ROOT/scripts/install-stack.sh"
bash "$ROOT/iso/strip-mint-field-only.sh"
bash "$ROOT/iso/secure-apt-h7.sh"
bash "$ROOT/iso/apply-stack.sh"
# strip again after apply (overlay may re-add nothing huge)
bash "$ROOT/iso/strip-mint-field-only.sh"
bash "$ROOT/iso/secure-apt-h7.sh"

EDIT="${SPEAR_WORK}/edit"
SIZE_M=$(du -sm "$EDIT" | awk '{print $1}')
echo "edit size after strip: ${SIZE_M}M"
if (( SIZE_M > 4500 )); then
  echo "WARN: edit still large (${SIZE_M}M) — squash may exceed 2GiB ISO" >&2
fi

bash "$ROOT/iso/rebuild-iso.sh"
ISO=$(readlink -f "$ROOT/out/spear-latest.iso")
BYTES=$(/usr/bin/stat -c%s "$ISO" 2>/dev/null || wc -c <"$ISO")
echo "ISO bytes=$BYTES"
if (( BYTES > 2147483648 )); then
  echo "FAIL: ISO still over 2 GiB ($BYTES). Strip more." >&2
  exit 1
fi
echo "LEAN ISO OK under 2 GiB: $ISO"
bash "$ROOT/scripts/release-receipt.sh" "$ISO"

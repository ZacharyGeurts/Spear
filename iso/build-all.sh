#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Full remaster: fetch → extract → apply stack → rebuild ISO
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
export SPEAR_FORCE_SQUASH=1
"$ROOT/iso/fetch-upstream.sh"
"$ROOT/iso/extract.sh"
"$ROOT/iso/apply-stack.sh"
"$ROOT/iso/rebuild-iso.sh"
echo "ISO remaster complete · QEMU: ./boot/qemu-gui.sh"

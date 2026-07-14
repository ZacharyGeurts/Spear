#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Apply Spear overlay + C++ stack + isolinux menu onto work/edit + iso-extract.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
WORK="${SPEAR_WORK:-$ROOT/work}"
# Prefer local work; fall back to NewLatest cache if present
if [[ ! -d "$WORK/edit/usr" && -d /home/zachary/Desktop/SG/NewLatest/Spear/work/edit/usr ]]; then
  WORK=/home/zachary/Desktop/SG/NewLatest/Spear/work
  echo "using NewLatest work cache: $WORK"
fi
EDIT="$WORK/edit"
ISOX="$WORK/iso-extract"
OVER="$ROOT/overlay"
VER="$(cat "$ROOT/VERSION" 2>/dev/null || echo 22.3.1-field)"

[[ -d "$EDIT/usr" ]] || { echo "need $EDIT — run iso/extract.sh or set SPEAR_WORK"; exit 1; }
[[ -d "$ISOX" ]] || { echo "need $ISOX"; exit 1; }

echo "══ apply Spear stack → $EDIT (version $VER) ══"
make -C "$ROOT/src" all
bash "$ROOT/scripts/install-stack.sh"

# rsync overlay (product replacements)
rsync -a --exclude '.git' "$OVER/" "$EDIT/"

# identity hard pins
cat >"$EDIT/etc/os-release" <<EOF
NAME="Spear"
VERSION="$VER (Field)"
ID=spear
ID_LIKE="ubuntu debian"
PRETTY_NAME="Spear $VER"
VERSION_ID="$VER"
HOME_URL="https://github.com/ZacharyGeurts/Spear"
SUPPORT_URL="https://github.com/ZacharyGeurts/Spear"
BUG_REPORT_URL="https://github.com/ZacharyGeurts/Spear/issues"
VERSION_CODENAME=field
UBUNTU_CODENAME=noble
VARIANT="Spear Field"
VARIANT_ID=spear
LOGO=spear
EOF
cat >"$EDIT/etc/lsb-release" <<EOF
DISTRIB_ID=Spear
DISTRIB_RELEASE=$VER
DISTRIB_CODENAME=field
DISTRIB_DESCRIPTION="Spear $VER Field"
EOF
echo spear >"$EDIT/etc/hostname"
cat >"$EDIT/etc/casper.conf" <<'EOF'
export USERNAME="spear"
export USERFULLNAME="Spear Live"
export HOST="spear"
export BUILD_SYSTEM="Spear"
export FLAVOUR="Spear"
EOF
cat >"$EDIT/etc/spear-release" <<EOF
Spear $VER (Field)
Stack of record · C++ product path · FIELD_UDP_WAR_BLASTERS · COOKED
No KILROY public repo · no satellite archives
ISO release path: make release / make iso
EOF
echo "Spear $VER \\n \\l" >"$EDIT/etc/issue"

# systemd wants
mkdir -p "$EDIT/etc/systemd/system/multi-user.target.wants"
for u in spear-boot-harden.service spear-wartime.service spear-fleet-link.service \
         spear-www.service spear-planet.service; do
  [[ -f "$EDIT/etc/systemd/system/$u" ]] || continue
  ln -sfn "../$u" "$EDIT/etc/systemd/system/multi-user.target.wants/$u"
done

# boot menu from data/iso-boot (full Field/Normal/War/…)
UUID=$(grep -oE 'uuid=[^ ]+' "$ISOX/isolinux/live.cfg" 2>/dev/null | head -1 | cut -d= -f2 || true)
[[ -n "$UUID" ]] || UUID=$(cat "$ISOX/.disk/casper-uuid-generic" 2>/dev/null || cat "$ISOX/.disk/casper-uuid" 2>/dev/null || true)
[[ -n "$UUID" ]] || UUID="6e72f523-dc09-4880-8910-93ffa64401c5"

if [[ -f "$ROOT/data/iso-boot/live.cfg" ]]; then
  python3 - "$ROOT/data/iso-boot/live.cfg" "$ISOX/isolinux/live.cfg" "$UUID" <<'PY'
import re, sys
src, dst, uuid = sys.argv[1], sys.argv[2], sys.argv[3].strip()
text = open(src, encoding="utf-8", errors="replace").read()
text = re.sub(r"uuid=[0-9a-fA-F-]+", "uuid=" + uuid, text)
open(dst, "w", encoding="utf-8").write(text)
print("live.cfg uuid ->", uuid)
PY
fi
for f in splash.png splash-classic.png isolinux.cfg vesamenu.cfg; do
  [[ -f "$ROOT/data/iso-boot/$f" ]] && cp -f "$ROOT/data/iso-boot/$f" "$ISOX/isolinux/$f" 2>/dev/null || true
done
# branding on disk label area
if [[ -d "$ISOX/.disk" ]]; then
  echo "Spear $VER" >"$ISOX/.disk/info" 2>/dev/null || true
fi

# doctrine into image
mkdir -p "$EDIT/usr/local/share/spear"
cp -f "$ROOT/data/"*.json "$EDIT/usr/local/share/spear/" 2>/dev/null || true
cp -f "$ROOT/docs/NO-ARCHIVES.md" "$EDIT/usr/local/share/spear/" 2>/dev/null || true
cp -f "$ROOT/VERSION" "$EDIT/usr/local/share/spear/VERSION"

echo "apply-stack OK · PRETTY_NAME=$(grep PRETTY_NAME "$EDIT/etc/os-release") · uuid=$UUID"

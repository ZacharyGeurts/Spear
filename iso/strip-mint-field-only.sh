#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Strip Mint bloat → Field stack + terminal. SAFE: never delete /etc /bin /lib /usr/bin …
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
WORK="${SPEAR_WORK:-$ROOT/work}"
if [[ ! -d "$WORK/edit/usr" && -d /home/zachary/Desktop/SG/NewLatest/Spear/work/edit/usr ]]; then
  WORK=/home/zachary/Desktop/SG/NewLatest/Spear/work
fi
EDIT="$WORK/edit"
[[ -d "$EDIT/usr" ]] || { echo "need $EDIT/usr"; exit 1; }
[[ -d "$EDIT/etc" ]] || { echo "need $EDIT/etc — re-extract rootfs first"; exit 1; }

echo "══ strip Mint → Field-only (safe trees) · EDIT=$EDIT ══"
BEFORE=$(du -sm "$EDIT" | awk '{print $1}')
echo "before: ${BEFORE}M"

# Only whole known-bloat trees — never walk dpkg lists (that deleted /etc)
KILL_TREES=(
  opt/ammocodium
  opt/field-research
  usr/lib/firefox
  usr/lib/thunderbird
  usr/lib/libreoffice
  usr/share/libreoffice
  usr/share/gimp
  usr/share/help
  usr/share/help-langpack
  usr/share/doc
  usr/share/man
  usr/share/info
  usr/share/app-install
  usr/share/mythes
  usr/share/hunspell
  usr/share/ibus
  usr/share/javascript
  usr/share/speech-dispatcher
  usr/share/backgrounds/linuxmint
  usr/share/backgrounds/xfce
  usr/share/icons/Papirus
  usr/share/icons/Papirus-Dark
  usr/share/icons/Papirus-Light
  usr/share/icons/Mint-X
  usr/share/icons/Mint-Y
  usr/share/icons/Mint-L
  usr/share/icons/Yaru
  usr/share/icons/hicolor/256x256
  usr/share/icons/hicolor/512x512
  usr/share/themes
  usr/share/cinnamon
  usr/share/nemo
  usr/share/mint-artwork
  usr/share/linuxmint
  usr/share/pixmaps/faces
  usr/share/fonts/truetype/noto
  usr/share/fonts/opentype
  usr/share/games
  usr/games
  usr/lib/debug
  usr/lib/cups
  var/cache/apt/archives
  var/lib/apt/lists
  var/cache/man
  var/cache/fontconfig
  var/tmp
)

for t in "${KILL_TREES[@]}"; do
  if [[ -e "$EDIT/$t" ]]; then
    rm -rf "$EDIT/$t"
    echo "  rm -rf $t"
  fi
done

# locales: keep C + en*
if [[ -d "$EDIT/usr/share/locale" ]]; then
  find "$EDIT/usr/share/locale" -mindepth 1 -maxdepth 1 -type d ! -name 'en*' ! -name 'C' -exec rm -rf {} + 2>/dev/null || true
fi
if [[ -d "$EDIT/usr/share/locale-langpack" ]]; then
  find "$EDIT/usr/share/locale-langpack" -mindepth 1 -maxdepth 1 -type d ! -name 'en*' -exec rm -rf {} + 2>/dev/null || true
fi

# desktop entries: only field-terminal + field-nvtop + spear*
APP="$EDIT/usr/share/applications"
if [[ -d "$APP" ]]; then
  find "$APP" -type f -name '*.desktop' \
    ! -name 'field-terminal.desktop' \
    ! -name 'field-nvtop.desktop' \
    ! -name 'spear*.desktop' \
    -delete 2>/dev/null || true
fi

# trim huge firmware blobs (keep generic)
if [[ -d "$EDIT/usr/lib/firmware" ]]; then
  rm -rf "$EDIT/usr/lib/firmware/amdgpu" \
         "$EDIT/usr/lib/firmware/nvidia" \
         "$EDIT/usr/lib/firmware/i915" \
         "$EDIT/usr/lib/firmware/qcom" \
         "$EDIT/usr/lib/firmware/mrvl" \
         "$EDIT/usr/lib/firmware/ath11k" \
         "$EDIT/usr/lib/firmware/ath12k" 2>/dev/null || true
fi

# caches
rm -rf "$EDIT/var/cache/apt/archives/"* \
  "$EDIT/var/lib/apt/lists/"* \
  "$EDIT/tmp/"* 2>/dev/null || true
mkdir -p "$EDIT/var/cache/apt/archives/partial" \
  "$EDIT/var/lib/apt/lists/partial" \
  "$EDIT/tmp" "$EDIT/var/tmp"

AFTER=$(du -sm "$EDIT" | awk '{print $1}')
echo "after: ${AFTER}M (was ${BEFORE}M, saved $((BEFORE - AFTER))M)"
echo "strip OK (safe)"

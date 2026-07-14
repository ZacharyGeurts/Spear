#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Install C++ Spear stack into overlay + user local bin (product path ELFs).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC="$ROOT/src"
OVER="$ROOT/overlay/usr/local/bin"
LIB="$ROOT/overlay/usr/local/lib/spear"
SHARE="$ROOT/overlay/usr/local/share/spear"
VER="$(cat "$ROOT/VERSION" 2>/dev/null || echo 22.3.1-field)"

make -C "$SRC" all

BINS=(
  spear
  spear-wartime
  spear-fleet-link
  spear-www
  spear-planet
  spear-export
  spear-hard-dispose
  spear-kill-copilot
  spear-copilot-monitor
  field-nvtop
  fieldbox
  field-obs
  field-gimp
)

mkdir -p "$OVER" "$LIB" "$SHARE" "$HOME/.local/bin"

for b in "${BINS[@]}"; do
  [[ -x "$SRC/$b" ]] || { echo "missing $SRC/$b"; exit 1; }
  install -m 0755 "$SRC/$b" "$OVER/$b"
  install -m 0755 "$SRC/$b" "$HOME/.local/bin/$b"
done

# AMOURANTHRTX GPU top — we ARE nvtop / nv-top
for a in nv-top nvtop field-gpu amouranthrtx-gpu; do
  ln -sfn field-nvtop "$OVER/$a"
  ln -sfn "$HOME/.local/bin/field-nvtop" "$HOME/.local/bin/$a"
done

# Field Linux multicall — we ARE top, ls, ps, …
ln -sfn fieldbox "$OVER/field"
ln -sfn "$HOME/.local/bin/fieldbox" "$HOME/.local/bin/field"
for a in top ls ps df free cat uname id whoami hostname env clear pwd echo head wc which \
         mkdir rm sleep kill stat uptime true false; do
  ln -sfn fieldbox "$OVER/$a"
  ln -sfn "$HOME/.local/bin/fieldbox" "$HOME/.local/bin/$a"
done
# Fun Field rewrites — we ARE obs / gimp (not Electron/GTK piles)
ln -sfn field-obs "$OVER/obs"
ln -sfn field-gimp "$OVER/gimp"
ln -sfn "$HOME/.local/bin/field-obs" "$HOME/.local/bin/obs"
ln -sfn "$HOME/.local/bin/field-gimp" "$HOME/.local/bin/gimp"
# desktops
mkdir -p "$ROOT/overlay/usr/share/applications"
cat >"$ROOT/overlay/usr/share/applications/field-obs.desktop" <<'E'
[Desktop Entry]
Type=Application
Name=Field OBS
Comment=Field capture · C++ · no Electron
Exec=field-terminal -e field-obs status
Icon=camera-video
Terminal=false
Categories=AudioVideo;Recorder;
E
cat >"$ROOT/overlay/usr/share/applications/field-gimp.desktop" <<'E'
[Desktop Entry]
Type=Application
Name=Field GIMP
Comment=Field image tools · C++ · PPM
Exec=field-terminal -e field-gimp status
Icon=multimedia-photo-viewer
Terminal=false
Categories=Graphics;
E


# spear primary may need setuid on real install (optional)
if [[ -w /usr/local/bin ]]; then
  install -m 0755 "$SRC/spear" /usr/local/bin/spear || true
fi

# doctrine + version into image share
cp -f "$ROOT/data/"*.json "$SHARE/" 2>/dev/null || true
mkdir -p "$SHARE/shot-certainty" "$SHARE/swallows" "$SHARE/iso-boot"
cp -a "$ROOT/data/shot-certainty/." "$SHARE/shot-certainty/" 2>/dev/null || true
cp -a "$ROOT/data/swallows/." "$SHARE/swallows/" 2>/dev/null || true
cp -a "$ROOT/data/iso-boot/." "$SHARE/iso-boot/" 2>/dev/null || true
echo "$VER" >"$SHARE/VERSION"
echo "Spear $VER (Field) — stack of record · C++ product path · no KILROY archive" \
  >"$ROOT/overlay/etc/spear-release"

# enable wartime units by default in overlay
SYS="$ROOT/overlay/etc/systemd/system"
mkdir -p "$SYS/multi-user.target.wants" "$SYS/graphical.target.wants"
for u in spear-boot-harden.service spear-wartime.service spear-fleet-link.service \
         spear-www.service spear-planet.service; do
  [[ -f "$SYS/$u" ]] || continue
  ln -sfn "../$u" "$SYS/multi-user.target.wants/$u" 2>/dev/null || true
done

ln -sfn "$HOME/.local/bin/spear-planet" "$HOME/.local/bin/spear-planet-live" 2>/dev/null || true
ln -sfn "$HOME/.local/bin/spear-fleet-link" "$HOME/.local/bin/spear-rack-guard" 2>/dev/null || true

echo "install-stack OK · version=$VER · overlay=$OVER · local=$HOME/.local/bin"
ls -la "$OVER"/spear "$OVER"/spear-wartime "$OVER"/spear-fleet-link 2>/dev/null | head -10

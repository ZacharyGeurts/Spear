#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Rewrite apt: only Hostess7 / Spear Field package tree on GitHub. No Mint/Ubuntu nets.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
WORK="${SPEAR_WORK:-$ROOT/work}"
if [[ ! -d "$WORK/edit/usr" && -d /home/zachary/Desktop/SG/NewLatest/Spear/work/edit/usr ]]; then
  WORK=/home/zachary/Desktop/SG/NewLatest/Spear/work
fi
EDIT="$WORK/edit"
[[ -d "$EDIT/etc" ]] || { echo "need $EDIT"; exit 1; }

run() {
  if [[ -w "$EDIT/etc" ]]; then "$@"; else sudo "$@"; fi
}

echo "══ secure apt → Hostess7 Field tree only ══"

run mkdir -p "$EDIT/etc/apt/sources.list.d" "$EDIT/etc/apt/preferences.d" \
  "$EDIT/etc/apt/apt.conf.d" "$EDIT/usr/local/bin" \
  "$EDIT/usr/local/share/spear/apt"

# disable every foreign source
run bash -c "echo '# Spear Field — foreign apt disabled. Use field-apt / Hostess7 tree only.' > '$EDIT/etc/apt/sources.list'"
if [[ -d "$EDIT/etc/apt/sources.list.d" ]]; then
  for f in "$EDIT/etc/apt/sources.list.d"/*; do
    [[ -e "$f" ]] || continue
    base=$(basename "$f")
    case "$base" in
      spear-h7.list|hostess7-field.list) continue ;;
      *.list)
        run mv -f "$f" "${f}.disabled-by-spear" 2>/dev/null || run rm -f "$f"
        ;;
      *) ;;
    esac
  done
fi

# sole source: Hostess7 GitHub Pages apt (field main)
# deb822 + classic for apt compatibility
cat >"$EDIT/etc/apt/sources.list.d/spear-h7.list" <<'EOF'
# Hostess 7 Field apt — ONLY allowed package origin
# Packages live under: https://zacharygeurts.github.io/Hostess7/apt/
# Mirror path (same content): https://raw.githubusercontent.com/ZacharyGeurts/Hostess7/main/apt/
deb [trusted=yes arch=amd64] https://zacharygeurts.github.io/Hostess7/apt field main
# fallback raw (some environments):
# deb [trusted=yes arch=amd64] https://raw.githubusercontent.com/ZacharyGeurts/Hostess7/main/apt field main
EOF

cat >"$EDIT/etc/apt/preferences.d/99-spear-h7-only" <<'EOF'
# Never install from any non-Field origin if one sneaks back in
Package: *
Pin: origin zacharygeurts.github.io
Pin-Priority: 1001

Package: *
Pin: origin raw.githubusercontent.com
Pin-Priority: 900

Package: *
Pin: release o=Ubuntu
Pin-Priority: -10

Package: *
Pin: release o=linuxmint
Pin-Priority: -10

Package: *
Pin: release o=Debian
Pin-Priority: -10
EOF

cat >"$EDIT/etc/apt/apt.conf.d/99spear-secure" <<'EOF'
// Spear secure apt — no recommends bloat, no unattended mint phones home
APT::Install-Recommends "false";
APT::Install-Suggests "false";
APT::Get::AllowUnauthenticated "false";
Acquire::AllowInsecureRepositories "false";
Acquire::AllowDowngradeToInsecureRepositories "false";
// Hostess7 tree ships InRelease or trusted=yes until signed keys land
Acquire::Check-Valid-Until "true";
APT::Periodic::Update-Package-Lists "0";
APT::Periodic::Unattended-Upgrade "0";
APT::Periodic::Download-Upgradeable-Packages "0";
Binary::apt::APT::Keep-Downloaded-Packages "false";
EOF

# field-apt: only interface operators should use
cat >"$EDIT/usr/local/bin/field-apt" <<'EOF'
#!/bin/bash
# field-apt — secure package tool · Hostess7 Field tree only
set -euo pipefail
export DEBIAN_FRONTEND="${DEBIAN_FRONTEND:-noninteractive}"
H7_LIST=/etc/apt/sources.list.d/spear-h7.list
if [[ ! -f "$H7_LIST" ]]; then
  echo "field-apt: missing $H7_LIST — Field tree not configured" >&2
  exit 2
fi
# refuse if foreign sources reappear active
if grep -RhsE '^\s*deb ' /etc/apt/sources.list /etc/apt/sources.list.d 2>/dev/null \
  | grep -vE 'zacharygeurts\.github\.io/Hostess7/apt|raw\.githubusercontent\.com/ZacharyGeurts/Hostess7' \
  | grep -vE '^\s*#' | grep -q .; then
  echo "field-apt: REFUSED — foreign apt source detected. Only Hostess7/apt allowed." >&2
  grep -RhsE '^\s*deb ' /etc/apt/sources.list /etc/apt/sources.list.d 2>/dev/null || true
  exit 3
fi
cmd="${1:-}"
shift || true
case "$cmd" in
  update) exec apt-get update "$@" ;;
  install) exec apt-get install -y --no-install-recommends "$@" ;;
  remove|purge) exec apt-get "$cmd" -y "$@" ;;
  search) exec apt-cache search "$@" ;;
  show) exec apt-cache show "$@" ;;
  list) exec apt-cache pkgnames "$@" ;;
  upgrade) exec apt-get upgrade -y --no-install-recommends "$@" ;;
  policy) exec apt-cache policy "$@" ;;
  sources) cat "$H7_LIST"; exit 0 ;;
  ""|-h|--help)
    cat <<H
field-apt — Hostess7 Field packages only
  field-apt update
  field-apt install <pkg>…
  field-apt remove|purge <pkg>…
  field-apt search|show|list|upgrade|policy|sources
H
    exit 0
    ;;
  *)
    echo "field-apt: unknown command: $cmd" >&2
    exit 1
    ;;
esac
EOF
run chmod +x "$EDIT/usr/local/bin/field-apt"

# neuter unattended-upgrades / mint update hooks
run rm -f "$EDIT/etc/apt/apt.conf.d/50unattended-upgrades" 2>/dev/null || true
run rm -f "$EDIT/etc/cron.daily/apt-compat" 2>/dev/null || true

# document policy in image
cat >"$EDIT/usr/local/share/spear/apt/POLICY.txt" <<'EOF'
Spear Field apt policy
======================
1. Only Hostess7 apt tree on GitHub is an allowed origin.
2. Use `field-apt` — not raw apt against Ubuntu/Mint mirrors.
3. Packages are Field / Hostess7 / Spear stack components (H7).
4. Unsigned bootstrap uses [trusted=yes] until H7 signing key is published.
5. Mint / Ubuntu / Debian package nets are pin-priority -10 (forbidden).
EOF

# also write local copy under Spear repo for GH Pages if mirrored
mkdir -p "$ROOT/apt" 2>/dev/null || true

echo "secure-apt OK · sole source Hostess7/apt field main"

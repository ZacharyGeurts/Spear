#!/bin/bash
# SPDX-License-Identifier: MIT
# Early multi-user: speed + harden live/desktop once spear_harden=1 (or always on Spear live).
set -euo pipefail

log() { echo "[spear-boot-harden] $*" | tee -a /run/spear-boot-harden.log 2>/dev/null || echo "[spear-boot-harden] $*"; }

CMDLINE=$(cat /proc/cmdline 2>/dev/null || true)
HARDEN=0
echo "$CMDLINE" | grep -q 'spear_harden=1' && HARDEN=1
# Always harden on Spear live identity
[[ -f /etc/spear-release ]] && HARDEN=1
[[ "$HARDEN" != "1" ]] && { log "skip (no spear_harden / not spear live)"; exit 0; }

log "start $(date -u +%Y-%m-%dT%H:%MZ)"

# --- SPEED: stop/mask bloat that often races the desktop ---
MASK_UNITS=(
  ModemManager.service
  cups.service
  cups-browsed.service
  cups.path
  cups.socket
  avahi-daemon.service
  avahi-daemon.socket
  blueman-mechanism.service
  openvpn.service
  touchegg.service
  kerneloops.service
  casper-md5check.service
  ubiquity.service
  mintsystem.service
  ssl-cert.service
  switcheroo-control.service
  apport.service
  unattended-upgrades.service
  apt-daily.service
  apt-daily.timer
  apt-daily-upgrade.timer
  motd-news.timer
  plocate-updatedb.timer
  fstrim.timer
)

for u in "${MASK_UNITS[@]}"; do
  systemctl mask --runtime "$u" 2>/dev/null || true
  systemctl stop "$u" 2>/dev/null || true
done

# --- SECURE: kernel runtime knobs (best-effort; ignore if absent) ---
sysctl_set() {
  local k="$1" v="$2"
  if [[ -e "/proc/sys/${k//./\/}" ]]; then
    sysctl -w "$k=$v" >/dev/null 2>&1 || true
  fi
}

# Network: no source routing, strict rp_filter, ignore broadcasts
sysctl_set net.ipv4.conf.all.rp_filter 1
sysctl_set net.ipv4.conf.default.rp_filter 1
sysctl_set net.ipv4.conf.all.accept_source_route 0
sysctl_set net.ipv4.conf.default.accept_source_route 0
sysctl_set net.ipv4.conf.all.accept_redirects 0
sysctl_set net.ipv4.conf.default.accept_redirects 0
sysctl_set net.ipv4.conf.all.send_redirects 0
sysctl_set net.ipv4.icmp_echo_ignore_broadcasts 1
sysctl_set net.ipv4.tcp_syncookies 1
sysctl_set net.ipv6.conf.all.accept_redirects 0
sysctl_set net.ipv6.conf.default.accept_redirects 0
sysctl_set kernel.kptr_restrict 2
sysctl_set kernel.dmesg_restrict 1
sysctl_set kernel.yama.ptrace_scope 1
sysctl_set fs.protected_hardlinks 1
sysctl_set fs.protected_symlinks 1
sysctl_set fs.protected_fifos 2
sysctl_set fs.protected_regular 2
sysctl_set kernel.unprivileged_bpf_disabled 1
sysctl_set net.core.bpf_jit_harden 2

# --- SECURE: ufw default deny if available (do not brick live loopback) ---
if command -v ufw >/dev/null 2>&1; then
  ufw --force default deny incoming 2>/dev/null || true
  ufw --force default allow outgoing 2>/dev/null || true
  ufw allow in on lo 2>/dev/null || true
  # NEXUS C2 / field panels stay loopback-only; no WAN open
  ufw --force enable 2>/dev/null || true
fi

# --- SPEED+SECURE: Field1 probe early so session scripts are instant ---
if [[ -x /usr/local/bin/spear-field-drive ]]; then
  if /usr/local/bin/spear-field-drive has-field-drive 2>/dev/null; then
    echo "field_drive=1" >/run/spear-field-drive.status
    log "Field1/Field Drive present"
  else
    echo "field_drive=0" >/run/spear-field-drive.status
    log "Field Drive absent"
  fi
fi

# Mark harden generation for C2 / status tools
cat >/run/spear-boot-posture.json <<EOF
{
  "schema": "spear-boot-posture/v1",
  "harden": true,
  "ts": "$(date -u +%Y-%m-%dT%H:%MZ)",
  "masked_units": ${#MASK_UNITS[@]},
  "cmdline_harden": $(echo "$CMDLINE" | grep -q spear_harden=1 && echo true || echo false)
}
EOF

# --- Field antivirus: Big Grin / UP swallows heuristic scan (log only at boot) ---
if [[ -x /usr/local/bin/spear-swallow ]]; then
  timeout 15 /usr/local/bin/spear-swallow scan 50 > /run/spear-swallow-boot-scan.txt 2>/dev/null || true
  log "swallow scan → /run/spear-swallow-boot-scan.txt"
fi

log "done"
exit 0

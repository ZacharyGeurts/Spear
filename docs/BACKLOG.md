# Spear · KILROY · NEXUS C2 — Backlog

Updated: 2026-07-13

| Track | Root |
|-------|------|
| Spear product | `/home/zachary/Desktop/SG/Spear` |
| Live ISO | `NewLatest/Spear/out/spear-latest.iso` |
| KILROY hub | `NewLatest/.pages-hub-KILROY` |
| NEXUS C2 | `NewLatest/lib/nexus-c2-harden.py` |

## Done

- [x] Field1 CLAIMED; SPEARMBR/FFAT detection; no false Field Drive nag
- [x] firstboot no browser; classic boot splash
- [x] Qubes security adoption doctrine
- [x] **Boot line doctrine** — every stage speed+harden (`docs/BOOT-HARDEN.md`)
- [x] Kernel cmdline: quiet, fsck skip, raid off, AppArmor, PTI, mask cups/modem/avahi/md5
- [x] `spear-boot-harden.service` + sysctl.d; War entry for max mitigations
- [x] KILROY `init` slimmed (no auto wipe wizard on Normal; debug-only benches)

## Next (Qubes-informed)

1. Disposable session profile (tmpfs home, wipe on logout)  
2. Trust-domain labels on apps (Queen=network, Terminal=operator, GIMP=media)  
3. USB / device whitelist gate (NEXUS)  
4. Split crypto via Hostess  
5. ISO menu path smoke (`SPEAR_DIRECT=0`) with classic splash  
6. KILROY TEAM ioctl/bench + secureboot enroll  

## Honesty

We adopt **Qubes principles** (isolation, least privilege, no casual admin browsing).  
We do **not** claim full Qubes/Xen/qrexec isolation in this live image yet.

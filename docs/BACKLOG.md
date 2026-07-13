# Spear backlog

Updated: 2026-07-13

| Track | Root |
|-------|------|
| Spear product | this repo (stack of record) |
| Live ISO | `out/spear-latest.iso` |
| Hostess7 | sibling Angel / library |

## Done

- [x] Field1 CLAIMED; SPEARMBR/FFAT detection
- [x] Boot line doctrine — speed + harden
- [x] C++ wartime · FIELD_UDP_WAR_BLASTERS · COOKED
- [x] Full stack layout: src + overlay + boot + iso/
- [x] `make release` · release receipt · no KILROY dependency in pack/limine
- [x] Wartime systemd units in overlay

## Next

1. Disposable session profile (tmpfs home)  
2. USB / device whitelist gate  
3. Secure Boot enroll for product ESP  
4. QEMU smoke CI on `out/spear-latest.iso`  
5. Optional: thinner squash (strip more Mint apps)

## Honesty

Qubes **principles** (isolation, least privilege) inform harden masks.  
We do **not** claim full Qubes/Xen isolation in the live image yet.

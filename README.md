# Spear

**Field storage · wartime C++ stack · field memory · LIVE_PLANET.**  
**This is the stack of record.** No satellite archives. No KILROY public repo.  
Repo: [ZacharyGeurts/Spear](https://github.com/ZacharyGeurts/Spear)

C++ or lower. No polkit product path. No H7c for study books.  
Hard path: **FIELD_UDP_WAR_BLASTERS** · **COOKED** to WALL_OUTLET · HATED · PISSED.  
FFAT is **not** Microsoft FAT. Live planet + Big Grin deck + zone mesh offload.

**Related (live only):** [Hostess7](https://github.com/ZacharyGeurts/Hostess7) · [Big_Grin_Terrorist_Hunter](https://github.com/ZacharyGeurts/Big_Grin_Terrorist_Hunter)

**Gone (do not clone):** `KILROY` public repo · `KILROY_iPXE` · `ZNetwork` — no archives, no redirects required.
---


**C++ or lower.** No polkit. No casual app pile. Field stack first.

**Field Fat ≠ FAT16/FAT32.** On-disk: `SPEARMBR` + type `0xE0` + superblock `FFAT\x03ENT` · FDRV · **CHIPs Field Die** pack pick (EntropyFold + WavePhase + PeakScan) · **ZERO/RLE/PAK/RAW/REF** · PAK = peaks + angle up/down · die L1 frame cache · `guaranteed` = pool worst case · `pack_ratio` measured · **never fake ×N**. Do not `mount -t vfat`.

**CHIPs / Field Die:** pack pick + die L1 (`chip-status` / `chip-bench` / `chip-demo`).

**Field MEMORY:** host **DRAM** and GPU **VRAM** are physical pools; Field Memory makes **more field-logical memory** (sparse map + PAK), not fake free bits. `spear fieldmem-ensure all` · `spear storage-status` · (legacy alias `spear kilroy-ensure` if present).

## Autoelevate (replaces polkit / sudo prompts)

| Model | setuid `spear` allowlist |
|-------|---------------------------|
| **Not** | polkit, pkexec, interactive root shell |
| **Yes** | `chmod u+s` on `/usr/local/bin/spear` once at install |

```bash
cd src && make
sudo install -o root -g root -m 4755 spear /usr/local/bin/spear
spear elevate-status
```

Allowlisted privileged ops only: `ffat-ensure`, `ffat-force`, `field1-backup`, `field1-claim`.

## Field1 drive (CLAIMED)

| Role | Device | Identity |
|------|--------|----------|
| **Field1** (whole disk) | `/dev/sdb` | FFAT label **FIELD1** · was `FIELD_QUBES` |
| Stable paths | | `/dev/disk/by-label/FIELD1` · `/dev/spear/field1` · by-id T-FORCE |
| **Hostess** (backup) | `/dev/nvme2n1` | `HOSTESS7_TEAM` · backup `field1-backup-20260713-035640` |

```bash
spear field1-status          # CLAIMED · physical · guaranteed · address_space
spear ffat-probe /dev/disk/by-label/FIELD1
spear storage-status
# (already claimed) backup was: spear field1-backup → claim
spear ffat-put /dev/sdb 0 zero
spear ffat-put /dev/sdb 10 wave  # peaks + angle → PAK
```

QEMU attaches host Field1 by default (`SPEAR_FIELD1=auto`). Use `SPEAR_FIELD1=img` for a stand-in image only.

## Boot: every line is speed + harden

See `data/boot-line-doctrine.json` and `data/qubes-security-adoption.json`.

| Stage | Speed | Harden |
|-------|-------|--------|
| isolinux | text menu, timeout 0 | classic splash; no VESA handoff junk |
| kernel cmdline | `quiet`, `fsck.mode=skip`, `raid=noautodetect`, mask cups/modem/avahi/md5 | AppArmor, page shuffle, kstack randomize, `slab_nomerge`, `pti=on`, serial audit |
| systemd | mask printers/modem/crash-phone-home | `spear-boot-harden.service` sysctl + ufw deny-in |
| session | no auto browser; cached Field1 probe | C2 loopback-only policy |

Entries: **Field** (fast default) · **Normal** (desktop) · **War** (extra mitigations) · **Debug** · **Field Drive** · **Compat**.

## Big Grin Swallows + UP Swallows (field AV)

Heuristic **eat** — freeze · quarantine · hard dispose. Not soft TERM theater.

```bash
spear-swallow scan          # heuristic pest list
spear-swallow eat <pid>     # swallow one
SPEAR_SWALLOW_FLEET=up spear-swallow scan
spear-miner serve           # loopback data miner :9489
curl http://127.0.0.1:9489/status
```

Hostess: `HOSTESS7_TEAM/fieldstorage/datacenter/{biggrin-eats,up-swallows}`  
Local brain: `fieldstorage/brain/local-grok/` (models slot ready; ~880 GiB free)

## Mint files

Only put **replacements** under:

```text
SG/Spear/overlay/<same-path-as-on-root>
```

```bash
./scripts/apply-overlay.sh /path/to/mint-root
```

Minimal desktops in overlay: Queen · Field Terminal · Field GIMP (add apps one by one later).

## Boot

### Product boot of record (KILROY)

```bash
./boot/pack.sh && ./boot/qemu.sh
```

No casper. Menu: Normal · Create Field Drive · Compat.

### Live Cinnamon GUI test (casper ISO)

```bash
./boot/qemu-gui.sh                 # SPEAR_DIRECT=1 · -vga std · serial log
SPEAR_DIRECT=0 ./boot/qemu-gui.sh  # full isolinux menu
```

ISO: `NewLatest/Spear/out/spear-latest.iso` · backlog: `docs/BACKLOG.md`

### Stack

| Layer | Role |
|-------|------|
| **Spear** | Product OS identity + Cinnamon class desktop |
| **KILROY** | Field-native kernel / Field Memory / FFAT stack |
| **NEXUS C2** | Local sealed C2 `127.0.0.1:9477` · always war |

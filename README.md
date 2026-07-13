# Spear

**Full stack to the OS · wartime C++ · field storage · LIVE_PLANET · ISO release.**  
**Stack of record.** No satellite archives. No KILROY public repo.  
Repo: [ZacharyGeurts/Spear](https://github.com/ZacharyGeurts/Spear)

**Version:** see `VERSION` · **Release:** [RELEASE.md](RELEASE.md)

C++ or lower product path. Hard path: **FIELD_UDP_WAR_BLASTERS** · **COOKED** to WALL_OUTLET · HATED · PISSED.  
FFAT is **not** Microsoft FAT. Live planet + Big Grin deck + zone mesh offload.

**Related (live only):** [Hostess7](https://github.com/ZacharyGeurts/Hostess7) · [Big_Grin_Terrorist_Hunter](https://github.com/ZacharyGeurts/Big_Grin_Terrorist_Hunter)

---

## Quick start — full stack

```bash
git clone https://github.com/ZacharyGeurts/Spear.git
cd Spear
make release          # C++ stack → overlay + initrd + receipt
make iso              # full live ISO remaster (long)
# or, with existing work tree:
# make iso-stamp
```

| Target | Does |
|--------|------|
| `make all` / `make stack` | Build all C++ ELFs |
| `make install` | Install ELFs into `overlay/` + `~/.local/bin` |
| `make initrd` | Field initramfs → `out/initramfs.cpio.gz` |
| `make pack` | Product boot image staging (limine paths) |
| `make iso` | Fetch Mint source → extract → apply → ISO |
| `make iso-stamp` | Apply stack to existing `work/` → rebuild ISO |
| `make release` | install + initrd + receipt (+ iso-stamp if work ready) |

Artifacts: `out/spear-latest.iso` · `out/release-receipt.json` · `out/initramfs.cpio.gz`

---

## Layout

```text
src/           C++ ELFs (spear, wartime, fleet-link, www, planet, …)
overlay/       OS identity + systemd + desktop + harden (live squashfs layer)
boot/          limine product boot · init · initrd · QEMU helpers
iso/           remaster pipeline (fetch · extract · apply · rebuild)
data/          doctrine + iso-boot menus + shot-certainty
docs/          BOOT-HARDEN · ISO-RELEASE · NO-ARCHIVES · library
```

---

## Autoelevate (replaces polkit)

```bash
cd src && make
sudo install -o root -g root -m 4755 spear /usr/local/bin/spear
spear elevate-status
```

Allowlisted privileged ops: `ffat-ensure`, `ffat-force`, `field1-backup`, `field1-claim`.

---

## Field storage

**Field Fat ≠ FAT16/FAT32.** On-disk: `SPEARMBR` + type `0xE0` + superblock `FFAT\x03ENT` · FDRV · CHIPs Field Die · ZERO/RLE/PAK/RAW/REF · **never fake ×N**.

```bash
spear field1-status
spear ffat-probe /dev/disk/by-label/FIELD1
spear storage-status
spear fieldmem-ensure all
```

---

## Wartime (on ISO + host)

```bash
spear-wartime --interval-ms 3000   # :9491 boards · COOKED ladder
spear-fleet-link                   # zone mesh
spear-www                          # deck :9490
spear-planet                       # LIVE_PLANET :9600
```

systemd units (enabled in overlay): `spear-boot-harden` · `spear-wartime` · `spear-fleet-link` · `spear-www` · `spear-planet`.

Pipeline: SPOT → VECTOR → COOK_FAT → QUEUE_REBURN → BURN → SCRUB → OUTLET_DESTROY → SEAL.

---

## Boot

| Path | How |
|------|-----|
| **Live ISO** | isolinux `data/iso-boot/live.cfg` — Field default · Normal · War · Debug · Field Drive · Compat |
| **Product** | `boot/limine.conf` + `boot/init` + initrd (no casper) |
| **QEMU** | `./boot/qemu-gui.sh` |

Every boot line is speed + harden — see `docs/BOOT-HARDEN.md`.

---

## Overlay rule

Only **replacements** under `overlay/<same-path-as-on-root>`. Applied by `iso/apply-stack.sh`.

```bash
./scripts/apply-overlay.sh /path/to/root
```

Minimal desktops: Queen · Field Terminal · Field GIMP.

---

## Gone

`KILROY` public · `KILROY_iPXE` · `ZNetwork` — **no archives**. See `docs/NO-ARCHIVES.md`.

God Bless.

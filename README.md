# Spear

**Full stack to the OS · wartime C++ · field storage · LIVE_PLANET · ISO release.**  
**Stack of record.** No satellite archives. No KILROY public repo.  
Repo: [ZacharyGeurts/Spear](https://github.com/ZacharyGeurts/Spear)

**Version:** see `VERSION` · **Release:** [RELEASE.md](RELEASE.md)

C++ or lower product path. Hard path: **FIELD_UDP_WAR_BLASTERS** · **COOKED** to WALL_OUTLET · HATED · PISSED.  
FFAT is **not** Microsoft FAT. Live planet + Big Grin deck + zone mesh offload.

**Related (live only):** [Hostess7](https://github.com/ZacharyGeurts/Hostess7) · [Big_Grin_Terrorist_Hunter](https://github.com/ZacharyGeurts/Big_Grin_Terrorist_Hunter)

---

## Product = test boot drive stack (not Mint underlay)

**Stack of record:** Field init + spear + Field1 — same path as the physical test boot drive.  
**Not product:** casper Mint remaster (optional utility only). **Not required:** QEMU underlay.

```bash
git clone https://github.com/ZacharyGeurts/Spear.git
cd Spear
make product          # Field ISO · no casper · no Mint underlay
# → out/spear-field-*.iso · out/spear-field-latest.iso · field-product-receipt.json
```

| Target | Does |
|--------|------|
| **`make product`** | **PRODUCT** Field ISO (test-drive path) |
| `make all` / `make stack` | Build all C++ ELFs |
| `make install` | Install ELFs → overlay + `~/.local/bin` |
| `make initrd` | Field initramfs |
| `make pack` | Product boot disk image `out/spear-boot.img` |
| `make iso` | Optional Mint casper remaster (**not** stack of record) |
| `make release` | = `make product` + receipt |

See [docs/PRODUCT-BOOT.md](docs/PRODUCT-BOOT.md).

---

## Field Linux tools (we own the names)

**C++ multicall** `fieldbox` (~100 KiB) + **field-nvtop** (AMOURANTHRTX GPU).  
Ironclad floor · CHIPs Field Die · Grok16 field_opt.

| You type | You get |
|----------|---------|
| `top` | Field process top |
| `nvtop` / `nv-top` | AMOURANTHRTX GPU top (AMD/NVIDIA/Intel) |
| `obs` | **field-obs** — capture rewrite (no Electron) |
| `gimp` | **field-gimp** — image rewrite (PPM · chips grade) |
| `ls` `ps` `df` `free` `cat` … | Field applets (fieldbox multicall) |
| `field help` | full applet list |
| `field chips` · `spear chip-*` | CHIPs / Field Die |

Docs: [FIELD-FUN-TOOLS.md](docs/FIELD-FUN-TOOLS.md) · [FIELD-LINUX-TOOLS.md](docs/FIELD-LINUX-TOOLS.md) · [FIELD-NVTOP.md](docs/FIELD-NVTOP.md)

## Layout

```text
src/           C++ ELFs (spear, fieldbox, field-nvtop, wartime, …)
overlay/       OS identity + systemd + desktop + harden (live squashfs layer)
boot/          limine product boot · init · initrd · QEMU helpers
iso/           remaster pipeline (fetch · extract · apply · rebuild)
data/          doctrine + iso-boot menus + shot-certainty
docs/          BOOT-HARDEN · ISO-RELEASE · Field tools · Grok16 notes
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

# Spear 22.3.1-field — ISO release

**Stack of record.** Full stack to the OS. C++ product path. No satellite archives.

| Item | Value |
|------|--------|
| Version | `22.3.1-field` (see `VERSION`) |
| Repo | https://github.com/ZacharyGeurts/Spear |
| Doctrine | FIELD_UDP_WAR_BLASTERS · COOKED · HATED · PISSED |
| Related | Hostess7 · Big_Grin_Terrorist_Hunter |

## What “full stack” means

| Layer | Path |
|-------|------|
| C++ ELFs | `src/` · `make -C src all` |
| OS identity overlay | `overlay/` (os-release, systemd, desktop, harden) |
| Live boot menu | `data/iso-boot/` (Field · Normal · War · Debug · Field Drive · Compat) |
| Product initrd boot | `boot/` (limine + init + fieldmem) |
| ISO remaster | `iso/` (fetch · extract · apply-stack · rebuild) |
| Wartime services | `overlay/etc/systemd/system/spear-*.service` |

## Release commands

```bash
git clone https://github.com/ZacharyGeurts/Spear.git
cd Spear

# build stack + install into overlay + initrd + receipt
make release

# full ISO remaster from upstream Mint (long)
make iso

# faster: apply stack onto existing work/ then rebuild ISO
export SPEAR_WORK=/path/to/work   # optional cache with edit/ + iso-extract/
make iso-stamp
```

Output:

- `out/spear-latest.iso` — live/hybrid ISO  
- `out/release-receipt.json` — version, sha256, binary list  
- `out/initramfs.cpio.gz` — product field initrd  

## Boot menus

**Live ISO (casper):** Field (default, fast multi-user) · Normal (Cinnamon) · War · Debug · Create Field Drive · Compat.

**Product limine image:** same modes via `boot/limine.conf` + `boot/init` (no casper).

## QEMU

```bash
./boot/qemu-gui.sh                 # SPEAR_DIRECT=1 default
SPEAR_ISO=out/spear-latest.iso ./boot/qemu-gui.sh
```

## Gone

- Public KILROY repo  
- KILROY_iPXE / ZNetwork archives as product path  
- Python / shell as runtime commander  

God Bless.

# Product boot = test boot drive stack (not Mint underlay)

## Stack of record

| Path | What | Role |
|------|------|------|
| **Field product** | `boot/` limine/isolinux + `init` + initrd + spear | **Product** — like Field1 test boot drive |
| Field1 disk | `/dev/disk/by-label/FIELD1` · SPEARMBR/FFAT | Storage of record |
| Hostess TEAM | `HOSTESS7_TEAM` | Angel library / backup |
| Casper Mint lean ISO | `iso/` remaster | **Optional** thin live only — **not** the stack |

## What we are *not*

- Not “stripped Mint with stickers” as the OS of record  
- Not QEMU/KILROY underlay as product (QEMU is optional test harness)  
- Not casper-required for Field identity  

## Build product ISO

```bash
make -C src all
make initrd
./boot/make-field-iso.sh
# → out/spear-field-*.iso · out/spear-field-latest.iso
# → out/field-product-receipt.json
```

## Boot entries (same spirit as test drive)

1. **Field** (default) — fast multi-user Field path  
2. **Normal** — full Field path  
3. **Create Field Drive** — claim FFAT Field1  
4. **War** — max harden  
5. **Debug** — verbose serial  

Init is `boot/init` (SPEAR FIELD), not casper.

## Operator attach Field1

Physical test drive: label **FIELD1** (or claim via menu).  
QEMU optional: `SPEAR_FIELD1=auto ./boot/qemu.sh` with Field disk — **not required** for product.

## Hostess 7

Angel holds books + Field apt + CRT manual.  
Spear Field ISO is the bootable stack she wires to.

God Bless.

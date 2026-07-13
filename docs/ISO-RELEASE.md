# ISO release pipeline

```text
iso/fetch-upstream.sh   → iso/upstream-mint-cinnamon.iso (Mint = source material)
iso/extract.sh          → work/iso-extract + work/edit
iso/apply-stack.sh      → overlay + C++ bins + live.cfg + identity
iso/rebuild-iso.sh      → out/spear-VERSION-cinnamon-64bit-DATE.iso
```

Or: `make iso` / `make iso-stamp` / `make release`.

## Environment

| Var | Meaning |
|-----|---------|
| `SPEAR_WORK` | work tree with `edit/` + `iso-extract/` (default `$ROOT/work`) |
| `SPEAR_UPSTREAM_ISO` | path to Mint source ISO |
| `SPEAR_FORCE_SQUASH` | `1` rebuild squashfs (default on rebuild) |
| `SPEAR_JOBS` | mksquashfs processors |
| `SPEAR_ISO` | QEMU ISO path |

## Host tools

`xorriso` · `mksquashfs` · `unsquashfs` · `g++` · `make` · optional `isohybrid` · `busybox` (initrd)

## Honesty

- Upstream packages may still be Debian/Mint lineage under the hood.  
- Product identity is **Spear** (`os-release`, hostname, menus, ELFs, services).  
- We do not claim full Qubes isolation in the live image.

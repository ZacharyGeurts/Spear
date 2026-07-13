# Spear boot — optimize · speed · harden · secure

Every stage of boot is a deliberate surface. No idle tokens.

## Paths

1. **Live (casper)** — `data/iso-boot/live.cfg` + squashfs units  
2. **Product (KILROY)** — `boot/limine.conf` + `boot/init`  
3. **QEMU direct** — `boot/qemu-gui.sh` APPEND mirrors Normal  

## Kernel cmdline (Normal)

| Token | Why |
|-------|-----|
| `quiet loglevel=3` | less console I/O |
| `systemd.show_status=error` | hide OK spam |
| `fsck.mode=skip` | live media — no block fsck |
| `raid=noautodetect` | skip md scan |
| `noresume` | no hibernate image hunt |
| `apparmor=1 security=apparmor` | MAC |
| `page_alloc.shuffle=1` | ASLR for pages |
| `randomize_kstack_offset=on` | stack ASLR |
| `slab_nomerge` | heap isolation |
| `pti=on` | KPTI |
| `systemd.mask=…` | kill cups/modem/avahi/md5/ubiquity/touchegg/kerneloops/apport |
| `console=ttyS0,115200n8` | always audit serial |
| `spear_harden=1` | userspace harden marker |

**War** entry adds `spectre_v2=on`, `mds=full`, `tsx=off`, etc. (slower, higher threat).

## Userspace

- `spear-boot-harden.service` — runtime mask + sysctl + ufw default deny in + Field1 probe cache  
- `sysctl.d/99-spear-harden.conf` — kptr/dmesg/ptrace/bpf/rp_filter  
- firstboot: **no browser**  
- field-drive: SPEARMBR/Field1 detection  

## Qubes parallel

| Qubes | Spear boot |
|-------|------------|
| No net in dom0 casually | no auto Queen; C2 loopback |
| Minimal admin surface | mask printer/modem stacks |
| Explicit device trust | Field1 serial / SPEARMBR |
| Policy before action | C2 capability gates (later session) |

## Measure

```bash
# serial timestamps after QEMU boot
grep -E 'Command line:|graphical.target|login:' out/qemu-logs/spear-gui-*.log | tail
```

# field-nvtop — AMOURANTHRTX Field GPU top

Replaces stock `nvtop` / `nv-top` with a Field-native C++ TUI.

| Vendor | Source |
|--------|--------|
| **AMD** | amdgpu sysfs · `gpu_busy_percent` · VRAM/GTT · hwmon temp/power/clocks · PCIe link |
| **NVIDIA** | DRM sysfs + `nvidia-smi` enrich when present |
| **Intel** | i915/xe sysfs + hwmon |
| **Clients** | `/proc/*/fdinfo` DRM memory touch |

```bash
field-nvtop          # live (q quit)
field-nvtop --once
field-nvtop --json
nv-top               # alias
nvtop                # alias
field-gpu            # alias
```

Brand ribbon: **AMOURANTHRTX** pink/cyan Field fabric.

God Bless.

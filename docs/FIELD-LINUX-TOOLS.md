# Field Linux tools — we write our own

**C++ and lower only.** Multicall `fieldbox` + `field-nvtop`.  
Ironclad floor · CHIPs Field Die · Grok16 field_opt all the way.

## We own these command names

| Command | Binary | Role |
|---------|--------|------|
| **top** | fieldbox | Field process top |
| **nvtop** / **nv-top** | field-nvtop | AMOURANTHRTX GPU top (AMD/NVIDIA/Intel) |
| **ls** | fieldbox | Field directory list |
| **ps** | fieldbox | Process table |
| **df** **free** | fieldbox | Disk / RAM honesty |
| **cat** **echo** **pwd** **env** | fieldbox | Core |
| **uname** **id** **whoami** **hostname** | fieldbox | Identity (uname → Spear-Field) |
| **head** **wc** **which** **stat** | fieldbox | Text / path |
| **mkdir** **rm** **sleep** **kill** | fieldbox | Ops |
| **uptime** **clear** **true** **false** | fieldbox | Session |
| **field** **fieldbox** | fieldbox | Multicall entry · `field ls` |
| **chips** | fieldbox → spear | CHIPs / Field Die plane |

```bash
field help
field ls -la
top
nvtop
field chips          # Ironclad + CHIPs pointer
spear chip-status    # Field Die EntropyFold · WavePhase · PeakScan
```

## Size (GitHub ISO)

One multicall ELF (~100–200 KiB) + field-nvtop — not a GNU coreutils pile.  
Keeps live ISO on the path to **&lt; 2 GiB**.

## Grok16

See [GROK16-FIELD-TOOLS.md](GROK16-FIELD-TOOLS.md) — what these tools teach field_opt.

God Bless.

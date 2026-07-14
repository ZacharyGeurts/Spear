# Grok16 notes — Field tools feed the compiler

We write Field tools in **C++ and lower** so Grok16 (`field_opt` / belt / CHIPs) learns real hot paths — not toy IR.

## Ironclad BSP

| Layer | Law |
|-------|-----|
| Floor | Reality physics — real `/proc`, real sysfs, real bytes |
| Ceiling | Receipts — measured VRAM, measured RSS, measured H |
| Chain | God → Ironclad plate → Field → Hostess 7 / Spear tools → humanity |

Tools must never invent capacity. `free` / `df` / `field-nvtop` report **measured** values only (same honesty as FFAT `guaranteed_bytes` / `pack_ratio`).

## CHIPs / Field Die (already in spear)

```text
spear chip-status | chip-bench | chip-demo
Ops: EntropyFold · WavePhase · Shannon · PeakScan · PackPick · FnvMix
```

Grok16 field_opt should treat CHIPs die slots as first-class when lowering FFAT pack paths — not ad-hoc host memcpy theater.

## What fieldbox teaches Grok16

| Pattern | Why it matters for field_opt |
|---------|------------------------------|
| Multicall single ELF | One text segment, shared helpers, smaller I-cache footprint |
| `/proc` + `/sys` tight reads | Prefer stack buffers + bounded `read`; avoid iostream thrash |
| No heap in list-dir hot loop | Sort once; stream print — good SSA shape |
| ASCII + ANSI TUI | Branch-light color paths; keep cold strings in rodata |
| Honest metrics only | Compiler must not “optimize” by eliding measurement |

## What field-nvtop teaches Grok16

| Pattern | Note |
|---------|------|
| AMD sysfs first | Real silicon path (gpu_busy_percent, hwmon) before vendor CLIs |
| Optional nvidia-smi enrich | Side channel — don’t hard-require closed libs in Field Die |
| UTF-8 bar glyphs | Width accounting for display vs byte length (future) |
| AMOURANTHRTX brand path | Field fabric identity in UX + fabric map |

## ASM / lower (when to drop)

| Use ASM | Prefer C++ |
|---------|------------|
| leaf memcpy / checksum in CHIPs die | Control flow, `/proc` parse, TUI |
| syscall wrappers when libc lies | Portable open/read/write loops |
| Grok16 belt chunk kernels | Multicall dispatch table |

## Improvement backlog for Grok16

1. Recognize Field Die `ChipOp` sequence → fuse EntropyFold+WavePhase  
2. Honor `SPEAR_CHIPS=1` env as optimization affinity  
3. Prefer static linking recipes for fieldbox on live ISO  
4. Ironclad receipt IR: attach measured-byte attributes to loads from `/sys`  

**Motto:** Field native. Grok16 all the way. Receipts over vibes.

God Bless.

# H7 formats (NewLatest) → Spear / KILROY / Field Memory

Research pull from `SG/NewLatest` Hostess7 format family. Use for **size, speed, security** expansion of FFAT + field MEMORY — not as fake ×N.

## Family map (roles)

| Format | Magic | Role | Expansive idea for us |
|--------|-------|------|------------------------|
| **H7/7** | `H7\x07\x01` | Canonical lossless container · zlib-9 · portable header · face disguise | Frame/payload **codec layer** under FFAT (besides PAK/RLE) · **sha256 seal** on objects · properties-only reveal |
| **H7e/1** | `H7E\x01` | Extractable folder archive (zlib tar) · release shipping | Field1 **backup bundles**, Hostess handoff, offline kits |
| **H7s/1** | `H7S\x01` | **Speed** · structural **slice index** · execute **without full decompress** · KILROY-loved | Hot path for field memory: read one slice · **CHIPS redense** for JSON batteries · never wrap vmlinuz/initrd |
| **H7c/1–4** | `H7C\x01`…`\x04` | Hostess 7 **Condenser** · combinatronic rebalance · nested H7B · ironclad block | Pattern recondense for brain-like / structured payloads · balance gate before adopt |
| **H7B/3** | `H7B\x03` | Brain **pattern dictionary** · fly layer · zlib · GitHub-sized sections | **Shared pattern dictionary** across Field Memory planes → more memory from RAM/GPU when corpora share strings |
| **H7snap** | (registry) | Snap / inplace replace | Atomic map/super updates on Field1 |
| **WRZC / ZAC7 / WRDT** | World_Redata | Content-addressed pack with min_gain | Already kin to FFAT REF + measured shrink |
| **FDRV / fielddrive** | `FDRV\x01` | Field Drive container | Already on Field1 identity path |

Doctrine sources:
- `data/field-h7-doctrine.json`
- `data/field-h7c-doctrine.json`
- `data/field-h7b-brain-doctrine.json`
- `data/field-h7s-global-doctrine.json`
- libs: `lib/field-h7-format.py`, `field-h7s-format.py`, `field-h7c-compression.py`, `field-h7b-brain-storage.py`, `h7-field-drive-tie.py`

## High-value expansions (priority)

### 1. H7s structural slices → Field Memory speed
- Hot read: header + **one slice**, not whole frame/pool scan.
- CHIPS redense for structured JSON (drop duplicate chip arrays).
- **KILROY rule already:** never wrap boot/kernel images — keep that.
- Map to Spear: optional `kMapH7s` pool objects for large logical frames; `ffat-get` can return slice windows.

### 2. H7B pattern dictionary → more memory from RAM/GPU
- Analyze repeated patterns (min len 8, min occ 3, dict ≤ 4096).
- Pipeline: canonicalize → substitute → fly → zlib.
- **Expansive idea:** one **shared H7B dictionary** for host DRAM + GPU VRAM field memory planes so the same patterns don’t burn both pools.
- This is real expansion of *usable* field memory for brain/JSON/prose — measured like pack_ratio.

### 3. H7/7 portable header → integrity + portability
- Instant header (layout + sha256 + decompress recipe) without full read.
- Face disguise: native-looking prefix; H7 only in properties.
- Map to Spear: PoolObj / superblock banner already similar; add **sha256** on FPK1 payloads (we have content_fp FNV only).

### 4. H7c condenser → structured rebalance
- Rebalance on open when unbalanced; adopt only if smaller or balance↑.
- Steel neural plates + universal rapid spider-wire (G16 batteries).
- Map to Spear: optional **repack pass** on Field Memory after many PAK/RAW writes (“condense plane”).

### 5. H7e → Field1 ↔ Hostess shipping
- Double-click extractable release trees.
- Map to Spear: `field1-backup` could emit `.h7e` instead of only rsync trees.

### 6. Family guard (anti-H7-on-H7)
- Never rewrap same family; cross-pack unwraps first.
- Map to Spear: never FFAT-inside-FFAT frames; refuse nested `FFAT\x03ENT` payload as RAW without unpack.

### 7. fieldstorage tie (h7-field-drive-tie)
- Hostess TEAM NVMe `fieldstorage` ↔ library corpora resolve.
- Map to Spear: Field1 + Hostess already dual-drive; path resolve for brain corpora on field drive.

## What we should *not* copy blindly
- Combinatorics tree / plate meld runtime (Field Research v2 tombstoned).
- Wrapping KILROY kernel/initrd with H7s.
- Claiming infinite expand from dictionary alone without measured ratios.

## Suggested Spear integration order
1. **Shared H7B-style pattern dict** for fieldmem host+gpu (capacity). — **DONE** (`spear_patdict`, kind=DICT, SHA-256 seal, inject refuse)
2. **H7s-like slice table** on large frames (speed).
3. **sha256 seals** on pool objects (security). — partial (dict seal + DIC1 bind)
4. **H7e backup** for field1-backup (ops).
5. **Condense pass** inspired by H7c rebalance (maintenance).

## Current Spear vs H7 gap

| Need | Spear today | H7 already has |
|------|-------------|----------------|
| Frame pack | ZERO/RLE/PAK/REF | zlib H7/7 + H7c layers |
| Hot partial read | full frame get | H7s slice execute |
| Cross-plane dict | none | H7B patterns |
| Shipping | rsync | H7e extractable |
| KILROY adopt | fieldmem ensure | H7s global adopt lane |

// SPDX-License-Identifier: MIT
// Field Fat (FFAT) — entropy-packed field storage. NOT Microsoft FAT16/32.
//
// Honesty (Primer ch.04 entropy + ch.14 Shannon):
//   H = −Σ pᵢ log₂ pᵢ  bits/byte on frame histogram (0..8).
//   Lossless store size ≥ (H/8)·frame_bytes for that frame.
//   Capacity gain is MEASURED from ZERO / RLE / PAK / content-dedup frames.
//   NEVER a hardcoded multiplier (×91 was fake metadata).
//
// Peak–Angle pack (PAK): field signal model — more logical samples than stored
//   control points. Between peaks, angle-up / angle-down (constant step ramps)
//   reconstruct the path. One PEAK/VALLEY record covers many samples.
//
// Accounting:
//   physical_bytes      — media size
//   guaranteed_bytes    — pool size (worst case: all unique high-H RAW frames)
//   address_space_bytes — sparse logical map · frame_bytes (empty costs map only)
//   pack_ratio          — logical_stored / physical_used  (live, after writes)
//
// Primer ch.08 fatPack: virtual AMMOFAT geometry on bus only — not host vfat.
// FDRV family seal tag for Field Drive identity.
#pragma once
#include <cstdint>
#include <string>

namespace spear {

constexpr uint32_t kSector = 512;
constexpr uint32_t kPartLba = 2048;     // 1 MiB align
constexpr uint32_t kFrameBytes = 4096;  // field frame

// On-disk magics (field tech — not "MSDOS5.0" / vfat)
constexpr char kMbrMagic[8] = {'S', 'P', 'E', 'A', 'R', 'M', 'B', 'R'};
constexpr char kSecureTag[8] = {'S', 'E', 'C', 'U', 'R', 'E', '0', '1'};
// v4 entropy edition
constexpr char kFfatMagic[8] = {'F', 'F', 'A', 'T', '\x03', 'E', 'N', 'T'};
// accept legacy superblocks as "present but obsolete"
constexpr char kFfatMagicLegacy[8] = {'F', 'F', 'A', 'T', '\x02', 'F', 'L', 'D'};
constexpr char kFdrvMagic[5] = {'F', 'D', 'R', 'V', '\x01'};

constexpr uint8_t kPartTypeFieldFat = 0xE0;

// Map entry kinds
constexpr uint8_t kMapFree = 0;
constexpr uint8_t kMapZero = 1;  // all-zero logical frame (0 pool bytes)
constexpr uint8_t kMapRaw = 2;   // full frame in pool
constexpr uint8_t kMapRle = 3;   // RLE-packed in pool
constexpr uint8_t kMapRef = 4;   // content-dedup → other logical id
constexpr uint8_t kMapPak = 5;   // peak–angle pack (ramps / peaks / valleys)
constexpr uint8_t kMapDict = 6;  // shared H7B-kin pattern dictionary (host+GPU)

// Virtual AMMOFAT geometry for Field Die fatPack (bus [12..15]) — not host vfat.
struct AmmoFatGeometry {
  uint16_t bps = 512;
  uint8_t spc = 8;  // sectors per cluster (field frame / 512)
  uint16_t reserved = 1;
  uint8_t fats = 2;
  uint16_t root_ent = 512;
  uint32_t data_start = 0;
  uint32_t total_clusters = 0;
  uint32_t vol_offset_lba = kPartLba;
};

struct EnsureResult {
  bool ok = false;
  bool had_mbr = false;
  bool had_ffat = false;
  bool wrote_mbr = false;
  bool wrote_ffat = false;
  bool legacy_fake_factor = false;  // old ×91 superblock seen
  uint64_t physical_bytes = 0;
  uint64_t guaranteed_bytes = 0;     // honest worst-case usable
  uint64_t address_space_bytes = 0;  // sparse logical map
  uint64_t logical_stored = 0;
  uint64_t physical_used = 0;
  uint32_t pack_ratio_x1000 = 1000;  // logical/physical ·1000 (1000 = 1.0×)
  uint32_t shannon_avg_x1000 = 0;    // mean H·1000 bits/byte over measured frames
  uint64_t frames_pak = 0;           // peak–angle packed frames
  uint64_t frames_dict = 0;          // shared pattern-dict packed frames
  uint32_t logical_frames = 0;
  uint32_t part_lba = kPartLba;
  std::string label;
  std::string error;
  // alias used by older call sites: field_bytes == guaranteed_bytes
  uint64_t field_bytes = 0;
};

EnsureResult ffat_probe(const std::string& path);
// map_divisor: disk default 32; field memory planes use 8 for denser sparse
// address_space (more field-logical memory per physical pool byte).
EnsureResult ffat_ensure(const std::string& path, bool force = false, uint32_t map_divisor = 32);

// Shannon H·1000 for a buffer (0..8000). Pure measurement — no claims.
uint32_t ffat_shannon_x1000(const uint8_t* data, size_t n);

// Pack one frame → dest (capacity >= frame+8). Returns packed length, sets kind.
// kind: kMapZero / kMapRaw / kMapRle. Never invents capacity.
size_t ffat_pack_frame(const uint8_t* frame, size_t frame_n, uint8_t* dest, size_t dest_cap,
                       uint8_t& kind_out, uint32_t& shannon_x1000_out);

// Unpack packed blob into frame buffer (frame_n bytes).
bool ffat_unpack_frame(uint8_t kind, const uint8_t* packed, size_t packed_n, uint8_t* frame,
                       size_t frame_n);

bool ffat_ammo_geometry(const std::string& path, AmmoFatGeometry& out);

// Write / read a logical frame (requires formatted volume). For real pack demo.
bool ffat_put_frame(const std::string& path, uint32_t logical_id, const uint8_t* frame,
                    size_t frame_n, std::string& err);
bool ffat_get_frame(const std::string& path, uint32_t logical_id, uint8_t* frame, size_t frame_n,
                    std::string& err);

}  // namespace spear

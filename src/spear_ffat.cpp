// SPDX-License-Identifier: MIT
// Field Fat formatter — Shannon + peak–angle packing (honest increased storage).
// Primer: ch.04 entropy · ch.08 fatPack · ch.14 Shannon H.
// Peak–angle: store control points (peaks/valleys) + angle up/down ramps;
// more logical samples than physical points when the path is structured.
// CHIPs / Field Die: pack pick + frame cache via spear_chip (AMOURANTHRTX-class).
// FDRV identity tag. No Microsoft FAT. No fake capacity multipliers.
#include "spear_ffat.hpp"
#include "spear_chip.hpp"
#include "spear_patdict.hpp"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <string>
#include <unistd.h>
#include <vector>

namespace spear {
namespace {

#pragma pack(push, 1)
// LBA part_start: Field Fat superblock (one sector)
struct FfatSuper {
  char magic[8];  // FFAT\x03ENT
  uint32_t version;  // 0x00040000
  uint32_t sector_bytes;
  uint32_t frame_bytes;
  uint32_t part_lba;
  uint32_t part_sectors;
  uint64_t physical_bytes;
  uint64_t payload_bytes;        // bytes after meta into end of part
  uint64_t guaranteed_bytes;     // = pool byte size (worst-case RAW)
  uint64_t address_space_bytes;  // logical_frames * frame_bytes
  uint32_t logical_frames;
  uint32_t map_lba;       // absolute LBA of map
  uint32_t map_sectors;
  uint32_t pool_lba;      // absolute LBA of pack pool
  uint32_t pool_sectors;
  uint64_t pool_cursor;   // next free byte offset in pool
  // live stats
  uint64_t logical_stored;
  uint64_t physical_used;
  uint64_t frames_zero;
  uint64_t frames_raw;
  uint64_t frames_rle;
  uint64_t frames_ref;
  uint64_t shannon_sum_x1000;  // sum of H·1000
  uint32_t frames_measured;
  uint32_t flags;  // bit0 sealed, bit1 field1, bit2 entropy_pack
  char label[16];
  // Virtual AMMOFAT geometry for Field Die fatPack
  uint16_t ammo_bps;
  uint8_t ammo_spc;
  uint8_t ammo_fats;
  uint16_t ammo_reserved;
  uint16_t ammo_root_ent;
  uint32_t ammo_data_start;
  uint32_t ammo_total_clusters;
  uint32_t ammo_vol_offset_lba;
  char fdrv_tag[5];
  uint8_t pad0[3];
  uint64_t created_unix;
  uint8_t seal[32];
  // Compiler-measured pad: prior fields are 236 bytes under #pragma pack(1)
  uint8_t reserved[276];
};
#pragma pack(pop)

static_assert(sizeof(FfatSuper) == 512, "super must be exactly one sector");

// 24-byte map entry per logical frame (pool_off is 64-bit — TB-class media)
#pragma pack(push, 1)
struct MapEntry {
  uint8_t kind;           // free/zero/raw/rle/ref
  uint8_t shannon_x32;    // H·32 (0..255), 0 if free/zero
  uint16_t packed_len;    // payload bytes in pool (0 for free/zero/ref)
  uint32_t ref_id;        // target logical id when kind=REF, else 0
  uint64_t pool_off;      // byte offset in pack pool
  uint64_t content_fp;    // FNV-1a of logical frame content
};
#pragma pack(pop)
static_assert(sizeof(MapEntry) == 24, "map entry 24 bytes");

// Pool object header (prepended to packed payload)
#pragma pack(push, 1)
struct PoolObj {
  char magic[4];  // "FPK1"
  uint8_t kind;
  uint8_t shannon_x32;
  uint16_t packed_len;
  uint32_t logical_id;
  uint64_t content_fp;
};
#pragma pack(pop)
static_assert(sizeof(PoolObj) == 20, "pool obj header");

bool read_all(int fd, uint64_t off, void* buf, size_t n) {
  if (lseek(fd, static_cast<off_t>(off), SEEK_SET) < 0) return false;
  auto* p = static_cast<uint8_t*>(buf);
  size_t got = 0;
  while (got < n) {
    const ssize_t r = ::read(fd, p + got, n - got);
    if (r <= 0) return false;
    got += static_cast<size_t>(r);
  }
  return true;
}

bool write_all(int fd, uint64_t off, const void* buf, size_t n) {
  if (lseek(fd, static_cast<off_t>(off), SEEK_SET) < 0) return false;
  auto* p = static_cast<const uint8_t*>(buf);
  size_t put = 0;
  while (put < n) {
    const ssize_t w = ::write(fd, p + put, n - put);
    if (w <= 0) return false;
    put += static_cast<size_t>(w);
  }
  return true;
}

uint64_t file_size(int fd) {
  const off_t cur = lseek(fd, 0, SEEK_CUR);
  const off_t end = lseek(fd, 0, SEEK_END);
  if (cur >= 0) lseek(fd, cur, SEEK_SET);
  return end < 0 ? 0 : static_cast<uint64_t>(end);
}

void put_u32(uint8_t* p, uint32_t v) {
  p[0] = static_cast<uint8_t>(v);
  p[1] = static_cast<uint8_t>(v >> 8);
  p[2] = static_cast<uint8_t>(v >> 16);
  p[3] = static_cast<uint8_t>(v >> 24);
}

uint64_t fnv1a64(const uint8_t* data, size_t n) {
  uint64_t h = 14695981039346656037ull;
  for (size_t i = 0; i < n; ++i) {
    h ^= data[i];
    h *= 1099511628211ull;
  }
  return h;
}

void seal_super(FfatSuper& s) {
  std::memset(s.seal, 0, sizeof s.seal);
  const auto* raw = reinterpret_cast<const uint8_t*>(&s);
  const size_t n = offsetof(FfatSuper, seal);
  uint8_t acc[32]{};
  for (size_t i = 0; i < n; ++i) acc[i % 32] ^= raw[i];
  for (int i = 0; i < 8; ++i) acc[i] ^= static_cast<uint8_t>(kFfatMagic[i]);
  acc[0] ^= 0xE4;  // entropy edition tag
  std::memcpy(s.seal, acc, 32);
}

bool check_seal(const FfatSuper& s) {
  FfatSuper t = s;
  uint8_t old[32];
  std::memcpy(old, t.seal, 32);
  seal_super(t);
  return std::memcmp(old, t.seal, 32) == 0;
}

bool has_mbr(const uint8_t* sec) {
  if (sec[510] != 0x55 || sec[511] != 0xAA) return false;
  if (std::memcmp(sec + 3, kMbrMagic, 8) != 0) return false;
  if (std::memcmp(sec + 11, kSecureTag, 8) != 0) return false;
  return sec[0x1BE + 4] == kPartTypeFieldFat || sec[0x1BE + 4] == 0x0E ||
         sec[0x1BE + 4] == 0x0C;
}

bool is_ffat_v4(const uint8_t* sec) {
  return std::memcmp(sec, kFfatMagic, 8) == 0;
}

bool is_ffat_legacy(const uint8_t* sec) {
  return std::memcmp(sec, kFfatMagicLegacy, 8) == 0;
}

uint32_t part_lba_of(const uint8_t* mbr) {
  return static_cast<uint32_t>(mbr[0x1BE + 8]) | (static_cast<uint32_t>(mbr[0x1BE + 9]) << 8) |
         (static_cast<uint32_t>(mbr[0x1BE + 10]) << 16) |
         (static_cast<uint32_t>(mbr[0x1BE + 11]) << 24);
}

void build_mbr(uint8_t* mbr, uint32_t total_sectors, uint32_t part_start) {
  std::memset(mbr, 0, kSector);
  mbr[0] = 0xEB;
  mbr[1] = 0x00;
  mbr[2] = 0x90;
  std::memcpy(mbr + 3, kMbrMagic, 8);
  std::memcpy(mbr + 11, kSecureTag, 8);
  put_u32(mbr + 19, 0x00040000u);  // v4 entropy FFAT
  put_u32(mbr + 0x1B8, static_cast<uint32_t>(std::time(nullptr)) ^ static_cast<uint32_t>(getpid()));
  uint8_t* p = mbr + 0x1BE;
  p[0] = 0x80;
  p[1] = 0xFE;
  p[2] = 0xFF;
  p[3] = 0xFF;
  p[4] = kPartTypeFieldFat;
  p[5] = 0xFE;
  p[6] = 0xFF;
  p[7] = 0xFF;
  put_u32(p + 8, part_start);
  put_u32(p + 12, total_sectors > part_start ? total_sectors - part_start : 0);
  mbr[510] = 0x55;
  mbr[511] = 0xAA;
}

// Plan layout from partition sector count.
// Meta: super(1) + banner(1) + map + pool.
// Map budget: min(256 MiB, ~1/256 of partition) — sparse address space, not free capacity.
struct Layout {
  uint32_t part_lba = 0;
  uint32_t part_sectors = 0;
  uint32_t map_lba = 0;
  uint32_t map_sectors = 0;
  uint32_t pool_lba = 0;
  uint32_t pool_sectors = 0;
  uint32_t logical_frames = 0;
  uint64_t guaranteed_bytes = 0;
  uint64_t address_space_bytes = 0;
  uint64_t payload_bytes = 0;
};

Layout plan_layout(uint32_t part_lba, uint32_t part_sectors, uint32_t map_divisor = 32) {
  Layout L;
  L.part_lba = part_lba;
  L.part_sectors = part_sectors;
  if (part_sectors < 64) return L;
  if (map_divisor < 4) map_divisor = 4;
  if (map_divisor > 64) map_divisor = 64;

  // sector 0 super, sector 1 banner; map starts at part_lba+2
  const uint32_t meta = 2;
  const uint64_t part_bytes = static_cast<uint64_t>(part_sectors) * kSector;
  L.payload_bytes = part_bytes > meta * kSector ? part_bytes - meta * kSector : 0;

  // Frame map budget:
  //   Smaller divisor → larger map → more sparse address_space (field memory).
  //   Disk default 32; host/GPU field memory uses 8 → more logical memory/point.
  //   Gain beyond address_space still only from ZERO/RLE/PAK/REF (measured).
  uint64_t map_bytes = L.payload_bytes / map_divisor;
  if (map_bytes < 1024ull * 1024ull) map_bytes = 1024ull * 1024ull;
  const uint64_t map_cap = 8ull * 1024ull * 1024ull * 1024ull;  // 8 GiB
  if (map_bytes > map_cap) map_bytes = map_cap;
  if (map_bytes + 64ull * kSector > L.payload_bytes) {
    map_bytes = L.payload_bytes > 128ull * kSector ? L.payload_bytes / 8 : kSector;
  }
  map_bytes = (map_bytes / kSector) * kSector;
  if (map_bytes < kSector) map_bytes = kSector;

  L.map_sectors = static_cast<uint32_t>(map_bytes / kSector);
  // logical_frames fits in u32; 8 GiB map / 24 ≈ 357M entries
  const uint64_t frames64 = map_bytes / sizeof(MapEntry);
  L.logical_frames = frames64 > 0xFFFFFFFFull ? 0xFFFFFFFFu : static_cast<uint32_t>(frames64);
  if (L.logical_frames < 16) L.logical_frames = 16;

  L.map_lba = part_lba + meta;
  L.pool_lba = L.map_lba + L.map_sectors;
  if (L.pool_lba >= part_lba + part_sectors) {
    L.pool_sectors = 0;
  } else {
    L.pool_sectors = (part_lba + part_sectors) - L.pool_lba;
  }
  L.guaranteed_bytes = static_cast<uint64_t>(L.pool_sectors) * kSector;
  L.address_space_bytes = static_cast<uint64_t>(L.logical_frames) * kFrameBytes;
  return L;
}

AmmoFatGeometry plan_ammo_geometry(uint64_t address_space_bytes, uint32_t part_lba) {
  AmmoFatGeometry g;
  g.bps = 512;
  g.spc = static_cast<uint8_t>(kFrameBytes / 512);
  g.reserved = 1;
  g.fats = 2;
  g.root_ent = 512;
  g.vol_offset_lba = part_lba;
  const uint64_t cluster_bytes = static_cast<uint64_t>(g.spc) * g.bps;
  uint64_t clusters = address_space_bytes / cluster_bytes;
  if (clusters < 3) clusters = 3;
  if (clusters > 0xFFFFFFFFull) clusters = 0xFFFFFFFFull;
  g.total_clusters = static_cast<uint32_t>(clusters);
  const uint32_t root_sec = (g.root_ent * 32u + 511u) / 512u;
  const uint32_t spf = static_cast<uint32_t>((g.total_clusters * 2u + 511u) / 512u);
  g.data_start = g.reserved + static_cast<uint32_t>(g.fats) * (spf ? spf : 1) + root_sec;
  return g;
}

void build_super(FfatSuper& s, uint64_t physical_bytes, const Layout& L) {
  std::memset(&s, 0, sizeof s);
  std::memcpy(s.magic, kFfatMagic, 8);
  s.version = 0x00040000u;
  s.sector_bytes = kSector;
  s.frame_bytes = kFrameBytes;
  s.part_lba = L.part_lba;
  s.part_sectors = L.part_sectors;
  s.physical_bytes = physical_bytes;
  s.payload_bytes = L.payload_bytes;
  s.guaranteed_bytes = L.guaranteed_bytes;
  s.address_space_bytes = L.address_space_bytes;
  s.logical_frames = L.logical_frames;
  s.map_lba = L.map_lba;
  s.map_sectors = L.map_sectors;
  s.pool_lba = L.pool_lba;
  s.pool_sectors = L.pool_sectors;
  s.pool_cursor = 0;
  s.logical_stored = 0;
  s.physical_used = 0;
  s.flags = 0x7;  // sealed + field1 + entropy_pack
  std::memcpy(s.label, "FIELD1\0\0\0\0\0\0\0\0\0", 16);

  const AmmoFatGeometry g = plan_ammo_geometry(L.address_space_bytes, L.part_lba);
  s.ammo_bps = g.bps;
  s.ammo_spc = g.spc;
  s.ammo_fats = g.fats;
  s.ammo_reserved = g.reserved;
  s.ammo_root_ent = g.root_ent;
  s.ammo_data_start = g.data_start;
  s.ammo_total_clusters = g.total_clusters;
  s.ammo_vol_offset_lba = g.vol_offset_lba;

  std::memcpy(s.fdrv_tag, kFdrvMagic, 5);
  s.created_unix = static_cast<uint64_t>(std::time(nullptr));
  seal_super(s);
}

void fill_result_from_super(EnsureResult& r, const FfatSuper& s) {
  r.physical_bytes = s.physical_bytes;
  r.guaranteed_bytes = s.guaranteed_bytes;
  r.address_space_bytes = s.address_space_bytes;
  r.logical_stored = s.logical_stored;
  r.physical_used = s.physical_used;
  r.logical_frames = s.logical_frames;
  r.field_bytes = s.guaranteed_bytes;  // honest alias: guaranteed, not fake ×N
  r.label = std::string(s.label, strnlen(s.label, 16));
  if (s.physical_used > 0) {
    const uint64_t ratio =
        (s.logical_stored * 1000ull) / (s.physical_used ? s.physical_used : 1ull);
    r.pack_ratio_x1000 = ratio > 0xFFFFFFFFull ? 0xFFFFFFFFu : static_cast<uint32_t>(ratio);
  } else {
    r.pack_ratio_x1000 = 1000;  // empty: no free lunch claimed
  }
  if (s.frames_measured > 0) {
    r.shannon_avg_x1000 =
        static_cast<uint32_t>(s.shannon_sum_x1000 / s.frames_measured);
  } else {
    r.shannon_avg_x1000 = 0;
  }
  // frames_pak / frames_dict live in reserved so super layout/seal stay stable
  uint64_t pak = 0, dct = 0;
  std::memcpy(&pak, s.reserved, sizeof pak);
  std::memcpy(&dct, s.reserved + 8, sizeof dct);
  r.frames_pak = pak;
  r.frames_dict = dct;
}

uint64_t super_frames_pak(const FfatSuper& s) {
  uint64_t pak = 0;
  std::memcpy(&pak, s.reserved, sizeof pak);
  return pak;
}

void super_set_frames_pak(FfatSuper& s, uint64_t pak) {
  std::memcpy(s.reserved, &pak, sizeof pak);
}

// Simple RLE: runs of (count:u16 LE, byte:u8). count>=1.
// Returns packed size or 0 if not beneficial / overflow.
size_t rle_pack(const uint8_t* src, size_t n, uint8_t* dst, size_t dst_cap) {
  size_t o = 0;
  size_t i = 0;
  while (i < n) {
    uint8_t b = src[i];
    size_t run = 1;
    while (i + run < n && src[i + run] == b && run < 65535) ++run;
    if (o + 3 > dst_cap) return 0;
    dst[o++] = static_cast<uint8_t>(run & 0xFF);
    dst[o++] = static_cast<uint8_t>((run >> 8) & 0xFF);
    dst[o++] = b;
    i += run;
  }
  // only win if meaningfully smaller (header overhead elsewhere)
  if (o >= n) return 0;
  return o;
}

bool rle_unpack(const uint8_t* src, size_t sn, uint8_t* dst, size_t dn) {
  size_t o = 0;
  size_t i = 0;
  while (i + 2 < sn && o < dn) {
    const uint16_t run =
        static_cast<uint16_t>(src[i]) | (static_cast<uint16_t>(src[i + 1]) << 8);
    const uint8_t b = src[i + 2];
    i += 3;
    if (run == 0) return false;
    if (o + run > dn) return false;
    std::memset(dst + o, b, run);
    o += run;
  }
  return o == dn;
}

// ─── Peak–Angle Pack (PAK1) ───────────────────────────────────────────────
// Model the frame as a 1-D field signal. Store control geometry, not every sample:
//   FLAT  — plateau (angle 0)
//   RAMP  — constant angle up or down (fixed step)
//   PEAK  — angle up then angle down (one apex, two slopes)
//   VALLEY— angle down then angle up
//   LIT   — escape for unstructured spans
// One PEAK/VALLEY record expands to many logical samples → more data than per point.
// Lossless: only emit structured segs when decode matches source exactly.

constexpr uint8_t kPakFlat = 0x10;
constexpr uint8_t kPakRamp = 0x11;
constexpr uint8_t kPakPeak = 0x12;
constexpr uint8_t kPakValley = 0x13;
constexpr uint8_t kPakLit = 0x1F;

void put_le16(uint8_t* p, uint16_t v) {
  p[0] = static_cast<uint8_t>(v & 0xFF);
  p[1] = static_cast<uint8_t>((v >> 8) & 0xFF);
}
uint16_t get_le16(const uint8_t* p) {
  return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
}

// Longest flat run at i.
size_t pak_flat_len(const uint8_t* s, size_t n, size_t i) {
  size_t j = i + 1;
  while (j < n && s[j] == s[i] && (j - i) < 65535) ++j;
  return j - i;
}

// Longest constant-step ramp at i. step = s[i+1]-s[i] (mod 256 as int8 path).
// Returns length (>=2) or 0 if not a ramp.
size_t pak_ramp_len(const uint8_t* s, size_t n, size_t i, int8_t& step_out) {
  if (i + 1 >= n) return 0;
  const int step = static_cast<int>(s[i + 1]) - static_cast<int>(s[i]);
  if (step < -128 || step > 127 || step == 0) return 0;
  size_t j = i + 1;
  while (j + 1 < n && (j - i) < 65534) {
    const int d = static_cast<int>(s[j + 1]) - static_cast<int>(s[j]);
    if (d != step) break;
    ++j;
  }
  const size_t len = j - i + 1;
  if (len < 3) return 0;  // short ramps not worth it vs LIT
  step_out = static_cast<int8_t>(step);
  return len;
}

// Longest PEAK (up then down) starting at i.
// up_count samples on rise including apex; dn_count samples after apex.
// Returns total samples covered, or 0.
size_t pak_peak_len(const uint8_t* s, size_t n, size_t i, int8_t& step_up, int8_t& step_dn,
                    uint16_t& up_count, uint16_t& dn_count) {
  int8_t su = 0;
  const size_t up = pak_ramp_len(s, n, i, su);
  if (up < 3 || su <= 0) return 0;  // need rising angle
  const size_t apex = i + up - 1;
  if (apex + 1 >= n) return 0;
  int8_t sd = 0;
  // Ramp from apex downward
  const size_t dn = pak_ramp_len(s, n, apex, sd);
  if (dn < 3 || sd >= 0) return 0;  // need falling angle
  // dn includes apex as start of second ramp → samples after apex = dn-1
  const size_t after = dn - 1;
  if (after < 2) return 0;
  step_up = su;
  step_dn = sd;
  up_count = static_cast<uint16_t>(up);
  dn_count = static_cast<uint16_t>(after);
  return up + after;
}

// Longest VALLEY (down then up) starting at i.
size_t pak_valley_len(const uint8_t* s, size_t n, size_t i, int8_t& step_dn, int8_t& step_up,
                      uint16_t& dn_count, uint16_t& up_count) {
  int8_t sd = 0;
  const size_t dn = pak_ramp_len(s, n, i, sd);
  if (dn < 3 || sd >= 0) return 0;
  const size_t trough = i + dn - 1;
  if (trough + 1 >= n) return 0;
  int8_t su = 0;
  const size_t up = pak_ramp_len(s, n, trough, su);
  if (up < 3 || su <= 0) return 0;
  const size_t after = up - 1;
  if (after < 2) return 0;
  step_dn = sd;
  step_up = su;
  dn_count = static_cast<uint16_t>(dn);
  up_count = static_cast<uint16_t>(after);
  return dn + after;
}

bool pak_emit(uint8_t* dst, size_t cap, size_t& o, uint8_t type, const uint8_t* body, size_t blen) {
  if (o + 1 + blen > cap) return false;
  dst[o++] = type;
  if (blen) {
    std::memcpy(dst + o, body, blen);
    o += blen;
  }
  return true;
}

// Pack entire frame to PAK1. Returns packed size or 0 if no win.
size_t pak_pack(const uint8_t* src, size_t n, uint8_t* dst, size_t dst_cap) {
  if (dst_cap < 8 || n == 0) return 0;
  // header: PAK1 + u16 nseg (filled at end)
  dst[0] = 'P';
  dst[1] = 'A';
  dst[2] = 'K';
  dst[3] = '1';
  size_t o = 6;  // skip nseg for now
  uint16_t nseg = 0;
  size_t i = 0;
  while (i < n) {
    // Prefer PEAK/VALLEY (most samples per control point), then RAMP, FLAT, LIT
    int8_t p_su = 0, p_sd = 0;
    uint16_t p_uc = 0, p_dc = 0;
    const size_t peak = pak_peak_len(src, n, i, p_su, p_sd, p_uc, p_dc);
    int8_t v_sd = 0, v_su = 0;
    uint16_t v_dc = 0, v_uc = 0;
    const size_t valley = pak_valley_len(src, n, i, v_sd, v_su, v_dc, v_uc);

    if (peak >= 8 && peak >= valley) {
      uint8_t body[7];
      body[0] = src[i];
      body[1] = static_cast<uint8_t>(p_su);
      put_le16(body + 2, p_uc);
      body[4] = static_cast<uint8_t>(p_sd);
      put_le16(body + 5, p_dc);
      if (!pak_emit(dst, dst_cap, o, kPakPeak, body, 7)) return 0;
      i += peak;
      ++nseg;
      continue;
    }
    if (valley >= 8) {
      uint8_t body[7];
      body[0] = src[i];
      body[1] = static_cast<uint8_t>(v_sd);
      put_le16(body + 2, v_dc);
      body[4] = static_cast<uint8_t>(v_su);
      put_le16(body + 5, v_uc);
      if (!pak_emit(dst, dst_cap, o, kPakValley, body, 7)) return 0;
      i += valley;
      ++nseg;
      continue;
    }

    int8_t step = 0;
    const size_t rlen = pak_ramp_len(src, n, i, step);
    const size_t flen = pak_flat_len(src, n, i);

    if (rlen >= 4 && rlen >= flen) {
      uint8_t body[4];
      body[0] = src[i];
      body[1] = static_cast<uint8_t>(step);
      put_le16(body + 2, static_cast<uint16_t>(rlen));
      if (!pak_emit(dst, dst_cap, o, kPakRamp, body, 4)) return 0;
      i += rlen;
      ++nseg;
      continue;
    }
    if (flen >= 3) {
      uint8_t body[3];
      body[0] = src[i];
      put_le16(body + 1, static_cast<uint16_t>(flen));
      if (!pak_emit(dst, dst_cap, o, kPakFlat, body, 3)) return 0;
      i += flen;
      ++nseg;
      continue;
    }

    // Literal span until next structured opportunity (cap 64)
    size_t lit = 1;
    while (i + lit < n && lit < 64) {
      int8_t tstep = 0;
      uint16_t tuc = 0, tdc = 0;
      int8_t tsu = 0, tsd = 0;
      if (pak_flat_len(src, n, i + lit) >= 3) break;
      if (pak_ramp_len(src, n, i + lit, tstep) >= 4) break;
      if (pak_peak_len(src, n, i + lit, tsu, tsd, tuc, tdc) >= 8) break;
      if (pak_valley_len(src, n, i + lit, tsd, tsu, tdc, tuc) >= 8) break;
      ++lit;
    }
    if (o + 1 + 2 + lit > dst_cap) return 0;
    dst[o++] = kPakLit;
    put_le16(dst + o, static_cast<uint16_t>(lit));
    o += 2;
    std::memcpy(dst + o, src + i, lit);
    o += lit;
    i += lit;
    ++nseg;
  }
  put_le16(dst + 4, nseg);
  // Win only if meaningfully smaller
  if (o + 16 >= n) return 0;
  return o;
}

bool pak_unpack(const uint8_t* src, size_t sn, uint8_t* dst, size_t dn) {
  if (!src || sn < 6 || std::memcmp(src, "PAK1", 4) != 0) return false;
  const uint16_t nseg = get_le16(src + 4);
  size_t i = 6;
  size_t o = 0;
  for (uint16_t s = 0; s < nseg; ++s) {
    if (i >= sn) return false;
    const uint8_t t = src[i++];
    if (t == kPakFlat) {
      if (i + 3 > sn) return false;
      const uint8_t v = src[i++];
      const uint16_t c = get_le16(src + i);
      i += 2;
      if (o + c > dn || c == 0) return false;
      std::memset(dst + o, v, c);
      o += c;
    } else if (t == kPakRamp) {
      if (i + 4 > sn) return false;
      uint8_t start = src[i++];
      const int8_t step = static_cast<int8_t>(src[i++]);
      const uint16_t c = get_le16(src + i);
      i += 2;
      if (o + c > dn || c == 0) return false;
      for (uint16_t k = 0; k < c; ++k) {
        dst[o + k] = static_cast<uint8_t>(static_cast<int>(start) + static_cast<int>(step) * static_cast<int>(k));
      }
      o += c;
    } else if (t == kPakPeak || t == kPakValley) {
      if (i + 7 > sn) return false;
      const uint8_t base = src[i++];
      const int8_t s1 = static_cast<int8_t>(src[i++]);
      const uint16_t c1 = get_le16(src + i);
      i += 2;
      const int8_t s2 = static_cast<int8_t>(src[i++]);
      const uint16_t c2 = get_le16(src + i);
      i += 2;
      if (c1 == 0 || o + c1 + c2 > dn) return false;
      // first leg including apex
      for (uint16_t k = 0; k < c1; ++k) {
        dst[o + k] = static_cast<uint8_t>(static_cast<int>(base) + static_cast<int>(s1) * static_cast<int>(k));
      }
      const uint8_t apex = dst[o + c1 - 1];
      o += c1;
      for (uint16_t k = 1; k <= c2; ++k) {
        dst[o + k - 1] =
            static_cast<uint8_t>(static_cast<int>(apex) + static_cast<int>(s2) * static_cast<int>(k));
      }
      o += c2;
      (void)t;
    } else if (t == kPakLit) {
      if (i + 2 > sn) return false;
      const uint16_t c = get_le16(src + i);
      i += 2;
      if (i + c > sn || o + c > dn || c == 0) return false;
      std::memcpy(dst + o, src + i, c);
      o += c;
      i += c;
    } else {
      return false;
    }
  }
  return o == dn;
}

bool write_field_fat(int fd, uint32_t part_lba, uint32_t total_sectors, uint64_t physical_bytes,
                     uint32_t map_divisor = 32) {
  if (total_sectors <= part_lba + 64) return false;
  const uint32_t part_sectors = total_sectors - part_lba;
  const Layout L = plan_layout(part_lba, part_sectors, map_divisor);
  if (L.pool_sectors < 8 || L.logical_frames < 16) return false;

  FfatSuper super{};
  build_super(super, physical_bytes, L);

  uint8_t sec[kSector];
  std::memset(sec, 0, sizeof sec);
  std::memcpy(sec, &super, sizeof super);
  if (!write_all(fd, static_cast<uint64_t>(part_lba) * kSector, sec, kSector)) return false;

  // Banner — honesty in plain text
  std::memset(sec, 0, sizeof sec);
  const char* banner =
      "SPEAR FIELD FAT v4 ENTROPY+PAK\n"
      "Not Microsoft FAT. Not fake xN capacity.\n"
      "ZERO/RLE/PAK/RAW/REF. PAK=peaks+angle up/down.\n"
      "Memory planes: denser sparse map = more field memory.\n"
      "guaranteed=pool worst case. pack_ratio measured.\n"
      "spear ffat-probe | fieldmem | storage-status\n";
  std::memcpy(sec, banner, std::strlen(banner));
  if (!write_all(fd, static_cast<uint64_t>(part_lba + 1) * kSector, sec, kSector)) return false;

  // Zero the map (all FREE). For large maps, write in 1 MiB chunks.
  {
    const uint64_t map_bytes = static_cast<uint64_t>(L.map_sectors) * kSector;
    const uint64_t map_off = static_cast<uint64_t>(L.map_lba) * kSector;
    const size_t chunk = 1024 * 1024;
    std::vector<uint8_t> z(chunk, 0);
    uint64_t done = 0;
    while (done < map_bytes) {
      size_t n = chunk;
      if (done + n > map_bytes) n = static_cast<size_t>(map_bytes - done);
      if (!write_all(fd, map_off + done, z.data(), n)) return false;
      done += n;
    }
  }

  // Touch first pool sector so layout is visible
  std::memset(sec, 0, sizeof sec);
  std::memcpy(sec, "FPK0", 4);  // pool region marker (empty)
  if (!write_all(fd, static_cast<uint64_t>(L.pool_lba) * kSector, sec, kSector)) return false;

  return true;
}

bool load_super(int fd, uint32_t part_lba, FfatSuper& s) {
  uint8_t sec[kSector]{};
  if (!read_all(fd, static_cast<uint64_t>(part_lba) * kSector, sec, kSector)) return false;
  if (!is_ffat_v4(sec)) return false;
  std::memcpy(&s, sec, sizeof s);
  return s.sector_bytes == kSector && s.frame_bytes == kFrameBytes;
}

bool save_super(int fd, const FfatSuper& s) {
  uint8_t sec[kSector]{};
  FfatSuper t = s;
  seal_super(t);
  std::memcpy(sec, &t, sizeof t);
  return write_all(fd, static_cast<uint64_t>(t.part_lba) * kSector, sec, kSector);
}

bool read_map_entry(int fd, const FfatSuper& s, uint32_t id, MapEntry& e) {
  if (id >= s.logical_frames) return false;
  const uint64_t off =
      static_cast<uint64_t>(s.map_lba) * kSector + static_cast<uint64_t>(id) * sizeof(MapEntry);
  return read_all(fd, off, &e, sizeof e);
}

bool write_map_entry(int fd, const FfatSuper& s, uint32_t id, const MapEntry& e) {
  if (id >= s.logical_frames) return false;
  const uint64_t off =
      static_cast<uint64_t>(s.map_lba) * kSector + static_cast<uint64_t>(id) * sizeof(MapEntry);
  return write_all(fd, off, &e, sizeof e);
}

// Linear scan for content_fp match (dedup). OK for moderate use; not a B-tree.
bool find_dup(int fd, const FfatSuper& s, uint64_t fp, uint32_t self_id, uint32_t& found_id) {
  // Cap scan cost: sample first 64k used entries max via sequential read of map chunks
  const uint32_t max_scan = s.logical_frames < 65536u ? s.logical_frames : 65536u;
  std::vector<MapEntry> buf(4096 / sizeof(MapEntry));
  uint32_t id = 0;
  while (id < max_scan) {
    const uint32_t batch = std::min(static_cast<uint32_t>(buf.size()), max_scan - id);
    const uint64_t off =
        static_cast<uint64_t>(s.map_lba) * kSector + static_cast<uint64_t>(id) * sizeof(MapEntry);
    if (!read_all(fd, off, buf.data(), batch * sizeof(MapEntry))) return false;
    for (uint32_t i = 0; i < batch; ++i) {
      const MapEntry& e = buf[i];
      if (id + i == self_id) continue;
      if ((e.kind == kMapRaw || e.kind == kMapRle || e.kind == kMapZero || e.kind == kMapDict ||
           e.kind == kMapPak) &&
          e.content_fp == fp) {
        found_id = id + i;
        return true;
      }
    }
    id += batch;
  }
  return false;
}

}  // namespace

uint32_t ffat_shannon_x1000(const uint8_t* data, size_t n) {
  if (!data || n == 0) return 0;
  uint64_t hist[256]{};
  for (size_t i = 0; i < n; ++i) ++hist[data[i]];
  double H = 0.0;
  const double inv = 1.0 / static_cast<double>(n);
  for (int b = 0; b < 256; ++b) {
    if (!hist[b]) continue;
    const double p = static_cast<double>(hist[b]) * inv;
    H -= p * (std::log(p) / std::log(2.0));
  }
  if (H < 0) H = 0;
  if (H > 8.0) H = 8.0;
  return static_cast<uint32_t>(H * 1000.0 + 0.5);
}

size_t ffat_pack_frame(const uint8_t* frame, size_t frame_n, uint8_t* dest, size_t dest_cap,
                       uint8_t& kind_out, uint32_t& shannon_x1000_out) {
  kind_out = kMapRaw;
  shannon_x1000_out = 0;
  if (!frame || !dest || frame_n == 0) return 0;

  // Field Die / CHIPs score — entropy fold + peak density + wave (not host-only H)
  const ChipFrameScore score = chip_score_frame(frame, frame_n);
  shannon_x1000_out = score.shannon_x1000;

  if (score.pick == ChipPackPick::Zero) {
    kind_out = kMapZero;
    return 0;
  }

  // Prefer chip pick order; still verify each codec actually shrinks (honest).
  auto try_pak = [&]() -> size_t {
    if (dest_cap < 16) return 0;
    const size_t p = pak_pack(frame, frame_n, dest, dest_cap);
    if (p > 0 && p + 24 < frame_n) return p;
    return 0;
  };
  auto try_rle = [&]() -> size_t {
    if (dest_cap < frame_n) return 0;
    const size_t r = rle_pack(frame, frame_n, dest, dest_cap);
    if (r > 0 && r + 32 < frame_n) return r;
    return 0;
  };
  auto try_dict = [&]() -> size_t {
    // Shared H7B-kin dict — both memory planes; sealed bind on unpack
    if (dest_cap < 32) return 0;
    return patdict_pack_frame(frame, frame_n, dest, dest_cap);
  };

  // Dictionary first when loaded: shared patterns across host+GPU memory
  if (const size_t d = try_dict()) {
    kind_out = kMapDict;
    return d;
  }

  if (score.pick == ChipPackPick::Pak) {
    if (const size_t p = try_pak()) {
      kind_out = kMapPak;
      return p;
    }
    if (const size_t r = try_rle()) {
      kind_out = kMapRle;
      return r;
    }
  } else if (score.pick == ChipPackPick::Rle) {
    if (const size_t r = try_rle()) {
      kind_out = kMapRle;
      return r;
    }
    if (const size_t p = try_pak()) {
      kind_out = kMapPak;
      return p;
    }
  } else {
    if (score.peak_density >= 0.2f) {
      if (const size_t p = try_pak()) {
        kind_out = kMapPak;
        return p;
      }
    }
    if (score.flat_density >= 0.2f) {
      if (const size_t r = try_rle()) {
        kind_out = kMapRle;
        return r;
      }
    }
  }

  if (dest_cap < frame_n) return 0;
  std::memcpy(dest, frame, frame_n);
  kind_out = kMapRaw;
  return frame_n;
}

bool ffat_unpack_frame(uint8_t kind, const uint8_t* packed, size_t packed_n, uint8_t* frame,
                       size_t frame_n) {
  if (!frame || frame_n == 0) return false;
  if (kind == kMapZero) {
    std::memset(frame, 0, frame_n);
    return true;
  }
  if (kind == kMapRaw) {
    if (!packed || packed_n < frame_n) return false;
    std::memcpy(frame, packed, frame_n);
    return true;
  }
  if (kind == kMapRle) {
    if (!packed) return false;
    return rle_unpack(packed, packed_n, frame, frame_n);
  }
  if (kind == kMapPak) {
    if (!packed) return false;
    return pak_unpack(packed, packed_n, frame, frame_n);
  }
  if (kind == kMapDict) {
    if (!packed) return false;
    // Ensure sealed dict is loaded (shared host+GPU path)
    if (!patdict_global().loaded) {
      if (!patdict_load()) return false;
    }
    return patdict_unpack_frame(packed, packed_n, frame, frame_n);
  }
  return false;
}

EnsureResult ffat_probe(const std::string& path) {
  EnsureResult r;
  const int fd = ::open(path.c_str(), O_RDONLY);
  if (fd < 0) {
    r.error = std::strerror(errno);
    return r;
  }
  r.physical_bytes = file_size(fd);
  r.guaranteed_bytes = 0;
  r.field_bytes = 0;
  uint8_t s0[kSector]{}, s1[kSector]{};
  read_all(fd, 0, s0, kSector);
  r.had_mbr = has_mbr(s0);
  uint32_t part = r.had_mbr ? part_lba_of(s0) : kPartLba;
  if (part == 0 || part == 1) part = kPartLba;
  r.part_lba = part;
  if (r.physical_bytes >= (static_cast<uint64_t>(part) + 1) * kSector) {
    read_all(fd, static_cast<uint64_t>(part) * kSector, s1, kSector);
    if (is_ffat_v4(s1)) {
      r.had_ffat = true;
      const auto* s = reinterpret_cast<const FfatSuper*>(s1);
      fill_result_from_super(r, *s);
      if (!check_seal(*s)) r.error = "seal_mismatch";
    } else if (is_ffat_legacy(s1)) {
      r.had_ffat = false;
      r.legacy_fake_factor = true;
      r.error = "legacy_fake_x91_superblock";
      // still surface physical
      const auto* old = reinterpret_cast<const uint8_t*>(s1);
      // best-effort read of physical_bytes at known old offset if present
      (void)old;
      r.guaranteed_bytes = r.physical_bytes > static_cast<uint64_t>(part) * kSector
                               ? r.physical_bytes - static_cast<uint64_t>(part) * kSector
                               : 0;
      r.field_bytes = r.guaranteed_bytes;
    } else if (s1[0] == 0xEB &&
               (std::memcmp(s1 + 54, "FAT1", 4) == 0 || std::memcmp(s1 + 82, "FAT3", 4) == 0)) {
      r.error = "legacy_msfat_layout";
    } else if (std::memcmp(s1 + 3, "AMMOFAT", 7) == 0) {
      r.error = "legacy_msfat_layout";
    }
  }
  r.ok = true;
  ::close(fd);
  return r;
}

EnsureResult ffat_ensure(const std::string& path, bool force, uint32_t map_divisor) {
  EnsureResult r;
  const int fd = ::open(path.c_str(), O_RDWR);
  if (fd < 0) {
    r.error = std::strerror(errno);
    return r;
  }
  r.physical_bytes = file_size(fd);
  // large memory images may exceed 32-bit sector count — cap carefully
  const uint64_t total64 = r.physical_bytes / kSector;
  if (total64 < kPartLba + 64) {
    r.error = "too_small";
    ::close(fd);
    return r;
  }
  if (total64 > 0xFFFFFFFFull) {
    r.error = "too_large_for_u32_sectors";
    ::close(fd);
    return r;
  }
  const uint32_t total = static_cast<uint32_t>(total64);

  uint8_t s0[kSector]{};
  read_all(fd, 0, s0, kSector);
  r.had_mbr = has_mbr(s0);
  uint32_t part = r.had_mbr ? part_lba_of(s0) : kPartLba;
  bool broken = false;
  if (r.had_mbr && part_lba_of(s0) == 1) broken = true;
  if (r.had_mbr) {
    const uint8_t t = s0[0x1BE + 4];
    if (t == 0x0E || t == 0x0C) broken = true;
  }
  uint8_t s1[kSector]{};
  const uint32_t try_part = (part >= 64) ? part : kPartLba;
  read_all(fd, static_cast<uint64_t>(try_part) * kSector, s1, kSector);
  r.had_ffat = is_ffat_v4(s1) && !broken;
  if (is_ffat_legacy(s1)) {
    r.legacy_fake_factor = true;
    r.had_ffat = false;
    broken = true;  // rewrite away fake ×91
  }
  if (s1[0] == 0xEB &&
      (std::memcmp(s1 + 54, "FAT1", 4) == 0 || std::memcmp(s1 + 82, "FAT3", 4) == 0)) {
    r.had_ffat = false;
    broken = true;
  }

  if (force || !r.had_mbr || broken) {
    uint8_t mbr[kSector];
    build_mbr(mbr, total, kPartLba);
    if (!write_all(fd, 0, mbr, kSector)) {
      r.error = "mbr_write_failed";
      ::close(fd);
      return r;
    }
    r.wrote_mbr = true;
    part = kPartLba;
  } else {
    part = part_lba_of(s0);
    if (part < 64) part = kPartLba;
  }
  r.part_lba = part;

  if (force || !r.had_ffat || broken) {
    if (!write_field_fat(fd, part, total, r.physical_bytes, map_divisor)) {
      r.error = "ffat_write_failed";
      ::close(fd);
      return r;
    }
    r.wrote_ffat = true;
  }

  ::fsync(fd);
  read_all(fd, 0, s0, kSector);
  part = part_lba_of(s0);
  read_all(fd, static_cast<uint64_t>(part) * kSector, s1, kSector);
  r.had_mbr = has_mbr(s0);
  r.had_ffat = is_ffat_v4(s1);
  if (r.had_ffat) {
    const auto* s = reinterpret_cast<const FfatSuper*>(s1);
    fill_result_from_super(r, *s);
  }
  r.ok = r.had_mbr && r.had_ffat;
  r.part_lba = part;
  ::close(fd);
  return r;
}

bool ffat_ammo_geometry(const std::string& path, AmmoFatGeometry& out) {
  auto r = ffat_probe(path);
  if (!r.had_ffat) return false;
  out = plan_ammo_geometry(r.address_space_bytes ? r.address_space_bytes : r.guaranteed_bytes,
                           r.part_lba);
  return true;
}

bool ffat_put_frame(const std::string& path, uint32_t logical_id, const uint8_t* frame,
                    size_t frame_n, std::string& err) {
  err.clear();
  if (!frame || frame_n != kFrameBytes) {
    err = "frame_must_be_4096";
    return false;
  }
  const int fd = ::open(path.c_str(), O_RDWR);
  if (fd < 0) {
    err = std::strerror(errno);
    return false;
  }
  uint8_t s0[kSector]{};
  if (!read_all(fd, 0, s0, kSector) || !has_mbr(s0)) {
    err = "no_mbr";
    ::close(fd);
    return false;
  }
  const uint32_t part = part_lba_of(s0);
  FfatSuper s{};
  if (!load_super(fd, part, s)) {
    err = "no_ffat_v4";
    ::close(fd);
    return false;
  }
  if (logical_id >= s.logical_frames) {
    err = "logical_id_oob";
    ::close(fd);
    return false;
  }

  uint8_t kind = kMapRaw;
  uint32_t H = 0;
  std::vector<uint8_t> packed(kFrameBytes + 8);
  const size_t plen = ffat_pack_frame(frame, frame_n, packed.data(), packed.size(), kind, H);
  const uint64_t fp = fnv1a64(frame, frame_n);

  // Die L1 cache — speed path for subsequent gets
  chip_cache_put(logical_id, frame, frame_n, fp);

  // Content-addressed dedup
  uint32_t dup_id = 0;
  if (kind != kMapZero && find_dup(fd, s, fp, logical_id, dup_id)) {
    MapEntry e{};
    e.kind = kMapRef;
    e.shannon_x32 = static_cast<uint8_t>(H / 32 > 255 ? 255 : H / 32);
    e.packed_len = 0;
    e.ref_id = dup_id;
    e.pool_off = 0;
    e.content_fp = fp;
    if (!write_map_entry(fd, s, logical_id, e)) {
      err = "map_write";
      ::close(fd);
      return false;
    }
    s.frames_ref++;
    s.logical_stored += kFrameBytes;
    // physical_used unchanged
    s.shannon_sum_x1000 += H;
    s.frames_measured++;
    if (!save_super(fd, s)) {
      err = "super_write";
      ::close(fd);
      return false;
    }
    ::fsync(fd);
    ::close(fd);
    return true;
  }

  MapEntry e{};
  e.kind = kind;
  e.shannon_x32 = static_cast<uint8_t>(H / 32 > 255 ? 255 : H / 32);
  e.content_fp = fp;
  e.packed_len = static_cast<uint16_t>(plen);
  e.ref_id = 0;

  if (kind == kMapZero) {
    e.pool_off = 0;
    e.packed_len = 0;
    s.frames_zero++;
  } else {
    const uint64_t pool_bytes = static_cast<uint64_t>(s.pool_sectors) * kSector;
    const uint64_t need = sizeof(PoolObj) + plen;
    // 16-byte align
    const uint64_t aligned = (need + 15ull) & ~15ull;
    if (s.pool_cursor + aligned > pool_bytes) {
      err = "pool_full";
      ::close(fd);
      return false;
    }
    PoolObj obj{};
    std::memcpy(obj.magic, "FPK1", 4);
    obj.kind = kind;
    obj.shannon_x32 = e.shannon_x32;
    obj.packed_len = e.packed_len;
    obj.logical_id = logical_id;
    obj.content_fp = fp;
    const uint64_t pool_base = static_cast<uint64_t>(s.pool_lba) * kSector;
    if (!write_all(fd, pool_base + s.pool_cursor, &obj, sizeof obj)) {
      err = "pool_hdr_write";
      ::close(fd);
      return false;
    }
    if (plen && !write_all(fd, pool_base + s.pool_cursor + sizeof(PoolObj), packed.data(), plen)) {
      err = "pool_pay_write";
      ::close(fd);
      return false;
    }
    e.pool_off = s.pool_cursor;
    s.pool_cursor += aligned;
    s.physical_used += aligned;
    if (kind == kMapRle) {
      s.frames_rle++;
    } else if (kind == kMapPak) {
      super_set_frames_pak(s, super_frames_pak(s) + 1);
    } else if (kind == kMapDict) {
      uint64_t dc = 0;
      std::memcpy(&dc, s.reserved + 8, 8);
      ++dc;
      std::memcpy(s.reserved + 8, &dc, 8);
    } else {
      s.frames_raw++;
    }
  }

  if (!write_map_entry(fd, s, logical_id, e)) {
    err = "map_write";
    ::close(fd);
    return false;
  }
  s.logical_stored += kFrameBytes;
  s.shannon_sum_x1000 += H;
  s.frames_measured++;
  if (!save_super(fd, s)) {
    err = "super_write";
    ::close(fd);
    return false;
  }
  ::fsync(fd);
  ::close(fd);
  return true;
}

bool ffat_get_frame(const std::string& path, uint32_t logical_id, uint8_t* frame, size_t frame_n,
                    std::string& err) {
  err.clear();
  if (!frame || frame_n != kFrameBytes) {
    err = "frame_must_be_4096";
    return false;
  }
  // CHIPs die L1 — skip disk when hot
  if (chip_cache_get(logical_id, frame, frame_n)) {
    return true;
  }
  const int fd = ::open(path.c_str(), O_RDONLY);
  if (fd < 0) {
    err = std::strerror(errno);
    return false;
  }
  uint8_t s0[kSector]{};
  if (!read_all(fd, 0, s0, kSector) || !has_mbr(s0)) {
    err = "no_mbr";
    ::close(fd);
    return false;
  }
  FfatSuper s{};
  if (!load_super(fd, part_lba_of(s0), s)) {
    err = "no_ffat_v4";
    ::close(fd);
    return false;
  }
  if (logical_id >= s.logical_frames) {
    err = "logical_id_oob";
    ::close(fd);
    return false;
  }
  MapEntry e{};
  if (!read_map_entry(fd, s, logical_id, e)) {
    err = "map_read";
    ::close(fd);
    return false;
  }
  if (e.kind == kMapFree) {
    std::memset(frame, 0, frame_n);
    ::close(fd);
    return true;  // unwritten = zero (sparse)
  }
  if (e.kind == kMapZero) {
    std::memset(frame, 0, frame_n);
    ::close(fd);
    return true;
  }
  if (e.kind == kMapRef) {
    // one level of indirection
    MapEntry ref{};
    if (!read_map_entry(fd, s, e.ref_id, ref)) {
      err = "ref_read";
      ::close(fd);
      return false;
    }
    e = ref;
    if (e.kind == kMapRef || e.kind == kMapFree) {
      err = "bad_ref";
      ::close(fd);
      return false;
    }
    if (e.kind == kMapZero) {
      std::memset(frame, 0, frame_n);
      ::close(fd);
      return true;
    }
  }
  const uint64_t pool_base = static_cast<uint64_t>(s.pool_lba) * kSector;
  PoolObj obj{};
  if (!read_all(fd, pool_base + e.pool_off, &obj, sizeof obj)) {
    err = "pool_hdr";
    ::close(fd);
    return false;
  }
  if (std::memcmp(obj.magic, "FPK1", 4) != 0) {
    err = "bad_pool_magic";
    ::close(fd);
    return false;
  }
  std::vector<uint8_t> packed(obj.packed_len);
  if (obj.packed_len &&
      !read_all(fd, pool_base + e.pool_off + sizeof(PoolObj), packed.data(), obj.packed_len)) {
    err = "pool_pay";
    ::close(fd);
    return false;
  }
  if (!ffat_unpack_frame(obj.kind, packed.data(), obj.packed_len, frame, frame_n)) {
    err = "unpack_fail";
    ::close(fd);
    return false;
  }
  chip_cache_put(logical_id, frame, frame_n, obj.content_fp);
  ::close(fd);
  return true;
}

}  // namespace spear

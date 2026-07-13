// SPDX-License-Identifier: MIT
// Spear Field Die / CHIPs plane — AMOURANTHRTX-class FieldX86 ops for FFAT.
//
// Honesty: this is a software Field Die (die slots + belt dispatch), not a claim
// that host silicon is powered off. Field work (pack, entropy, peaks, cache)
// routes through the chip plane instead of ad-hoc host paths — same doctrine as
// AMOURANTHRTX FieldX86 + entropy fold + wave phase (Grok16 field_opt / belt).
//
// Perfect-x86 here means: complete Field Die instruction set for field storage
// ops (EntropyFold, WavePhase, Shannon, PeakScan, PackPick, Fnv), hot-path
// optimized — not a full ring-0 guest emulator (that is FieldX86Core/libx86emu).
#pragma once
#include <cstdint>
#include <cstddef>
#include <string>

namespace spear {

constexpr std::size_t kChipDieSlots = 256;
constexpr std::size_t kChipWaveBands = 16;
constexpr std::size_t kChipBeltChunk = 32;
constexpr std::size_t kChipFrameCache = 64;  // hot logical frames
constexpr float kChipPhi = 0.6180339887f;

enum class ChipOp : std::uint8_t {
  Nop = 0,
  Add,
  Mul,
  Xor,
  EntropyFold,  // phi blend — Primer / Grok16
  WavePhase,    // multi-band phase decouple
  Shannon,      // byte histogram H → die slot (scaled)
  PeakScan,     // structural peak/angle density score
  PackPick,     // choose ZERO/RLE/PAK/RAW from die scores
  FnvMix,       // content fingerprint mix on die
};

struct ChipInsn {
  ChipOp op = ChipOp::Nop;
  std::uint8_t dst = 0;
  std::uint8_t src = 0;
  std::uint8_t imm = 0;
};

// Pack strategy selected by chip (matches FFAT map kinds for shared path)
enum class ChipPackPick : std::uint8_t {
  Zero = 1,
  Raw = 2,
  Rle = 3,
  Pak = 5,  // peak–angle
};

struct ChipFrameScore {
  float shannon_h = 0.f;       // 0..8
  float peak_density = 0.f;    // 0..1 structural
  float flat_density = 0.f;    // 0..1
  float wave_energy = 0.f;     // multi-band
  float fold = 0.f;            // entropy_fold result
  ChipPackPick pick = ChipPackPick::Raw;
  std::uint32_t shannon_x1000 = 0;
};

struct ChipStatus {
  bool online = true;
  const char* path = "FieldDie/CHIPs";  // not HostCPU casual
  std::uint64_t ops_retired = 0;
  std::uint64_t frames_scored = 0;
  std::uint64_t cache_hits = 0;
  std::uint64_t cache_misses = 0;
  std::uint64_t pack_via_chip = 0;
  float last_fold = 0.f;
  float last_wave = 0.f;
};

// Die lifecycle
void chip_reset();
ChipStatus chip_status();
std::string chip_status_text();

// FieldX86 belt run (AMOURANTHRTX-style)
void chip_run(const ChipInsn* prog, std::size_t n);

// Score a frame on the die — entropy + peaks + wave → pack pick
ChipFrameScore chip_score_frame(const std::uint8_t* frame, std::size_t n);

// Hot frame cache (die L1) — speed for FFAT get/put
bool chip_cache_get(std::uint32_t logical_id, std::uint8_t* frame, std::size_t n);
void chip_cache_put(std::uint32_t logical_id, const std::uint8_t* frame, std::size_t n,
                    std::uint64_t content_fp);

// Microbench: retire N FieldX86 programs; returns ops/sec estimate
double chip_bench_ops_per_sec(int epochs = 64);

}  // namespace spear

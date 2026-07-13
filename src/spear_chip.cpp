// SPDX-License-Identifier: MIT
// Field Die CHIPs plane — FieldX86 + entropy fold + wave phase for FFAT.
#include "spear_chip.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

namespace spear {
namespace {

alignas(64) float g_die[kChipDieSlots]{};
std::uint64_t g_ops = 0;
std::uint64_t g_frames = 0;
std::uint64_t g_hits = 0;
std::uint64_t g_miss = 0;
std::uint64_t g_pack = 0;
float g_last_fold = 0.f;
float g_last_wave = 0.f;

struct CacheSlot {
  bool valid = false;
  std::uint32_t logical_id = 0;
  std::uint64_t content_fp = 0;
  std::uint8_t data[4096]{};
};
CacheSlot g_cache[kChipFrameCache]{};
std::uint32_t g_cache_clock = 0;

[[gnu::hot]] float entropy_fold(float e, float thermo) noexcept {
  if (!std::isfinite(e)) e = 0.f;
  if (!std::isfinite(thermo)) thermo = 0.f;
  float x = e * kChipPhi + thermo * (1.f - kChipPhi);
  for (int i = 0; i < 4; ++i) {
    x = std::fma(x, 1.113f, std::sin(x * 3.14159265f) * 0.01f);
  }
  if (!std::isfinite(x)) x = 0.f;
  return x;
}

[[gnu::hot]] float wave_phase_decouple(float phase, float speed, int band) noexcept {
  const float w = speed * static_cast<float>(band + 1) * 0.001f;
  return std::fma(std::cos(phase * w), kChipPhi, std::sin(phase * (1.f - kChipPhi)) * 0.05f);
}

[[gnu::hot]] float shannon_h(const std::uint8_t* data, std::size_t n) noexcept {
  if (!data || n == 0) return 0.f;
  std::uint32_t hist[256]{};
  for (std::size_t i = 0; i < n; ++i) ++hist[data[i]];
  double H = 0.0;
  const double inv = 1.0 / static_cast<double>(n);
  for (int b = 0; b < 256; ++b) {
    if (!hist[b]) continue;
    const double p = static_cast<double>(hist[b]) * inv;
    H -= p * (std::log(p) / std::log(2.0));
  }
  if (H < 0) H = 0;
  if (H > 8.0) H = 8.0;
  return static_cast<float>(H);
}

// Peak/angle structural density: fraction of samples on constant-step ramps or
// at local extrema (AMOURANTHRTX field-signal habit).
[[gnu::hot]] void peak_flat_density(const std::uint8_t* s, std::size_t n, float& peak_out,
                                    float& flat_out) noexcept {
  if (!s || n < 3) {
    peak_out = 0.f;
    flat_out = 0.f;
    return;
  }
  std::size_t flat = 0, ramp = 0, extrema = 0;
  for (std::size_t i = 0; i + 1 < n;) {
    if (s[i + 1] == s[i]) {
      std::size_t j = i + 1;
      while (j + 1 < n && s[j + 1] == s[i]) ++j;
      const std::size_t len = j - i + 1;
      if (len >= 3) flat += len;
      i = j;
      continue;
    }
    const int step = static_cast<int>(s[i + 1]) - static_cast<int>(s[i]);
    std::size_t j = i + 1;
    while (j + 1 < n) {
      const int d = static_cast<int>(s[j + 1]) - static_cast<int>(s[j]);
      if (d != step) break;
      ++j;
    }
    const std::size_t len = j - i + 1;
    if (len >= 3) ramp += len;
    // local extremum at end of ramp if direction flips next
    if (j + 1 < n) {
      const int d2 = static_cast<int>(s[j + 1]) - static_cast<int>(s[j]);
      if ((step > 0 && d2 < 0) || (step < 0 && d2 > 0)) ++extrema;
    }
    i = j;
  }
  const float inv = 1.f / static_cast<float>(n);
  flat_out = std::min(1.f, static_cast<float>(flat) * inv);
  peak_out = std::min(1.f, static_cast<float>(ramp) * inv + static_cast<float>(extrema) * 0.01f);
}

// Multi-band wave energy from sample stream (subsample → die wave bands).
[[gnu::hot]] float wave_energy(const std::uint8_t* s, std::size_t n) noexcept {
  if (!s || n < kChipWaveBands) return 0.f;
  float e = 0.f;
  for (std::size_t b = 0; b < kChipWaveBands; ++b) {
    const std::size_t idx = (b * n) / kChipWaveBands;
    const float phase = static_cast<float>(s[idx]) * (1.f / 255.f);
    const float speed = static_cast<float>((s[(idx + 17) % n])) * (1.f / 255.f);
    const float w = wave_phase_decouple(phase, speed, static_cast<int>(b));
    e += w * w;
  }
  return e / static_cast<float>(kChipWaveBands);
}

[[gnu::hot]] void exec_one(const ChipInsn& in) noexcept {
  const std::size_t dst = in.dst % kChipDieSlots;
  const std::size_t src = in.src % kChipDieSlots;
  const std::size_t imm = in.imm % kChipDieSlots;
  switch (in.op) {
    case ChipOp::Add:
      g_die[dst] += g_die[src];
      break;
    case ChipOp::Mul:
      g_die[dst] *= g_die[imm] * 0.01f + 1.f;
      break;
    case ChipOp::Xor:
      g_die[dst] += static_cast<float>(in.imm ^ in.src) * 1e-4f;
      break;
    case ChipOp::EntropyFold:
      g_die[dst] = entropy_fold(g_die[src], g_die[imm]);
      g_last_fold = g_die[dst];
      break;
    case ChipOp::WavePhase:
      g_die[dst] = wave_phase_decouple(g_die[src], g_die[imm], static_cast<int>(in.imm % kChipWaveBands));
      g_last_wave = g_die[dst];
      break;
    case ChipOp::Shannon:
      // imm holds pre-scaled H in die[imm] already set by host injection
      g_die[dst] = g_die[src];
      break;
    case ChipOp::PeakScan:
      g_die[dst] = g_die[src] * kChipPhi + g_die[imm] * (1.f - kChipPhi);
      break;
    case ChipOp::PackPick:
      // leave decision in die[dst] as code float (1,2,3,5)
      g_die[dst] = g_die[src];
      break;
    case ChipOp::FnvMix:
      g_die[dst] = g_die[src] + g_die[imm] * kChipPhi;
      break;
    case ChipOp::Nop:
    default:
      break;
  }
  ++g_ops;
}

}  // namespace

void chip_reset() {
  std::memset(g_die, 0, sizeof g_die);
  g_ops = g_frames = g_hits = g_miss = g_pack = 0;
  g_last_fold = g_last_wave = 0.f;
  for (auto& c : g_cache) c.valid = false;
  g_cache_clock = 0;
  // seed die with phi lattice (Field Die habit)
  for (std::size_t i = 0; i < kChipDieSlots; ++i) {
    g_die[i] = std::sin(static_cast<float>(i) * 0.07f) * 0.5f + 0.25f;
  }
}

ChipStatus chip_status() {
  ChipStatus s;
  s.online = true;
  s.path = "FieldDie/CHIPs";
  s.ops_retired = g_ops;
  s.frames_scored = g_frames;
  s.cache_hits = g_hits;
  s.cache_misses = g_miss;
  s.pack_via_chip = g_pack;
  s.last_fold = g_last_fold;
  s.last_wave = g_last_wave;
  return s;
}

std::string chip_status_text() {
  const auto s = chip_status();
  char buf[512];
  std::snprintf(buf, sizeof buf,
                "Spear CHIPs / Field Die\n"
                "  path:           %s  (not HostCPU casual path)\n"
                "  die_slots:      %zu\n"
                "  wave_bands:     %zu\n"
                "  belt_chunk:     %zu\n"
                "  frame_cache:    %zu\n"
                "  ops_retired:    %llu\n"
                "  frames_scored:  %llu\n"
                "  pack_via_chip:  %llu\n"
                "  cache_hits:     %llu\n"
                "  cache_misses:   %llu\n"
                "  last_fold:      %.6f\n"
                "  last_wave:      %.6f\n"
                "  doctrine:       FieldX86 EntropyFold+WavePhase · AMOURANTHRTX-class\n"
                "  perfect_x86:    Field Die ISA for storage ops (not full ring0 emu)\n",
                s.path, kChipDieSlots, kChipWaveBands, kChipBeltChunk, kChipFrameCache,
                (unsigned long long)s.ops_retired, (unsigned long long)s.frames_scored,
                (unsigned long long)s.pack_via_chip, (unsigned long long)s.cache_hits,
                (unsigned long long)s.cache_misses, s.last_fold, s.last_wave);
  return buf;
}

void chip_run(const ChipInsn* prog, std::size_t n) {
  if (!prog || n == 0) return;
  const std::size_t chunks = (n + kChipBeltChunk - 1) / kChipBeltChunk;
  for (std::size_t ci = 0; ci < chunks; ++ci) {
    const std::size_t base = ci * kChipBeltChunk;
    const std::size_t end = std::min(base + kChipBeltChunk, n);
    for (std::size_t pi = base; pi < end; ++pi) exec_one(prog[pi]);
  }
}

ChipFrameScore chip_score_frame(const std::uint8_t* frame, std::size_t n) {
  ChipFrameScore sc;
  if (!frame || n == 0) return sc;

  sc.shannon_h = shannon_h(frame, n);
  sc.shannon_x1000 = static_cast<std::uint32_t>(sc.shannon_h * 1000.f + 0.5f);
  peak_flat_density(frame, n, sc.peak_density, sc.flat_density);
  sc.wave_energy = wave_energy(frame, n);

  // Inject measurements onto die, run FieldX86 program (entropy fold + pack pick)
  g_die[0] = sc.shannon_h * 0.125f;  // normalize ~0..1
  g_die[1] = sc.peak_density;
  g_die[2] = sc.flat_density;
  g_die[3] = sc.wave_energy;
  g_die[4] = 0.5f;  // thermo proxy

  ChipInsn prog[] = {
      {ChipOp::EntropyFold, 10, 0, 4},  // fold shannon with thermo
      {ChipOp::PeakScan, 11, 1, 2},     // blend peak + flat
      {ChipOp::WavePhase, 12, 3, 4},    // wave band
      {ChipOp::EntropyFold, 13, 10, 11},
      {ChipOp::EntropyFold, 14, 13, 12},
      {ChipOp::FnvMix, 15, 14, 1},
  };
  chip_run(prog, sizeof prog / sizeof prog[0]);
  sc.fold = g_die[14];
  g_last_fold = sc.fold;

  // All-zero?
  bool all_zero = true;
  for (std::size_t i = 0; i < n; ++i) {
    if (frame[i] != 0) {
      all_zero = false;
      break;
    }
  }
  if (all_zero) {
    sc.pick = ChipPackPick::Zero;
  } else if (sc.peak_density >= 0.45f || (sc.peak_density >= 0.25f && sc.shannon_h > 4.f)) {
    // Structured signal even if Shannon is high (triangle waves) → PAK
    sc.pick = ChipPackPick::Pak;
  } else if (sc.flat_density >= 0.35f && sc.shannon_h < 4.5f) {
    sc.pick = ChipPackPick::Rle;
  } else if (sc.shannon_h < 3.0f && sc.flat_density >= 0.15f) {
    sc.pick = ChipPackPick::Rle;
  } else {
    sc.pick = ChipPackPick::Raw;
  }

  g_die[20] = static_cast<float>(static_cast<int>(sc.pick));
  ChipInsn pick_insn{ChipOp::PackPick, 21, 20, 0};
  chip_run(&pick_insn, 1);

  ++g_frames;
  ++g_pack;
  return sc;
}

bool chip_cache_get(std::uint32_t logical_id, std::uint8_t* frame, std::size_t n) {
  if (!frame || n == 0 || n > 4096) return false;
  for (auto& c : g_cache) {
    if (c.valid && c.logical_id == logical_id) {
      std::memcpy(frame, c.data, n);
      ++g_hits;
      return true;
    }
  }
  ++g_miss;
  return false;
}

void chip_cache_put(std::uint32_t logical_id, const std::uint8_t* frame, std::size_t n,
                    std::uint64_t content_fp) {
  if (!frame || n == 0 || n > 4096) return;
  // update existing
  for (auto& c : g_cache) {
    if (c.valid && c.logical_id == logical_id) {
      std::memcpy(c.data, frame, n);
      if (n < 4096) std::memset(c.data + n, 0, 4096 - n);
      c.content_fp = content_fp;
      return;
    }
  }
  // round-robin insert
  CacheSlot& c = g_cache[g_cache_clock++ % kChipFrameCache];
  c.valid = true;
  c.logical_id = logical_id;
  c.content_fp = content_fp;
  std::memcpy(c.data, frame, n);
  if (n < 4096) std::memset(c.data + n, 0, 4096 - n);
}

double chip_bench_ops_per_sec(int epochs) {
  if (epochs < 1) epochs = 1;
  std::vector<ChipInsn> prog;
  prog.reserve(512);
  for (int i = 0; i < 512; ++i) {
    const auto op = static_cast<ChipOp>(i % 6);
    prog.push_back({op, static_cast<std::uint8_t>(i), static_cast<std::uint8_t>(i * 3),
                    static_cast<std::uint8_t>(i * 7)});
  }
  // warm die
  for (std::size_t i = 0; i < kChipDieSlots; ++i)
    g_die[i] = std::sin(static_cast<float>(i) * 0.07f) * 0.5f + 0.25f;

  const auto t0 = std::chrono::steady_clock::now();
  std::uint64_t ops0 = g_ops;
  for (int e = 0; e < epochs; ++e) {
    chip_run(prog.data(), prog.size());
    for (std::size_t b = 0; b < kChipWaveBands; ++b) {
      g_die[b] = wave_phase_decouple(g_die[b], g_die[(b + 1) % kChipDieSlots], static_cast<int>(b));
      ++g_ops;
    }
  }
  const auto t1 = std::chrono::steady_clock::now();
  const double sec =
      std::chrono::duration_cast<std::chrono::duration<double>>(t1 - t0).count();
  const std::uint64_t delta = g_ops - ops0;
  if (sec <= 0.0) return 0.0;
  return static_cast<double>(delta) / sec;
}

}  // namespace spear

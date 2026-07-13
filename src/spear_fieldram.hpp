// SPDX-License-Identifier: MIT
// Field RAM — FFAT entropy store in RAM (tmpfs / shm), part of KILROY.
// Same honesty as disk FFAT: guaranteed = pool, address_space = sparse map,
// pack_ratio measured (ZERO/RLE/PAK/REF via CHIPs). Not fake ×N.
// Field Research: thin host holds control; Field RAM is host-side field
// storage fabric until GPU Field Die SSBO path is wired.
#pragma once
#include "spear_ffat.hpp"
#include <cstdint>
#include <string>

namespace spear {

// Default Field RAM image (tmpfs = real RAM bytes)
constexpr const char* kFieldRamDefaultPath = "/dev/shm/spear-fieldram.img";
// KILROY initrd / mnt path fallback
constexpr const char* kFieldRamKilroyPath = "/mnt/field/fieldram.img";

struct FieldRamPlan {
  std::string path;
  uint64_t request_bytes = 0;   // size we try to allocate
  uint64_t physical_bytes = 0;  // actual file size
  uint64_t guaranteed_bytes = 0;
  uint64_t address_space_bytes = 0;
  uint64_t host_mem_total = 0;
  uint64_t host_mem_available = 0;
  bool exists = false;
  bool formatted = false;
  bool ok = false;
  std::string error;
  EnsureResult ffat;  // probe result when formatted
};

// Read MemTotal / MemAvailable from /proc/meminfo
void fieldram_host_mem(uint64_t& total, uint64_t& available);

// Choose default size: min(available/4, available-512MiB headroom), clamp [64MiB, 16GiB]
uint64_t fieldram_default_size_bytes();

// Probe existing Field RAM image (does not create)
FieldRamPlan fieldram_probe(const std::string& path = kFieldRamDefaultPath);

// Create/resize image to size_bytes (0 = default), write FFAT if missing or force
FieldRamPlan fieldram_ensure(const std::string& path = kFieldRamDefaultPath,
                             uint64_t size_bytes = 0, bool force = false);

// Human status for CLI / KILROY banner
std::string fieldram_status_text(const std::string& path = kFieldRamDefaultPath);

// Combined Field1 disk + Field RAM accounting
std::string field_storage_status_text();

}  // namespace spear

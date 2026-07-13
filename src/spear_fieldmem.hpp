// SPDX-License-Identifier: MIT
// Field Memory — make MORE field-logical memory from physical RAM and GPU VRAM.
//
// Physical bits stay physical. Expansion is field fabric (same as FFAT disk):
//   guaranteed   = pool worst-case (unique high-H RAW) ≤ physical pool
//   address_space = sparse logical map · frame (empty free)  >> physical
//   pack_ratio    = measured after writes (ZERO / RLE / PAK / REF)
//
// Host RAM and GPU DEVICE_LOCAL are the two physical memory sources.
// KILROY brings both online as system field-native memory planes.
#pragma once
#include "spear_ffat.hpp"
#include <cstdint>
#include <string>

namespace spear {

enum class FieldMemKind : uint8_t {
  HostRam = 1,  // system DRAM
  GpuVram = 2,  // discrete GPU device-local heap
};

// Layout density for memory planes (larger address_space / physical than disk).
// Disk uses divisor 32; memory uses 8 → ~4× denser sparse map for field memory.
constexpr uint32_t kMemMapDivisor = 8;

constexpr const char* kFieldMemHostPath = "/dev/shm/spear-fieldmem-host.img";
constexpr const char* kFieldMemGpuPath = "/dev/shm/spear-fieldmem-gpu.img";
constexpr const char* kFieldMemHostAlt = "/mnt/field/fieldmem-host.img";
constexpr const char* kFieldMemGpuAlt = "/mnt/field/fieldmem-gpu.img";

struct GpuMemInfo {
  bool present = false;
  std::string name;             // e.g. NVIDIA GeForce RTX 4070 Ti
  uint64_t device_local = 0;    // DEVICE_LOCAL heap bytes (true VRAM)
  uint64_t device_budget = 0;   // reported budget if known
  uint64_t host_visible = 0;    // non-local heap (not counted as VRAM pool)
};

struct FieldMemPlan {
  FieldMemKind kind = FieldMemKind::HostRam;
  std::string path;
  std::string source;  // "host DRAM" / GPU name
  uint64_t physical_source_total = 0;  // MemTotal or VRAM total
  uint64_t physical_source_avail = 0;  // MemAvailable or VRAM budget
  uint64_t physical_pool = 0;          // bytes reserved as field pool
  uint64_t guaranteed = 0;             // worst-case usable
  uint64_t address_space = 0;          // sparse field-logical memory
  uint32_t expand_x1000 = 1000;        // address_space/physical_pool ·1000
  uint32_t pack_ratio_x1000 = 1000;
  uint64_t frames_pak = 0;
  bool exists = false;
  bool formatted = false;
  bool ok = false;
  std::string error;
  EnsureResult ffat;
};

// --- probes ---
void fieldmem_host_phys(uint64_t& total, uint64_t& available);
GpuMemInfo fieldmem_gpu_probe();  // parse vulkaninfo / fallback

// Default physical pool from source (leave headroom for OS / display)
uint64_t fieldmem_default_pool(FieldMemKind kind);

// Ensure field memory plane: allocate physical pool, format FFAT with memory density
FieldMemPlan fieldmem_ensure(FieldMemKind kind, uint64_t pool_bytes = 0, bool force = false);
FieldMemPlan fieldmem_probe(FieldMemKind kind);

// Human status
std::string fieldmem_status_text(FieldMemKind kind);
std::string fieldmem_all_status_text();  // host + gpu + expand summary

// Full system: Field1 disk + Field Memory (RAM) + Field Memory (GPU)
std::string field_native_status_text();

// KILROY system field-native bring-up
struct KilroyNative {
  bool ok = false;
  FieldMemPlan host;
  FieldMemPlan gpu;
  EnsureResult field1;
  std::string summary;
};
KilroyNative kilroy_field_native_ensure(bool force_mem = false);

}  // namespace spear

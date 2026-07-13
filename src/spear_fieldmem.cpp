// SPDX-License-Identifier: MIT
// Field Memory — expand host RAM + GPU VRAM into field-logical memory.
// Physical source stays physical; sparse map + PAK make more field memory.
#include "spear_fieldmem.hpp"
#include "spear_field1.hpp"
#include "spear_patdict.hpp"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace spear {
namespace {

uint64_t parse_meminfo_kb(const char* key) {
  std::ifstream in("/proc/meminfo");
  if (!in) return 0;
  std::string line;
  const std::string prefix = std::string(key) + ":";
  while (std::getline(in, line)) {
    if (line.compare(0, prefix.size(), prefix) != 0) continue;
    unsigned long long kb = 0;
    if (std::sscanf(line.c_str() + prefix.size(), "%llu", &kb) == 1) return kb * 1024ull;
  }
  return 0;
}

bool ensure_parent_dir(const std::string& path) {
  const auto slash = path.find_last_of('/');
  if (slash == std::string::npos || slash == 0) return true;
  std::string dir = path.substr(0, slash);
  if (dir == "/dev/shm" || dir == "/tmp") return true;
  ::mkdir("/mnt", 0755);
  ::mkdir("/mnt/field", 0755);
  ::mkdir(dir.c_str(), 0755);
  return true;
}

bool truncate_file(const std::string& path, uint64_t bytes) {
  ensure_parent_dir(path);
  const int fd = ::open(path.c_str(), O_RDWR | O_CREAT, 0644);
  if (fd < 0) return false;
  if (::ftruncate(fd, static_cast<off_t>(bytes)) != 0) {
    ::close(fd);
    return false;
  }
  ::fsync(fd);
  ::close(fd);
  return true;
}

uint64_t file_size_path(const std::string& path) {
  struct stat st {};
  if (stat(path.c_str(), &st) != 0) return 0;
  return static_cast<uint64_t>(st.st_size);
}

void human(char* out, size_t n, uint64_t b) {
  if (b >= (1ull << 40))
    std::snprintf(out, n, "%.2f TiB", b / 1099511627776.0);
  else if (b >= (1ull << 30))
    std::snprintf(out, n, "%.2f GiB", b / 1073741824.0);
  else if (b >= (1ull << 20))
    std::snprintf(out, n, "%.2f MiB", b / 1048576.0);
  else
    std::snprintf(out, n, "%llu B", (unsigned long long)b);
}

const char* primary_path(FieldMemKind k) {
  return k == FieldMemKind::GpuVram ? kFieldMemGpuPath : kFieldMemHostPath;
}
const char* alt_path(FieldMemKind k) {
  return k == FieldMemKind::GpuVram ? kFieldMemGpuAlt : kFieldMemHostAlt;
}

void fill_from_ffat(FieldMemPlan& p, const EnsureResult& f) {
  p.ffat = f;
  p.formatted = f.had_ffat;
  p.guaranteed = f.guaranteed_bytes;
  p.address_space = f.address_space_bytes;
  p.pack_ratio_x1000 = f.pack_ratio_x1000;
  p.frames_pak = f.frames_pak;
  if (p.physical_pool > 0 && p.address_space > 0) {
    const uint64_t x = (p.address_space * 1000ull) / p.physical_pool;
    p.expand_x1000 = x > 0xFFFFFFFFull ? 0xFFFFFFFFu : static_cast<uint32_t>(x);
  }
}

GpuMemInfo parse_vulkaninfo_text(const std::string& out) {
  GpuMemInfo g;
  std::string best_name;
  uint64_t best_local = 0;
  uint64_t best_budget = 0;

  size_t pos = 0;
  while (pos < out.size()) {
    const size_t dn = out.find("deviceName", pos);
    if (dn == std::string::npos) break;
    const size_t eq = out.find('=', dn);
    const size_t nl = out.find('\n', eq == std::string::npos ? dn : eq);
    if (eq == std::string::npos || nl == std::string::npos) {
      pos = dn + 10;
      continue;
    }
    std::string name = out.substr(eq + 1, nl - eq - 1);
    while (!name.empty() && (name[0] == ' ' || name[0] == '\t')) name.erase(0, 1);

    const size_t next = out.find("deviceName", nl);
    const std::string block =
        out.substr(nl, (next == std::string::npos ? out.size() : next) - nl);

    const bool is_cpu =
        name.find("llvmpipe") != std::string::npos || name.find("(LLVM") != std::string::npos;

    uint64_t local = 0, budget = 0;
    const size_t hs = block.find("memoryHeaps");
    if (hs != std::string::npos) {
      const size_t s0 = block.find("size", hs);
      if (s0 != std::string::npos) {
        unsigned long long v = 0;
        if (std::sscanf(block.c_str() + s0, "size   = %llu", &v) == 1 ||
            std::sscanf(block.c_str() + s0, "size = %llu", &v) == 1)
          local = v;
      }
      const size_t b0 = block.find("budget", hs);
      if (b0 != std::string::npos) {
        unsigned long long v = 0;
        if (std::sscanf(block.c_str() + b0, "budget = %llu", &v) == 1 ||
            std::sscanf(block.c_str() + b0, "budget   = %llu", &v) == 1)
          budget = v;
      }
    }

    if (!is_cpu && local >= (1ull << 30)) {
      const bool nvidia = name.find("NVIDIA") != std::string::npos ||
                          name.find("GeForce") != std::string::npos;
      if (nvidia || local > best_local) {
        best_name = name;
        best_local = local;
        best_budget = budget ? budget : local;
      }
    }
    pos = nl + 1;
  }

  if (best_local > 0) {
    g.present = true;
    g.name = best_name;
    g.device_local = best_local;
    g.device_budget = best_budget;
  }
  return g;
}

}  // namespace

void fieldmem_host_phys(uint64_t& total, uint64_t& available) {
  total = parse_meminfo_kb("MemTotal");
  available = parse_meminfo_kb("MemAvailable");
  if (available == 0) available = parse_meminfo_kb("MemFree");
}

GpuMemInfo fieldmem_gpu_probe() {
  GpuMemInfo g;
  FILE* fp = ::popen("vulkaninfo 2>/dev/null", "r");
  if (!fp) return g;
  std::string out;
  char buf[8192];
  while (std::fgets(buf, sizeof buf, fp)) out += buf;
  ::pclose(fp);
  return parse_vulkaninfo_text(out);
}

uint64_t fieldmem_default_pool(FieldMemKind kind) {
  if (kind == FieldMemKind::GpuVram) {
    const GpuMemInfo g = fieldmem_gpu_probe();
    if (!g.present || g.device_local == 0) return 0;
    // Leave headroom for display/chrome: use min(budget, local) * 1/2, cap 8 GiB
    uint64_t base = g.device_budget ? g.device_budget : g.device_local;
    // Prefer not to exceed half of DEVICE_LOCAL
    if (base > g.device_local / 2) base = g.device_local / 2;
    // Keep 1.5 GiB VRAM for desktop if large
    const uint64_t head = 1536ull * 1024 * 1024;
    if (g.device_local > head * 2 && base > g.device_local - head)
      base = g.device_local > head ? g.device_local - head : base;
    uint64_t pool = base / 2;  // half of usable budget → field GPU memory pool
    if (pool < 256ull * 1024 * 1024) pool = 256ull * 1024 * 1024;
    if (pool > 8ull * 1024 * 1024 * 1024) pool = 8ull * 1024 * 1024 * 1024;
    pool = (pool / (1024 * 1024)) * (1024 * 1024);
    return pool;
  }

  // Host RAM: half of available after 1 GiB OS headroom — this IS the physical
  // memory we field-expand (not a tiny demo slice).
  uint64_t total = 0, avail = 0;
  fieldmem_host_phys(total, avail);
  if (avail == 0) return 512ull * 1024 * 1024;
  const uint64_t head = 1024ull * 1024 * 1024;
  uint64_t usable = avail > head ? avail - head : avail / 2;
  uint64_t pool = usable / 2;  // half remaining available DRAM → field host memory
  if (pool < 256ull * 1024 * 1024) pool = 256ull * 1024 * 1024;
  if (pool > 32ull * 1024 * 1024 * 1024) pool = 32ull * 1024 * 1024 * 1024;
  pool = (pool / (1024 * 1024)) * (1024 * 1024);
  return pool;
}

FieldMemPlan fieldmem_probe(FieldMemKind kind) {
  FieldMemPlan p;
  p.kind = kind;
  p.path = primary_path(kind);

  if (kind == FieldMemKind::HostRam) {
    p.source = "host DRAM";
    fieldmem_host_phys(p.physical_source_total, p.physical_source_avail);
  } else {
    const GpuMemInfo g = fieldmem_gpu_probe();
    p.source = g.present ? g.name : "GPU VRAM (absent)";
    p.physical_source_total = g.device_local;
    p.physical_source_avail = g.device_budget ? g.device_budget : g.device_local;
  }

  p.physical_pool = file_size_path(p.path);
  if (p.physical_pool == 0) {
    p.path = alt_path(kind);
    p.physical_pool = file_size_path(p.path);
  }
  p.exists = p.physical_pool > 0;
  if (!p.exists) {
    p.ok = true;
    p.error = "missing";
    return p;
  }
  fill_from_ffat(p, ffat_probe(p.path));
  p.ok = true;
  return p;
}

FieldMemPlan fieldmem_ensure(FieldMemKind kind, uint64_t pool_bytes, bool force) {
  FieldMemPlan p;
  p.kind = kind;
  p.path = primary_path(kind);

  if (kind == FieldMemKind::HostRam) {
    p.source = "host DRAM";
    fieldmem_host_phys(p.physical_source_total, p.physical_source_avail);
  } else {
    const GpuMemInfo g = fieldmem_gpu_probe();
    if (!g.present) {
      p.error = "no_discrete_gpu_vram";
      p.source = "GPU VRAM (absent)";
      return p;
    }
    p.source = g.name;
    p.physical_source_total = g.device_local;
    p.physical_source_avail = g.device_budget ? g.device_budget : g.device_local;
  }

  p.physical_pool = pool_bytes ? pool_bytes : fieldmem_default_pool(kind);
  if (p.physical_pool == 0) {
    p.error = "zero_pool";
    return p;
  }

  // Do not reserve more than ~60% of available source
  if (p.physical_source_avail > 0 && p.physical_pool > p.physical_source_avail * 6 / 10) {
    p.physical_pool = (p.physical_source_avail * 6 / 10 / (1024 * 1024)) * (1024 * 1024);
  }

  if (!truncate_file(p.path, p.physical_pool)) {
    p.path = alt_path(kind);
    if (!truncate_file(p.path, p.physical_pool)) {
      p.error = std::string("truncate_failed: ") + std::strerror(errno);
      return p;
    }
  }
  p.physical_pool = file_size_path(p.path);
  p.exists = true;

  // Memory density map_divisor=8 → denser sparse address_space (more field memory)
  auto cur = ffat_probe(p.path);
  const bool weak_map = cur.had_ffat && cur.address_space_bytes < cur.physical_bytes * 4ull;
  if (force || !cur.had_ffat || weak_map) {
    auto wr = ffat_ensure(p.path, true, kMemMapDivisor);
    if (!wr.ok && !wr.had_ffat) {
      p.error = wr.error.empty() ? "ffat_format_failed" : wr.error;
      p.ffat = wr;
      return p;
    }
  }
  fill_from_ffat(p, ffat_probe(p.path));
  p.ok = p.formatted;
  if (!p.ok) p.error = "format_incomplete";
  return p;
}

std::string fieldmem_status_text(FieldMemKind kind) {
  auto p = fieldmem_probe(kind);
  char st[32], sa[32], pool[32], guar[32], aspc[32];
  human(st, sizeof st, p.physical_source_total);
  human(sa, sizeof sa, p.physical_source_avail);
  human(pool, sizeof pool, p.physical_pool);
  human(guar, sizeof guar, p.guaranteed);
  human(aspc, sizeof aspc, p.address_space);
  const char* title = kind == FieldMemKind::GpuVram ? "Field Memory · GPU VRAM" : "Field Memory · host DRAM";
  char buf[1200];
  std::snprintf(
      buf, sizeof buf,
      "%s\n"
      "  source:         %s\n"
      "  phys_total:     %s\n"
      "  phys_avail:     %s\n"
      "  path:           %s\n"
      "  physical_pool:  %s   ← real memory reserved\n"
      "  guaranteed:     %s   ← worst-case field memory\n"
      "  address_space:  %s   ← sparse field-logical memory (MORE than pool)\n"
      "  expand:         %.2f× address/pool  (empty free; not free lunch for high-H)\n"
      "  pack_ratio:     %.2f× measured after writes (PAK/ZERO/RLE/REF)\n"
      "  formatted:      %s\n",
      title, p.source.c_str(), st, sa, p.path.c_str(), pool, guar, aspc,
      p.expand_x1000 / 1000.0, p.pack_ratio_x1000 / 1000.0, p.formatted ? "yes" : "no");
  return buf;
}

std::string fieldmem_all_status_text() {
  return fieldmem_status_text(FieldMemKind::HostRam) + "\n" +
         fieldmem_status_text(FieldMemKind::GpuVram);
}

std::string field_native_status_text() {
  auto d = field1_discover();
  uint64_t disk_g = d.field_guaranteed, disk_a = d.field_address_space, disk_p = d.qubes_bytes;
  if (d.qubes_present) {
    auto fp = ffat_probe(d.qubes_disk);
    if (fp.had_ffat) {
      disk_g = fp.guaranteed_bytes;
      disk_a = fp.address_space_bytes;
      disk_p = fp.physical_bytes;
    }
  }
  auto host = fieldmem_probe(FieldMemKind::HostRam);
  auto gpu = fieldmem_probe(FieldMemKind::GpuVram);

  const uint64_t mem_g = host.guaranteed + gpu.guaranteed;
  const uint64_t mem_a = host.address_space + gpu.address_space;
  const uint64_t all_g = disk_g + mem_g;
  const uint64_t all_a = disk_a + mem_a;

  char dp[32], dg[32], da[32], hp[32], ha[32], gp[32], ga[32], mg[32], ma[32], ag[32], aa[32];
  human(dp, sizeof dp, disk_p);
  human(dg, sizeof dg, disk_g);
  human(da, sizeof da, disk_a);
  human(hp, sizeof hp, host.physical_pool);
  human(ha, sizeof ha, host.address_space);
  human(gp, sizeof gp, gpu.physical_pool);
  human(ga, sizeof ga, gpu.address_space);
  human(mg, sizeof mg, mem_g);
  human(ma, sizeof ma, mem_a);
  human(ag, sizeof ag, all_g);
  human(aa, sizeof aa, all_a);

  char buf[2048];
  std::snprintf(
      buf, sizeof buf,
      "=== KILROY field-native system ===\n"
      "Field1 DISK  (%s)\n"
      "  physical:       %s\n"
      "  guaranteed:     %s\n"
      "  address_space:  %s\n"
      "Field MEMORY from host DRAM  (%s)\n"
      "  physical_pool:  %s   ← real RAM\n"
      "  field_logical:  %s   ← MORE memory (sparse map)\n"
      "  expand:         %.2f×\n"
      "Field MEMORY from GPU VRAM  (%s)\n"
      "  physical_pool:  %s   ← sized from DEVICE_LOCAL VRAM budget\n"
      "  field_logical:  %s   ← MORE memory (sparse map)\n"
      "  expand:         %.2f×\n"
      "MEMORY planes combined (RAM+GPU)\n"
      "  guaranteed:     %s\n"
      "  field_logical:  %s\n"
      "SYSTEM TOTAL (disk + field memory)\n"
      "  guaranteed:     %s\n"
      "  field_logical:  %s\n"
      "Honesty: physical_pool is real silicon/DRAM; field_logical is sparse address\n"
      "space + measured pack. High-H data → approaches guaranteed, not infinite.\n",
      d.qubes_disk.empty() ? "-" : d.qubes_disk.c_str(), dp, dg, da, host.source.c_str(), hp, ha,
      host.expand_x1000 / 1000.0, gpu.source.c_str(), gp, ga, gpu.expand_x1000 / 1000.0, mg, ma,
      ag, aa);
  return buf;
}

KilroyNative kilroy_field_native_ensure(bool force_mem) {
  KilroyNative k;
  // 0) Shared sealed pattern dictionary (host+GPU) — load if present
  if (!patdict_global().loaded) {
    patdict_load();  // ok if missing
  }
  // 1) Field memory from host DRAM
  k.host = fieldmem_ensure(FieldMemKind::HostRam, 0, force_mem);
  // 2) Field memory from GPU VRAM
  k.gpu = fieldmem_ensure(FieldMemKind::GpuVram, 0, force_mem);
  // 3) Field1 disk probe (do not wipe)
  auto d = field1_discover();
  if (d.qubes_present) k.field1 = ffat_probe(d.qubes_disk);
  k.ok = k.host.ok || k.gpu.ok || k.field1.had_ffat;
  k.summary = field_native_status_text();
  if (patdict_global().loaded) {
    k.summary += "\n";
    k.summary += patdict_status_text();
  }
  return k;
}

}  // namespace spear

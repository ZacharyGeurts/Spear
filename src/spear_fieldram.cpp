// SPDX-License-Identifier: MIT
#include "spear_fieldram.hpp"
#include "spear_field1.hpp"
#include "spear_fieldmem.hpp"

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
    // "MemAvailable:   123456 kB"
    unsigned long long kb = 0;
    if (std::sscanf(line.c_str() + prefix.size(), "%llu", &kb) == 1) return kb * 1024ull;
  }
  return 0;
}

bool ensure_parent_dir(const std::string& path) {
  const auto slash = path.find_last_of('/');
  if (slash == std::string::npos || slash == 0) return true;
  std::string dir = path.substr(0, slash);
  // mkdir -p shallow (one or two levels common for /mnt/field /dev/shm)
  if (dir == "/dev/shm" || dir == "/tmp") return true;
  ::mkdir(dir.c_str(), 0755);
  // parent of that
  const auto s2 = dir.find_last_of('/');
  if (s2 != std::string::npos && s2 > 0) {
    ::mkdir(dir.substr(0, s2).c_str(), 0755);
    ::mkdir(dir.c_str(), 0755);
  }
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

}  // namespace

void fieldram_host_mem(uint64_t& total, uint64_t& available) {
  total = parse_meminfo_kb("MemTotal");
  available = parse_meminfo_kb("MemAvailable");
  if (available == 0) available = parse_meminfo_kb("MemFree");
}

uint64_t fieldram_default_size_bytes() {
  uint64_t total = 0, avail = 0;
  fieldram_host_mem(total, avail);
  if (avail == 0) return 256ull * 1024 * 1024;  // 256 MiB fallback
  // Keep 512 MiB headroom for host; take 1/4 of available
  const uint64_t head = 512ull * 1024 * 1024;
  uint64_t usable = avail > head ? avail - head : avail / 2;
  uint64_t sz = usable / 4;
  const uint64_t min_sz = 64ull * 1024 * 1024;
  const uint64_t max_sz = 16ull * 1024 * 1024 * 1024;  // 16 GiB cap
  if (sz < min_sz) sz = min_sz;
  if (sz > max_sz) sz = max_sz;
  if (sz > usable && usable > min_sz) sz = usable;
  // Align to 1 MiB
  sz = (sz / (1024 * 1024)) * (1024 * 1024);
  if (sz < min_sz) sz = min_sz;
  return sz;
}

FieldRamPlan fieldram_probe(const std::string& path) {
  FieldRamPlan p;
  p.path = path;
  fieldram_host_mem(p.host_mem_total, p.host_mem_available);
  p.physical_bytes = file_size_path(path);
  p.exists = p.physical_bytes > 0;
  if (!p.exists) {
    p.error = "missing";
    p.ok = true;  // probe ok, just empty
    return p;
  }
  p.ffat = ffat_probe(path);
  p.formatted = p.ffat.had_ffat;
  if (p.formatted) {
    p.guaranteed_bytes = p.ffat.guaranteed_bytes;
    p.address_space_bytes = p.ffat.address_space_bytes;
  } else {
    p.guaranteed_bytes = p.physical_bytes > 2ull * 1024 * 1024
                             ? p.physical_bytes - 2ull * 1024 * 1024
                             : p.physical_bytes;
  }
  p.ok = true;
  return p;
}

FieldRamPlan fieldram_ensure(const std::string& path, uint64_t size_bytes, bool force) {
  FieldRamPlan p;
  p.path = path;
  fieldram_host_mem(p.host_mem_total, p.host_mem_available);
  p.request_bytes = size_bytes ? size_bytes : fieldram_default_size_bytes();
  // Don't request more than ~half available
  if (p.host_mem_available > 0 && p.request_bytes > p.host_mem_available / 2) {
    p.request_bytes = (p.host_mem_available / 2 / (1024 * 1024)) * (1024 * 1024);
    if (p.request_bytes < 64ull * 1024 * 1024) p.request_bytes = 64ull * 1024 * 1024;
  }

  const uint64_t cur = file_size_path(path);
  const bool need_create = force || cur < p.request_bytes || cur == 0;
  if (need_create) {
    if (!truncate_file(path, p.request_bytes)) {
      p.error = std::string("truncate_failed: ") + std::strerror(errno);
      // try kilroy path
      if (path == kFieldRamDefaultPath) {
        return fieldram_ensure(kFieldRamKilroyPath, p.request_bytes, force);
      }
      return p;
    }
  }
  p.physical_bytes = file_size_path(path);
  p.exists = p.physical_bytes > 0;

  auto probe = ffat_probe(path);
  if (force || !probe.had_ffat) {
    auto wr = ffat_ensure(path, true);
    if (!wr.ok) {
      p.error = wr.error.empty() ? "ffat_format_failed" : wr.error;
      p.ffat = wr;
      return p;
    }
    p.ffat = wr;
  } else {
    p.ffat = probe;
  }
  p.formatted = p.ffat.had_ffat;
  p.guaranteed_bytes = p.ffat.guaranteed_bytes;
  p.address_space_bytes = p.ffat.address_space_bytes;
  p.ok = p.formatted;
  return p;
}

std::string fieldram_status_text(const std::string& path) {
  auto p = fieldram_probe(path);
  char ht[32], ha[32], phys[32], guar[32], aspc[32];
  human(ht, sizeof ht, p.host_mem_total);
  human(ha, sizeof ha, p.host_mem_available);
  human(phys, sizeof phys, p.physical_bytes);
  human(guar, sizeof guar, p.guaranteed_bytes);
  human(aspc, sizeof aspc, p.address_space_bytes);
  char buf[1024];
  std::snprintf(
      buf, sizeof buf,
      "Field RAM (KILROY · FFAT-on-tmpfs)\n"
      "  path:           %s\n"
      "  exists:         %s  formatted: %s\n"
      "  host_mem_total: %s\n"
      "  host_mem_avail: %s\n"
      "  physical:       %s  (%llu bytes)\n"
      "  guaranteed:     %s  (worst-case pool)\n"
      "  address_space:  %s  (sparse map)\n"
      "  pack_ratio_x1000: %u  frames_pak=%llu\n"
      "  note: same ZERO/RLE/PAK/REF as Field1 · measured only · KILROY boot ensures\n",
      p.path.c_str(), p.exists ? "yes" : "no", p.formatted ? "yes" : "no", ht, ha, phys,
      (unsigned long long)p.physical_bytes, guar, aspc, p.ffat.pack_ratio_x1000,
      (unsigned long long)p.ffat.frames_pak);
  if (!p.error.empty() && p.error != "missing") {
    std::strncat(buf, ("  error: " + p.error + "\n").c_str(), sizeof buf - std::strlen(buf) - 1);
  }
  return buf;
}

std::string field_storage_status_text() {
  // Prefer field-native view (disk + host DRAM memory + GPU VRAM memory)
  return field_native_status_text();
}

}  // namespace spear

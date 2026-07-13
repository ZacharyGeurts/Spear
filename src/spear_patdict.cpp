// SPDX-License-Identifier: MIT
// Shared sealed pattern dictionary — H7B-style, Field Memory host+GPU.
#include "spear_patdict.hpp"
#include "spear_sha256.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <map>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace spear {
namespace {

PatDict g_dict;
// Training accumulator: pattern key -> count (key = raw bytes as string for map)
std::map<std::string, uint32_t> g_train_counts;
uint64_t g_train_bytes = 0;
constexpr const char* kTrainStagePath = "/dev/shm/spear-fieldmem-dict.train";

void put_u16(uint8_t* p, uint16_t v) {
  p[0] = static_cast<uint8_t>(v & 0xFF);
  p[1] = static_cast<uint8_t>((v >> 8) & 0xFF);
}
void put_u32(uint8_t* p, uint32_t v) {
  p[0] = static_cast<uint8_t>(v);
  p[1] = static_cast<uint8_t>(v >> 8);
  p[2] = static_cast<uint8_t>(v >> 16);
  p[3] = static_cast<uint8_t>(v >> 24);
}
uint16_t get_u16(const uint8_t* p) {
  return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
}
uint32_t get_u32(const uint8_t* p) {
  return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}

bool read_all_file(const std::string& path, std::vector<uint8_t>& out, size_t max_n = 64ull << 20) {
  const int fd = ::open(path.c_str(), O_RDONLY);
  if (fd < 0) return false;
  struct stat st {};
  if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode)) {
    ::close(fd);
    return false;
  }
  size_t n = static_cast<size_t>(st.st_size);
  if (n > max_n) n = max_n;
  out.resize(n);
  size_t got = 0;
  while (got < n) {
    const ssize_t r = ::read(fd, out.data() + got, n - got);
    if (r <= 0) break;
    got += static_cast<size_t>(r);
  }
  ::close(fd);
  out.resize(got);
  return got > 0;
}

bool write_atomic(const std::string& path, const uint8_t* data, size_t n) {
  const std::string tmp = path + ".tmp";
  // parent
  const auto slash = path.find_last_of('/');
  if (slash != std::string::npos) {
    std::string dir = path.substr(0, slash);
    ::mkdir(dir.c_str(), 0755);
  }
  const int fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);  // owner-only
  if (fd < 0) return false;
  size_t put = 0;
  while (put < n) {
    const ssize_t w = ::write(fd, data + put, n - put);
    if (w <= 0) {
      ::close(fd);
      ::unlink(tmp.c_str());
      return false;
    }
    put += static_cast<size_t>(w);
  }
  ::fsync(fd);
  ::close(fd);
  if (::rename(tmp.c_str(), path.c_str()) != 0) {
    ::unlink(tmp.c_str());
    return false;
  }
  // tighten perms
  ::chmod(path.c_str(), 0600);
  return true;
}

void train_stage_save() {
  std::vector<uint8_t> raw;
  raw.reserve(16 + g_train_counts.size() * 40);
  raw.push_back('T');
  raw.push_back('R');
  raw.push_back('N');
  raw.push_back('1');
  uint8_t h[12];
  put_u32(h, static_cast<uint32_t>(g_train_counts.size()));
  for (int i = 0; i < 8; ++i) h[4 + i] = static_cast<uint8_t>((g_train_bytes >> (8 * i)) & 0xFF);
  raw.insert(raw.end(), h, h + 12);
  for (const auto& kv : g_train_counts) {
    if (kv.first.size() > 65535) continue;
    uint8_t eh[6];
    put_u16(eh, static_cast<uint16_t>(kv.first.size()));
    put_u32(eh + 2, kv.second);
    raw.insert(raw.end(), eh, eh + 6);
    raw.insert(raw.end(), kv.first.begin(), kv.first.end());
  }
  write_atomic(kTrainStagePath, raw.data(), raw.size());
  ::chmod(kTrainStagePath, 0600);
}

void train_stage_load() {
  std::vector<uint8_t> raw;
  if (!read_all_file(kTrainStagePath, raw, 32ull << 20)) return;
  if (raw.size() < 16 || std::memcmp(raw.data(), "TRN1", 4) != 0) return;
  const uint32_t n = get_u32(raw.data() + 4);
  uint64_t tb = 0;
  for (int i = 0; i < 8; ++i) tb |= uint64_t(raw[8 + i]) << (8 * i);
  g_train_bytes = tb;
  size_t off = 16;
  for (uint32_t i = 0; i < n && off + 6 <= raw.size(); ++i) {
    const uint16_t plen = get_u16(raw.data() + off);
    const uint32_t cnt = get_u32(raw.data() + off + 2);
    off += 6;
    if (off + plen > raw.size()) break;
    std::string key(reinterpret_cast<const char*>(raw.data() + off), plen);
    off += plen;
    g_train_counts[key] += cnt;
  }
}

void recompute_seal(PatDict& d) {
  // Seal = SHA-256(version || generation || concat(id_be,len_be,pat)...)
  std::vector<uint8_t> body;
  body.reserve(64 + d.entries.size() * 32);
  uint8_t hdr[8];
  put_u32(hdr, d.version);
  put_u32(hdr + 4, d.generation);
  body.insert(body.end(), hdr, hdr + 8);
  for (const auto& e : d.entries) {
    uint8_t eh[4];
    put_u16(eh, e.id);
    put_u16(eh + 2, static_cast<uint16_t>(e.pat.size()));
    body.insert(body.end(), eh, eh + 4);
    body.insert(body.end(), e.pat.begin(), e.pat.end());
  }
  const Sha256 s = sha256(body.data(), body.size());
  std::memcpy(d.seal, s.digest, 32);
  d.sealed_ok = true;
}

// Rolling 64-bit hash for substrings of length L (for counting)
uint64_t fnv_window(const uint8_t* p, size_t L) {
  uint64_t h = 14695981039346656037ull;
  for (size_t i = 0; i < L; ++i) {
    h ^= p[i];
    h *= 1099511628211ull;
  }
  return h;
}

}  // namespace

PatDict& patdict_global() { return g_dict; }

bool patdict_path_is_safe(const std::string& path) {
  if (const char* env = std::getenv("SPEAR_DICT_ALLOW_UNSAFE")) {
    if (env[0] == '1') return true;
  }
  // Prefer /dev/shm and /mnt/field owned paths; reject obvious world inject points
  if (path.find("/tmp/") == 0 && path.find("/tmp/spear") != 0) return false;
  if (path.find("/var/tmp/") == 0) return false;
  if (path.find("..") != std::string::npos) return false;
  struct stat st {};
  if (stat(path.c_str(), &st) == 0) {
    // reject world-writable dict files
    if (st.st_mode & S_IWOTH) return false;
  }
  // parent world-writable?
  const auto slash = path.find_last_of('/');
  if (slash != std::string::npos) {
    std::string dir = path.substr(0, slash);
    if (stat(dir.c_str(), &st) == 0 && (st.st_mode & S_IWOTH) && dir != "/dev/shm") {
      // /dev/shm is often sticky+world-writable — require filename prefix spear-
      if (path.find("/dev/shm/spear-") != 0) return false;
    }
  }
  return true;
}

bool patdict_load(const std::string& path) {
  g_dict = PatDict{};
  g_dict.path = path;
  if (!patdict_path_is_safe(path)) {
    g_dict.error = "unsafe_dict_path";
    return false;
  }
  std::vector<uint8_t> raw;
  if (!read_all_file(path, raw, 8ull << 20)) {
    // try alt
    if (path == kPatDictDefaultPath) return patdict_load(kPatDictAltPath);
    g_dict.error = "missing";
    return false;
  }
  if (raw.size() < 56 || std::memcmp(raw.data(), kPatDictMagic, 4) != 0) {
    g_dict.error = "bad_magic";
    return false;
  }
  const uint32_t ver = get_u32(raw.data() + 4);
  const uint32_t gen = get_u32(raw.data() + 8);
  const uint32_t nent = get_u32(raw.data() + 12);
  if (ver != kPatDictVersion || nent > kPatDictMaxEntries) {
    g_dict.error = "bad_version_or_count";
    return false;
  }
  uint8_t file_seal[32];
  std::memcpy(file_seal, raw.data() + 16, 32);
  const uint64_t trained = 0;  // optional at offset 48
  (void)trained;
  size_t off = 48;
  if (raw.size() >= 56) {
    // trained_bytes u64 at 48
  }
  uint64_t trb = 0;
  if (raw.size() >= 56) {
    for (int i = 0; i < 8; ++i) trb |= uint64_t(raw[48 + i]) << (8 * i);
    off = 56;
  }

  std::vector<PatEntry> entries;
  entries.reserve(nent);
  for (uint32_t i = 0; i < nent; ++i) {
    if (off + 4 > raw.size()) {
      g_dict.error = "truncated_entries";
      return false;
    }
    const uint16_t id = get_u16(raw.data() + off);
    const uint16_t plen = get_u16(raw.data() + off + 2);
    off += 4;
    if (plen < kPatDictMinPatLen || plen > kPatDictMaxPatLen || off + plen > raw.size()) {
      g_dict.error = "bad_entry";
      return false;
    }
    PatEntry e;
    e.id = id;
    e.pat.assign(raw.begin() + static_cast<std::ptrdiff_t>(off),
                 raw.begin() + static_cast<std::ptrdiff_t>(off + plen));
    off += plen;
    entries.push_back(std::move(e));
  }

  g_dict.version = ver;
  g_dict.generation = gen;
  g_dict.trained_bytes = trb;
  g_dict.entries = std::move(entries);
  g_dict.patterns_found = g_dict.entries.size();
  recompute_seal(g_dict);
  if (std::memcmp(g_dict.seal, file_seal, 32) != 0) {
    g_dict = PatDict{};
    g_dict.error = "SEAL_MISMATCH — dict refused (tamper or corrupt)";
    g_dict.sealed_ok = false;
    return false;
  }
  // longest first
  std::sort(g_dict.entries.begin(), g_dict.entries.end(),
            [](const PatEntry& a, const PatEntry& b) {
              if (a.pat.size() != b.pat.size()) return a.pat.size() > b.pat.size();
              return a.id < b.id;
            });
  g_dict.loaded = true;
  g_dict.sealed_ok = true;
  g_dict.path = path;
  g_dict.error.clear();
  return true;
}

bool patdict_save(const std::string& path) {
  if (g_dict.entries.empty()) {
    g_dict.error = "empty_dict";
    return false;
  }
  if (!patdict_path_is_safe(path)) {
    g_dict.error = "unsafe_dict_path";
    return false;
  }
  g_dict.version = kPatDictVersion;
  if (g_dict.generation == 0) g_dict.generation = 1;
  recompute_seal(g_dict);

  std::vector<uint8_t> raw;
  raw.reserve(64 + g_dict.entries.size() * 40);
  raw.insert(raw.end(), kPatDictMagic, kPatDictMagic + 4);
  uint8_t h[12];
  put_u32(h, g_dict.version);
  put_u32(h + 4, g_dict.generation);
  put_u32(h + 8, static_cast<uint32_t>(g_dict.entries.size()));
  raw.insert(raw.end(), h, h + 12);
  raw.insert(raw.end(), g_dict.seal, g_dict.seal + 32);
  for (int i = 0; i < 8; ++i)
    raw.push_back(static_cast<uint8_t>((g_dict.trained_bytes >> (8 * i)) & 0xFF));
  for (const auto& e : g_dict.entries) {
    uint8_t eh[4];
    put_u16(eh, e.id);
    put_u16(eh + 2, static_cast<uint16_t>(e.pat.size()));
    raw.insert(raw.end(), eh, eh + 4);
    raw.insert(raw.end(), e.pat.begin(), e.pat.end());
  }
  if (!write_atomic(path, raw.data(), raw.size())) {
    g_dict.error = std::string("save_failed: ") + std::strerror(errno);
    return false;
  }
  g_dict.path = path;
  g_dict.loaded = true;
  g_dict.sealed_ok = true;
  ::chmod(path.c_str(), 0600);
  return true;
}

void patdict_clear(bool unlink_file) {
  g_dict = PatDict{};
  g_train_counts.clear();
  g_train_bytes = 0;
  ::unlink(kTrainStagePath);
  if (unlink_file) {
    ::unlink(kPatDictDefaultPath);
    ::unlink(kPatDictAltPath);
  }
}

uint32_t patdict_train_buffer(const uint8_t* data, size_t n) {
  if (!data || n < kPatDictMinPatLen * 2) return 0;
  // Cap train slice to keep memory bounded
  if (n > 4ull << 20) n = 4ull << 20;
  if (g_train_counts.empty() && g_train_bytes == 0) train_stage_load();
  g_train_bytes += n;

  // Fixed lengths: 8, 12, 16, 24, 32, 48, 64 — full step-1 scan for fidelity
  static const size_t lens[] = {8, 12, 16, 24, 32, 48, 64};
  uint32_t newish = 0;
  for (size_t L : lens) {
    if (n < L) continue;
    for (size_t i = 0; i + L <= n; ++i) {
      bool all0 = true;
      for (size_t j = 0; j < L; ++j) {
        if (data[i + j] != 0) {
          all0 = false;
          break;
        }
      }
      if (all0) continue;
      std::string key(reinterpret_cast<const char*>(data + i), L);
      auto it = g_train_counts.find(key);
      if (it == g_train_counts.end()) {
        g_train_counts.emplace(std::move(key), 1u);
        ++newish;
      } else {
        ++it->second;
      }
      if (g_train_counts.size() > 250000) break;
    }
    if (g_train_counts.size() > 250000) break;
  }
  (void)fnv_window;
  train_stage_save();
  return newish;
}

uint32_t patdict_train_file(const std::string& path, std::string& err) {
  err.clear();
  struct stat st {};
  if (stat(path.c_str(), &st) != 0) {
    err = "stat_failed";
    return 0;
  }
  if (!S_ISREG(st.st_mode)) {
    err = "not_regular_file";
    return 0;
  }
  // Refuse world-writable sources (inject surface)
  if (st.st_mode & S_IWOTH) {
    err = "refuse_world_writable_source";
    return 0;
  }
  std::vector<uint8_t> raw;
  if (!read_all_file(path, raw, 16ull << 20)) {
    err = "read_failed";
    return 0;
  }
  return patdict_train_buffer(raw.data(), raw.size());
}

bool patdict_commit(uint32_t max_entries) {
  if (max_entries == 0 || max_entries > kPatDictMaxEntries) max_entries = 1024;
  if (g_train_counts.empty()) train_stage_load();
  struct Cand {
    std::string pat;
    uint32_t count;
    uint64_t score;
  };
  std::vector<Cand> cands;
  cands.reserve(g_train_counts.size());
  for (const auto& kv : g_train_counts) {
    if (kv.second < kPatDictMinOcc) continue;
    const uint64_t score =
        static_cast<uint64_t>(kv.second) * static_cast<uint64_t>(kv.first.size());
    // Need real savings: score * (len - 3) roughly vs keep raw
    if (kv.first.size() < kPatDictMinPatLen) continue;
    cands.push_back({kv.first, kv.second, score});
  }
  std::sort(cands.begin(), cands.end(),
            [](const Cand& a, const Cand& b) { return a.score > b.score; });
  if (cands.size() > max_entries) cands.resize(max_entries);

  g_dict.entries.clear();
  g_dict.entries.reserve(cands.size());
  uint16_t id = 1;
  for (const auto& c : cands) {
    PatEntry e;
    e.id = id++;
    e.pat.assign(c.pat.begin(), c.pat.end());
    g_dict.entries.push_back(std::move(e));
  }
  std::sort(g_dict.entries.begin(), g_dict.entries.end(),
            [](const PatEntry& a, const PatEntry& b) {
              if (a.pat.size() != b.pat.size()) return a.pat.size() > b.pat.size();
              return a.id < b.id;
            });
  g_dict.version = kPatDictVersion;
  g_dict.generation += 1;
  if (g_dict.generation == 0) g_dict.generation = 1;
  g_dict.trained_bytes = g_train_bytes;
  g_dict.patterns_found = g_dict.entries.size();
  recompute_seal(g_dict);
  g_dict.loaded = !g_dict.entries.empty();
  g_dict.sealed_ok = g_dict.loaded;
  if (g_dict.loaded) {
    g_train_counts.clear();
    ::unlink(kTrainStagePath);
  }
  return g_dict.loaded;
}

std::string patdict_status_text() {
  char sealhex[65]{};
  if (g_dict.sealed_ok) {
    Sha256 s;
    std::memcpy(s.digest, g_dict.seal, 32);
    sha256_hex(s, sealhex);
  } else {
    std::snprintf(sealhex, sizeof sealhex, "(none)");
  }
  char buf[1024];
  std::snprintf(
      buf, sizeof buf,
      "Shared Field Pattern Dictionary (H7B-kin · sealed)\n"
      "  loaded:       %s  sealed_ok=%s\n"
      "  path:         %s\n"
      "  generation:   %u\n"
      "  entries:      %zu  (max %u)\n"
      "  trained_bytes:%llu (pending_scan=%llu)\n"
      "  train_pending:%zu unique windows\n"
      "  seal_sha256:  %s\n"
      "  shared_by:    host DRAM fieldmem + GPU VRAM fieldmem\n"
      "  security:     seal bind on pack; refuse world inject sources; mode 0600\n"
      "  error:        %s\n",
      g_dict.loaded ? "yes" : "no", g_dict.sealed_ok ? "yes" : "no",
      g_dict.path.empty() ? "-" : g_dict.path.c_str(), g_dict.generation, g_dict.entries.size(),
      kPatDictMaxEntries, (unsigned long long)g_dict.trained_bytes, (unsigned long long)g_train_bytes,
      g_train_counts.size(), sealhex, g_dict.error.empty() ? "-" : g_dict.error.c_str());
  return buf;
}

size_t patdict_pack_frame(const uint8_t* frame, size_t frame_n, uint8_t* dest, size_t dest_cap) {
  if (!g_dict.loaded || !g_dict.sealed_ok || g_dict.entries.empty() || !frame || !dest)
    return 0;
  if (dest_cap < 32) return 0;

  // Header: DIC1 | gen:u32 | seal[16] | stream
  // stream tokens: 0x00 len:u8 lit[len] | 0x01 id:u16
  std::vector<uint8_t> stream;
  stream.reserve(frame_n);
  size_t i = 0;
  while (i < frame_n) {
    size_t best_len = 0;
    uint16_t best_id = 0;
    for (const auto& e : g_dict.entries) {
      const size_t L = e.pat.size();
      if (L <= best_len || i + L > frame_n) continue;
      if (std::memcmp(frame + i, e.pat.data(), L) == 0) {
        best_len = L;
        best_id = e.id;
        break;  // longest-first sorted
      }
    }
    if (best_len >= kPatDictMinPatLen) {
      stream.push_back(0x01);
      stream.push_back(static_cast<uint8_t>(best_id & 0xFF));
      stream.push_back(static_cast<uint8_t>((best_id >> 8) & 0xFF));
      i += best_len;
    } else {
      // gather literals until next match or 255
      size_t j = i;
      size_t lit_start = i;
      while (j < frame_n && j - lit_start < 255) {
        bool hit = false;
        for (const auto& e : g_dict.entries) {
          const size_t L = e.pat.size();
          if (j + L <= frame_n && std::memcmp(frame + j, e.pat.data(), L) == 0) {
            hit = true;
            break;
          }
        }
        if (hit && j > lit_start) break;
        if (hit && j == lit_start) break;
        ++j;
        if (hit) break;
      }
      if (j == lit_start) {
        // force one byte
        j = lit_start + 1;
      }
      const size_t ln = j - lit_start;
      stream.push_back(0x00);
      stream.push_back(static_cast<uint8_t>(ln));
      stream.insert(stream.end(), frame + lit_start, frame + lit_start + ln);
      i = j;
    }
  }

  const size_t hdr = 4 + 4 + 16;  // DIC1 + gen + seal16
  const size_t total = hdr + stream.size();
  if (total + 32 >= frame_n || total > dest_cap) return 0;  // need real savings

  dest[0] = 'D';
  dest[1] = 'I';
  dest[2] = 'C';
  dest[3] = '1';
  put_u32(dest + 4, g_dict.generation);
  std::memcpy(dest + 8, g_dict.seal, 16);  // bind first 16 of seal
  std::memcpy(dest + hdr, stream.data(), stream.size());
  return total;
}

bool patdict_unpack_frame(const uint8_t* packed, size_t packed_n, uint8_t* frame, size_t frame_n) {
  if (!packed || packed_n < 24 || !frame || frame_n == 0) return false;
  if (std::memcmp(packed, "DIC1", 4) != 0) return false;
  if (!g_dict.loaded || !g_dict.sealed_ok) return false;
  const uint32_t gen = get_u32(packed + 4);
  if (gen != g_dict.generation) return false;
  if (std::memcmp(packed + 8, g_dict.seal, 16) != 0) return false;

  // id -> entry map
  std::map<uint16_t, const PatEntry*> by_id;
  for (const auto& e : g_dict.entries) by_id[e.id] = &e;

  size_t i = 24;
  size_t o = 0;
  while (i < packed_n && o < frame_n) {
    const uint8_t t = packed[i++];
    if (t == 0x00) {
      if (i >= packed_n) return false;
      const uint8_t ln = packed[i++];
      if (i + ln > packed_n || o + ln > frame_n) return false;
      std::memcpy(frame + o, packed + i, ln);
      o += ln;
      i += ln;
    } else if (t == 0x01) {
      if (i + 2 > packed_n) return false;
      const uint16_t id = get_u16(packed + i);
      i += 2;
      auto it = by_id.find(id);
      if (it == by_id.end()) return false;
      const auto& pat = it->second->pat;
      if (o + pat.size() > frame_n) return false;
      std::memcpy(frame + o, pat.data(), pat.size());
      o += pat.size();
    } else {
      return false;
    }
  }
  return o == frame_n;
}

}  // namespace spear

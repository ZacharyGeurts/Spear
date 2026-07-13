// SPDX-License-Identifier: MIT
// Shared pattern dictionary for Field Memory (host DRAM + GPU VRAM).
// Kin to Hostess H7B/3 pattern condenser — measured expansion, SHA-256 sealed.
//
// Security (serious):
//   - Dict file sealed with SHA-256 over entries; load refuses seal mismatch.
//   - Packed frames bind dict seal prefix; unpack refuses wrong/missing dict.
//   - Train only via explicit operator command (no ambient untrusted inject).
//   - World-writable dict paths rejected unless SPEAR_DICT_ALLOW_UNSAFE=1.
//
// Capacity: shared patterns across both memory planes burn pool once in dict,
// not once per plane per occurrence. pack_ratio measured — never fake ×N.
#pragma once
#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

namespace spear {

constexpr const char* kPatDictDefaultPath = "/dev/shm/spear-fieldmem-dict.spdb";
constexpr const char* kPatDictAltPath = "/mnt/field/spear-fieldmem-dict.spdb";
constexpr char kPatDictMagic[4] = {'S', 'P', 'D', 'B'};  // Spear Pattern Dict Binary
constexpr uint32_t kPatDictVersion = 0x00010000u;
constexpr uint32_t kPatDictMaxEntries = 4096;
constexpr uint32_t kPatDictMinPatLen = 8;
constexpr uint32_t kPatDictMaxPatLen = 64;
constexpr uint32_t kPatDictMinOcc = 2;  // appear at least twice in train corpus

struct PatEntry {
  uint16_t id = 0;
  std::vector<uint8_t> pat;
};

struct PatDict {
  bool loaded = false;
  bool sealed_ok = false;
  uint32_t version = 0;
  uint32_t generation = 0;  // bumps on each successful train/save
  uint64_t trained_bytes = 0;
  uint64_t patterns_found = 0;
  std::string path;
  uint8_t seal[32]{};  // SHA-256 of entry body
  std::vector<PatEntry> entries;  // sorted longest-first for greedy pack
  std::string error;
};

// Global shared dict (both fieldmem planes)
PatDict& patdict_global();

// Load sealed dict; returns false if missing or seal fail (does not throw).
bool patdict_load(const std::string& path = kPatDictDefaultPath);

// Save sealed dict atomically.
bool patdict_save(const std::string& path = kPatDictDefaultPath);

// Clear in-memory + optional file delete
void patdict_clear(bool unlink_file = false);

// Train from buffer (append candidates). Does not save until patdict_save.
// Returns number of new patterns accepted into working set.
uint32_t patdict_train_buffer(const uint8_t* data, size_t n);

// Train from file path (regular files only; refuses devices).
uint32_t patdict_train_file(const std::string& path, std::string& err);

// Finalize working candidates into sealed entry table (top by score).
// max_entries caps dictionary size.
bool patdict_commit(uint32_t max_entries = 1024);

// Status text
std::string patdict_status_text();

// Pack frame using loaded dict. Returns packed size or 0 if no win / no dict.
// Output format: DIC1 | gen:u32 | seal16 | stream
size_t patdict_pack_frame(const uint8_t* frame, size_t frame_n, uint8_t* dest, size_t dest_cap);

// Unpack DIC1 stream; requires loaded dict with matching seal prefix.
bool patdict_unpack_frame(const uint8_t* packed, size_t packed_n, uint8_t* frame, size_t frame_n);

// Path safety: reject world-writable dict locations unless override env set.
bool patdict_path_is_safe(const std::string& path);

}  // namespace spear

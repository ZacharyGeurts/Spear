// SPDX-License-Identifier: MIT
// spear — C++ only CLI. Autoelevate allowlist. No polkit.
#include "spear_chip.hpp"
#include "spear_elevate.hpp"
#include "spear_ffat.hpp"
#include "spear_field1.hpp"
#include "spear_fieldmem.hpp"
#include "spear_fieldram.hpp"
#include "spear_patdict.hpp"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

static void usage() {
  std::fprintf(stderr,
               "spear — Field OS control (C++, autoelevate, no polkit)\n\n"
               "  spear version | elevate-status | storage-status | kilroy-status\n"
               "  spear kilroy-ensure          # whole system field-native\n"
               "  spear fieldmem-status|ensure|force  host|gpu|all\n"
               "  spear dict-status|train|commit|save|load|clear|verify\n"
               "  spear chip-status | chip-bench | chip-demo\n"
               "  spear ffat-probe|ensure|force <dev|image>\n"
               "  spear ffat-put|get …\n"
               "  spear field1-status|backup|claim\n\n"
               "KILROY: disk + field MEMORY from host DRAM + GPU VRAM.\n"
               "Physical pool is real RAM/VRAM; field_logical is MORE (sparse+PAK).\n"
               "Install once: chown root:root spear && chmod u+s spear\n");
}

static int need_elev(const std::string& cmd) {
  std::string err;
  if (!spear::autoelevate_begin(cmd, err)) {
    std::fprintf(stderr, "autoelevate: %s\n", err.c_str());
    std::fprintf(stderr, "%s\n", spear::elevate_status_line().c_str());
    return 77;
  }
  return 0;
}

static int print_ffat(const spear::EnsureResult& r) {
  if (!r.error.empty() && !r.ok && !r.legacy_fake_factor) {
    std::printf("ok=false error=%s\n", r.error.c_str());
    return 1;
  }
  std::printf(
      "ok=%s kind=FieldFat_v4_entropy+pak(not-msfat) physical=%llu guaranteed=%llu "
      "address_space=%llu stored_logical=%llu used_physical=%llu "
      "pack_ratio_x1000=%u shannon_avg_x1000=%u frames_pak=%llu frames=%u part_lba=%u "
      "mbr=%s ffat=%s wrote_mbr=%s wrote_ffat=%s label=%s legacy_fake=%s err=%s\n",
      r.ok ? "true" : "false", (unsigned long long)r.physical_bytes,
      (unsigned long long)r.guaranteed_bytes, (unsigned long long)r.address_space_bytes,
      (unsigned long long)r.logical_stored, (unsigned long long)r.physical_used,
      r.pack_ratio_x1000, r.shannon_avg_x1000, (unsigned long long)r.frames_pak, r.logical_frames,
      r.part_lba, r.had_mbr ? "yes" : "no", r.had_ffat ? "yes" : "no", r.wrote_mbr ? "yes" : "no",
      r.wrote_ffat ? "yes" : "no", r.label.empty() ? "-" : r.label.c_str(),
      r.legacy_fake_factor ? "yes" : "no", r.error.empty() ? "-" : r.error.c_str());
  return (r.ok || r.legacy_fake_factor) ? 0 : 1;
}

static void fill_pattern(std::vector<uint8_t>& frame, const char* mode) {
  frame.assign(spear::kFrameBytes, 0);
  if (std::strcmp(mode, "zero") == 0) {
    return;
  }
  if (std::strcmp(mode, "rle") == 0) {
    // low Shannon: long runs of two values (H ≈ 1 bit/byte) so RLE wins
    for (size_t i = 0; i < frame.size(); ++i) frame[i] = (i & 512) ? 0xAA : 0x00;
    return;
  }
  if (std::strcmp(mode, "text") == 0) {
    const char* msg = "FIELD FAT entropy pack SPEAR FIELD1 primer shannon ";
    const size_t mlen = std::strlen(msg);
    for (size_t i = 0; i < frame.size(); ++i) frame[i] = static_cast<uint8_t>(msg[i % mlen]);
    return;
  }
  if (std::strcmp(mode, "wave") == 0 || std::strcmp(mode, "peaks") == 0) {
    // Clean tents: angle up 0→180, angle down 179→0 (single apex, PEAK-friendly)
    size_t pos = 0;
    while (pos < frame.size()) {
      for (int v = 0; v <= 180 && pos < frame.size(); ++v)
        frame[pos++] = static_cast<uint8_t>(v);
      for (int v = 179; v >= 0 && pos < frame.size(); --v)
        frame[pos++] = static_cast<uint8_t>(v);
    }
    return;
  }
  // random-ish (LFSR) — high Shannon, no pack gain
  uint32_t x = 0xA5A5C3C3u;
  for (size_t i = 0; i < frame.size(); ++i) {
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    frame[i] = static_cast<uint8_t>(x);
  }
}

int main(int argc, char** argv) {
  spear::elevate_set_cli(argc, argv);
  static bool chip_inited = false;
  if (!chip_inited) {
    spear::chip_reset();
    chip_inited = true;
  }
  if (argc < 2) {
    usage();
    return 2;
  }
  const std::string cmd = argv[1];

  if (cmd == "version") {
    std::puts("spear 1.7.0 (KILROY · shared sealed patdict host+GPU · field MEMORY · FFAT)");
    return 0;
  }
  if (cmd == "dict-status") {
    if (!spear::patdict_global().loaded) spear::patdict_load();
    std::fputs(spear::patdict_status_text().c_str(), stdout);
    return 0;
  }
  if (cmd == "dict-clear") {
    spear::patdict_clear(argc >= 3 && std::strcmp(argv[2], "--unlink") == 0);
    std::puts("dict-clear ok");
    return 0;
  }
  if (cmd == "dict-load") {
    const char* path = argc >= 3 ? argv[2] : spear::kPatDictDefaultPath;
    if (!spear::patdict_load(path)) {
      std::printf("dict-load FAIL err=%s\n", spear::patdict_global().error.c_str());
      return 1;
    }
    std::printf("dict-load ok entries=%zu gen=%u\n", spear::patdict_global().entries.size(),
                spear::patdict_global().generation);
    return 0;
  }
  if (cmd == "dict-save") {
    const char* path = argc >= 3 ? argv[2] : spear::kPatDictDefaultPath;
    if (!spear::patdict_save(path)) {
      std::printf("dict-save FAIL err=%s\n", spear::patdict_global().error.c_str());
      return 1;
    }
    std::printf("dict-save ok path=%s entries=%zu\n", path, spear::patdict_global().entries.size());
    return 0;
  }
  if (cmd == "dict-commit") {
    uint32_t maxe = 1024;
    if (argc >= 3) maxe = static_cast<uint32_t>(std::strtoul(argv[2], nullptr, 0));
    if (!spear::patdict_commit(maxe)) {
      std::puts("dict-commit FAIL (no patterns met min_occ)");
      return 1;
    }
    if (!spear::patdict_save()) {
      std::printf("dict-commit ok but save FAIL err=%s\n", spear::patdict_global().error.c_str());
      return 1;
    }
    std::printf("dict-commit+save ok entries=%zu gen=%u\n", spear::patdict_global().entries.size(),
                spear::patdict_global().generation);
    return 0;
  }
  if (cmd == "dict-train") {
    if (argc < 3) {
      std::fprintf(stderr, "usage: spear dict-train [--commit] <trusted-file> [file2...]\n");
      return 2;
    }
    bool do_commit = false;
    uint32_t total_new = 0;
    std::vector<const char*> files;
    for (int i = 2; i < argc; ++i) {
      if (std::strcmp(argv[i], "--commit") == 0) {
        do_commit = true;
        continue;
      }
      files.push_back(argv[i]);
    }
    for (const char* f : files) {
      std::string err;
      const uint32_t n = spear::patdict_train_file(f, err);
      if (!err.empty() && n == 0) {
        std::printf("dict-train skip %s err=%s\n", f, err.c_str());
        continue;
      }
      total_new += n;
      std::printf("dict-train ok file=%s new_windows~=%u\n", f, n);
    }
    std::printf("dict-train staged new_windows_approx=%u\n", total_new);
    if (do_commit) {
      if (!spear::patdict_commit(1024) || !spear::patdict_save()) {
        std::printf("dict-train --commit FAIL err=%s\n", spear::patdict_global().error.c_str());
        return 1;
      }
      std::printf("dict-train --commit+save ok entries=%zu gen=%u\n",
                  spear::patdict_global().entries.size(), spear::patdict_global().generation);
    } else {
      std::puts("dict-train note: staging persisted; run: spear dict-commit");
    }
    std::fputs(spear::patdict_status_text().c_str(), stdout);
    return 0;
  }
  if (cmd == "dict-verify") {
    if (!spear::patdict_load()) {
      std::printf("dict-verify FAIL err=%s\n", spear::patdict_global().error.c_str());
      return 1;
    }
    // re-seal check already in load
    std::printf("dict-verify ok sealed gen=%u entries=%zu\n", spear::patdict_global().generation,
                spear::patdict_global().entries.size());
    return 0;
  }
  if (cmd == "storage-status" || cmd == "kilroy-status") {
    std::fputs(spear::field_native_status_text().c_str(), stdout);
    return 0;
  }
  if (cmd == "kilroy-ensure") {
    const bool force = (argc >= 3 && std::strcmp(argv[2], "--force") == 0);
    auto k = spear::kilroy_field_native_ensure(force);
    std::fputs(k.summary.c_str(), stdout);
    return k.ok ? 0 : 1;
  }
  if (cmd == "fieldmem-status") {
    const char* which = argc >= 3 ? argv[2] : "all";
    if (std::strcmp(which, "host") == 0)
      std::fputs(spear::fieldmem_status_text(spear::FieldMemKind::HostRam).c_str(), stdout);
    else if (std::strcmp(which, "gpu") == 0)
      std::fputs(spear::fieldmem_status_text(spear::FieldMemKind::GpuVram).c_str(), stdout);
    else
      std::fputs(spear::fieldmem_all_status_text().c_str(), stdout);
    return 0;
  }
  if (cmd == "fieldmem-ensure" || cmd == "fieldmem-force") {
    const bool force = (cmd == "fieldmem-force");
    const char* which = argc >= 3 ? argv[2] : "all";
    int rc = 0;
    auto run = [&](spear::FieldMemKind k) {
      auto p = spear::fieldmem_ensure(k, 0, force);
      std::printf("ok=%s kind=%s pool=%llu field_logical=%llu expand_x1000=%u err=%s\n",
                  p.ok ? "true" : "false",
                  k == spear::FieldMemKind::GpuVram ? "gpu_vram" : "host_dram",
                  (unsigned long long)p.physical_pool, (unsigned long long)p.address_space,
                  p.expand_x1000, p.error.empty() ? "-" : p.error.c_str());
      if (!p.ok) rc = 1;
    };
    if (std::strcmp(which, "host") == 0)
      run(spear::FieldMemKind::HostRam);
    else if (std::strcmp(which, "gpu") == 0)
      run(spear::FieldMemKind::GpuVram);
    else {
      run(spear::FieldMemKind::HostRam);
      run(spear::FieldMemKind::GpuVram);
    }
    return rc;
  }
  // legacy aliases
  if (cmd == "fieldram-status" || cmd == "fieldram-probe") {
    std::fputs(spear::fieldmem_status_text(spear::FieldMemKind::HostRam).c_str(), stdout);
    return 0;
  }
  if (cmd == "fieldram-ensure" || cmd == "fieldram-force") {
    auto p = spear::fieldmem_ensure(spear::FieldMemKind::HostRam, 0, cmd == "fieldram-force");
    std::printf("ok=%s pool=%llu field_logical=%llu expand_x1000=%u\n", p.ok ? "true" : "false",
                (unsigned long long)p.physical_pool, (unsigned long long)p.address_space,
                p.expand_x1000);
    return p.ok ? 0 : 1;
  }
  if (cmd == "elevate-status" || cmd == "help") {
    if (cmd == "help") usage();
    std::puts(spear::elevate_status_line().c_str());
    return 0;
  }
  if (cmd == "chip-status") {
    std::fputs(spear::chip_status_text().c_str(), stdout);
    return 0;
  }
  if (cmd == "chip-bench") {
    spear::chip_reset();
    const double ops = spear::chip_bench_ops_per_sec(96);
    const auto st = spear::chip_status();
    std::printf("chip-bench ok path=%s ops_per_sec=%.0f ops_retired=%llu last_fold=%.6f\n",
                st.path, ops, (unsigned long long)st.ops_retired, st.last_fold);
    std::vector<uint8_t> wave(spear::kFrameBytes);
    fill_pattern(wave, "wave");
    const auto sc = spear::chip_score_frame(wave.data(), wave.size());
    std::printf("chip-score wave shannon_x1000=%u peak=%.3f flat=%.3f pick=%u fold=%.6f\n",
                sc.shannon_x1000, sc.peak_density, sc.flat_density,
                static_cast<unsigned>(sc.pick), sc.fold);
    return 0;
  }
  if (cmd == "chip-demo") {
    // Single process: score → pack → die L1 cache hit path (CLI is multi-process otherwise)
    spear::chip_reset();
    std::vector<uint8_t> wave(spear::kFrameBytes), out(spear::kFrameBytes), packed(spear::kFrameBytes + 64);
    fill_pattern(wave, "wave");
    const auto sc = spear::chip_score_frame(wave.data(), wave.size());
    uint8_t kind = 0;
    uint32_t H = 0;
    const size_t plen =
        spear::ffat_pack_frame(wave.data(), wave.size(), packed.data(), packed.size(), kind, H);
    spear::chip_cache_put(42, wave.data(), wave.size(), 0xC0FFEEULL);
    const bool miss = !spear::chip_cache_get(99, out.data(), out.size());
    const bool hit = spear::chip_cache_get(42, out.data(), out.size());
    const auto st = spear::chip_status();
    std::printf(
        "chip-demo ok pick=%u kind=%u packed=%zu shannon_x1000=%u peak=%.3f "
        "cache_miss=%s cache_hit=%s hits=%llu miss=%llu ops=%llu fold=%.6f\n",
        static_cast<unsigned>(sc.pick), kind, plen, H, sc.peak_density, miss ? "yes" : "no",
        hit ? "yes" : "no", (unsigned long long)st.cache_hits, (unsigned long long)st.cache_misses,
        (unsigned long long)st.ops_retired, sc.fold);
    std::printf("  note: die L1 is process-local; long-running Spear daemon keeps hits warm\n");
    return hit && miss ? 0 : 1;
  }

  if (cmd == "field1-status") {
    if (int e = need_elev(cmd)) return e;
    std::fputs(spear::field1_status_text().c_str(), stdout);
    return 0;
  }
  if (cmd == "field1-backup") {
    if (int e = need_elev(cmd)) return e;
    std::string err;
    const int rc = spear::field1_backup(err);
    if (rc != 0) {
      std::fprintf(stderr, "field1-backup failed: %s\n", err.c_str());
      return rc;
    }
    std::puts("field1-backup OK → Hostess field1-backup-<ts>/");
    return 0;
  }
  if (cmd == "field1-claim") {
    if (int e = need_elev(cmd)) return e;
    bool force = false;
    for (int i = 2; i < argc; ++i) {
      if (std::strcmp(argv[i], "--force-no-backup") == 0) force = true;
    }
    std::string err;
    const int rc = spear::field1_claim(force, err);
    if (rc != 0) {
      std::fprintf(stderr, "field1-claim failed: %s\n", err.c_str());
      return rc;
    }
    std::puts("field1-claim OK — Field1 is FFAT v4 entropy (guaranteed=pool, no fake ×N)");
    return 0;
  }

  if (cmd == "ffat-probe" || cmd == "ffat-ensure" || cmd == "ffat-force") {
    if (argc < 3) {
      usage();
      return 2;
    }
    if (int e = need_elev(cmd)) return e;
    const std::string path = argv[2];
    if (cmd == "ffat-probe") {
      auto r = spear::ffat_probe(path);
      r.ok = r.ok || r.had_ffat || r.legacy_fake_factor;
      return print_ffat(r);
    }
    return print_ffat(spear::ffat_ensure(path, cmd == "ffat-force"));
  }

  if (cmd == "ffat-put") {
    if (argc < 5) {
      usage();
      return 2;
    }
    if (int e = need_elev(cmd)) return e;
    const std::string path = argv[2];
    const uint32_t id = static_cast<uint32_t>(std::strtoul(argv[3], nullptr, 0));
    std::vector<uint8_t> frame;
    fill_pattern(frame, argv[4]);
    const uint32_t H = spear::ffat_shannon_x1000(frame.data(), frame.size());
    std::string err;
    if (!spear::ffat_put_frame(path, id, frame.data(), frame.size(), err)) {
      std::fprintf(stderr, "ffat-put failed: %s\n", err.c_str());
      return 1;
    }
    std::printf("ffat-put ok id=%u shannon_x1000=%u mode=%s\n", id, H, argv[4]);
    return 0;
  }

  if (cmd == "ffat-get") {
    if (argc < 4) {
      usage();
      return 2;
    }
    if (int e = need_elev(cmd)) return e;
    const std::string path = argv[2];
    const uint32_t id = static_cast<uint32_t>(std::strtoul(argv[3], nullptr, 0));
    std::vector<uint8_t> frame(spear::kFrameBytes);
    std::string err;
    if (!spear::ffat_get_frame(path, id, frame.data(), frame.size(), err)) {
      std::fprintf(stderr, "ffat-get failed: %s\n", err.c_str());
      return 1;
    }
    const uint32_t H = spear::ffat_shannon_x1000(frame.data(), frame.size());
    // print first 32 bytes hex + shannon
    std::printf("ffat-get ok id=%u shannon_x1000=%u head=", id, H);
    for (int i = 0; i < 32; ++i) std::printf("%02x", frame[static_cast<size_t>(i)]);
    std::printf("\n");
    return 0;
  }

  usage();
  return 2;
}

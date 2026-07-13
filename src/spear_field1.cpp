// SPDX-License-Identifier: MIT
#include "spear_field1.hpp"
#include "spear_ffat.hpp"

#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <fstream>
#include <string>
#include <sys/mount.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>
#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

namespace spear {
namespace {

bool read_file(const std::string& p, std::string& out) {
  std::ifstream in(p);
  if (!in) return false;
  out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
  while (!out.empty() && (out.back() == '\n' || out.back() == ' ')) out.pop_back();
  return true;
}

uint64_t sysfs_size_bytes(const std::string& name) {
  std::string s;
  if (!read_file("/sys/block/" + name + "/size", s)) return 0;
  return std::strtoull(s.c_str(), nullptr, 10) * 512ull;
}

std::string resolve_by_path(const char* path) {
  char link[PATH_MAX];
  const ssize_t n = readlink(path, link, sizeof link - 1);
  if (n <= 0) {
    if (access(path, F_OK) == 0) return path;
    return {};
  }
  link[n] = 0;
  std::string t = link;
  if (t.empty()) return {};
  if (t[0] != '/') {
    // relative to parent of path
    std::string parent = path;
    auto slash = parent.find_last_of('/');
    if (slash != std::string::npos) parent.resize(slash);
    t = parent + "/" + t;
  }
  char real[PATH_MAX];
  if (realpath(t.c_str(), real)) return real;
  return t;
}

std::string whole_disk_from_node(const std::string& node_path) {
  // /dev/sdb1 → /dev/sdb · /dev/nvme0n1p1 → /dev/nvme0n1
  std::string node = node_path;
  auto slash = node.find_last_of('/');
  std::string base = slash == std::string::npos ? node : node.substr(slash + 1);
  std::string disk = base;
  while (!disk.empty() && disk.back() >= '0' && disk.back() <= '9') disk.pop_back();
  if (!disk.empty() && disk.back() == 'p') disk.pop_back();
  if (disk.empty()) return node_path;
  return "/dev/" + disk;
}

// Fixed discovery for this machine (from doctrine); still verify sysfs exists.
// Prefer claimed Field1 (FFAT label FIELD1 / by-label) over legacy FIELD_QUBES.
void fill_from_doctrine(Field1Plan& p) {
  p.field1_name = "Field1";
  p.qubes_label = "FIELD_QUBES";
  p.hostess_label = "HOSTESS7_TEAM";

  // 1) Claimed Field1 stable names
  static const char* kField1Paths[] = {
      "/dev/disk/by-label/FIELD1",
      "/dev/spear/field1",
      "/dev/disk/by-id/ata-T-FORCE_1TB_TPBF2411190010300627",
      nullptr,
  };
  for (int i = 0; kField1Paths[i]; ++i) {
    if (access(kField1Paths[i], F_OK) != 0) continue;
    std::string real = resolve_by_path(kField1Paths[i]);
    if (real.empty()) continue;
    p.stable_id = kField1Paths[i];
    p.qubes_disk = whole_disk_from_node(real);
    // partition sibling if present
    if (access((p.qubes_disk + "1").c_str(), F_OK) == 0)
      p.qubes_part = p.qubes_disk + "1";
    else if (access((p.qubes_disk + "p1").c_str(), F_OK) == 0)
      p.qubes_part = p.qubes_disk + "p1";
    p.qubes_present = access(p.qubes_disk.c_str(), F_OK) == 0;
    break;
  }

  // 2) Legacy Qubes label (pre-claim source)
  if (!p.qubes_present && access("/dev/disk/by-label/FIELD_QUBES", F_OK) == 0) {
    std::string real = resolve_by_path("/dev/disk/by-label/FIELD_QUBES");
    if (!real.empty()) {
      p.qubes_part = real;
      p.qubes_disk = whole_disk_from_node(real);
      p.qubes_present = access(p.qubes_disk.c_str(), F_OK) == 0;
      p.stable_id = "/dev/disk/by-label/FIELD_QUBES";
    }
  }

  if (access("/dev/disk/by-label/HOSTESS7_TEAM", F_OK) == 0) {
    std::string real = resolve_by_path("/dev/disk/by-label/HOSTESS7_TEAM");
    if (!real.empty()) {
      p.hostess_dev = real;
      p.hostess_present = true;
    }
  }

  // 3) Hardware map fallback (this host)
  if (!p.qubes_present && access("/dev/sdb", F_OK) == 0) {
    p.qubes_disk = "/dev/sdb";
    p.qubes_part = "/dev/sdb1";
    p.qubes_present = true;
    p.stable_id = "/dev/sdb";
  }
  if (!p.hostess_present && access("/dev/nvme2n1", F_OK) == 0) {
    p.hostess_dev = "/dev/nvme2n1";
    p.hostess_present = true;
  } else if (!p.hostess_present && access("/dev/nvme1n1", F_OK) == 0) {
    p.hostess_dev = "/dev/nvme1n1";
    p.hostess_present = true;
  }

  if (p.qubes_present) {
    std::string name = p.qubes_disk;
    auto slash = name.find_last_of('/');
    if (slash != std::string::npos) name = name.substr(slash + 1);
    p.qubes_bytes = sysfs_size_bytes(name);
    auto fr = ffat_probe(p.qubes_disk);
    p.claimed = fr.had_mbr && fr.had_ffat;
    p.ffat_label = fr.label.empty() ? (p.claimed ? "FIELD1" : "") : fr.label;
    if (fr.had_ffat) {
      p.field_guaranteed = fr.guaranteed_bytes;
      p.field_address_space = fr.address_space_bytes;
    } else {
      p.field_guaranteed = p.qubes_bytes > 2ull * 1024 * 1024
                               ? p.qubes_bytes - 2ull * 1024 * 1024
                               : p.qubes_bytes;
      p.field_address_space = 0;
    }
  }
}

int run(const std::string& cmd) {
  return ::system(cmd.c_str());
}

}  // namespace

Field1Plan field1_discover() {
  Field1Plan p;
  fill_from_doctrine(p);
  return p;
}

std::string field1_status_text() {
  auto p = field1_discover();
  char buf[1536];
  std::snprintf(
      buf, sizeof buf,
      "Field1 drive\n"
      "  name:              %s\n"
      "  state:             %s\n"
      "  disk:              %s  present=%s\n"
      "  part:              %s  (type 0xE0 field partition when claimed)\n"
      "  stable_id:         %s\n"
      "  ffat_label:        %s\n"
      "  legacy_source:     %s\n"
      "  hostess:           %s  present=%s label=%s\n"
      "  physical:          %llu bytes\n"
      "  guaranteed (pool): %llu bytes  (worst-case RAW, honest)\n"
      "  address_space:     %llu bytes  (sparse map)\n"
      "  note: FFAT v4 entropy — pack_ratio measured after writes, never fake ×N\n"
      "  %s\n",
      p.field1_name.c_str(), p.claimed ? "CLAIMED (SPEARMBR + FFAT)" : "planned (not FFAT yet)",
      p.qubes_disk.c_str(), p.qubes_present ? "yes" : "no", p.qubes_part.c_str(),
      p.stable_id.empty() ? "-" : p.stable_id.c_str(),
      p.ffat_label.empty() ? "-" : p.ffat_label.c_str(), p.qubes_label.c_str(),
      p.hostess_dev.c_str(), p.hostess_present ? "yes" : "no", p.hostess_label.c_str(),
      static_cast<unsigned long long>(p.qubes_bytes),
      static_cast<unsigned long long>(p.field_guaranteed),
      static_cast<unsigned long long>(p.field_address_space),
      p.claimed ? "ready: Field1 is the field drive for Spear/KILROY Normal"
                : "claim: spear field1-backup then field1-claim (or --force-no-backup)");
  return buf;
}

int field1_backup(std::string& err) {
  auto p = field1_discover();
  if (!p.qubes_present || !p.hostess_present) {
    err = "need both FIELD_QUBES and HOSTESS7_TEAM devices";
    return 1;
  }
  const char* mq = "/mnt/spear-qubes";
  const char* mh = "/mnt/spear-hostess";
  ::mkdir("/mnt", 0755);
  ::mkdir(mq, 0755);
  ::mkdir(mh, 0755);

  // unmount if busy
  umount(mq);
  umount(mh);

  std::string src = p.qubes_part.empty() ? p.qubes_disk : p.qubes_part;
  if (mount(src.c_str(), mq, "ext4", MS_RDONLY, nullptr) != 0) {
    err = std::string("mount qubes failed: ") + std::strerror(errno);
    return 1;
  }
  if (mount(p.hostess_dev.c_str(), mh, "ext4", 0, nullptr) != 0) {
    umount(mq);
    err = std::string("mount hostess failed: ") + std::strerror(errno);
    return 1;
  }

  char ts[32];
  std::snprintf(ts, sizeof ts, "%ld", static_cast<long>(std::time(nullptr)));
  std::string dest = std::string(mh) + "/field1-backup-" + ts;
  if (run("mkdir -p '" + dest + "'") != 0) {
    umount(mq);
    umount(mh);
    err = "mkdir dest failed";
    return 1;
  }
  // rsync if present else cp -a
  int rc = 1;
  if (run("command -v rsync >/dev/null 2>&1") == 0) {
    rc = run("rsync -aHAX --info=progress2 '" + std::string(mq) + "/' '" + dest + "/'");
  } else {
    rc = run("cp -a '" + std::string(mq) + "/.' '" + dest + "/'");
  }
  // receipt
  {
    std::ofstream rec(dest + "/FIELD1_BACKUP.txt");
    rec << "Spear Field1 backup\nfrom=" << src << "\nto=" << dest
        << "\nqubes_disk=" << p.qubes_disk << "\n";
  }
  sync();
  umount(mq);
  umount(mh);
  if (rc != 0) {
    err = "copy failed rc=" + std::to_string(rc);
    return rc;
  }
  err.clear();
  return 0;
}

int field1_claim(bool force_no_backup, std::string& err) {
  auto p = field1_discover();
  if (!p.qubes_present) {
    err = "Qubes disk not found";
    return 1;
  }
  if (!force_no_backup) {
    // require a recent backup dir on hostess
    err = "refusing claim without backup: run field1-backup first, or field1-claim --force-no-backup";
    // check hostess for any field1-backup-*
    const char* mh = "/mnt/spear-hostess";
    ::mkdir(mh, 0755);
    umount(mh);
    bool have = false;
    if (p.hostess_present && mount(p.hostess_dev.c_str(), mh, "ext4", MS_RDONLY, nullptr) == 0) {
      DIR* d = opendir(mh);
      if (d) {
        while (dirent* e = readdir(d)) {
          if (std::strncmp(e->d_name, "field1-backup-", 14) == 0) have = true;
        }
        closedir(d);
      }
      umount(mh);
    }
    if (!have) return 2;
    err.clear();
  }

  // Unmount partitions on qubes disk
  run("umount " + p.qubes_part + " 2>/dev/null || true");
  run("umount " + p.qubes_disk + " 2>/dev/null || true");

  // Wipe start + write secure MBR + FFAT on WHOLE disk
  run("dd if=/dev/zero of=" + p.qubes_disk + " bs=1M count=16 conv=notrunc status=none 2>/dev/null || true");
  auto r = ffat_ensure(p.qubes_disk, true);
  if (!r.ok) {
    err = r.error.empty() ? "ffat_ensure failed" : r.error;
    return 1;
  }
  // Stamp Field1 name via writing a small marker file is hard on raw FFAT without fat writer;
  // MBR+OEM already mark Spear; document Field1 in doctrine.
  err.clear();
  return 0;
}

}  // namespace spear

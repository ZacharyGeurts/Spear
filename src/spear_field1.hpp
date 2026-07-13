// SPDX-License-Identifier: MIT
// Field1 — whole Qubes drive (FIELD_QUBES / sdb) as Field Drive.
// Optional backup to Hostess drive (HOSTESS7_TEAM) before claim.
#pragma once
#include <cstdint>
#include <string>

namespace spear {

struct Field1Plan {
  std::string qubes_disk;      // whole-disk node (e.g. /dev/sdb)
  std::string qubes_part;      // partition if any (e.g. /dev/sdb1)
  std::string qubes_label;     // legacy source: FIELD_QUBES
  std::string hostess_dev;     // e.g. /dev/nvme2n1
  std::string hostess_label;   // HOSTESS7_TEAM
  std::string field1_name;     // Field1
  std::string stable_id;       // by-id / by-label path preferred
  std::string ffat_label;      // on-disk FFAT label (FIELD1 when claimed)
  uint64_t qubes_bytes = 0;
  uint64_t field_guaranteed = 0;  // honest worst-case pool (from FFAT probe)
  uint64_t field_address_space = 0;
  bool qubes_present = false;
  bool hostess_present = false;
  bool claimed = false;  // SPEARMBR + FFAT present on whole disk
};

Field1Plan field1_discover();
std::string field1_status_text();

// Mount Qubes RO, Hostess RW; rsync tree to Hostess/field1-backup-<ts>/
// Returns 0 on success.
int field1_backup(std::string& err);

// Wipe whole Qubes disk and write secure MBR + FFAT as Field1.
// Requires prior backup unless force_no_backup.
int field1_claim(bool force_no_backup, std::string& err);

}  // namespace spear

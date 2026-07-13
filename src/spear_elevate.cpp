// SPDX-License-Identifier: MIT
#include "spear_elevate.hpp"

#include <cstdio>
#include <cstring>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace spear {
namespace {

bool needs_priv(const std::string& cmd) {
  // Privileged: mutate disks / mounts / frame I/O on block devices.
  // fieldram-ensure on /dev/shm is user-writable; no elev required.
  return cmd == "ffat-ensure" || cmd == "ffat-force" || cmd == "ffat-put" || cmd == "ffat-get" ||
         cmd == "field1-backup" || cmd == "field1-claim";
}

// Regular image files (not block devices) may be formatted as the calling user.
// Block devices and field1 ops still require setuid euid=0.
bool path_is_regular_file(const std::string& path) {
  if (path.empty()) return false;
  struct stat st {};
  if (stat(path.c_str(), &st) != 0) {
    // Allow create of missing image path only if parent is writable — treat as file.
    return path.find("/dev/") != 0;
  }
  return S_ISREG(st.st_mode);
}

bool cmd_targets_image_file(const std::string& cmd, int argc, char** argv) {
  if (!(cmd == "ffat-ensure" || cmd == "ffat-force" || cmd == "ffat-put" || cmd == "ffat-get" ||
        cmd == "ffat-probe"))
    return false;
  if (argc < 3) return false;
  return path_is_regular_file(argv[2]);
}

// Stash argv from main via set_cli_args — simple globals for elev decision.
int g_argc = 0;
char** g_argv = nullptr;

}  // namespace

void elevate_set_cli(int argc, char** argv) {
  g_argc = argc;
  g_argv = argv;
}

bool elevated() { return ::geteuid() == 0; }
bool real_root() { return ::getuid() == 0; }

bool elevate_allowed(const std::string& cmd) {
  return needs_priv(cmd) || cmd == "help" || cmd == "version" || cmd == "elevate-status" ||
         cmd == "ffat-probe" || cmd == "field1-status" || cmd == "chip-status" ||
         cmd == "chip-bench" || cmd == "chip-demo" || cmd == "fieldram-status" ||
         cmd == "fieldram-probe" || cmd == "fieldram-ensure" || cmd == "fieldram-force" ||
         cmd == "storage-status" || cmd == "fieldmem-status" || cmd == "fieldmem-ensure" ||
         cmd == "fieldmem-force" || cmd == "kilroy-status" || cmd == "kilroy-ensure" ||
         cmd == "dict-status" || cmd == "dict-train" || cmd == "dict-commit" ||
         cmd == "dict-save" || cmd == "dict-load" || cmd == "dict-clear" || cmd == "dict-verify";
}

bool autoelevate_begin(const std::string& cmd, std::string& err) {
  if (!elevate_allowed(cmd)) {
    err = "command not on autoelevate allowlist (no polkit, no shell escape)";
    return false;
  }
  if (!needs_priv(cmd)) return true;

  // Image-file lab path: no setuid required for regular files under user control.
  if (g_argv && cmd_targets_image_file(cmd, g_argc, g_argv)) return true;

  if (elevated()) return true;

  err =
      "autoelevate unavailable: install Spear with setuid spear "
      "(chmod u+s /usr/local/bin/spear) or file caps — no polkit, no sudo prompt";
  return false;
}

void autoelevate_drop() {
  if (real_root()) return;
  // Drop euid back to ruid when possible
  if (elevated() && getuid() != 0) {
    if (seteuid(getuid()) != 0) {
      // ignore
    }
  }
}

std::string elevate_status_line() {
  char buf[256];
  std::snprintf(buf, sizeof buf,
                "uid=%d euid=%d elevated=%s model=autoelevate(no-polkit) allowlist=field-disk-ops",
                static_cast<int>(getuid()), static_cast<int>(geteuid()),
                elevated() ? "yes" : "no");
  return buf;
}

}  // namespace spear

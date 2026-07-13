// SPDX-License-Identifier: MIT
// Spear autoelevate — no polkit, no interactive root shell.
// Install binary setuid-root (or CAP_SYS_ADMIN+DAC) so field ops elevate in-process.
#pragma once
#include <string>

namespace spear {

// True if effective uid is 0 (setuid or already root).
bool elevated();

// True if real uid is 0.
bool real_root();

// Pass argv so elev can allow regular image files without setuid.
void elevate_set_cli(int argc, char** argv);

// Return true if this subcommand is on the elevate allowlist.
bool elevate_allowed(const std::string& cmd);

// If action needs privilege and we are setuid, keep euid=0 for the call.
// If not elevated and need_priv, return false (caller prints install hint).
// Regular files (images) are allowed without elev for ffat-*.
bool autoelevate_begin(const std::string& cmd, std::string& err);

// Drop elev after privileged work (optional; process may exit).
void autoelevate_drop();

// Human status line.
std::string elevate_status_line();

}  // namespace spear

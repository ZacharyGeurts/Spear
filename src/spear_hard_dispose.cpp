// SPDX-License-Identifier: MIT
// spear-hard-dispose — C++ hard reaper. FIELD UDP WAR BLASTERS.
// Hunts: Hotdog hallway terrorist kit + every copilot class.
// Multi-signal only. No fork. No system. No SIGTERM.
#include "spear_common.hpp"

#include <cstdio>
#include <string>
#include <vector>

int main(int argc, char** argv) {
  bool dry = false;
  const char* log_path = nullptr;
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--dry-run") == 0)
      dry = true;
    else if (std::strcmp(argv[i], "--log") == 0 && i + 1 < argc)
      log_path = argv[++i];
    else if (std::strcmp(argv[i], "-h") == 0 || std::strcmp(argv[i], "--help") == 0) {
      std::fprintf(stderr,
                   "%s — hard dispose (C++)\n"
                   "  kills: hotdog hallway kit + all copilot\n"
                   "  FIELD UDP WAR BLASTERS · multi-signal · no soft TERM\n"
                   "  %s [--dry-run] [--log PATH]\n",
                   argv[0], argv[0]);
      return 0;
    }
  }

  spear::mkdir_p(spear::state_dir());
  std::vector<spear::Hit> hits;
  spear::hunt_threats(hits);
  int killed = 0;
  const std::string ts = spear::now_z();
  for (const auto& h : hits) {
    std::printf("%s %s pid=%d score=%d comm=%s dry=%d\n", ts.c_str(), h.kind,
                static_cast<int>(h.pid), h.score, h.comm.c_str(), dry ? 1 : 0);
    if (log_path) {
      char line[256];
      std::snprintf(line, sizeof(line), "%s %s pid=%d score=%d comm=%s\n", ts.c_str(), h.kind,
                    static_cast<int>(h.pid), h.score, h.comm.c_str());
      spear::append_file(log_path, line);
    }
    if (!dry) {
      if (::kill(h.pid, spear::kSigHard) == 0 || errno == ESRCH) ++killed;
    }
  }
  std::printf("%s DONE hits=%zu killed=%d hotdog+copilot FIELD_UDP_WAR_BLASTERS\n", ts.c_str(), hits.size(),
              killed);
  return 0;
}

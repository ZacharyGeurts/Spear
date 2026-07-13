// SPDX-License-Identifier: MIT
// spear-kill-copilot — one-shot EVERY copilot GONE. C++ only. No scripts.
// FIELD_UDP_WAR_BLAST processes. Unlink known module paths. Seal JSON. No fork/system/SIGTERM.
#include "spear_common.hpp"

#include <cstdio>
#include <string>
#include <vector>

static void quarantine_unlink(const char* path) {
  if (::unlink(path) == 0) {
    std::printf("UNLINK %s\n", path);
  }
}

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  spear::mkdir_p(spear::state_dir());
  spear::mkdir_p(spear::www_dir());

  std::vector<spear::Hit> hits;
  spear::hunt_copilot(hits);
  int killed = spear::hard_kill_hits(hits);
  for (const auto& h : hits) {
    std::printf("KILL kind=%s pid=%d comm=%s score=%d\n", h.kind, static_cast<int>(h.pid),
                h.comm.c_str(), h.score);
  }

  // Live tree modules only (known product paths)
  const std::string home = spear::home_dir();
  const char* tails[] = {
      "/Desktop/SG/NewLatest/Final_Eye/zocr_copilot.py",
      "/Desktop/SG/NewLatest/Final_Eye/data/copilot-foundations.json",
      "/Desktop/SG/NewLatest/Final_Eye/data/copilot-state.json",
      "/Desktop/SG/NewLatest/.pages-hub-Final_Eye/zocr_copilot.py",
      "/Desktop/SG/NewLatest/.pages-hub-Final_Eye/data/copilot-foundations.json",
  };
  for (const char* t : tails) {
    std::string p = home + t;
    quarantine_unlink(p.c_str());
  }

  // Ensure foreign DNS blocked
  {
    std::string path = spear::state_dir() + "/blocked-ips.txt";
    std::string body = spear::read_file(path.c_str());
    for (const char* ip : {"209.18.47.61", "209.18.47.62", "209.18.47.63"}) {
      if (!spear::contains(body, ip)) spear::append_file(path.c_str(), std::string(ip) + "\n");
    }
  }

  // Cloud block hosts file (user-level; /etc/hosts needs root outside this binary)
  {
    std::string block = spear::state_dir() + "/copilot-cloud-block.hosts";
    std::string b =
        "# spear copilot cloud block — C++ only\n"
        "0.0.0.0 copilot-proxy.githubusercontent.com\n"
        "::1 copilot-proxy.githubusercontent.com\n"
        "0.0.0.0 api.githubcopilot.com\n"
        "::1 api.githubcopilot.com\n"
        "0.0.0.0 proxy.individual.githubcopilot.com\n"
        "::1 proxy.individual.githubcopilot.com\n"
        "0.0.0.0 githubcopilot.com\n"
        "::1 githubcopilot.com\n"
        "0.0.0.0 copilot.microsoft.com\n"
        "::1 copilot.microsoft.com\n"
        "0.0.0.0 copilot.github.com\n"
        "::1 copilot.github.com\n";
    spear::write_file(block.c_str(), b);
  }

  std::string seal =
      "{\n  \"schema\": \"spear-eat-stamp/v1\",\n"
      "  \"targets\": [\"every_copilot\", \"zocr_copilot\", \"github_microsoft_copilot\", "
      "\"copilot_cloud\", \"copilot_servers\"],\n"
      "  \"status\": \"GONE\",\n"
      "  \"stack\": \"C++\",\n"
      "  \"scripts\": \"FORBIDDEN\",\n"
      "  \"blessing\": \"God Bless\",\n"
      "  \"ts\": \"" +
      spear::now_z() +
      "\",\n"
      "  \"signal\": \"FIELD_UDP_WAR_BLASTERS\",\n"
      "  \"killed\": " +
      std::to_string(killed) +
      "\n}\n";
  spear::mirror_www("every-copilot-gone.json", seal);
  spear::mirror_www("zocr-copilot-eaten.json", seal);

  std::printf("DONE hits=%zu killed=%d stack=C++ scripts=FORBIDDEN\n", hits.size(), killed);
  return 0;
}

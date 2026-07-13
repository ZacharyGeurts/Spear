// SPDX-License-Identifier: MIT
// spear-copilot-monitor — ACTIVE HUNT for every copilot process.
// C++ only. SIGKILL only. No fork. No system/popen. No SIGTERM.
// Hunt cadence uses clock_nanosleep (monitor interval — not soft dispose theater).
//
// Build: g++ -O2 -Wall -Wextra -std=c++17 -o spear-copilot-monitor spear_copilot_monitor.cpp
#include <dirent.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <cerrno>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>

namespace {

constexpr int kSigHard = 9;
volatile sig_atomic_t g_stop = 0;

static void on_signal(int) { g_stop = 1; }

static bool is_digits(const char* s) {
  if (!s || !*s) return false;
  for (const char* p = s; *p; ++p)
    if (*p < '0' || *p > '9') return false;
  return true;
}

static std::string read_file_raw(const char* path, size_t maxn = 8192) {
  int fd = ::open(path, O_RDONLY | O_CLOEXEC);
  if (fd < 0) return {};
  std::string out;
  out.resize(maxn);
  ssize_t n = ::read(fd, out.data(), maxn);
  ::close(fd);
  if (n <= 0) return {};
  out.resize(static_cast<size_t>(n));
  return out;
}

static std::string read_cmdline(pid_t pid) {
  char path[64];
  std::snprintf(path, sizeof(path), "/proc/%d/cmdline", pid);
  std::string raw = read_file_raw(path, 4096);
  for (char& c : raw)
    if (c == '\0') c = ' ';
  return raw;
}

static std::string read_comm(pid_t pid) {
  char path[64];
  std::snprintf(path, sizeof(path), "/proc/%d/comm", pid);
  std::string s = read_file_raw(path, 256);
  while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
  return s;
}

static std::string read_exe(pid_t pid) {
  char path[64], buf[512];
  std::snprintf(path, sizeof(path), "/proc/%d/exe", pid);
  ssize_t n = ::readlink(path, buf, sizeof(buf) - 1);
  if (n < 0) return {};
  buf[n] = '\0';
  return std::string(buf);
}

static void tolower_inplace(std::string& s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
}

static bool contains(const std::string& hay, const char* needle) {
  return hay.find(needle) != std::string::npos;
}

static bool is_sacred(const std::string& blob_l, const std::string& exe_l,
                      const std::string& comm_l) {
  if (contains(exe_l, "spear") || contains(exe_l, "hostess") || contains(exe_l, "nexus"))
    return true;
  if (contains(blob_l, "spear-hard-dispose") || contains(blob_l, "spear-kill-copilot") ||
      contains(blob_l, "spear-copilot-monitor") || contains(blob_l, "spear-copilot-threat") ||
      contains(blob_l, "spear-swallow") || contains(blob_l, "spear-swallows") ||
      contains(blob_l, "spear-eats") || contains(blob_l, "spear-planet") ||
      contains(blob_l, "spear-fleet") || contains(blob_l, "copilot-purge") ||
      contains(blob_l, "copilot-kill.log") || contains(blob_l, "every-copilot-gone") ||
      contains(blob_l, "copilot-cloud-block") || contains(blob_l, "copilot-threat-monitor") ||
      contains(blob_l, "dogshit-") || contains(blob_l, "ironclad-copilot") ||
      contains(blob_l, "ironclad-zocr") || contains(blob_l, "pwnership/kills"))
    return true;
  if (comm_l == "grok" || comm_l == "grokz" || comm_l == "spear") return true;
  return false;
}

static bool is_leave_alone_ai(const std::string& blob_l) {
  if (contains(blob_l, "zocr_copilot") || contains(blob_l, "zocr-copilot") ||
      contains(blob_l, "zocrcopilot") || contains(blob_l, "github.copilot") ||
      contains(blob_l, "copilot-language") || contains(blob_l, "copilot-agent"))
    return false;
  if (contains(blob_l, "chatgpt") || contains(blob_l, "openai.com") ||
      contains(blob_l, "openai/") || contains(blob_l, "gpt-4") || contains(blob_l, "gpt4"))
    return true;
  return false;
}

static int score_copilot(const std::string& comm_l, const std::string& exe_l,
                         const std::string& cmd_l, const char** kind_out) {
  const std::string blob = comm_l + " " + exe_l + " " + cmd_l;
  int s = 0;
  const char* kind = "copilot";
  if (contains(blob, "copilot-purge") || contains(blob, "copilot-kill.log") ||
      contains(blob, "every-copilot-gone") || contains(blob, "copilot-cloud-block") ||
      contains(blob, "copilot-threat-monitor") || contains(blob, "spear-kill-copilot") ||
      contains(blob, "dogshit-copilot") || contains(blob, "dogshit-zocr")) {
    if (kind_out) *kind_out = kind;
    return 0;
  }
  if (contains(blob, "zocr_copilot") || contains(blob, "zocr-copilot") ||
      contains(blob, "zocrcopilot")) {
    s += 5;
    kind = "zocr_copilot";
  }
  if (contains(blob, "github.copilot") || contains(blob, "github-copilot")) {
    s += 4;
    kind = "github_copilot";
  }
  if (contains(blob, "copilot-language-server") || contains(blob, "copilot-agent") ||
      contains(blob, "copilot-chat")) {
    s += 4;
    kind = "copilot_server";
  }
  if (contains(blob, "microsoft.copilot") || contains(blob, "copilot.exe") ||
      contains(blob, "copilot.microsoft")) {
    s += 4;
    kind = "microsoft_copilot";
  }
  if (contains(blob, "api.githubcopilot") || contains(blob, "githubcopilot.com") ||
      contains(blob, "copilot-proxy")) {
    s += 4;
    kind = "copilot_cloud";
  }
  if (contains(blob, "@github/copilot") || contains(blob, "gh-copilot")) {
    s += 3;
    kind = "copilot_cli";
  }
  if (comm_l == "copilot" || comm_l == "zocr_copilot") s += 3;
  const auto slash = exe_l.find_last_of('/');
  const std::string base = (slash == std::string::npos) ? exe_l : exe_l.substr(slash + 1);
  if (base.rfind("copilot", 0) == 0 || contains(base, "zocr_copilot") ||
      contains(base, "github.copilot"))
    s += 3;
  if (s < 3 && (comm_l == "copilot" || contains(exe_l, "/copilot") || contains(base, "copilot"))) {
    s += 3;
    kind = "copilot_generic";
  }
  if (kind_out) *kind_out = kind;
  return s;
}

struct Hit {
  pid_t pid;
  int score;
  const char* kind;
  std::string comm;
};

static int hunt(std::vector<Hit>& hits) {
  hits.clear();
  const pid_t self = ::getpid();
  const pid_t parent = ::getppid();
  DIR* d = ::opendir("/proc");
  if (!d) return -1;
  while (dirent* ent = ::readdir(d)) {
    if (!is_digits(ent->d_name)) continue;
    const pid_t pid = static_cast<pid_t>(std::atoi(ent->d_name));
    if (pid <= 1 || pid == self || pid == parent) continue;
    std::string comm = read_comm(pid);
    std::string exe = read_exe(pid);
    std::string cmd = read_cmdline(pid);
    std::string comm_l = comm, exe_l = exe, cmd_l = cmd;
    tolower_inplace(comm_l);
    tolower_inplace(exe_l);
    tolower_inplace(cmd_l);
    const std::string blob_l = comm_l + " " + exe_l + " " + cmd_l;
    if (is_sacred(blob_l, exe_l, comm_l)) continue;
    if (is_leave_alone_ai(blob_l)) continue;
    const char* kind = "copilot";
    const int sc = score_copilot(comm_l, exe_l, cmd_l, &kind);
    if (sc >= 3) hits.push_back(Hit{pid, sc, kind, comm});
  }
  ::closedir(d);
  return static_cast<int>(hits.size());
}

static int hard_kill_all(const std::vector<Hit>& hits) {
  int n = 0;
  for (const Hit& h : hits) {
    if (::kill(h.pid, kSigHard) == 0 || errno == ESRCH) ++n;
  }
  return n;
}

static void write_status(const char* path, int cycle, int seen, int killed, int lifetime_kills,
                         const std::vector<Hit>& last_hits, bool dry) {
  if (!path || !path[0]) return;
  char tbuf[64];
  {
    std::time_t t = std::time(nullptr);
    std::tm gmt{};
    gmtime_r(&t, &gmt);
    std::snprintf(tbuf, sizeof(tbuf), "%04d-%02d-%02dT%02d:%02d:%02dZ", gmt.tm_year + 1900,
                  gmt.tm_mon + 1, gmt.tm_mday, gmt.tm_hour, gmt.tm_min, gmt.tm_sec);
  }
  char body[8192];
  int off = 0;
  off += std::snprintf(
      body + off, sizeof(body) - static_cast<size_t>(off),
      "{\n"
      "  \"schema\": \"spear-copilot-monitor/v1\",\n"
      "  \"mode\": \"ACTIVE_HUNT\",\n"
      "  \"demo\": false,\n"
      "  \"ts\": \"%s\",\n"
      "  \"cycle\": %d,\n"
      "  \"dry_run\": %s,\n"
      "  \"signal\": \"SIGKILL_ONLY\",\n"
      "  \"forbid\": [\"SIGTERM\", \"fork\", \"system\", \"popen\", \"injection\"],\n"
      "  \"stack\": \"C++\",\n"
      "  \"seen_this_cycle\": %d,\n"
      "  \"killed_this_cycle\": %d,\n"
      "  \"lifetime_kills\": %d,\n"
      "  \"status\": \"%s\",\n"
      "  \"policy\": \"every_copilot_gone · multi_signal · leave GPT-4/OpenAI/Grok\",\n"
      "  \"hits\": [",
      tbuf, cycle, dry ? "true" : "false", seen, killed, lifetime_kills,
      (seen == 0) ? "CLEAR" : "HUNTING");
  for (size_t i = 0; i < last_hits.size() && i < 32; ++i) {
    const Hit& h = last_hits[i];
    off += std::snprintf(body + off, sizeof(body) - static_cast<size_t>(off),
                         "%s\n    {\"pid\":%d,\"score\":%d,\"kind\":\"%s\",\"comm\":\"%s\"}",
                         (i ? "," : ""), static_cast<int>(h.pid), h.score, h.kind, h.comm.c_str());
  }
  off += std::snprintf(body + off, sizeof(body) - static_cast<size_t>(off), "\n  ]\n}\n");
  int fd = ::open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
  if (fd < 0) return;
  ssize_t w = ::write(fd, body, static_cast<size_t>(off));
  (void)w;
  ::close(fd);
}

static void usage(const char* a0) {
  std::fprintf(stderr,
               "%s — ACTIVE HUNT every copilot (C++)\n"
               "  SIGKILL only · no fork · no SIGTERM\n"
               "  %s [--interval-ms N] [--status PATH] [--log PATH] [--once] [--dry-run]\n",
               a0, a0);
}

}  // namespace

int main(int argc, char** argv) {
  int interval_ms = 3000;
  const char* status_path = nullptr;
  const char* log_path = nullptr;
  bool once = false;
  bool dry = false;
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--interval-ms") == 0 && i + 1 < argc) {
      interval_ms = std::atoi(argv[++i]);
      if (interval_ms < 200) interval_ms = 200;
    } else if (std::strcmp(argv[i], "--status") == 0 && i + 1 < argc) {
      status_path = argv[++i];
    } else if (std::strcmp(argv[i], "--log") == 0 && i + 1 < argc) {
      log_path = argv[++i];
    } else if (std::strcmp(argv[i], "--once") == 0) {
      once = true;
    } else if (std::strcmp(argv[i], "--dry-run") == 0) {
      dry = true;
    } else if (std::strcmp(argv[i], "-h") == 0 || std::strcmp(argv[i], "--help") == 0) {
      usage(argv[0]);
      return 0;
    } else {
      usage(argv[0]);
      return 2;
    }
  }

  // Only handle INT/HUP for clean stop of *this* monitor — never used on targets
  struct sigaction sa {};
  sa.sa_handler = on_signal;
  ::sigaction(SIGINT, &sa, nullptr);
  ::sigaction(SIGHUP, &sa, nullptr);

  std::fprintf(stdout, "ACTIVE_HUNT copilot monitor interval_ms=%d status=%s\n", interval_ms,
               status_path ? status_path : "-");
  std::fflush(stdout);

  int cycle = 0;
  int lifetime = 0;
  std::vector<Hit> hits;
  while (!g_stop) {
    ++cycle;
    hunt(hits);
    int killed = 0;
    if (!dry && !hits.empty()) killed = hard_kill_all(hits);
    lifetime += killed;
    if (status_path) write_status(status_path, cycle, static_cast<int>(hits.size()), killed,
                                  lifetime, hits, dry);
    if (log_path && (!hits.empty() || cycle == 1)) {
      int fd = ::open(log_path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
      if (fd >= 0) {
        char line[256];
        int n = std::snprintf(line, sizeof(line), "cycle=%d seen=%zu killed=%d lifetime=%d\n",
                              cycle, hits.size(), killed, lifetime);
        ssize_t w = ::write(fd, line, static_cast<size_t>(n > 0 ? n : 0));
        (void)w;
        ::close(fd);
      }
    }
    if (!hits.empty()) {
      std::printf("HUNT cycle=%d seen=%zu killed=%d lifetime=%d\n", cycle, hits.size(), killed,
                  lifetime);
      std::fflush(stdout);
    }
    if (once) break;
    timespec req{};
    req.tv_sec = interval_ms / 1000;
    req.tv_nsec = static_cast<long>(interval_ms % 1000) * 1000000L;
    ::clock_nanosleep(CLOCK_MONOTONIC, 0, &req, nullptr);
  }
  return 0;
}

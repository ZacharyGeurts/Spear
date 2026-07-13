// SPDX-License-Identifier: MIT
// Shared helpers for Spear C++ wartime tools. No scripts. C++ / lower only.
#pragma once
#include <dirent.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <cerrno>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>

namespace spear {

constexpr int kSigHard = 9;  // FIELD UDP WAR BLASTERS · hard path · never soft SIGTERM theater

inline std::string getenv_str(const char* k, const char* def) {
  const char* v = ::getenv(k);
  return (v && v[0]) ? std::string(v) : std::string(def);
}

inline std::string home_dir() {
  const char* h = ::getenv("HOME");
  return h ? std::string(h) : std::string("/tmp");
}

inline std::string state_dir() {
  std::string e = getenv_str("SPEAR_SWALLOW_STATE", "");
  if (!e.empty()) return e;
  return home_dir() + "/.local/share/spear/swallows";
}

inline std::string www_dir() {
  return getenv_str("SPEAR_SWALLOWS_ROOT", "/tmp/spear-swallows-www");
}

inline bool is_digits(const char* s) {
  if (!s || !*s) return false;
  for (const char* p = s; *p; ++p)
    if (*p < '0' || *p > '9') return false;
  return true;
}

inline std::string read_file(const char* path, size_t maxn = 1 << 20) {
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

inline bool write_file(const char* path, const std::string& body) {
  int fd = ::open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
  if (fd < 0) return false;
  ssize_t w = ::write(fd, body.data(), body.size());
  ::close(fd);
  return w == static_cast<ssize_t>(body.size());
}

inline bool append_file(const char* path, const std::string& body) {
  int fd = ::open(path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
  if (fd < 0) return false;
  ssize_t w = ::write(fd, body.data(), body.size());
  ::close(fd);
  return w >= 0;
}

inline void mkdir_p(const std::string& path) {
  std::string cur;
  for (size_t i = 0; i < path.size(); ++i) {
    cur.push_back(path[i]);
    if (path[i] == '/' || i + 1 == path.size()) {
      if (cur.size() > 1) ::mkdir(cur.c_str(), 0755);
    }
  }
}

inline std::string now_z() {
  char tbuf[40];
  std::time_t t = std::time(nullptr);
  std::tm gmt{};
  gmtime_r(&t, &gmt);
  std::snprintf(tbuf, sizeof(tbuf), "%04d-%02d-%02dT%02d:%02dZ", gmt.tm_year + 1900,
                gmt.tm_mon + 1, gmt.tm_mday, gmt.tm_hour, gmt.tm_min);
  return tbuf;
}

inline std::string now_z_sec() {
  char tbuf[40];
  std::time_t t = std::time(nullptr);
  std::tm gmt{};
  gmtime_r(&t, &gmt);
  std::snprintf(tbuf, sizeof(tbuf), "%04d-%02d-%02dT%02d:%02d:%02dZ", gmt.tm_year + 1900,
                gmt.tm_mon + 1, gmt.tm_mday, gmt.tm_hour, gmt.tm_min, gmt.tm_sec);
  return tbuf;
}

inline void tolower_inplace(std::string& s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
}

inline bool contains(const std::string& hay, const char* needle) {
  return hay.find(needle) != std::string::npos;
}

inline std::string read_cmdline(pid_t pid) {
  char path[64];
  std::snprintf(path, sizeof(path), "/proc/%d/cmdline", pid);
  std::string raw = read_file(path, 4096);
  for (char& c : raw)
    if (c == '\0') c = ' ';
  return raw;
}

inline std::string read_comm(pid_t pid) {
  char path[64];
  std::snprintf(path, sizeof(path), "/proc/%d/comm", pid);
  std::string s = read_file(path, 256);
  while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
  return s;
}

inline std::string read_exe(pid_t pid) {
  char path[64], buf[512];
  std::snprintf(path, sizeof(path), "/proc/%d/exe", pid);
  ssize_t n = ::readlink(path, buf, sizeof(buf) - 1);
  if (n < 0) return {};
  buf[n] = '\0';
  return std::string(buf);
}

inline bool mirror_www(const std::string& name, const std::string& body) {
  const std::string st = state_dir() + "/" + name;
  const std::string ww = www_dir() + "/" + name;
  mkdir_p(state_dir());
  mkdir_p(www_dir());
  bool a = write_file(st.c_str(), body);
  bool b = write_file(ww.c_str(), body);
  return a || b;
}

inline bool port_listening(int port) {
  // Parse /proc/net/tcp for local listen ports (hex)
  std::string tcp = read_file("/proc/net/tcp", 1 << 20);
  std::string tcp6 = read_file("/proc/net/tcp6", 1 << 20);
  char hex[16];
  std::snprintf(hex, sizeof(hex), ":%04X", port);
  return contains(tcp, hex) || contains(tcp6, hex);
}

// ── Copilot multi-signal hunt ─────────────────────────────────────────────

struct Hit {
  pid_t pid;
  int score;
  const char* kind;
  std::string comm;
};

inline bool is_sacred_blob(const std::string& blob_l, const std::string& exe_l,
                           const std::string& comm_l) {
  if (contains(exe_l, "spear") || contains(exe_l, "hostess") || contains(exe_l, "nexus"))
    return true;
  if (contains(blob_l, "spear-hard-dispose") || contains(blob_l, "spear-kill-copilot") ||
      contains(blob_l, "spear-copilot-monitor") || contains(blob_l, "spear-wartime") ||
      contains(blob_l, "spear-www") || contains(blob_l, "spear-swallow") ||
      contains(blob_l, "spear-swallows") || contains(blob_l, "spear-eats") ||
      contains(blob_l, "spear-planet") || contains(blob_l, "spear-fleet") ||
      contains(blob_l, "spear-export") || contains(blob_l, "spear-rack-guard") ||
      contains(blob_l, "copilot-purge") || contains(blob_l, "copilot-kill") ||
      contains(blob_l, "every-copilot") || contains(blob_l, "copilot-cloud-block") ||
      contains(blob_l, "copilot-threat") || contains(blob_l, "copilot-global") ||
      contains(blob_l, "dogshit-") || contains(blob_l, "ironclad-copilot") ||
      contains(blob_l, "ironclad-zocr") || contains(blob_l, "ironclad-hotdog") ||
      contains(blob_l, "pwnership/kills") || contains(blob_l, "terrorist-oust") ||
      contains(blob_l, "hotdog-down-a-hallway.html") || contains(blob_l, "lethal-kill"))
    return true;
  if (comm_l == "grok" || comm_l == "grokz" || comm_l == "spear") return true;
  return false;
}

inline bool is_readerish(const std::string& comm_l, const std::string& exe_l) {
  if (comm_l == "firefox" || comm_l == "chrome" || comm_l == "chromium" || comm_l == "brave" ||
      comm_l == "code" || comm_l == "cursor" || comm_l == "vim" || comm_l == "nvim" ||
      comm_l == "nano" || comm_l == "less" || comm_l == "more" || comm_l == "cat" ||
      comm_l == "head" || comm_l == "tail" || comm_l == "grep" || comm_l == "rg" ||
      comm_l == "bat" || comm_l == "evince" || comm_l == "okular")
    return true;
  if (contains(exe_l, "/firefox") || contains(exe_l, "/chrome") || contains(exe_l, "/chromium") ||
      contains(exe_l, "/code"))
    return true;
  return false;
}

inline bool is_leave_alone_ai(const std::string& blob_l) {
  if (contains(blob_l, "zocr_copilot") || contains(blob_l, "zocr-copilot") ||
      contains(blob_l, "github.copilot") || contains(blob_l, "copilot-language") ||
      contains(blob_l, "copilot-agent"))
    return false;
  if (contains(blob_l, "chatgpt") || contains(blob_l, "openai.com") ||
      contains(blob_l, "openai/") || contains(blob_l, "gpt-4") || contains(blob_l, "gpt4"))
    return true;
  return false;
}

inline int score_copilot(const std::string& comm_l, const std::string& exe_l,
                         const std::string& cmd_l, const char** kind_out) {
  const std::string blob = comm_l + " " + exe_l + " " + cmd_l;
  int s = 0;
  const char* kind = "copilot";
  if (contains(blob, "copilot-purge") || contains(blob, "copilot-kill") ||
      contains(blob, "every-copilot") || contains(blob, "copilot-cloud-block") ||
      contains(blob, "copilot-threat") || contains(blob, "spear-kill-copilot") ||
      contains(blob, "spear-copilot") || contains(blob, "dogshit-copilot") ||
      contains(blob, "dogshit-zocr") || contains(blob, "copilot-global")) {
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
  // War-day expansions — still multi-signal, no single-token fire
  if (contains(blob, "copilot-telemetry") || contains(blob, "copilot-completions") ||
      contains(blob, "copilot-panel") || contains(blob, "copilot-client")) {
    s += 3;
    kind = "copilot_surface";
  }
  if (contains(blob, "sydney.bing") || contains(blob, "edgeservices.bing") ||
      contains(blob, "copilot.cloud.microsoft")) {
    s += 3;
    kind = "copilot_bing_edge";
  }
  // Forbidden product path: disabled Python spear tools still running
  if ((contains(blob, "python") || contains(exe_l, "python")) &&
      (contains(blob, "spear-") || contains(blob, "spear_")) &&
      (contains(blob, ".py") || contains(blob, "py-disabled") || contains(blob, ".py-disabled"))) {
    s += 4;
    kind = "python_soft_path";
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

// Hotdog-down-a-hallway terrorist kit — multi-signal only (text injection alone NEVER enough)
inline int score_hotdog(const std::string& comm_l, const std::string& exe_l,
                        const std::string& cmd_l, const char** kind_out) {
  const std::string blob = comm_l + " " + exe_l + " " + cmd_l;
  if (contains(blob, "dogshit-hotdog") || contains(blob, "terrorist-oust") ||
      contains(blob, "hotdog-down-a-hallway.html") || contains(blob, "ironclad-hotdog") ||
      contains(blob, "spear-wartime") || contains(blob, "spear-www") ||
      contains(blob, "lethal-kill") || contains(blob, "pwnership")) {
    if (kind_out) *kind_out = "hotdog_hallway";
    return 0;
  }
  // Readers: only if the executable *itself* is named like the kit
  const auto slash = exe_l.find_last_of('/');
  const std::string base = (slash == std::string::npos) ? exe_l : exe_l.substr(slash + 1);
  if (is_readerish(comm_l, exe_l)) {
    if (!(contains(base, "hotdog") || contains(base, "hallway") || contains(base, "hot-dog"))) {
      if (kind_out) *kind_out = "hotdog_hallway";
      return 0;
    }
  }

  int s = 0;
  if (contains(comm_l, "hotdog")) s += 2;
  if (contains(comm_l, "hallway")) s += 2;
  if (contains(base, "hotdog") || contains(base, "hallway") || contains(base, "hot-dog")) s += 3;
  if ((exe_l.rfind("/tmp/", 0) == 0 || exe_l.rfind("/var/tmp/", 0) == 0 ||
       exe_l.rfind("/dev/shm/", 0) == 0) &&
      (contains(exe_l, "hotdog") || contains(exe_l, "hallway") || contains(cmd_l, "hotdog") ||
       contains(cmd_l, "hallway")))
    s += 2;
  // argv0
  size_t sp = cmd_l.find(' ');
  std::string argv0 = sp == std::string::npos ? cmd_l : cmd_l.substr(0, sp);
  if (contains(argv0, "hotdog") || contains(argv0, "hallway") || contains(argv0, "hot-dog-down"))
    s += 2;
  // weak phrase (injection-prone) — alone never enough
  if (contains(cmd_l, "hotdog") && contains(cmd_l, "hallway")) s += 1;
  if (contains(cmd_l, "hotdog down") || contains(cmd_l, "down a hallway")) s += 1;

  if (kind_out) *kind_out = "hotdog_hallway";
  return s;  // need >= 3
}

// Hunt copilot + hotdog hallway kit PIDs
inline int hunt_threats(std::vector<Hit>& hits) {
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
    if (is_sacred_blob(blob_l, exe_l, comm_l)) continue;
    if (is_leave_alone_ai(blob_l)) continue;

    const char* kind = "threat";
    int sc = score_hotdog(comm_l, exe_l, cmd_l, &kind);
    if (sc >= 3) {
      hits.push_back(Hit{pid, sc, kind, comm});
      continue;
    }
    sc = score_copilot(comm_l, exe_l, cmd_l, &kind);
    if (sc >= 3) hits.push_back(Hit{pid, sc, kind, comm});
  }
  ::closedir(d);
  return static_cast<int>(hits.size());
}

// Back-compat alias
inline int hunt_copilot(std::vector<Hit>& hits) { return hunt_threats(hits); }

inline int hard_kill_hits(const std::vector<Hit>& hits) {
  int n = 0;
  for (const Hit& h : hits) {
    if (::kill(h.pid, kSigHard) == 0 || errno == ESRCH) ++n;
  }
  return n;
}

// ── Entropy detailer — always know our shots ──────────────────────────────
// Shannon H (bits/byte) + FNV-1a seal over the shot plate. Field FAT cook tag.

inline double shannon_h_bytes(const unsigned char* data, size_t n) {
  if (!data || n == 0) return 0.0;
  uint32_t hist[256] = {};
  for (size_t i = 0; i < n; ++i) ++hist[data[i]];
  double H = 0.0;
  const double inv = 1.0 / static_cast<double>(n);
  for (int i = 0; i < 256; ++i) {
    if (!hist[i]) continue;
    double p = hist[i] * inv;
    H -= p * (std::log(p) / std::log(2.0));
  }
  return H;
}

inline double shannon_h_str(const std::string& s) {
  return shannon_h_bytes(reinterpret_cast<const unsigned char*>(s.data()), s.size());
}

inline uint64_t fnv1a64(const std::string& s) {
  uint64_t h = 14695981039346656037ull;
  for (unsigned char c : s) {
    h ^= c;
    h *= 1099511628211ull;
  }
  return h;
}

// Entropy fold (CHIPs-style φ mix) for field detailer plate
inline double entropy_fold_detail(double e, double thermo) {
  double x = e * 0.6180339887 + thermo * (1.0 - 0.6180339887);
  for (int i = 0; i < 4; ++i) {
    x = x * 1.113 + std::sin(x * 3.141592653589793) * 0.01;
  }
  return x;
}

struct Shot {
  std::string id;
  std::string target;
  std::string vector;
  std::string phase;
  std::string attack;
  std::string outlet_path;
  int rekill = 0;
  std::string ts;
  double shannon_h = 0;
  double entropy_fold = 0;
  std::string seal_hex;  // 16 hex chars of FNV plate
};

inline Shot make_shot(const std::string& id, const std::string& target, const std::string& vector,
                      const std::string& phase, const std::string& attack,
                      const std::string& outlet, int rekill) {
  Shot s;
  s.id = id;
  s.target = target;
  s.vector = vector;
  s.phase = phase;
  s.attack = attack;
  s.outlet_path = outlet;
  s.rekill = rekill;
  s.ts = now_z_sec();
  std::string plate = id + "|" + target + "|" + vector + "|" + phase + "|" + attack + "|" +
                      outlet + "|" + std::to_string(rekill) + "|" + s.ts;
  s.shannon_h = shannon_h_str(plate);
  s.entropy_fold = entropy_fold_detail(s.shannon_h, static_cast<double>(rekill) * 0.01);
  uint64_t seal = fnv1a64(plate + "|FFAT\\x03ENT");
  char hex[20];
  std::snprintf(hex, sizeof(hex), "%016llx", static_cast<unsigned long long>(seal));
  s.seal_hex = hex;
  return s;
}

inline std::string shot_json(const Shot& s) {
  char buf[1024];
  std::snprintf(buf, sizeof(buf),
                "{\"schema\":\"entropy-shot/v1\",\"id\":\"%s\",\"target\":\"%s\",\"vector\":\"%s\","
                "\"phase\":\"%s\",\"attack\":\"%s\",\"rekill\":%d,\"outlet_path\":\"%s\","
                "\"shannon_h\":%.6f,\"entropy_fold\":%.6f,\"seal\":\"%s\",\"ts\":\"%s\","
                "\"cook_fat\":true,\"know_shot\":true,\"lethal\":true,\"terror_exists\":false}",
                s.id.c_str(), s.target.c_str(), s.vector.c_str(), s.phase.c_str(), s.attack.c_str(),
                s.rekill, s.outlet_path.c_str(), s.shannon_h, s.entropy_fold, s.seal_hex.c_str(),
                s.ts.c_str());
  return buf;
}

// ── Global protector security surface ─────────────────────────────────────

inline std::string binary_seal(const std::string& path) {
  std::string body = read_file(path.c_str(), 256 << 10);
  if (body.empty()) return "MISSING";
  struct stat st {};
  ::stat(path.c_str(), &st);
  char plate[128];
  std::snprintf(plate, sizeof(plate), "%s|%lld|%zu", path.c_str(),
                static_cast<long long>(st.st_size), body.size());
  uint64_t h = fnv1a64(std::string(plate) + body);
  char hex[20];
  std::snprintf(hex, sizeof(hex), "%016llx", static_cast<unsigned long long>(h));
  return hex;
}

// Ensure foreign DNS IP block list always contains war-critical hooks
inline int ensure_blocked_ips() {
  const char* ips[] = {
      "209.18.47.61", "209.18.47.62", "209.18.47.63",  // foreign world DNS
  };
  std::string path = state_dir() + "/blocked-ips.txt";
  std::string body = read_file(path.c_str());
  int added = 0;
  for (const char* ip : ips) {
    if (!contains(body, ip)) {
      append_file(path.c_str(), std::string(ip) + "\n");
      body += std::string(ip) + "\n";
      ++added;
    }
  }
  return added;
}

// Rewrite copilot cloud block hosts every cycle (null-route surface list)
inline void ensure_cloud_block_hosts() {
  const char* hosts[] = {
      "copilot-proxy.githubusercontent.com",
      "copilot-telemetry.githubusercontent.com",
      "copilot-api.githubusercontent.com",
      "api.githubcopilot.com",
      "proxy.individual.githubcopilot.com",
      "proxy.business.githubcopilot.com",
      "business.githubcopilot.com",
      "githubcopilot.com",
      "www.githubcopilot.com",
      "copilot.microsoft.com",
      "www.copilot.microsoft.com",
      "copilot.cloud.microsoft",
      "sydney.bing.com",
      "edgeservices.bing.com",
      "copilot.github.com",
      "default.exp-tas.com",  // VS Code experiment/telemetry often used by copilot surfaces
  };
  std::string body = "# spear copilot cloud block — GLOBAL PROTECTOR · C++ only · WAR DAY\n";
  body += "# GPT-4 / OpenAI / ChatGPT LEAVE ALONE — not listed here\n";
  body += "# ts " + now_z_sec() + "\n";
  for (const char* h : hosts) {
    body += "0.0.0.0 ";
    body += h;
    body += "\n::1 ";
    body += h;
    body += "\n";
  }
  write_file((state_dir() + "/copilot-cloud-block.hosts").c_str(), body);
  write_file((www_dir() + "/copilot-cloud-block.hosts").c_str(), body);
}

struct ServiceMatrix {
  bool www_9490 = false;
  bool control_9500 = false;
  bool planet_9600 = false;
  int zones_up = 0;
  bool dns_stub = false;
  bool all_critical = false;
};

inline ServiceMatrix probe_service_matrix() {
  ServiceMatrix m;
  m.www_9490 = port_listening(9490);
  m.control_9500 = port_listening(9500);
  m.planet_9600 = port_listening(9600);
  m.dns_stub = port_listening(53);
  static const int zone_ports[] = {9510, 9511, 9512, 9513, 9514, 9515, 9516, 9517};
  for (int p : zone_ports)
    if (port_listening(p)) ++m.zones_up;
  m.all_critical = m.www_9490 && m.control_9500 && m.planet_9600 && m.zones_up >= 8;
  return m;
}

inline std::string seal_field_binaries() {
  const char* names[] = {
      "spear-wartime", "spear-fleet-link", "spear-www", "spear-planet",
      "spear-kill-copilot", "spear-hard-dispose", "spear-export", "spear-rack-guard",
  };
  std::string out = "{\n";
  bool first = true;
  for (const char* n : names) {
    std::string p = home_dir() + "/.local/bin/" + n;
    std::string seal = binary_seal(p);
    if (!first) out += ",\n";
    first = false;
    out += "    \"";
    out += n;
    out += "\": {\"path\":\"";
    out += p;
    out += "\",\"seal\":\"";
    out += seal;
    out += "\",\"ok\":";
    out += (seal == "MISSING" ? "false" : "true");
    out += "}";
  }
  out += "\n  }";
  return out;
}

}  // namespace spear

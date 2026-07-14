// SPDX-License-Identifier: MIT
// field / fieldbox — multicall Field Linux tools (C++ · lower)
// Ironclad floor · CHIPs-ready paths · Grok16 field_opt friendly (no heap thrash)
// Replaces busybox theater with Field-native applets. Own: top, ls, ps, df, …
#include "spear_common.hpp"

#include <dirent.h>
#include <fcntl.h>
#include <grp.h>
#include <pwd.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/sysinfo.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <functional>
#include <map>
#include <string>
#include <vector>

using namespace spear;

// ── tiny helpers (Grok16: tight loops, few allocations) ────────────────────
static const char* base_name(const char* p) {
  const char* s = p;
  for (const char* q = p; *q; ++q)
    if (*q == '/') s = q + 1;
  return s;
}

static bool flag(char** argv, int argc, char f) {
  for (int i = 1; i < argc; ++i) {
    if (argv[i][0] == '-' && argv[i][1] != '-' && argv[i][1]) {
      for (const char* p = argv[i] + 1; *p; ++p)
        if (*p == f) return true;
    }
  }
  return false;
}

static std::string mode_str(mode_t m) {
  char b[11];
  b[0] = S_ISDIR(m) ? 'd' : S_ISLNK(m) ? 'l' : S_ISCHR(m) ? 'c' : S_ISBLK(m) ? 'b' : S_ISFIFO(m) ? 'p' : S_ISSOCK(m) ? 's' : '-';
  const char* rwx = "rwx";
  for (int i = 0; i < 9; ++i) b[1 + i] = (m & (1 << (8 - i))) ? rwx[i % 3] : '-';
  b[10] = 0;
  return b;
}

static std::string human(uint64_t n) {
  char b[32];
  if (n >= (1ull << 30))
    std::snprintf(b, sizeof(b), "%.1fG", n / double(1ull << 30));
  else if (n >= (1ull << 20))
    std::snprintf(b, sizeof(b), "%.1fM", n / double(1ull << 20));
  else if (n >= (1ull << 10))
    std::snprintf(b, sizeof(b), "%.1fK", n / double(1ull << 10));
  else
    std::snprintf(b, sizeof(b), "%llu", (unsigned long long)n);
  return b;
}

// ── applets ────────────────────────────────────────────────────────────────
static int ap_echo(int argc, char** argv) {
  bool n = flag(argv, argc, 'n');
  bool first = true;
  for (int i = 1; i < argc; ++i) {
    if (argv[i][0] == '-') continue;
    if (!first) std::fputc(' ', stdout);
    first = false;
    std::fputs(argv[i], stdout);
  }
  if (!n) std::fputc('\n', stdout);
  return 0;
}

static int ap_pwd(int, char**) {
  char buf[4096];
  if (!::getcwd(buf, sizeof(buf))) {
    std::perror("pwd");
    return 1;
  }
  std::puts(buf);
  return 0;
}

static int ap_true(int, char**) { return 0; }
static int ap_false(int, char**) { return 1; }

static int ap_clear(int, char**) {
  std::fputs("\033[2J\033[H", stdout);
  return 0;
}

static int ap_uname(int argc, char** argv) {
  utsname u{};
  if (uname(&u) != 0) {
    std::perror("uname");
    return 1;
  }
  bool a = flag(argv, argc, 'a');
  if (a) {
    std::printf("%s %s %s %s %s Field\n", u.sysname, u.nodename, u.release, u.version, u.machine);
    return 0;
  }
  // Field identity
  std::printf("Spear-Field\n");
  return 0;
}

static int ap_id(int, char**) {
  uid_t u = getuid(), e = geteuid();
  gid_t g = getgid();
  passwd* pw = getpwuid(u);
  group* gr = getgrgid(g);
  std::printf("uid=%u(%s) gid=%u(%s) Field\n", (unsigned)u, pw ? pw->pw_name : "?", (unsigned)g,
              gr ? gr->gr_name : "?");
  if (u != e) std::printf("euid=%u\n", (unsigned)e);
  return 0;
}

static int ap_whoami(int, char**) {
  passwd* pw = getpwuid(getuid());
  std::puts(pw ? pw->pw_name : "?");
  return 0;
}

static int ap_hostname(int, char**) {
  char b[256];
  if (gethostname(b, sizeof(b)) != 0) {
    std::perror("hostname");
    return 1;
  }
  std::puts(b);
  return 0;
}

static int ap_env(int, char**) {
  extern char** environ;
  for (char** e = environ; e && *e; ++e) std::puts(*e);
  return 0;
}

static int ap_cat(int argc, char** argv) {
  if (argc < 2) {
    // stdin
    char buf[8192];
    ssize_t n;
    while ((n = ::read(0, buf, sizeof(buf))) > 0) ::write(1, buf, (size_t)n);
    return 0;
  }
  int rc = 0;
  for (int i = 1; i < argc; ++i) {
    if (argv[i][0] == '-') continue;
    int fd = ::open(argv[i], O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
      std::perror(argv[i]);
      rc = 1;
      continue;
    }
    char buf[8192];
    ssize_t n;
    while ((n = ::read(fd, buf, sizeof(buf))) > 0) ::write(1, buf, (size_t)n);
    ::close(fd);
  }
  return rc;
}

static int ap_ls(int argc, char** argv) {
  bool longf = flag(argv, argc, 'l');
  bool all = flag(argv, argc, 'a');
  bool hum = flag(argv, argc, 'h');
  bool one = flag(argv, argc, '1');
  std::vector<const char*> paths;
  for (int i = 1; i < argc; ++i)
    if (argv[i][0] != '-') paths.push_back(argv[i]);
  if (paths.empty()) paths.push_back(".");

  int rc = 0;
  for (const char* path : paths) {
    struct stat st {};
    if (stat(path, &st) != 0) {
      std::perror(path);
      rc = 1;
      continue;
    }
    if (!S_ISDIR(st.st_mode)) {
      if (longf) {
        std::printf("%s %3lu %s\n", mode_str(st.st_mode).c_str(), (unsigned long)st.st_nlink,
                    path);
      } else
        std::puts(path);
      continue;
    }
    DIR* d = ::opendir(path);
    if (!d) {
      std::perror(path);
      rc = 1;
      continue;
    }
    std::vector<std::string> names;
    while (dirent* e = ::readdir(d)) {
      if (!all && e->d_name[0] == '.') continue;
      names.emplace_back(e->d_name);
    }
    ::closedir(d);
    std::sort(names.begin(), names.end());
    for (const auto& name : names) {
      std::string full = std::string(path) + "/" + name;
      if (longf) {
        struct stat ls {};
        if (lstat(full.c_str(), &ls) != 0) continue;
        passwd* pw = getpwuid(ls.st_uid);
        group* gr = getgrgid(ls.st_gid);
        char tbuf[32];
        std::time_t tt = ls.st_mtime;
        std::strftime(tbuf, sizeof(tbuf), "%b %e %H:%M", std::localtime(&tt));
        std::string sz = hum ? human((uint64_t)ls.st_size) : std::to_string((long long)ls.st_size);
        // color: dir cyan, exec green, link pink
        const char* col = "";
        const char* rst = "\033[0m";
        if (S_ISDIR(ls.st_mode))
          col = "\033[1;36m";
        else if (S_ISLNK(ls.st_mode))
          col = "\033[1;35m";
        else if (ls.st_mode & 0111)
          col = "\033[1;32m";
        else
          rst = "";
        std::printf("%s %3lu %-8s %-8s %8s %s %s%s%s\n", mode_str(ls.st_mode).c_str(),
                    (unsigned long)ls.st_nlink, pw ? pw->pw_name : "?", gr ? gr->gr_name : "?",
                    sz.c_str(), tbuf, col, name.c_str(), rst);
      } else if (one) {
        std::puts(name.c_str());
      } else {
        std::printf("%s  ", name.c_str());
      }
    }
    if (!longf && !one) std::fputc('\n', stdout);
  }
  return rc;
}

static int ap_wc(int argc, char** argv) {
  auto count_fd = [](int fd, long& L, long& W, long& C) {
    char buf[8192];
    ssize_t n;
    bool sp = true;
    while ((n = ::read(fd, buf, sizeof(buf))) > 0) {
      C += n;
      for (ssize_t i = 0; i < n; ++i) {
        if (buf[i] == '\n') ++L;
        if (buf[i] == ' ' || buf[i] == '\t' || buf[i] == '\n')
          sp = true;
        else if (sp) {
          ++W;
          sp = false;
        }
      }
    }
  };
  if (argc < 2) {
    long L = 0, W = 0, C = 0;
    count_fd(0, L, W, C);
    std::printf("%7ld %7ld %7ld\n", L, W, C);
    return 0;
  }
  for (int i = 1; i < argc; ++i) {
    if (argv[i][0] == '-') continue;
    int fd = ::open(argv[i], O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
      std::perror(argv[i]);
      continue;
    }
    long L = 0, W = 0, C = 0;
    count_fd(fd, L, W, C);
    ::close(fd);
    std::printf("%7ld %7ld %7ld %s\n", L, W, C, argv[i]);
  }
  return 0;
}

static int ap_head(int argc, char** argv) {
  int n = 10;
  const char* file = nullptr;
  for (int i = 1; i < argc; ++i) {
    if (!std::strcmp(argv[i], "-n") && i + 1 < argc)
      n = std::atoi(argv[++i]);
    else if (argv[i][0] != '-')
      file = argv[i];
  }
  FILE* f = file ? std::fopen(file, "r") : stdin;
  if (!f) {
    std::perror(file);
    return 1;
  }
  char* line = nullptr;
  size_t cap = 0;
  for (int i = 0; i < n; ++i) {
    ssize_t r = getline(&line, &cap, f);
    if (r < 0) break;
    std::fwrite(line, 1, (size_t)r, stdout);
  }
  free(line);
  if (file) std::fclose(f);
  return 0;
}

static uint64_t meminfo_kib(const std::string& mi, const char* key) {
  auto p = mi.find(key);
  if (p == std::string::npos) return 0;
  p += std::strlen(key);
  while (p < mi.size() && (mi[p] == ' ' || mi[p] == '\t')) ++p;
  return std::strtoull(mi.c_str() + p, nullptr, 10);
}

static int ap_free(int argc, char** argv) {
  bool h = flag(argv, argc, 'h');
  std::string mi = read_file("/proc/meminfo", 8192);
  if (mi.empty()) {
    std::perror("free");
    return 1;
  }
  uint64_t total = meminfo_kib(mi, "MemTotal:") * 1024ull;
  uint64_t freeb = meminfo_kib(mi, "MemFree:") * 1024ull;
  uint64_t avail = meminfo_kib(mi, "MemAvailable:") * 1024ull;
  uint64_t buf = meminfo_kib(mi, "Buffers:") * 1024ull;
  uint64_t cached = meminfo_kib(mi, "Cached:") * 1024ull;
  uint64_t used = total > freeb + buf + cached ? total - freeb - buf - cached : 0;
  uint64_t st = meminfo_kib(mi, "SwapTotal:") * 1024ull;
  uint64_t sf = meminfo_kib(mi, "SwapFree:") * 1024ull;
  auto show = [&](const char* lab, uint64_t v) {
    if (h)
      std::printf("%-8s %10s\n", lab, human(v).c_str());
    else
      std::printf("%-8s %10llu\n", lab, (unsigned long long)(v / 1024));
  };
  std::puts(h ? "Field memory (human)" : "Field memory (KiB)");
  show("total", total);
  show("used", used);
  show("free", freeb);
  show("avail", avail);
  show("buff", buf);
  show("cache", cached);
  show("swap_t", st);
  show("swap_f", sf);
  return 0;
}

static int ap_df(int argc, char** argv) {
  bool h = flag(argv, argc, 'h');
  std::string m = read_file("/proc/mounts", 1 << 20);
  std::printf("%-20s %10s %10s %10s %5s %s\n", "Filesystem", h ? "Size" : "1K-blocks", "Used",
              "Avail", "Use%", "Mounted");
  size_t i = 0;
  while (i < m.size()) {
    size_t e = m.find('\n', i);
    if (e == std::string::npos) e = m.size();
    std::string line = m.substr(i, e - i);
    i = e + 1;
    // src mp type
    char src[256], mp[256], typ[64];
    if (std::sscanf(line.c_str(), "%255s %255s %63s", src, mp, typ) < 3) continue;
    if (!std::strcmp(typ, "proc") || !std::strcmp(typ, "sysfs") || !std::strcmp(typ, "devtmpfs") ||
        !std::strcmp(typ, "devpts") || !std::strcmp(typ, "cgroup") || !std::strcmp(typ, "cgroup2") ||
        !std::strcmp(typ, "pstore") || !std::strcmp(typ, "bpf") || !std::strcmp(typ, "tracefs") ||
        !std::strcmp(typ, "debugfs") || !std::strcmp(typ, "securityfs") ||
        !std::strcmp(typ, "fusectl") || !std::strcmp(typ, "configfs") || !std::strcmp(typ, "mqueue"))
      continue;
    struct statvfs vfs {};
    if (statvfs(mp, &vfs) != 0) continue;
    uint64_t bsize = vfs.f_frsize ? vfs.f_frsize : vfs.f_bsize;
    uint64_t total = (uint64_t)vfs.f_blocks * bsize;
    uint64_t freeb = (uint64_t)vfs.f_bavail * bsize;
    uint64_t used = total > freeb ? total - freeb : 0;
    int pct = total ? (int)((used * 100) / total) : 0;
    if (h)
      std::printf("%-20.20s %10s %10s %10s %4d%% %s\n", src, human(total).c_str(), human(used).c_str(),
                  human(freeb).c_str(), pct, mp);
    else
      std::printf("%-20.20s %10llu %10llu %10llu %4d%% %s\n", src, (unsigned long long)(total / 1024),
                  (unsigned long long)(used / 1024), (unsigned long long)(freeb / 1024), pct, mp);
  }
  return 0;
}

static int ap_uptime(int, char**) {
  std::string up = read_file("/proc/uptime", 64);
  std::string ld = read_file("/proc/loadavg", 64);
  double secs = std::atof(up.c_str());
  long s = (long)secs;
  long d = s / 86400, h = (s % 86400) / 3600, m = (s % 3600) / 60;
  double l1 = 0, l5 = 0, l15 = 0;
  std::sscanf(ld.c_str(), "%lf %lf %lf", &l1, &l5, &l15);
  int procs = 0;
  DIR* pd = ::opendir("/proc");
  if (pd) {
    while (dirent* e = ::readdir(pd))
      if (is_digits(e->d_name)) ++procs;
    ::closedir(pd);
  }
  std::printf("Field up %ld days, %ld:%02ld  load %.2f %.2f %.2f  procs %d\n", d, h, m, l1, l5, l15,
              procs);
  return 0;
}

struct Proc {
  pid_t pid;
  std::string user;
  std::string comm;
  char state;
  long rss_kb;
  double pcpu;
  unsigned long utime, stime;
};

static std::vector<Proc> scan_procs() {
  std::vector<Proc> out;
  DIR* d = ::opendir("/proc");
  if (!d) return out;
  long ticks = sysconf(_SC_CLK_TCK);
  if (ticks <= 0) ticks = 100;
  while (dirent* e = ::readdir(d)) {
    if (!is_digits(e->d_name)) continue;
    pid_t pid = (pid_t)std::atoi(e->d_name);
    Proc p;
    p.pid = pid;
    p.comm = read_comm(pid);
    std::string st = read_file((std::string("/proc/") + e->d_name + "/stat").c_str(), 1024);
    // after last ) state
    auto rp = st.rfind(')');
    if (rp != std::string::npos && rp + 2 < st.size()) {
      p.state = st[rp + 2];
      // fields after state: ppid pgrp session tty tpgid flags minflt cminflt majflt cmajflt utime stime
      unsigned long ut = 0, stt = 0;
      std::sscanf(st.c_str() + rp + 3, "%*d %*d %*d %*d %*d %*u %*u %*u %*u %*u %lu %lu", &ut, &stt);
      p.utime = ut;
      p.stime = stt;
    } else {
      p.state = '?';
      p.utime = p.stime = 0;
    }
    std::string sm = read_file((std::string("/proc/") + e->d_name + "/status").c_str(), 4096);
    p.rss_kb = 0;
    auto vm = sm.find("VmRSS:");
    if (vm != std::string::npos) {
      vm += 6;
      while (vm < sm.size() && sm[vm] == ' ') ++vm;
      p.rss_kb = std::strtol(sm.c_str() + vm, nullptr, 10);
    }
    uid_t uid = 0;
    auto ul = sm.find("Uid:");
    if (ul != std::string::npos) {
      ul += 4;
      while (ul < sm.size() && (sm[ul] == ' ' || sm[ul] == '\t')) ++ul;
      uid = (uid_t)std::strtoul(sm.c_str() + ul, nullptr, 10);
    }
    passwd* pw = getpwuid(uid);
    p.user = pw ? pw->pw_name : std::to_string(uid);
    p.pcpu = 0;
    out.push_back(std::move(p));
  }
  ::closedir(d);
  return out;
}

static int ap_ps(int argc, char** argv) {
  bool a = flag(argv, argc, 'a') || flag(argv, argc, 'e') || flag(argv, argc, 'A');
  auto procs = scan_procs();
  std::printf("%-8s %-8s %5s %6s %s\n", "PID", "USER", "STAT", "RSS", "COMMAND");
  uid_t me = getuid();
  for (const auto& p : procs) {
    if (!a) {
      passwd* pw = getpwuid(me);
      if (pw && p.user != pw->pw_name) continue;
    }
    std::printf("%-8d %-8.8s %5c %6ld %s\n", (int)p.pid, p.user.c_str(), p.state, p.rss_kb,
                p.comm.c_str());
  }
  return 0;
}

static volatile sig_atomic_t g_top_run = 1;
static void top_sig(int) { g_top_run = 0; }

static int ap_top(int, char**) {
  ::signal(SIGINT, top_sig);
  ::signal(SIGTERM, top_sig);
  termios oldt{}, newt{};
  bool raw = false;
  if (isatty(0) && tcgetattr(0, &oldt) == 0) {
    newt = oldt;
    newt.c_lflag &= ~(ICANON | ECHO);
    newt.c_cc[VMIN] = 0;
    newt.c_cc[VTIME] = 0;
    tcsetattr(0, TCSANOW, &newt);
    raw = true;
  }
  std::map<pid_t, unsigned long> prev;
  int gen = 0;
  while (g_top_run) {
    auto procs = scan_procs();
    std::string ups = read_file("/proc/uptime", 64);
    std::string ld = read_file("/proc/loadavg", 64);
    long up_s = (long)std::atof(ups.c_str());
    double l1 = 0;
    std::sscanf(ld.c_str(), "%lf", &l1);
    // cpu%
    for (auto& p : procs) {
      unsigned long tot = p.utime + p.stime;
      auto it = prev.find(p.pid);
      if (it != prev.end() && tot >= it->second) {
        p.pcpu = (tot - it->second) * 100.0 / 1.0;  // per refresh (~1s) rough
      }
      prev[p.pid] = tot;
    }
    std::sort(procs.begin(), procs.end(),
              [](const Proc& a, const Proc& b) { return a.rss_kb > b.rss_kb; });
    std::fputs("\033[2J\033[H", stdout);
    std::printf("\033[1;35m◆ Field top\033[0m  gen=%d  up %lds  load %.2f  procs %zu  \033[2m(q quit)\033[0m\n",
                gen++, up_s, l1, procs.size());
    std::printf("%-7s %-8s %5s %7s %6s %s\n", "PID", "USER", "S", "RSS(K)", "%CPU", "COMMAND");
    int n = 0;
    for (const auto& p : procs) {
      if (n++ >= 22) break;
      std::printf("%-7d %-8.8s %5c %7ld %6.1f %s\n", (int)p.pid, p.user.c_str(), p.state, p.rss_kb,
                  p.pcpu, p.comm.c_str());
    }
    std::fflush(stdout);
    char c = 0;
    if (raw && ::read(0, &c, 1) == 1 && (c == 'q' || c == 'Q' || c == 3)) break;
    ::sleep(1);
  }
  if (raw) tcsetattr(0, TCSANOW, &oldt);
  std::fputs("\033[0m\n", stdout);
  return 0;
}

static int ap_which(int argc, char** argv) {
  if (argc < 2) return 1;
  const char* path = ::getenv("PATH");
  if (!path) path = "/usr/local/bin:/usr/bin:/bin";
  std::string p = path;
  for (int a = 1; a < argc; ++a) {
    if (argv[a][0] == '-') continue;
    bool found = false;
    size_t i = 0;
    while (i <= p.size()) {
      size_t j = p.find(':', i);
      if (j == std::string::npos) j = p.size();
      std::string dir = p.substr(i, j - i);
      i = j + 1;
      std::string full = dir + "/" + argv[a];
      if (::access(full.c_str(), X_OK) == 0) {
        std::puts(full.c_str());
        found = true;
        break;
      }
    }
    if (!found) return 1;
  }
  return 0;
}

static int ap_mkdir(int argc, char** argv) {
  bool p = flag(argv, argc, 'p');
  int rc = 0;
  for (int i = 1; i < argc; ++i) {
    if (argv[i][0] == '-') continue;
    if (p) {
      mkdir_p(argv[i]);
    } else if (::mkdir(argv[i], 0755) != 0) {
      std::perror(argv[i]);
      rc = 1;
    }
  }
  return rc;
}

static int ap_rm(int argc, char** argv) {
  bool r = flag(argv, argc, 'r') || flag(argv, argc, 'R');
  bool f = flag(argv, argc, 'f');
  int rc = 0;
  std::function<int(const char*)> rmrf = [&](const char* path) -> int {
    struct stat st {};
    if (lstat(path, &st) != 0) {
      if (!f) std::perror(path);
      return f ? 0 : 1;
    }
    if (S_ISDIR(st.st_mode) && r) {
      DIR* d = ::opendir(path);
      if (d) {
        while (dirent* e = ::readdir(d)) {
          if (!std::strcmp(e->d_name, ".") || !std::strcmp(e->d_name, "..")) continue;
          std::string c = std::string(path) + "/" + e->d_name;
          rmrf(c.c_str());
        }
        ::closedir(d);
      }
      if (::rmdir(path) != 0 && !f) {
        std::perror(path);
        return 1;
      }
      return 0;
    }
    if (::unlink(path) != 0 && !f) {
      std::perror(path);
      return 1;
    }
    return 0;
  };
  for (int i = 1; i < argc; ++i) {
    if (argv[i][0] == '-') continue;
    rc |= rmrf(argv[i]);
  }
  return rc;
}

static int ap_sleep(int argc, char** argv) {
  if (argc < 2) return 1;
  double s = std::atof(argv[1]);
  if (s < 0) s = 0;
  ::usleep((useconds_t)(s * 1e6));
  return 0;
}

static int ap_kill(int argc, char** argv) {
  int sig = SIGTERM;
  int rc = 0;
  for (int i = 1; i < argc; ++i) {
    if (argv[i][0] == '-' && is_digits(argv[i] + 1))
      sig = std::atoi(argv[i] + 1);
    else if (argv[i][0] == '-' && !std::strcmp(argv[i], "-9"))
      sig = 9;
    else if (is_digits(argv[i])) {
      pid_t p = (pid_t)std::atoi(argv[i]);
      if (::kill(p, sig) != 0) {
        std::perror(argv[i]);
        rc = 1;
      }
    }
  }
  return rc;
}

static int ap_stat(int argc, char** argv) {
  if (argc < 2) return 1;
  for (int i = 1; i < argc; ++i) {
    if (argv[i][0] == '-') continue;
    struct stat st {};
    if (stat(argv[i], &st) != 0) {
      std::perror(argv[i]);
      continue;
    }
    std::printf("  File: %s\n  Size: %lld\n  Mode: %s (%o)\n  Uid: %u  Gid: %u\n", argv[i],
                (long long)st.st_size, mode_str(st.st_mode).c_str(), (unsigned)(st.st_mode & 07777),
                (unsigned)st.st_uid, (unsigned)st.st_gid);
  }
  return 0;
}

static int ap_field(int argc, char** argv);  // forward

static int ap_help(int, char**) {
  std::puts(
      "Field Linux tools (multicall) — C++ · Ironclad floor · CHIPs path\n"
      "  field <applet> | or symlink name\n"
      "Applets:\n"
      "  ls cat echo pwd env clear true false uname id whoami hostname\n"
      "  head wc which mkdir rm sleep kill stat free df uptime ps top\n"
      "  help version chips\n"
      "Also: field-nvtop (nvtop/nv-top) · spear chip-* · Grok16 field_opt\n"
      "God Bless.");
  return 0;
}

static int ap_version(int, char**) {
  std::puts("fieldbox 1.0.0-field  Spear Field tools  C++  Grok16-ready");
  return 0;
}

static int ap_chips(int, char**) {
  // Point at spear chip plane without pulling full link — exec if present
  if (::access("/usr/local/bin/spear", X_OK) == 0 || ::access("spear", X_OK) == 0) {
    std::puts("Field CHIPs / Field Die — via spear chip-status | chip-bench | chip-demo");
    std::fflush(stdout);
    const char* sp = ::access("/usr/local/bin/spear", X_OK) == 0 ? "/usr/local/bin/spear" : "spear";
    ::execl(sp, "spear", "chip-status", (char*)nullptr);
  }
  std::puts("CHIPs: install spear for Field Die plane (EntropyFold · WavePhase · PeakScan · PackPick)");
  std::puts("Ironclad: God → plate → Field → tools. Grok16 field_opt owns hot paths.");
  return 0;
}

// ── dispatch table ─────────────────────────────────────────────────────────
using Applet = int (*)(int, char**);

struct Ent {
  const char* name;
  Applet fn;
};

static const Ent kTab[] = {
    {"ls", ap_ls},
    {"cat", ap_cat},
    {"echo", ap_echo},
    {"pwd", ap_pwd},
    {"env", ap_env},
    {"printenv", ap_env},
    {"clear", ap_clear},
    {"true", ap_true},
    {"false", ap_false},
    {"uname", ap_uname},
    {"id", ap_id},
    {"whoami", ap_whoami},
    {"hostname", ap_hostname},
    {"head", ap_head},
    {"wc", ap_wc},
    {"which", ap_which},
    {"mkdir", ap_mkdir},
    {"rm", ap_rm},
    {"sleep", ap_sleep},
    {"kill", ap_kill},
    {"stat", ap_stat},
    {"free", ap_free},
    {"df", ap_df},
    {"uptime", ap_uptime},
    {"ps", ap_ps},
    {"top", ap_top},
    {"help", ap_help},
    {"version", ap_version},
    {"chips", ap_chips},
    {"field", ap_field},
    {"fieldbox", ap_field},
    {nullptr, nullptr},
};

static int ap_field(int argc, char** argv) {
  if (argc < 2) return ap_help(argc, argv);
  // field ls → redispatch
  const char* name = argv[1];
  for (const Ent* e = kTab; e->name; ++e) {
    if (!std::strcmp(e->name, name) && std::strcmp(name, "field") && std::strcmp(name, "fieldbox")) {
      return e->fn(argc - 1, argv + 1);
    }
  }
  std::fprintf(stderr, "field: unknown applet: %s\n", name);
  return ap_help(argc, argv);
}

int main(int argc, char** argv) {
  const char* ap = base_name(argv[0] ? argv[0] : "field");
  // strip field- prefix: field-ls → ls
  if (!std::strncmp(ap, "field-", 6)) ap = ap + 6;
  for (const Ent* e = kTab; e->name; ++e) {
    if (!std::strcmp(e->name, ap)) return e->fn(argc, argv);
  }
  // default
  return ap_help(argc, argv);
}

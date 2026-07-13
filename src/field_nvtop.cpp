// SPDX-License-Identifier: MIT
// field-nvtop — AMOURANTHRTX Field GPU top
// AMD · NVIDIA · Intel · DRM · cool TUI · C++ only · replaces nvtop theater
#include "spear_common.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <dirent.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

using namespace spear;

static volatile sig_atomic_t g_run = 1;
static void on_sig(int) { g_run = 0; }

// ── ANSI / look ────────────────────────────────────────────────────────────
namespace ui {
constexpr const char* RESET = "\033[0m";
constexpr const char* BOLD = "\033[1m";
constexpr const char* DIM = "\033[2m";
constexpr const char* PINK = "\033[38;2;255;64;160m";
constexpr const char* HOT = "\033[38;2;255;32;96m";
constexpr const char* CYAN = "\033[38;2;0;230;255m";
constexpr const char* MINT = "\033[38;2;80;255;180m";
constexpr const char* GOLD = "\033[38;2;255;200;64m";
constexpr const char* LILAC = "\033[38;2;180;140;255m";
constexpr const char* WHITE = "\033[38;2;240;240;255m";
constexpr const char* RED = "\033[38;2;255;80;80m";
constexpr const char* BG = "\033[48;2;8;6;14m";
constexpr const char* CLEAR = "\033[2J\033[H";
constexpr const char* HIDE = "\033[?25l";
constexpr const char* SHOW = "\033[?25h";

static int cols() {
  winsize w{};
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0 && w.ws_col > 40) return w.ws_col;
  return 100;
}

static std::string bar(double pct, int width, bool hot) {
  if (width < 4) width = 4;
  if (pct < 0) pct = 0;
  if (pct > 100) pct = 100;
  int fill = static_cast<int>(std::lround(pct / 100.0 * width));
  if (fill > width) fill = width;
  std::string s;
  s.reserve(static_cast<size_t>(width) * 8 + 32);
  const char* col = pct >= 90 ? RED : (pct >= 70 ? HOT : (hot ? PINK : CYAN));
  s += col;
  for (int i = 0; i < width; ++i) {
    if (i < fill)
      s += "█";
    else if (i == fill && fill < width)
      s += "▓";
    else
      s += std::string(DIM) + "░" + RESET;
  }
  s += RESET;
  return s;
}

static std::string human_bytes(uint64_t b) {
  char buf[64];
  if (b >= (1ull << 30))
    std::snprintf(buf, sizeof(buf), "%.2f GiB", b / double(1ull << 30));
  else if (b >= (1ull << 20))
    std::snprintf(buf, sizeof(buf), "%.1f MiB", b / double(1ull << 20));
  else if (b >= (1ull << 10))
    std::snprintf(buf, sizeof(buf), "%.0f KiB", b / double(1ull << 10));
  else
    std::snprintf(buf, sizeof(buf), "%llu B", (unsigned long long)b);
  return buf;
}

static std::string rainbow(const char* text) {
  // AMOURANTHRTX ribbon — pink → cyan → mint → gold
  static const char* cols[] = {PINK, HOT, LILAC, CYAN, MINT, GOLD};
  std::string out;
  size_t n = std::strlen(text);
  for (size_t i = 0; i < n; ++i) {
    out += cols[i % 6];
    out += text[i];
  }
  out += RESET;
  return out;
}
}  // namespace ui

// ── GPU model ──────────────────────────────────────────────────────────────
enum class Vendor { AMD, NVIDIA, Intel, Virt, Unknown };

struct Gpu {
  int card = -1;
  Vendor vendor = Vendor::Unknown;
  std::string name;
  std::string bus;
  std::string driver;
  double util = -1;        // %
  double mem_util = -1;    // %
  uint64_t vram_total = 0;
  uint64_t vram_used = 0;
  uint64_t gtt_total = 0;
  uint64_t gtt_used = 0;
  double temp_c = -1;
  double power_w = -1;
  double power_cap_w = -1;
  double sclk_mhz = -1;
  double mclk_mhz = -1;
  std::string link;  // PCIe
  std::string path;  // sysfs device
};

static uint64_t read_u64_file(const std::string& p) {
  std::string s = read_file(p.c_str(), 64);
  if (s.empty()) return 0;
  return std::strtoull(s.c_str(), nullptr, 0);
}

static double read_f_file(const std::string& p) {
  std::string s = read_file(p.c_str(), 64);
  if (s.empty()) return -1;
  return std::strtod(s.c_str(), nullptr);
}

static std::string vendor_name(Vendor v) {
  switch (v) {
    case Vendor::AMD: return "AMD";
    case Vendor::NVIDIA: return "NVIDIA";
    case Vendor::Intel: return "Intel";
    case Vendor::Virt: return "Virt";
    default: return "GPU";
  }
}

static Vendor vendor_from_id(uint32_t id, const std::string& driver) {
  if (id == 0x1002 || contains(driver, "amdgpu") || contains(driver, "radeon")) return Vendor::AMD;
  if (id == 0x10de || contains(driver, "nvidia")) return Vendor::NVIDIA;
  if (id == 0x8086 || contains(driver, "i915") || contains(driver, "xe")) return Vendor::Intel;
  if (contains(driver, "virtio") || contains(driver, "vbox") || contains(driver, "simple-framebuffer"))
    return Vendor::Virt;
  return Vendor::Unknown;
}

static void fill_hwmon(Gpu& g) {
  std::string hm = g.path + "/hwmon";
  DIR* d = ::opendir(hm.c_str());
  if (!d) return;
  while (dirent* e = ::readdir(d)) {
    if (std::strncmp(e->d_name, "hwmon", 5) != 0) continue;
    std::string base = hm + "/" + e->d_name;
    // temp
    for (int i = 1; i <= 4; ++i) {
      char p[512];
      std::snprintf(p, sizeof(p), "%s/temp%d_input", base.c_str(), i);
      double t = read_f_file(p);
      if (t > 0) {
        g.temp_c = t / 1000.0;
        break;
      }
    }
    // power (µW)
    for (const char* pf : {"power1_average", "power1_input"}) {
      double p = read_f_file(base + "/" + pf);
      if (p > 0) {
        g.power_w = p / 1e6;
        break;
      }
    }
    double cap = read_f_file(base + "/power1_cap");
    if (cap > 0) g.power_cap_w = cap / 1e6;
    // clocks (Hz)
    for (int i = 1; i <= 3; ++i) {
      char p[512], lab[512];
      std::snprintf(p, sizeof(p), "%s/freq%d_input", base.c_str(), i);
      std::snprintf(lab, sizeof(lab), "%s/freq%d_label", base.c_str(), i);
      double f = read_f_file(p);
      if (f <= 0) continue;
      std::string label = read_file(lab, 64);
      tolower_inplace(label);
      double mhz = f / 1e6;
      if (contains(label, "sclk") || contains(label, "gfx") || contains(label, "core"))
        g.sclk_mhz = mhz;
      else if (contains(label, "mclk") || contains(label, "mem"))
        g.mclk_mhz = mhz;
      else if (g.sclk_mhz < 0)
        g.sclk_mhz = mhz;
      else if (g.mclk_mhz < 0)
        g.mclk_mhz = mhz;
    }
  }
  ::closedir(d);
}

static void fill_amd(Gpu& g) {
  double busy = read_f_file(g.path + "/gpu_busy_percent");
  if (busy >= 0) g.util = busy;
  g.vram_total = read_u64_file(g.path + "/mem_info_vram_total");
  g.vram_used = read_u64_file(g.path + "/mem_info_vram_used");
  g.gtt_total = read_u64_file(g.path + "/mem_info_gtt_total");
  g.gtt_used = read_u64_file(g.path + "/mem_info_gtt_used");
  if (g.vram_total > 0) g.mem_util = 100.0 * double(g.vram_used) / double(g.vram_total);
  std::string speed = read_file((g.path + "/current_link_speed").c_str(), 64);
  std::string width = read_file((g.path + "/current_link_width").c_str(), 64);
  while (!speed.empty() && (speed.back() == '\n' || speed.back() == ' ')) speed.pop_back();
  while (!width.empty() && (width.back() == '\n' || width.back() == ' ')) width.pop_back();
  if (!speed.empty() || !width.empty()) g.link = "PCIe " + speed + " x" + width;
  fill_hwmon(g);
  // pretty name from uevent or marketing
  std::string ue = read_file((g.path + "/uevent").c_str(), 4096);
  // product name on some cards
  std::string pn = read_file((g.path + "/product_name").c_str(), 128);
  while (!pn.empty() && (pn.back() == '\n')) pn.pop_back();
  if (!pn.empty())
    g.name = pn;
  else {
    char buf[128];
    std::snprintf(buf, sizeof(buf), "AMD Radeon (card%d)", g.card);
    g.name = buf;
  }
}

static void fill_nvidia_sysfs(Gpu& g) {
  // limited without NVML — still show bus + driver
  g.name = "NVIDIA GPU";
  fill_hwmon(g);
  // try nvidia-smi one-shot for this bus if available
}

static bool nvidia_smi_enrich(std::vector<Gpu>& gpus) {
  // Parse CSV: index, name, util.gpu, util.mem, memory.total, memory.used, temperature.gpu, power.draw, power.limit, clocks.sm, clocks.mem, pci.bus_id
  FILE* fp = ::popen(
      "nvidia-smi --query-gpu=index,name,utilization.gpu,utilization.memory,memory.total,memory.used,"
      "temperature.gpu,power.draw,power.limit,clocks.sm,clocks.mem,pci.bus_id "
      "--format=csv,noheader,nounits 2>/dev/null",
      "r");
  if (!fp) return false;
  char line[1024];
  bool any = false;
  while (std::fgets(line, sizeof(line), fp)) {
    // split CSV
    std::vector<std::string> f;
    std::string cur;
    for (char* p = line; *p; ++p) {
      if (*p == ',') {
        while (!cur.empty() && cur.front() == ' ') cur.erase(cur.begin());
        while (!cur.empty() && (cur.back() == ' ' || cur.back() == '\n' || cur.back() == '\r'))
          cur.pop_back();
        f.push_back(cur);
        cur.clear();
      } else
        cur.push_back(*p);
    }
    while (!cur.empty() && (cur.back() == '\n' || cur.back() == '\r' || cur.front() == ' ')) {
      if (cur.back() == '\n' || cur.back() == '\r') cur.pop_back();
      else if (cur.front() == ' ') cur.erase(cur.begin());
      else
        break;
    }
    if (!cur.empty()) {
      while (!cur.empty() && cur.front() == ' ') cur.erase(cur.begin());
      f.push_back(cur);
    }
    if (f.size() < 11) continue;
    any = true;
    int idx = std::atoi(f[0].c_str());
    Gpu* target = nullptr;
    for (auto& g : gpus) {
      if (g.vendor == Vendor::NVIDIA && (g.card == idx || g.bus.find(f[11]) != std::string::npos)) {
        target = &g;
        break;
      }
    }
    Gpu local;
    Gpu& g = target ? *target : local;
    g.vendor = Vendor::NVIDIA;
    g.card = idx;
    g.name = f[1];
    g.util = std::strtod(f[2].c_str(), nullptr);
    g.mem_util = std::strtod(f[3].c_str(), nullptr);
    g.vram_total = (uint64_t)(std::strtod(f[4].c_str(), nullptr) * 1024.0 * 1024.0);
    g.vram_used = (uint64_t)(std::strtod(f[5].c_str(), nullptr) * 1024.0 * 1024.0);
    g.temp_c = std::strtod(f[6].c_str(), nullptr);
    g.power_w = std::strtod(f[7].c_str(), nullptr);
    g.power_cap_w = std::strtod(f[8].c_str(), nullptr);
    g.sclk_mhz = std::strtod(f[9].c_str(), nullptr);
    g.mclk_mhz = std::strtod(f[10].c_str(), nullptr);
    if (f.size() > 11) g.bus = f[11];
    g.driver = "nvidia";
    if (!target) gpus.push_back(g);
  }
  ::pclose(fp);
  return any;
}

static void fill_intel(Gpu& g) {
  g.name = "Intel Graphics";
  // try gt_cur_freq_mhz
  double f = read_f_file(g.path + "/gt_cur_freq_mhz");
  if (f < 0) f = read_f_file(g.path + "/gt/gt0/rps_cur_freq_mhz");
  if (f > 0) g.sclk_mhz = f;
  fill_hwmon(g);
  // busy sometimes via drm clients — leave util -1 if unknown
}

static std::vector<Gpu> scan_gpus() {
  std::vector<Gpu> out;
  DIR* d = ::opendir("/sys/class/drm");
  if (!d) return out;
  while (dirent* e = ::readdir(d)) {
    // cardN only, not cardN-DP-1
    if (std::strncmp(e->d_name, "card", 4) != 0) continue;
    const char* rest = e->d_name + 4;
    if (!is_digits(rest)) continue;
    int card = std::atoi(rest);
    std::string dev = std::string("/sys/class/drm/") + e->d_name + "/device";
    struct stat st {};
    if (::stat(dev.c_str(), &st) != 0) continue;

    Gpu g;
    g.card = card;
    g.path = dev;
    std::string vend = read_file((dev + "/vendor").c_str(), 32);
    std::string drv_link;
    char buf[256];
    ssize_t n = ::readlink((dev + "/driver").c_str(), buf, sizeof(buf) - 1);
    if (n > 0) {
      buf[n] = '\0';
      drv_link = buf;
      auto slash = drv_link.find_last_of('/');
      g.driver = slash == std::string::npos ? drv_link : drv_link.substr(slash + 1);
    }
    uint32_t vid = 0;
    if (!vend.empty()) vid = (uint32_t)std::strtoul(vend.c_str(), nullptr, 0);
    g.vendor = vendor_from_id(vid, g.driver);
    if (g.vendor == Vendor::Virt && g.driver == "simple-framebuffer") {
      // skip pure framebuffer stubs if we have real GPUs later — keep for now as low priority
    }
    std::string ue = read_file((dev + "/uevent").c_str(), 2048);
    // PCI_SLOT_NAME=
    auto pos = ue.find("PCI_SLOT_NAME=");
    if (pos != std::string::npos) {
      pos += 14;
      size_t end = ue.find('\n', pos);
      g.bus = ue.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
    }

    switch (g.vendor) {
      case Vendor::AMD: fill_amd(g); break;
      case Vendor::NVIDIA: fill_nvidia_sysfs(g); break;
      case Vendor::Intel: fill_intel(g); break;
      default: {
        char nm[64];
        std::snprintf(nm, sizeof(nm), "%s card%d", vendor_name(g.vendor).c_str(), card);
        g.name = nm;
        fill_hwmon(g);
      } break;
    }
    out.push_back(std::move(g));
  }
  ::closedir(d);

  // Prefer real GPUs over simple-framebuffer when both exist
  bool has_real = false;
  for (const auto& g : out)
    if (g.vendor != Vendor::Virt && g.vendor != Vendor::Unknown) has_real = true;
  if (has_real) {
    out.erase(std::remove_if(out.begin(), out.end(),
                             [](const Gpu& g) {
                               return g.vendor == Vendor::Virt || g.driver == "simple-framebuffer";
                             }),
              out.end());
  }

  nvidia_smi_enrich(out);

  std::sort(out.begin(), out.end(), [](const Gpu& a, const Gpu& b) { return a.card < b.card; });
  return out;
}

// ── clients (fdinfo DRM) ───────────────────────────────────────────────────
struct Client {
  pid_t pid = 0;
  std::string comm;
  uint64_t mem = 0;
  std::string engine;
};

static std::vector<Client> scan_clients() {
  std::vector<Client> clients;
  DIR* pd = ::opendir("/proc");
  if (!pd) return clients;
  while (dirent* pe = ::readdir(pd)) {
    if (!is_digits(pe->d_name)) continue;
    pid_t pid = (pid_t)std::atoi(pe->d_name);
    std::string fdinfo = std::string("/proc/") + pe->d_name + "/fdinfo";
    DIR* fd = ::opendir(fdinfo.c_str());
    if (!fd) continue;
    uint64_t mem = 0;
    bool drm = false;
    while (dirent* fe = ::readdir(fd)) {
      if (!is_digits(fe->d_name)) continue;
      std::string body = read_file((fdinfo + "/" + fe->d_name).c_str(), 8192);
      if (body.find("drm-driver:") == std::string::npos && body.find("drm-pdev:") == std::string::npos)
        continue;
      drm = true;
      // drm-memory-vram: 123 KiB  OR  drm-shared-vram:
      auto grab = [&](const char* key) {
        auto p = body.find(key);
        if (p == std::string::npos) return;
        p += std::strlen(key);
        while (p < body.size() && (body[p] == ' ' || body[p] == '\t')) ++p;
        uint64_t v = std::strtoull(body.c_str() + p, nullptr, 10);
        // unit KiB often
        if (body.find("KiB", p) != std::string::npos && body.find("KiB", p) < p + 32)
          v *= 1024;
        mem += v;
      };
      grab("drm-memory-vram:");
      grab("drm-shared-vram:");
      grab("drm-memory-gtt:");
    }
    ::closedir(fd);
    if (!drm) continue;
    Client c;
    c.pid = pid;
    c.comm = read_comm(pid);
    c.mem = mem;
    clients.push_back(std::move(c));
  }
  ::closedir(pd);
  std::sort(clients.begin(), clients.end(),
            [](const Client& a, const Client& b) { return a.mem > b.mem; });
  if (clients.size() > 12) clients.resize(12);
  return clients;
}

// ── render ─────────────────────────────────────────────────────────────────
static void draw(const std::vector<Gpu>& gpus, const std::vector<Client>& clients, int tick) {
  const int W = ui::cols();
  std::string out;
  out.reserve(8192);
  out += ui::CLEAR;
  out += ui::HIDE;
  out += ui::BG;

  // header
  out += ui::BOLD;
  out += ui::rainbow("◆ AMOURANTHRTX");
  out += ui::RESET;
  out += std::string(ui::PINK) + "  field-nvtop  " + std::string(ui::RESET);
  out += std::string(ui::DIM) + "Field GPU · AMD · NVIDIA · Intel · DRM" + std::string(ui::RESET);
  out += "\n";
  out += ui::CYAN;
  for (int i = 0; i < W - 1; ++i) out += (i % 4 == 0 ? "━" : "─");
  out += std::string(ui::RESET) + "\n";

  if (gpus.empty()) {
    out += std::string(ui::HOT) + "  No GPUs found under /sys/class/drm (and no nvidia-smi).\n" + std::string(ui::RESET);
  }

  for (const auto& g : gpus) {
    const char* vcol = g.vendor == Vendor::AMD     ? ui::HOT
                       : g.vendor == Vendor::NVIDIA ? ui::MINT
                       : g.vendor == Vendor::Intel  ? ui::CYAN
                                                    : ui::LILAC;
    char line[512];
    std::snprintf(line, sizeof(line), " %s●%s  %s%s%s  %s[%s]%s  card%d  %s%s%s",
                  vcol, ui::RESET, ui::BOLD, g.name.c_str(), ui::RESET, ui::DIM,
                  vendor_name(g.vendor).c_str(), ui::RESET, g.card, ui::DIM, g.driver.c_str(),
                  ui::RESET);
    out += line;
    out += "\n";
    if (!g.bus.empty() || !g.link.empty()) {
      out += std::string(ui::DIM) + "     ";
      out += g.bus;
      if (!g.link.empty()) {
        out += "  ·  ";
        out += g.link;
      }
      out += std::string(ui::RESET) + "\n";
    }

    int bar_w = W - 28;
    if (bar_w < 10) bar_w = 10;
    if (bar_w > 50) bar_w = 50;

    // GPU util
    {
      double u = g.util >= 0 ? g.util : 0;
      char lab[64];
      if (g.util >= 0)
        std::snprintf(lab, sizeof(lab), "%5.1f%%", g.util);
      else
        std::snprintf(lab, sizeof(lab), "  n/a");
      out += "     GPU  ";
      out += ui::bar(u, bar_w, true);
      out += "  ";
      out += ui::WHITE;
      out += lab;
      out += std::string(ui::RESET) + "\n";
    }
    // MEM
    {
      double u = g.mem_util >= 0 ? g.mem_util : 0;
      char lab[96];
      if (g.vram_total > 0)
        std::snprintf(lab, sizeof(lab), "%s / %s", ui::human_bytes(g.vram_used).c_str(),
                      ui::human_bytes(g.vram_total).c_str());
      else
        std::snprintf(lab, sizeof(lab), "n/a");
      out += "     VRAM ";
      out += ui::bar(u, bar_w, false);
      out += "  ";
      out += ui::GOLD;
      out += lab;
      out += std::string(ui::RESET) + "\n";
    }
    // stats row
    out += "     ";
    auto chip = [&](const char* k, const std::string& v, const char* col) {
      out += col;
      out += k;
      out += ui::WHITE;
      out += v;
      out += std::string(ui::DIM) + "  │  " + std::string(ui::RESET);
    };
    if (g.temp_c >= 0) {
      char b[32];
      std::snprintf(b, sizeof(b), "%.0f°C", g.temp_c);
      chip("temp ", b, g.temp_c >= 85 ? ui::RED : ui::CYAN);
    }
    if (g.power_w >= 0) {
      char b[48];
      if (g.power_cap_w > 0)
        std::snprintf(b, sizeof(b), "%.1f / %.0f W", g.power_w, g.power_cap_w);
      else
        std::snprintf(b, sizeof(b), "%.1f W", g.power_w);
      chip("pwr ", b, ui::MINT);
    }
    if (g.sclk_mhz >= 0) {
      char b[32];
      std::snprintf(b, sizeof(b), "%.0f MHz", g.sclk_mhz);
      chip("sclk ", b, ui::LILAC);
    }
    if (g.mclk_mhz >= 0) {
      char b[32];
      std::snprintf(b, sizeof(b), "%.0f MHz", g.mclk_mhz);
      chip("mclk ", b, ui::PINK);
    }
    if (g.gtt_total > 0) {
      chip("gtt ", ui::human_bytes(g.gtt_used) + "/" + ui::human_bytes(g.gtt_total), ui::DIM);
    }
    out += "\n\n";
  }

  // clients
  out += ui::CYAN;
  for (int i = 0; i < W - 1; ++i) out += "─";
  out += std::string(ui::RESET) + "\n";
  out += std::string(ui::BOLD) + std::string(ui::PINK) + "  DRM clients" + std::string(ui::RESET) + std::string(ui::DIM) + "  (fdinfo · vram/gtt touch)\n" + std::string(ui::RESET);
  out += std::string(ui::DIM) + "  PID      COMM                 MEM\n" + std::string(ui::RESET);
  if (clients.empty()) {
    out += std::string(ui::DIM) + "  (none reported — no open DRM clients)\n" + std::string(ui::RESET);
  }
  for (const auto& c : clients) {
    char line[256];
    std::snprintf(line, sizeof(line), "  %-8d %-20.20s %s\n", (int)c.pid, c.comm.c_str(),
                  ui::human_bytes(c.mem).c_str());
    out += ui::WHITE;
    out += line;
    out += ui::RESET;
  }

  // footer pulse
  static const char* spin = "⠋⠙⠹⠸⠼⠴⠦⠧⠇⠏";
  // each glyph is 3 bytes utf8
  int si = (tick % 10) * 3;
  out += "\n";
  out += std::string(ui::DIM) + "  ";
  out += ui::PINK;
  out += std::string(spin + si, spin + si + 3);
  out += std::string(ui::RESET) + std::string(ui::DIM) + "  q quit  ·  Field fabric · AMOURANTHRTX  ·  God Bless\n" + std::string(ui::RESET);
  out += ui::RESET;

  std::fwrite(out.data(), 1, out.size(), stdout);
  std::fflush(stdout);
}

static void usage() {
  std::printf(
      "field-nvtop — AMOURANTHRTX Field GPU top\n"
      "  field-nvtop           live TUI (AMD/NVIDIA/Intel/DRM)\n"
      "  field-nvtop --once    one snapshot\n"
      "  field-nvtop --json    machine JSON\n"
      "Aliases: nv-top, nvtop, field-gpu\n"
      "C++ product path · no scripts · Field native\n");
}

static std::string json_escape(const std::string& s) {
  std::string o;
  o.reserve(s.size() + 8);
  for (char c : s) {
    if (c == '"' || c == '\\') o.push_back('\\');
    if (c == '\n' || c == '\r') continue;
    o.push_back(c);
  }
  return o;
}

static void print_json(const std::vector<Gpu>& gpus) {
  std::printf("{\n  \"schema\": \"field-nvtop/v1\",\n  \"brand\": \"AMOURANTHRTX\",\n  "
              "\"updated\": \"%s\",\n  \"gpus\": [\n",
              now_z_sec().c_str());
  for (size_t i = 0; i < gpus.size(); ++i) {
    const auto& g = gpus[i];
    std::printf(
        "    {\"card\":%d,\"vendor\":\"%s\",\"name\":\"%s\",\"driver\":\"%s\",\"bus\":\"%s\","
        "\"util_pct\":%.2f,\"mem_util_pct\":%.2f,\"vram_total\":%llu,\"vram_used\":%llu,"
        "\"temp_c\":%.1f,\"power_w\":%.2f,\"sclk_mhz\":%.1f,\"mclk_mhz\":%.1f}%s\n",
        g.card, vendor_name(g.vendor).c_str(), json_escape(g.name).c_str(), g.driver.c_str(),
        json_escape(g.bus).c_str(), g.util, g.mem_util, (unsigned long long)g.vram_total,
        (unsigned long long)g.vram_used, g.temp_c, g.power_w, g.sclk_mhz, g.mclk_mhz,
        i + 1 < gpus.size() ? "," : "");
  }
  std::printf("  ]\n}\n");
}

int main(int argc, char** argv) {
  bool once = false, json = false;
  for (int i = 1; i < argc; ++i) {
    if (!std::strcmp(argv[i], "--once") || !std::strcmp(argv[i], "-1")) once = true;
    else if (!std::strcmp(argv[i], "--json") || !std::strcmp(argv[i], "-j")) json = true;
    else if (!std::strcmp(argv[i], "-h") || !std::strcmp(argv[i], "--help")) {
      usage();
      return 0;
    }
  }

  if (json || once) {
    auto gpus = scan_gpus();
    if (json) {
      print_json(gpus);
      return 0;
    }
    auto clients = scan_clients();
    draw(gpus, clients, 0);
    std::fputs(ui::SHOW, stdout);
    return 0;
  }

  ::signal(SIGINT, on_sig);
  ::signal(SIGTERM, on_sig);
  termios oldt{}, newt{};
  bool raw = false;
  if (isatty(STDIN_FILENO) && tcgetattr(STDIN_FILENO, &oldt) == 0) {
    newt = oldt;
    newt.c_lflag &= ~(ICANON | ECHO);
    newt.c_cc[VMIN] = 0;
    newt.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);
    raw = true;
  }

  int tick = 0;
  while (g_run) {
    auto gpus = scan_gpus();
    auto clients = scan_clients();
    draw(gpus, clients, tick++);
    // non-blocking key
    char c = 0;
    if (raw && ::read(STDIN_FILENO, &c, 1) == 1) {
      if (c == 'q' || c == 'Q' || c == 3) break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
  }

  if (raw) tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
  std::fputs((std::string(ui::SHOW) + "\033[0m\n").c_str(), stdout);
  return 0;
}

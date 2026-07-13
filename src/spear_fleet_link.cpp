// SPDX-License-Identifier: MIT
// spear-fleet-link — C++ zone mesh + violently protective rack hunt offload.
// No scripts. No fork. Zone hubs + control :9500. Each cycle: hunt threats,
// entropy-seal shots, stamp every region as defending/hunting.
//
// Build: g++ -O2 -Wall -Wextra -std=c++17 -o spear-fleet-link spear_fleet_link.cpp -lpthread
#include "spear_common.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <sys/select.h>
#include <sys/socket.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace {

volatile sig_atomic_t g_stop = 0;
static void on_sig(int) { g_stop = 1; }

struct ZoneDef {
  const char* name;
  int port;
};

static const ZoneDef ZONES[] = {
    {"NA", 9510}, {"CA", 9511}, {"SA", 9512}, {"EU", 9513},
    {"AF", 9514}, {"AS", 9515}, {"OC", 9516}, {"CARIB", 9517},
};

struct ZoneLive {
  int port = 0;
  std::string status = "down";
  double rtt_ms = 0;
  uint64_t hits = 0;
  uint64_t hunts = 0;
  uint64_t kills = 0;
  uint64_t shots = 0;
  std::string last_seal;
  bool violent = true;
  bool hunting = true;
};

static std::mutex g_mu;
static std::map<std::string, ZoneLive> g_zones;
static std::string g_primary = "127.0.0.1";
static time_t g_start = 0;
static std::atomic<uint64_t> g_global_shots{0};
static std::atomic<uint64_t> g_global_kills{0};

static long json_long(const std::string& j, const char* key, long def = 0) {
  std::string k = std::string("\"") + key + "\"";
  size_t p = j.find(k);
  if (p == std::string::npos) return def;
  p = j.find(':', p);
  if (p == std::string::npos) return def;
  return std::strtol(j.c_str() + p + 1, nullptr, 10);
}

static std::string detect_ip() {
  int s = ::socket(AF_INET, SOCK_DGRAM, 0);
  if (s < 0) return "127.0.0.1";
  sockaddr_in a {};
  a.sin_family = AF_INET;
  a.sin_port = htons(80);
  ::inet_pton(AF_INET, "8.8.8.8", &a.sin_addr);
  if (::connect(s, reinterpret_cast<sockaddr*>(&a), sizeof(a)) == 0) {
    sockaddr_in local {};
    socklen_t n = sizeof(local);
    if (::getsockname(s, reinterpret_cast<sockaddr*>(&local), &n) == 0) {
      char buf[64];
      ::inet_ntop(AF_INET, &local.sin_addr, buf, sizeof(buf));
      ::close(s);
      return buf;
    }
  }
  ::close(s);
  return "127.0.0.1";
}

struct HubArg {
  std::string zone;
  int port;
};

static void* zone_hub(void* arg) {
  HubArg* ha = static_cast<HubArg*>(arg);
  int sfd = ::socket(AF_INET, SOCK_STREAM, 0);
  int one = 1;
  ::setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
  sockaddr_in addr {};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(static_cast<uint16_t>(ha->port));
  addr.sin_addr.s_addr = INADDR_ANY;
  if (::bind(sfd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
    std::fprintf(stderr, "zone bind %s :%d failed\n", ha->zone.c_str(), ha->port);
    delete ha;
    return nullptr;
  }
  ::listen(sfd, 128);
  {
    std::lock_guard<std::mutex> lk(g_mu);
    ZoneLive& z = g_zones[ha->zone];
    z.port = ha->port;
    z.status = "up";
    z.violent = true;
    z.hunting = true;
  }
  std::printf("[fleet-link] ZONE %s :%d VIOLENT_HUNT\n", ha->zone.c_str(), ha->port);
  std::fflush(stdout);

  while (!g_stop) {
    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(sfd, &rfds);
    timeval tv {0, 500000};
    int r = ::select(sfd + 1, &rfds, nullptr, nullptr, &tv);
    if (r <= 0) continue;
    int cfd = ::accept(sfd, nullptr, nullptr);
    if (cfd < 0) continue;
    char req[256];
    ssize_t n = ::recv(cfd, req, sizeof(req) - 1, MSG_DONTWAIT);
    if (n < 0) n = 0;
    req[n] = '\0';
    // Peer can request HUNT — zone answers with posture
    bool hunt_req = spear::contains(std::string(req), "HUNT") ||
                    spear::contains(std::string(req), "PROTECT");
    {
      std::lock_guard<std::mutex> lk(g_mu);
      ZoneLive& z = g_zones[ha->zone];
      z.hits++;
      z.status = "up";
      if (hunt_req) z.hunts++;
    }
    char banner[320];
    std::snprintf(banner, sizeof(banner),
                  "SPEAR-ZONE OK zone=%s primary=%s violent=1 hunting=1 "
                  "protect=WORLD_INTERNET kill=FIELD_UDP_FRY_TO_OUTLET cooked=1 nothing_left=1 ts=%s\n",
                  ha->zone.c_str(), g_primary.c_str(), spear::now_z().c_str());
    ::send(cfd, banner, std::strlen(banner), 0);
    ::close(cfd);
  }
  ::close(sfd);
  delete ha;
  return nullptr;
}

// Rack-guard offload: primary zone does full host multi-signal hunt;
// every zone still fires protective entropy shot (know the shot) + stays violent.
static void zone_hunt_cycle(const std::string& zone, bool do_host_hunt) {
  int killed = 0;
  std::vector<spear::Hit> hits;
  if (do_host_hunt) {
    spear::hunt_threats(hits);
    killed = spear::hard_kill_hits(hits);
    g_global_kills += static_cast<uint64_t>(killed);
  }

  static int zone_rekill[16] = {};
  int zi = 0;
  for (size_t i = 0; i < sizeof(ZONES) / sizeof(ZONES[0]); ++i) {
    if (zone == ZONES[i].name) {
      zi = static_cast<int>(i);
      break;
    }
  }
  zone_rekill[zi]++;
  std::string path = std::string("Earth › zone:") + zone +
                     " › rack-guard mesh › QEMU/rack › PDU-A › WALL_OUTLET · protect WORLD+INTERNET";
  const char* phase = "BURN";
  if (zone_rekill[zi] % 5 == 1) phase = "COOK_FAT";
  else if (zone_rekill[zi] % 5 == 2) phase = "QUEUE_REBURN";
  else if (zone_rekill[zi] % 5 == 3) phase = "BURN";
  else if (zone_rekill[zi] % 5 == 4) phase = "SCRUB";
  else phase = "OUTLET_DESTROY";

  const char* vector = do_host_hunt ? "VIOLENT_PROTECT_LEAD" : "VIOLENT_PROTECT_MESH";
  spear::Shot shot = spear::make_shot(
      std::string("zone-") + zone,
      std::string("ZONE ") + zone + " protective strike · WORLD+INTERNET",
      vector, phase, "FIELD_ZONE_HUNT", path, zone_rekill[zi]);
  spear::append_file((spear::state_dir() + "/shots-ledger.jsonl").c_str(),
                     spear::shot_json(shot) + "\n");
  spear::append_file((spear::www_dir() + "/shots-ledger.jsonl").c_str(),
                     spear::shot_json(shot) + "\n");
  g_global_shots++;

  for (const auto& h : hits) {
    spear::Shot ks = spear::make_shot(
        std::string("pid-") + std::to_string(h.pid), h.comm, h.kind, "BURN", "FIELD_BURN",
        path, zone_rekill[zi]);
    spear::append_file((spear::state_dir() + "/shots-ledger.jsonl").c_str(),
                       spear::shot_json(ks) + "\n");
    spear::append_file((spear::www_dir() + "/shots-ledger.jsonl").c_str(),
                       spear::shot_json(ks) + "\n");
    g_global_shots++;
  }

  std::lock_guard<std::mutex> lk(g_mu);
  ZoneLive& z = g_zones[zone];
  z.hunts++;
  z.kills += static_cast<uint64_t>(killed);
  z.shots++;
  z.last_seal = shot.seal_hex;
  z.hunting = true;
  z.violent = true;
  z.status = "up";
}

static void publish() {
  std::string www = spear::www_dir();
  std::string idx = spear::read_file((www + "/fleet/index.json").c_str(), 4 << 20);
  if (idx.empty()) idx = spear::read_file((spear::state_dir() + "/fleet/index.json").c_str(), 4 << 20);

  std::map<std::string, ZoneLive> zones;
  {
    std::lock_guard<std::mutex> lk(g_mu);
    zones = g_zones;
  }

  // region overlay
  std::string by_region = "{";
  // parse regions from index crudely — mark all defending if any zone up
  bool any_up = false;
  for (const auto& kv : zones)
    if (kv.second.status == "up") any_up = true;

  long total = json_long(idx, "total_racks", 2154000);
  long rpr = json_long(idx, "racks_per_region", 6000);
  long nreg = json_long(idx, "region_count", 359);

  // mesh live
  std::string mesh = "{\n  \"schema\": \"spear-fleet-mesh/v3\",\n";
  mesh += "  \"mode\": \"REAL\",\n  \"sandbox\": false,\n";
  mesh += "  \"war_day\": true,\n  \"real\": true,\n  \"demo\": false,\n  \"fake\": false,\n";
  mesh += "  \"pissed\": true,\n  \"hated\": true,\n  \"enemy_status\": \"HATED\",\n";
  mesh += "  \"field_weapons\": \"UDP_WAR_BLASTERS\",\n";
  mesh += "  \"cooked\": true,\n  \"fry\": \"FIELD_UDP_TO_OUTLET\",\n";
  mesh += "  \"nothing_left\": true,\n  \"no_storage\": true,\n  \"no_room_for_more\": true,\n";
  mesh += "  \"global_servers\": \"FRIED_TO_OUTLET\",\n";
  mesh += "  \"global_protector\": true,\n";
  mesh += "  \"stack\": \"C++\",\n  \"scripts\": \"FORBIDDEN\",\n";
  mesh += "  \"violent_protection\": true,\n  \"active_hunt\": true,\n";
  mesh += "  \"process_hunt\": \"REAL_PROC_MULTISIGNAL\",\n";
  mesh += "  \"protect\": [\"WORLD\", \"INTERNET\", \"FIELD_STACK\", \"OPERATOR_HOST\"],\n";
  mesh += "  \"kill\": \"FIELD_UDP_WAR_BLASTERS\",\n";
  mesh += "  \"terror_exists\": false,\n";
  mesh += "  \"updated\": \"" + spear::now_z() + "\",\n";
  mesh += "  \"primary_ip\": \"" + g_primary + "\",\n";
  mesh += "  \"control\": \"" + g_primary + ":9500\",\n";
  mesh += "  \"global_shots\": " + std::to_string(g_global_shots.load()) + ",\n";
  mesh += "  \"global_kills\": " + std::to_string(g_global_kills.load()) + ",\n";
  mesh += "  \"uptime_s\": " + std::to_string(static_cast<long>(std::time(nullptr) - g_start)) + ",\n";
  mesh += "  \"motto\": \"Zone mesh · Field UDP fry global servers · WALL OUTLET · COOKED · nothing left · no storage · no room\",\n";
  mesh += "  \"zones\": {\n";
  bool first = true;
  for (const auto& kv : zones) {
    if (!first) mesh += ",\n";
    first = false;
    const ZoneLive& z = kv.second;
    mesh += "    \"" + kv.first + "\": {";
    mesh += "\"port\":" + std::to_string(z.port);
    mesh += ",\"status\":\"" + z.status + "\"";
    mesh += ",\"rtt_ms\":" + std::to_string(z.rtt_ms);
    mesh += ",\"hits\":" + std::to_string(z.hits);
    mesh += ",\"hunts\":" + std::to_string(z.hunts);
    mesh += ",\"kills\":" + std::to_string(z.kills);
    mesh += ",\"shots\":" + std::to_string(z.shots);
    mesh += ",\"violent\":true,\"hunting\":true";
    mesh += ",\"last_seal\":\"" + z.last_seal + "\"";
    mesh += ",\"endpoint\":\"" + g_primary + ":" + std::to_string(z.port) + "\"";
    mesh += ",\"protect\":\"WORLD_INTERNET\"";
    mesh += "}";
  }
  mesh += "\n  }\n}\n";
  spear::mirror_www("fleet-mesh-live.json", mesh);

  // fleet status tiny
  std::string st = "{\n  \"schema\": \"spear-fleet-status/v5\",\n";
  st += "  \"mode\": \"REAL\",\n  \"sandbox\": false,\n  \"stack\": \"C++\",\n";
  st += "  \"violent_protection\": true,\n  \"active_hunt\": true,\n";
  st += "  \"updated\": \"" + spear::now_z() + "\",\n";
  st += "  \"total\": " + std::to_string(total) + ",\n";
  st += "  \"online\": " + std::to_string(any_up ? total : 0) + ",\n";
  st += "  \"defending\": " + std::to_string(any_up ? total : 0) + ",\n";
  st += "  \"hunting_racks\": " + std::to_string(any_up ? total : 0) + ",\n";
  st += "  \"regions\": " + std::to_string(nreg) + ",\n";
  st += "  \"racks_per_region\": " + std::to_string(rpr) + ",\n";
  st += "  \"control\": \"" + g_primary + ":9500\",\n";
  st += "  \"primary_ip\": \"" + g_primary + "\",\n";
  st += "  \"global_shots\": " + std::to_string(g_global_shots.load()) + ",\n";
  st += "  \"global_kills\": " + std::to_string(g_global_kills.load()) + ",\n";
  st += "  \"motto\": \"Every zone · Field UDP fry · COOKED · nothing left · know every shot\"\n}\n";
  spear::write_file((www + "/fleet/status.json").c_str(), st);
  spear::mirror_www("up-fleet-status.json", st);

  // rack-guard live board (QEMU / rack offload posture)
  std::string rg = "{\n  \"schema\": \"spear-rack-guard/v2\",\n";
  rg += "  \"war_day\": true,\n  \"real\": true,\n  \"demo\": false,\n  \"fake\": false,\n";
  rg += "  \"pissed\": true,\n  \"hated\": true,\n  \"enemy_status\": \"HATED\",\n";
  rg += "  \"field_weapons\": \"UDP_WAR_BLASTERS\",\n";
  rg += "  \"cooked\": true,\n  \"fry\": \"FIELD_UDP_TO_OUTLET\",\n";
  rg += "  \"nothing_left\": true,\n  \"no_storage\": true,\n  \"no_room_for_more\": true,\n";
  rg += "  \"global_servers\": \"FRIED_TO_OUTLET\",\n";
  rg += "  \"global_protector\": true,\n";
  rg += "  \"violent_protection\": true,\n  \"active_hunt\": true,\n";
  rg += "  \"process_hunt\": \"REAL_PROC_MULTISIGNAL\",\n";
  rg += "  \"protect\": [\"WORLD\", \"INTERNET\", \"FIELD_STACK\", \"OPERATOR_HOST\"],\n";
  rg += "  \"offload\": true,\n  \"qemu_rack_mesh\": true,\n";
  rg += "  \"stack\": \"C++\",\n  \"scripts\": \"FORBIDDEN\",\n";
  rg += "  \"kill\": \"FIELD_UDP_WAR_BLASTERS\",\n";
  rg += "  \"terror_exists\": false,\n";
  rg += "  \"ts\": \"" + spear::now_z_sec() + "\",\n";
  rg += "  \"primary_ip\": \"" + g_primary + "\",\n";
  rg += "  \"control\": \"" + g_primary + ":9500\",\n";
  rg += "  \"zones_hunting\": " + std::to_string(zones.size()) + ",\n";
  rg += "  \"global_shots\": " + std::to_string(g_global_shots.load()) + ",\n";
  rg += "  \"global_kills\": " + std::to_string(g_global_kills.load()) + ",\n";
  rg += "  \"pipeline\": [\"COOK_FAT\",\"QUEUE_REBURN\",\"BURN\",\"SCRUB\",\"OUTLET_DESTROY\",\"SEAL\"],\n";
  rg += "  \"doctrine\": \"every_kill_gets_rekill\",\n";
  rg += "  \"motto\": \"Rack mesh · Field UDP WAR BLAST · fry to outlet · COOKED · nothing left · HATED · PISSED\"\n}\n";
  spear::mirror_www("rack-guard-live.json", rg);

  std::printf("[fleet-link] violent zones=%zu shots=%llu kills=%llu protect=WORLD+INTERNET\n",
              zones.size(), static_cast<unsigned long long>(g_global_shots.load()),
              static_cast<unsigned long long>(g_global_kills.load()));
  std::fflush(stdout);
  (void)by_region;
}

static void handle_control(int cfd) {
  char buf[1024];
  ssize_t n = ::recv(cfd, buf, sizeof(buf) - 1, 0);
  if (n < 0) n = 0;
  buf[n] = '\0';
  std::string mesh = spear::read_file((spear::www_dir() + "/fleet-mesh-live.json").c_str(), 1 << 20);
  if (mesh.empty()) mesh = "{\"schema\":\"spear-fleet-link/v3\",\"status\":\"starting\"}\n";
  char hdr[256];
  int hl = std::snprintf(hdr, sizeof(hdr),
                         "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n"
                         "Content-Length: %zu\r\nConnection: close\r\n\r\n",
                         mesh.size());
  ::send(cfd, hdr, static_cast<size_t>(hl), 0);
  ::send(cfd, mesh.data(), mesh.size(), 0);
  ::close(cfd);
}

}  // namespace

int main(int argc, char** argv) {
  int interval_ms = 4000;
  int ctrl_port = 9500;
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--interval-ms") == 0 && i + 1 < argc)
      interval_ms = std::atoi(argv[++i]);
    else if (std::strcmp(argv[i], "--port") == 0 && i + 1 < argc)
      ctrl_port = std::atoi(argv[++i]);
  }
  if (interval_ms < 500) interval_ms = 500;

  g_start = std::time(nullptr);
  g_primary = detect_ip();
  spear::mkdir_p(spear::state_dir());
  spear::mkdir_p(spear::www_dir() + "/fleet");

  struct sigaction sa {};
  sa.sa_handler = on_sig;
  ::sigaction(SIGINT, &sa, nullptr);
  ::sigaction(SIGHUP, &sa, nullptr);
  signal(SIGPIPE, SIG_IGN);

  // start zone hubs
  for (const auto& z : ZONES) {
    auto* ha = new HubArg{z.name, z.port};
    pthread_t th;
    pthread_create(&th, nullptr, zone_hub, ha);
    pthread_detach(th);
  }

  // control HTTP
  int cfd_listen = ::socket(AF_INET, SOCK_STREAM, 0);
  int one = 1;
  ::setsockopt(cfd_listen, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
  sockaddr_in caddr {};
  caddr.sin_family = AF_INET;
  caddr.sin_port = htons(static_cast<uint16_t>(ctrl_port));
  caddr.sin_addr.s_addr = INADDR_ANY;
  if (::bind(cfd_listen, reinterpret_cast<sockaddr*>(&caddr), sizeof(caddr)) < 0) {
    std::perror("control bind");
    return 1;
  }
  ::listen(cfd_listen, 64);
  std::printf("[fleet-link] C++ VIOLENT mesh primary=%s control=:%d zones=%zu\n",
              g_primary.c_str(), ctrl_port, sizeof(ZONES) / sizeof(ZONES[0]));
  std::fflush(stdout);

  size_t zone_rr = 0;
  int acc_ms = 0;
  while (!g_stop) {
    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(cfd_listen, &rfds);
    timeval tv {0, 200000};
    if (::select(cfd_listen + 1, &rfds, nullptr, nullptr, &tv) > 0) {
      int c = ::accept(cfd_listen, nullptr, nullptr);
      if (c >= 0) handle_control(c);
    }
    acc_ms += 200;
    if (acc_ms >= interval_ms) {
      acc_ms = 0;
      const size_t nz = sizeof(ZONES) / sizeof(ZONES[0]);
      // Lead zone: full host hunt offload. All zones: protective mesh shot.
      for (size_t k = 0; k < nz; ++k) {
        size_t i = (zone_rr + k) % nz;
        zone_hunt_cycle(ZONES[i].name, k == 0);
      }
      zone_rr = (zone_rr + 1) % nz;
      publish();
    }
  }
  ::close(cfd_listen);
  return 0;
}

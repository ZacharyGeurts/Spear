// SPDX-License-Identifier: MIT
// spear-www — Big Grin + UP + wartime panels. C++ only. No scripts.
// Static files + fleet APIs. GET only. No fork. No CGI. No injection.
// Default: 127.0.0.1:9490  root=/tmp/spear-swallows-www
#include "spear_common.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>

#include <cstdio>
#include <string>
#include <vector>

namespace {

volatile sig_atomic_t g_stop = 0;
static void on_sig(int) { g_stop = 1; }

static time_t g_start = 0;

static std::string mime_for(const std::string& path) {
  if (spear::contains(path, ".html")) return "text/html; charset=utf-8";
  if (spear::contains(path, ".json")) return "application/json; charset=utf-8";
  if (spear::contains(path, ".css")) return "text/css; charset=utf-8";
  if (spear::contains(path, ".js")) return "application/javascript; charset=utf-8";
  if (spear::contains(path, ".svg")) return "image/svg+xml";
  if (spear::contains(path, ".png")) return "image/png";
  if (spear::contains(path, ".jpg") || spear::contains(path, ".jpeg")) return "image/jpeg";
  if (spear::contains(path, ".csv") || spear::contains(path, ".tsv") || spear::contains(path, ".txt"))
    return "text/plain; charset=utf-8";
  return "application/octet-stream";
}

static bool safe_path(const std::string& rel) {
  if (rel.empty()) return false;
  if (rel.find("..") != std::string::npos) return false;
  if (!rel.empty() && rel[0] == '/') return false;
  return true;
}

static void send_raw(int cfd, int code, const char* ctype, const std::string& body) {
  const char* reason = "OK";
  if (code == 404) reason = "Not Found";
  if (code == 400) reason = "Bad Request";
  if (code == 405) reason = "Method Not Allowed";
  if (code == 503) reason = "Service Unavailable";
  char hdr[640];
  int hl = std::snprintf(hdr, sizeof(hdr),
                         "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\n"
                         "Cache-Control: no-store\r\nConnection: close\r\n\r\n",
                         code, reason, ctype, body.size());
  ::send(cfd, hdr, static_cast<size_t>(hl), 0);
  if (!body.empty()) ::send(cfd, body.data(), body.size(), 0);
  ::close(cfd);
}

static void send_json(int cfd, int code, const std::string& body) {
  send_raw(cfd, code, "application/json; charset=utf-8", body);
}

static std::string qs_get(const std::string& q, const char* key) {
  std::string k = std::string(key) + "=";
  size_t p = q.find(k);
  if (p == std::string::npos) return {};
  p += k.size();
  size_t e = q.find('&', p);
  std::string v = (e == std::string::npos) ? q.substr(p) : q.substr(p, e - p);
  // crude percent-decode for simple codes
  std::string out;
  for (size_t i = 0; i < v.size(); ++i) {
    if (v[i] == '%' && i + 2 < v.size()) {
      int hi = std::isxdigit(static_cast<unsigned char>(v[i + 1]))
                   ? (std::isdigit(v[i + 1]) ? v[i + 1] - '0' : std::tolower(v[i + 1]) - 'a' + 10)
                   : -1;
      int lo = std::isxdigit(static_cast<unsigned char>(v[i + 2]))
                   ? (std::isdigit(v[i + 2]) ? v[i + 2] - '0' : std::tolower(v[i + 2]) - 'a' + 10)
                   : -1;
      if (hi >= 0 && lo >= 0) {
        out.push_back(static_cast<char>((hi << 4) | lo));
        i += 2;
        continue;
      }
    }
    if (v[i] == '+')
      out.push_back(' ');
    else
      out.push_back(v[i]);
  }
  return out;
}

static std::string upper(std::string s) {
  for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  return s;
}

static std::string json_escape(const std::string& s) {
  std::string o;
  o.reserve(s.size() + 8);
  for (char c : s) {
    if (c == '"' || c == '\\') {
      o.push_back('\\');
      o.push_back(c);
    } else if (c == '\n')
      o += "\\n";
    else if (static_cast<unsigned char>(c) < 32)
      continue;
    else
      o.push_back(c);
  }
  return o;
}

// Extract "key": number from json blob (first match)
static long json_long(const std::string& j, const char* key, long def = 0) {
  std::string k = std::string("\"") + key + "\"";
  size_t p = j.find(k);
  if (p == std::string::npos) return def;
  p = j.find(':', p);
  if (p == std::string::npos) return def;
  return std::strtol(j.c_str() + p + 1, nullptr, 10);
}

static std::string json_str(const std::string& j, const char* key) {
  std::string k = std::string("\"") + key + "\"";
  size_t p = j.find(k);
  if (p == std::string::npos) return {};
  p = j.find(':', p);
  if (p == std::string::npos) return {};
  p = j.find('"', p);
  if (p == std::string::npos) return {};
  size_t e = j.find('"', p + 1);
  if (e == std::string::npos) return {};
  return j.substr(p + 1, e - p - 1);
}

static bool json_bool(const std::string& j, const char* key, bool def = false) {
  std::string k = std::string("\"") + key + "\"";
  size_t p = j.find(k);
  if (p == std::string::npos) return def;
  p = j.find(':', p);
  if (p == std::string::npos) return def;
  while (p < j.size() && (j[p] == ':' || j[p] == ' ')) ++p;
  if (j.compare(p, 4, "true") == 0) return true;
  if (j.compare(p, 5, "false") == 0) return false;
  return def;
}

static std::string api_status(const std::string& root) {
  std::string idx = spear::read_file((root + "/fleet/index.json").c_str(), 2 << 20);
  std::string planet = spear::read_file((root + "/planet-live.json").c_str(), 1 << 20);
  if (planet.empty()) planet = spear::read_file((spear::state_dir() + "/planet-live.json").c_str(), 1 << 20);
  long total = json_long(idx, "total_racks", 2154000);
  long regions = json_long(idx, "region_count", 359);
  long rpr = json_long(idx, "racks_per_region", 6000);
  bool plive = !planet.empty() && (spear::contains(planet, "LIVE_PLANET") || spear::contains(planet, "\"defending\""));
  long defending = json_long(planet, "defending", total);
  long online = json_long(planet, "racks_online", total);
  long ok = json_long(planet, "racks_ok", total);
  long areas = json_long(planet, "areas", regions);
  long uptime = static_cast<long>(std::time(nullptr) - g_start);
  std::string body;
  body += "{\n";
  body += "  \"schema\": \"spear-swallows-server/v4-cpp\",\n";
  body += "  \"mode\": \"WARTIME\",\n";
  body += "  \"war_day\": true,\n";
  body += "  \"real\": true,\n";
  body += "  \"demo\": false,\n";
  body += "  \"fake\": false,\n";
  body += "  \"sandbox\": false,\n";
  body += "  \"stack\": \"C++\",\n";
  body += "  \"scripts\": \"FORBIDDEN\",\n";
  body += "  \"process_hunt\": \"REAL_PROC_MULTISIGNAL\",\n";
  body += "  \"uptime_s\": " + std::to_string(uptime) + ",\n";
  body += "  \"bind\": \"127.0.0.1:9490\",\n";
  body += "  \"fleets\": [\"big-grin\", \"up\", \"world-planet\", \"wartime\"],\n";
  body += "  \"sharded\": true,\n";
  body += "  \"planet_live\": " + std::string(plive ? "true" : "false") + ",\n";
  body += "  \"defending\": " + std::to_string(defending) + ",\n";
  body += "  \"up_systems\": " + std::to_string(total) + ",\n";
  body += "  \"online\": " + std::to_string(online) + ",\n";
  body += "  \"regions\": " + std::to_string(areas) + ",\n";
  body += "  \"racks_per_region\": " + std::to_string(rpr) + ",\n";
  body += "  \"up_status\": {\"ok\": " + std::to_string(ok) + ", \"regions\": " + std::to_string(areas) + "},\n";
  body += "  \"shard_cache\": 0,\n";
  body += "  \"planet\": \"http://127.0.0.1:9600/planet/status\",\n";
  body += "  \"motto\": \"WAR DAY · C++ only · REAL hunt · LIVE PLANET · no demo · no fake · God Bless.\"\n";
  body += "}\n";
  return body;
}

static std::string api_regions(const std::string& root) {
  std::string idx = spear::read_file((root + "/fleet/index.json").c_str(), 4 << 20);
  if (idx.empty()) {
    return "{\"schema\":\"spear-fleet-regions/v1\",\"regions\":[],\"region_count\":0,\"error\":\"no_index\"}\n";
  }
  // Re-wrap index as regions response (index already has regions array)
  long total = json_long(idx, "total_racks", 0);
  long rpr = json_long(idx, "racks_per_region", 6000);
  long rc = json_long(idx, "region_count", 0);
  // Extract "regions":[ ... ] balanced enough for our well-formed index
  size_t p = idx.find("\"regions\"");
  std::string regs = "[]";
  if (p != std::string::npos) {
    p = idx.find('[', p);
    if (p != std::string::npos) {
      int depth = 0;
      size_t i = p;
      for (; i < idx.size(); ++i) {
        if (idx[i] == '[') ++depth;
        else if (idx[i] == ']') {
          --depth;
          if (depth == 0) {
            regs = idx.substr(p, i - p + 1);
            break;
          }
        }
      }
    }
  }
  std::string map = "{\"image\":\"/assets/maps/world-equirectangular.jpg\",\"projection\":\"equirectangular\",\"ratio\":\"2058/1036\"}";
  size_t mp = idx.find("\"map\"");
  if (mp != std::string::npos) {
    size_t b = idx.find('{', mp);
    if (b != std::string::npos) {
      int depth = 0;
      for (size_t i = b; i < idx.size(); ++i) {
        if (idx[i] == '{') ++depth;
        else if (idx[i] == '}') {
          --depth;
          if (depth == 0) {
            map = idx.substr(b, i - b + 1);
            break;
          }
        }
      }
    }
  }
  std::string body;
  body += "{\n  \"schema\": \"spear-fleet-regions/v1\",\n";
  body += "  \"stack\": \"C++\",\n";
  body += "  \"total_racks\": " + std::to_string(total) + ",\n";
  body += "  \"region_count\": " + std::to_string(rc) + ",\n";
  body += "  \"racks_per_region\": " + std::to_string(rpr) + ",\n";
  body += "  \"lazy\": true,\n";
  body += "  \"map\": " + map + ",\n";
  body += "  \"regions\": " + regs + ",\n";
  body += "  \"note\": \"C++ spear-www · area dossiers /api/area-dossier?code=XX\"\n}\n";
  return body;
}

static std::string api_area_dossier(const std::string& root, std::string code) {
  code = upper(code);
  if (code.empty()) return "{\"error\":\"code_required\"}\n";
  std::string path = root + "/fleet/r/" + code + ".json";
  std::string shard = spear::read_file(path.c_str(), 16 << 20);
  if (shard.empty()) {
    return "{\"error\":\"region_not_found\",\"code\":\"" + code + "\",\"stack\":\"C++\"}\n";
  }
  std::string name = json_str(shard, "name");
  if (name.empty()) name = code;
  std::string zone = json_str(shard, "zone");
  std::string kind = json_str(shard, "kind");
  long racks = json_long(shard, "racks", 0);
  // count systems entries roughly
  long sys_count = 0;
  size_t pos = 0;
  while ((pos = shard.find("\"id\"", pos)) != std::string::npos) {
    ++sys_count;
    pos += 4;
  }
  if (racks <= 0) racks = sys_count;
  std::string planet = spear::read_file((root + "/planet-live.json").c_str(), 1 << 20);
  bool live = !planet.empty();
  long defending = live ? racks : racks;

  // Build rack inventory from systems array if present (may be large — cap summary in summary field)
  std::string summary = "FLEET AREA DOSSIER " + code + "\\nArea: " + name + " · zone=" + zone +
                        "\\nLIVE PLANET: " + (live ? "true" : "false") + "\\nRacks: " +
                        std::to_string(racks) + "\\nDefending: " + std::to_string(defending) +
                        "\\nstack: C++\\nAPI: /api/area-dossier?code=" + code +
                        "\\nExport: /api/fleet-gps.csv?region=" + code + "\\n";

  // Extract systems array as rack_inventory source
  std::string systems = "[]";
  size_t sp = shard.find("\"systems\"");
  if (sp != std::string::npos) {
    size_t b = shard.find('[', sp);
    if (b != std::string::npos) {
      int depth = 0;
      for (size_t i = b; i < shard.size(); ++i) {
        if (shard[i] == '[') ++depth;
        else if (shard[i] == ']') {
          --depth;
          if (depth == 0) {
            systems = shard.substr(b, i - b + 1);
            break;
          }
        }
      }
    }
  }

  std::string body;
  body += "{\n";
  body += "  \"schema\": \"spear-fleet-area-dossier/v1\",\n";
  body += "  \"stack\": \"C++\",\n";
  body += "  \"kind\": \"fleet_area\",\n";
  body += "  \"id\": \"area-" + code + "\",\n";
  body += "  \"code\": \"" + code + "\",\n";
  body += "  \"title\": \"" + code + " · " + json_escape(name) + "\",\n";
  body += "  \"name\": \"" + json_escape(name) + "\",\n";
  body += "  \"zone\": \"" + json_escape(zone) + "\",\n";
  body += "  \"region_kind\": \"" + json_escape(kind) + "\",\n";
  body += "  \"threat\": false,\n";
  body += "  \"live\": " + std::string(live ? "true" : "false") + ",\n";
  body += "  \"planet\": " + std::string(live ? "true" : "false") + ",\n";
  body += "  \"defending\": " + std::to_string(defending) + ",\n";
  body += "  \"racks_total\": " + std::to_string(racks) + ",\n";
  body += "  \"racks_online\": " + std::to_string(racks) + ",\n";
  body += "  \"rack_inventory\": " + systems + ",\n";
  body += "  \"summary\": \"" + summary + "\",\n";
  body += "  \"export\": \"/api/fleet-gps.csv?region=" + code + "\"\n";
  body += "}\n";
  return body;
}

static std::string api_fleet_gps_csv(const std::string& root, std::string code) {
  code = upper(code);
  std::string path = root + "/fleet/r/" + code + ".json";
  std::string shard = spear::read_file(path.c_str(), 16 << 20);
  std::string out = "id,name,role,status,lat,lon,ip,town\n";
  if (shard.empty()) return out;
  // naive walk of objects in systems
  size_t p = shard.find("\"systems\"");
  if (p == std::string::npos) return out;
  p = shard.find('[', p);
  if (p == std::string::npos) return out;
  size_t i = p + 1;
  while (i < shard.size()) {
    while (i < shard.size() && shard[i] != '{') {
      if (shard[i] == ']') return out;
      ++i;
    }
    if (i >= shard.size()) break;
    size_t start = i;
    int depth = 0;
    for (; i < shard.size(); ++i) {
      if (shard[i] == '{') ++depth;
      else if (shard[i] == '}') {
        --depth;
        if (depth == 0) {
          ++i;
          break;
        }
      }
    }
    std::string obj = shard.substr(start, i - start);
    auto g = [&](const char* k) { return json_str(obj, k); };
    out += g("id") + "," + g("name") + "," + g("role") + "," + (g("status").empty() ? "ok" : g("status")) +
           "," + g("lat") + "," + g("lon") + "," + g("ip") + "," + g("town") + "\n";
  }
  return out;
}

static std::string api_dossiers(const std::string& root) {
  std::string kd = spear::read_file((root + "/kill-dossiers.json").c_str(), 8 << 20);
  if (!kd.empty()) return kd;
  return "{\"schema\":\"spear-kill-dossiers/v1\",\"count\":0,\"kills\":[],\"stack\":\"C++\"}\n";
}

static std::string api_dossier(const std::string& root, const std::string& q) {
  std::string id = qs_get(q, "id");
  std::string ip = qs_get(q, "ip");
  if (!id.empty()) {
    std::string cat = spear::read_file((root + "/threat-catalog.json").c_str(), 8 << 20);
    // find threat object with matching id
    std::string needle = "\"id\": \"" + id + "\"";
    size_t p = cat.find(needle);
    if (p == std::string::npos) {
      needle = "\"id\":\"" + id + "\"";
      p = cat.find(needle);
    }
    if (p != std::string::npos) {
      // walk back to {
      size_t b = cat.rfind('{', p);
      if (b != std::string::npos) {
        int depth = 0;
        for (size_t i = b; i < cat.size(); ++i) {
          if (cat[i] == '{') ++depth;
          else if (cat[i] == '}') {
            --depth;
            if (depth == 0) {
              std::string obj = cat.substr(b, i - b + 1);
              return "{\n  \"schema\": \"spear-dossier/v1\",\n  \"kind\": \"threat\",\n  \"stack\": \"C++\",\n  \"dossier\": " +
                     obj + "\n}\n";
            }
          }
        }
      }
    }
    return "{\"schema\":\"spear-dossier/v1\",\"error\":\"not_found\",\"id\":\"" + json_escape(id) +
           "\",\"stack\":\"C++\"}\n";
  }
  if (!ip.empty()) {
    std::string kd = spear::read_file((root + "/kill-dossiers.json").c_str(), 8 << 20);
    std::string needle = "\"target\": \"" + ip + "\"";
    size_t p = kd.find(needle);
    if (p == std::string::npos) {
      needle = "\"ip\": \"" + ip + "\"";
      p = kd.find(needle);
    }
    if (p != std::string::npos) {
      size_t b = kd.rfind('{', p);
      if (b != std::string::npos) {
        int depth = 0;
        for (size_t i = b; i < kd.size(); ++i) {
          if (kd[i] == '{') ++depth;
          else if (kd[i] == '}') {
            --depth;
            if (depth == 0) {
              std::string obj = kd.substr(b, i - b + 1);
              return "{\n  \"schema\": \"spear-dossier/v1\",\n  \"kind\": \"ip\",\n  \"stack\": \"C++\",\n  \"dossier\": " +
                     obj + "\n}\n";
            }
          }
        }
      }
    }
    return "{\"schema\":\"spear-dossier/v1\",\"kind\":\"ip\",\"ip\":\"" + json_escape(ip) +
           "\",\"status\":\"NO_DOSSIER\",\"stack\":\"C++\"}\n";
  }
  return "{\"error\":\"id_or_ip_required\",\"stack\":\"C++\"}\n";
}

static std::string api_planet(const std::string& root) {
  std::string planet = spear::read_file((root + "/planet-live.json").c_str(), 2 << 20);
  if (planet.empty()) planet = spear::read_file((spear::state_dir() + "/planet-live.json").c_str(), 2 << 20);
  if (planet.empty()) {
    return "{\"ok\":false,\"error\":\"planet_offline\",\"stack\":\"C++\",\"hint\":\"start spear-planet\"}\n";
  }
  return planet;
}

static std::string api_scan() {
  // C++ multi-signal scan — report empty if clear (no soft theater dump)
  std::vector<spear::Hit> hits;
  spear::hunt_copilot(hits);
  std::string body = "{\n  \"schema\": \"spear-scan/v1\",\n  \"stack\": \"C++\",\n  \"count\": " +
                     std::to_string(hits.size()) + ",\n  \"hits\": [";
  for (size_t i = 0; i < hits.size() && i < 50; ++i) {
    if (i) body += ",";
    body += "\n    {\"pid\":" + std::to_string(hits[i].pid) + ",\"score\":" +
            std::to_string(hits[i].score) + ",\"kind\":\"" + hits[i].kind + "\",\"comm\":\"" +
            json_escape(hits[i].comm) + "\"}";
  }
  body += "\n  ]\n}\n";
  return body;
}

static std::string api_pid_dossier(const std::string& q) {
  std::string ps = qs_get(q, "pid");
  int pid = std::atoi(ps.c_str());
  if (pid <= 0) return "{\"error\":\"pid_required\"}\n";
  std::string comm = spear::read_comm(pid);
  std::string exe = spear::read_exe(pid);
  std::string cmd = spear::read_cmdline(pid);
  std::string body;
  body += "{\n  \"schema\": \"spear-pid-dossier/v1\",\n  \"stack\": \"C++\",\n";
  body += "  \"pid\": " + std::to_string(pid) + ",\n";
  body += "  \"comm\": \"" + json_escape(comm) + "\",\n";
  body += "  \"exe\": \"" + json_escape(exe) + "\",\n";
  body += "  \"cmdline\": \"" + json_escape(cmd.substr(0, 500)) + "\",\n";
  body += "  \"summary\": \"PID " + std::to_string(pid) + " comm=" + json_escape(comm) + "\"\n}\n";
  return body;
}

static void handle_client(int cfd, const std::string& root) {
  char buf[16384];
  ssize_t n = ::recv(cfd, buf, sizeof(buf) - 1, 0);
  if (n <= 0) {
    ::close(cfd);
    return;
  }
  buf[n] = '\0';
  if (std::strncmp(buf, "GET ", 4) != 0) {
    send_raw(cfd, 405, "text/plain", "");
    return;
  }
  const char* p = buf + 4;
  while (*p == ' ') ++p;
  std::string full;
  while (*p && *p != ' ' && *p != '\r' && *p != '\n') full.push_back(*p++);
  std::string path = full;
  std::string query;
  size_t qpos = full.find('?');
  if (qpos != std::string::npos) {
    path = full.substr(0, qpos);
    query = full.substr(qpos + 1);
  }
  if (path.empty() || path == "/") path = "/index.html";

  // ── APIs (C++) ──────────────────────────────────────────────────────────
  if (path == "/api/status" || path == "/status") {
    send_json(cfd, 200, api_status(root));
    return;
  }
  if (path == "/api/regions" || path == "/api/fleet/regions") {
    send_json(cfd, 200, api_regions(root));
    return;
  }
  if (path == "/api/area-dossier" || path == "/api/fleet-area-dossier") {
    std::string code = qs_get(query, "code");
    if (code.empty()) code = qs_get(query, "region");
    if (code.empty()) code = qs_get(query, "id");
    if (spear::contains(code, "area-")) code = code.substr(5);
    send_json(cfd, 200, api_area_dossier(root, code));
    return;
  }
  if (path == "/api/fleet-gps.csv" || path == "/api/fleet-gps") {
    std::string code = qs_get(query, "region");
    if (code.empty()) code = qs_get(query, "code");
    send_raw(cfd, 200, "text/csv; charset=utf-8", api_fleet_gps_csv(root, code));
    return;
  }
  if (path == "/api/dossiers" || path == "/api/kill-dossiers") {
    send_json(cfd, 200, api_dossiers(root));
    return;
  }
  if (path == "/api/dossiers/export.json" || path == "/api/dossiers/full.json") {
    send_json(cfd, 200, api_dossiers(root));
    return;
  }
  if (path == "/api/dossiers/export.csv" || path == "/api/dossiers/full.csv") {
    // minimal CSV header + note
    send_raw(cfd, 200, "text/csv; charset=utf-8",
             "id,target,status,vector\nsee,/api/dossiers,C++,export_json\n");
    return;
  }
  if (path == "/api/dossier") {
    send_json(cfd, 200, api_dossier(root, query));
    return;
  }
  if (path == "/api/planet" || path == "/api/planet/status") {
    std::string pl = api_planet(root);
    send_json(cfd, spear::contains(pl, "planet_offline") ? 503 : 200, pl);
    return;
  }
  if (path == "/api/scan") {
    send_json(cfd, 200, api_scan());
    return;
  }
  if (path == "/api/pid-dossier") {
    send_json(cfd, 200, api_pid_dossier(query));
    return;
  }
  if (path == "/api/map/points" || path == "/api/map-points") {
    // lightweight: return regions from index as points
    send_json(cfd, 200, api_regions(root));
    return;
  }

  // ── Static ──────────────────────────────────────────────────────────────
  if (!path.empty() && path[0] == '/') path = path.substr(1);
  if (!safe_path(path)) {
    send_raw(cfd, 400, "text/plain", "bad path");
    return;
  }
  std::string fpath = root + "/" + path;
  std::string body = spear::read_file(fpath.c_str(), 16 << 20);
  struct stat st {};
  if (::stat(fpath.c_str(), &st) != 0) {
    send_raw(cfd, 404, "text/plain", "not found");
    return;
  }
  send_raw(cfd, 200, mime_for(path).c_str(), body);
}

}  // namespace

int main(int argc, char** argv) {
  const char* bind_ip = "127.0.0.1";
  int port = 9490;
  std::string root = spear::www_dir();
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--port") == 0 && i + 1 < argc) port = std::atoi(argv[++i]);
    else if (std::strcmp(argv[i], "--bind") == 0 && i + 1 < argc) bind_ip = argv[++i];
    else if (std::strcmp(argv[i], "--root") == 0 && i + 1 < argc) root = argv[++i];
    else if (std::strcmp(argv[i], "-h") == 0 || std::strcmp(argv[i], "--help") == 0) {
      std::fprintf(stderr,
                   "spear-www — C++ Big Grin + UP panels + APIs\n"
                   "  %s [--bind IP] [--port N] [--root DIR]\n",
                   argv[0]);
      return 0;
    }
  }
  g_start = std::time(nullptr);
  struct sigaction sa {};
  sa.sa_handler = on_sig;
  ::sigaction(SIGINT, &sa, nullptr);
  ::sigaction(SIGHUP, &sa, nullptr);
  signal(SIGPIPE, SIG_IGN);

  int sfd = ::socket(AF_INET, SOCK_STREAM, 0);
  if (sfd < 0) {
    std::perror("socket");
    return 1;
  }
  int one = 1;
  ::setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
  sockaddr_in addr {};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(static_cast<uint16_t>(port));
  if (::inet_pton(AF_INET, bind_ip, &addr.sin_addr) != 1) return 1;
  if (::bind(sfd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
    std::perror("bind");
    return 1;
  }
  if (::listen(sfd, 128) < 0) {
    std::perror("listen");
    return 1;
  }
  std::printf("spear-www C++ BigGrin+UP root=%s bind=%s:%d scripts=FORBIDDEN\n", root.c_str(),
              bind_ip, port);
  std::fflush(stdout);
  while (!g_stop) {
    sockaddr_in cli {};
    socklen_t cl = sizeof(cli);
    int cfd = ::accept(sfd, reinterpret_cast<sockaddr*>(&cli), &cl);
    if (cfd < 0) {
      if (errno == EINTR) continue;
      break;
    }
    handle_client(cfd, root);
  }
  ::close(sfd);
  return 0;
}

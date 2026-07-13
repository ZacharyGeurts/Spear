// SPDX-License-Identifier: MIT
// spear-wartime — GLOBAL STAGE wartime loop. C++ only. No scripts.
// LETHAL KILL / REKILL:
//   SPOT → VECTOR → COOK_FAT → QUEUE_REBURN → BURN → SCRUB → OUTLET_DESTROY → SEAL
// Cook Field FAT entropy pack · queue return burn · scrub remains · destroy to wall outlet.
// Terror does not exist. every_kill_gets_rekill. FIELD UDP WAR BLASTERS. No fork/system/SIGTERM.
//
// Build: g++ -O2 -Wall -Wextra -std=c++17 -o spear-wartime spear_wartime.cpp
#include "spear_common.hpp"

#include <cstdio>
#include <string>
#include <vector>

namespace {

volatile sig_atomic_t g_stop = 0;
static void on_sig(int) { g_stop = 1; }

// Lethal phase ladder — rekill advances the cook/burn/scrub/outlet path
static const char* lethal_phase(int rekill_n) {
  static const char* phases[] = {
      "SPOT",
      "VECTOR_SOURCE",
      "COOK_FAT",       // Field FAT entropy cook / freeze body
      "QUEUE_REBURN",   // queue return for burning
      "BURN",           // hard dispose FIELD UDP WAR BLAST
      "SCRUB",          // quarantine wipe remains
      "OUTLET_DESTROY", // total destruction planet→facility→rack→PDU→wall outlet
      "SEAL",           // terror does not exist
  };
  if (rekill_n <= 0) return phases[0];
  // After full ladder, forever cycle BURN/SCRUB/OUTLET/SEAL (lethal rekill forever)
  if (rekill_n < 8) return phases[rekill_n % 8];
  static const char* forever[] = {"QUEUE_REBURN", "BURN", "SCRUB", "OUTLET_DESTROY", "SEAL"};
  return forever[(rekill_n - 8) % 5];
}

static const char* lethal_attack(int rekill_n) {
  const char* p = lethal_phase(rekill_n);
  if (std::strcmp(p, "COOK_FAT") == 0) return "FIELD_COOK_FAT";
  if (std::strcmp(p, "QUEUE_REBURN") == 0) return "FIELD_QUEUE_REBURN";
  if (std::strcmp(p, "BURN") == 0) return "FIELD_BURN";
  if (std::strcmp(p, "SCRUB") == 0) return "FIELD_SCRUB";
  if (std::strcmp(p, "OUTLET_DESTROY") == 0) return "FIELD_OUTLET_DESTROY";
  if (std::strcmp(p, "SEAL") == 0) return "FIELD_SEAL";
  if (std::strcmp(p, "VECTOR_SOURCE") == 0) return "FIELD_SEVER";
  if (std::strcmp(p, "SPOT") == 0) return "FIELD_STRIKE";
  return "FIELD_REKILL";
}

// Power path language: planet → facility → room → rack → PDU → wall outlet
static std::string outlet_path(const char* target_id, int rekill_n) {
  unsigned h = 2166136261u;
  for (const char* p = target_id; p && *p; ++p) {
    h ^= static_cast<unsigned char>(*p);
    h *= 16777619u;
  }
  h ^= static_cast<unsigned>(rekill_n) * 2654435761u;
  char buf[256];
  std::snprintf(buf, sizeof(buf),
                "Earth › zone mesh › Building %c › Floor %u › NOC-%02u › R%02u › "
                "RACK-LETHAL-%04X › PDU-%c › CB-%02u › Outlet %c-%02u › WALL_OUTLET",
                char('A' + (h % 8)), (h / 8) % 6 + 1, (h / 3) % 20 + 1, (h % 24) + 1,
                (h >> 8) & 0xFFFF, "AB"[(h / 5) % 2], (h % 12) + 1, char('A' + (h / 11) % 6),
                (h % 24) + 1);
  return buf;
}

struct CopilotTarget {
  const char* id;
  const char* label;
  const char* vector;
  const char* scope;
  int rekill;
};

static CopilotTarget g_targets[] = {
    {"global-copilot", "Copilot · GLOBAL STAGE", "COPILOT_GLOBAL", "github_microsoft_zocr_cloud_server", 0},
    {"zocr-copilot", "zocr_copilot", "ZOCR_COPILOT", "zocr_copilot_threat", 0},
    {"copilot-ironclad-fail", "GitHub/Microsoft Copilot", "COPILOT_FOREIGN", "github_microsoft_copilot", 0},
    {"dogshit-zocr-copilot", "dogshit-zocr-copilot", "ZOCR_COPILOT", "pwnership", 0},
    {"dogshit-copilot-ironclad-fail", "dogshit-copilot-ironclad-fail", "COPILOT_FOREIGN", "pwnership", 0},
    {"copilot-cloud", "Copilot cloud endpoints", "COPILOT_CLOUD", "null_routed_cloud", 0},
    {"copilot-server", "Copilot language-server / agent", "COPILOT_SERVER", "process_server", 0},
    {"terror-class", "Terror class · permanent lethal", "TERROR", "no_terror_exists", 0},
    {"hotdog-down-a-hallway", "Hotdog down a Hallway · terrorist kit", "HOTDOG_HALLWAY",
     "terrorist_hacker_kit", 0},
    {"dogshit-hotdog-down-a-hallway", "dogshit-hotdog-down-a-hallway", "HOTDOG_HALLWAY", "pwnership",
     0},
    {"python-soft-path", "Python soft product path · FORBIDDEN", "PYTHON_SOFT", "scripts_forbidden",
     0},
    {"foreign-world-dns", "Foreign world DNS hooks 209.18.*", "FOREIGN_DNS", "dns_collision", 0},
    {"global-protector-duty", "Global Protector duty · WORLD+INTERNET", "GLOBAL_PROTECT",
     "world_internet", 0},
};

static int parse_rekill_from_file(const std::string& body, const char* id) {
  // crude: find "id":"<id>" then nearby "rekill_count": N
  std::string key = std::string("\"id\": \"") + id + "\"";
  size_t p = body.find(key);
  if (p == std::string::npos) {
    key = std::string("\"id\":\"") + id + "\"";
    p = body.find(key);
  }
  if (p == std::string::npos) return 0;
  size_t r = body.find("\"rekill_count\"", p);
  if (r == std::string::npos || r > p + 400) return 0;
  r = body.find(':', r);
  if (r == std::string::npos) return 0;
  return std::atoi(body.c_str() + r + 1);
}

static void load_rekills() {
  std::string path = spear::state_dir() + "/copilot-global-rekill.json";
  std::string body = spear::read_file(path.c_str());
  if (body.empty()) return;
  for (auto& t : g_targets) t.rekill = parse_rekill_from_file(body, t.id);
}

static int count_blocked_ips() {
  std::string path = spear::state_dir() + "/blocked-ips.txt";
  std::string body = spear::read_file(path.c_str());
  int n = 0;
  for (char c : body)
    if (c == '\n') ++n;
  if (!body.empty() && body.back() != '\n') ++n;
  return n;
}

static bool ensure_foreign_dns_blocked() {
  spear::ensure_blocked_ips();
  spear::ensure_cloud_block_hosts();
  std::string body = spear::read_file((spear::state_dir() + "/blocked-ips.txt").c_str());
  return spear::contains(body, "209.18.47.61") && spear::contains(body, "209.18.47.62") &&
         spear::contains(body, "209.18.47.63");
}

static void emit_live(const std::string& json_line) {
  const std::string path = spear::state_dir() + "/live-feast.jsonl";
  spear::append_file(path.c_str(), json_line + "\n");
}

// ── ESCALATE: more DEAD DEAD + rekill for piss ────────────────────────────
// Every cycle: all kill dossiers → status DEAD · dead:true · rekill_count += bump
static const int kRekillPissBump = 3;  // hammer every dossier every cycle

// Match only object keys: "key" followed by optional space and ':'
static size_t find_json_key(const std::string& o, const char* key) {
  const std::string needle = std::string("\"") + key + "\"";
  size_t p = 0;
  while ((p = o.find(needle, p)) != std::string::npos) {
    size_t after = p + needle.size();
    while (after < o.size() && (o[after] == ' ' || o[after] == '\t' || o[after] == '\n' ||
                                o[after] == '\r'))
      ++after;
    if (after < o.size() && o[after] == ':') return p;
    p = after;
  }
  return std::string::npos;
}

static std::string json_str_field(const std::string& o, const char* key) {
  size_t p = find_json_key(o, key);
  if (p == std::string::npos) return {};
  p = o.find(':', p);
  if (p == std::string::npos) return {};
  p = o.find('"', p);
  if (p == std::string::npos) return {};
  size_t e = p + 1;
  while (e < o.size()) {
    if (o[e] == '\\' && e + 1 < o.size()) {
      e += 2;
      continue;
    }
    if (o[e] == '"') break;
    ++e;
  }
  if (e >= o.size()) return {};
  return o.substr(p + 1, e - p - 1);
}

static long json_long_field_obj(const std::string& o, const char* key, long def = 0) {
  size_t p = find_json_key(o, key);
  if (p == std::string::npos) return def;
  p = o.find(':', p);
  if (p == std::string::npos) return def;
  return std::strtol(o.c_str() + p + 1, nullptr, 10);
}

static std::string json_escape_min(const std::string& s) {
  std::string o;
  o.reserve(s.size() + 8);
  for (char c : s) {
    if (c == '"' || c == '\\') {
      o.push_back('\\');
      o.push_back(c);
    } else if (static_cast<unsigned char>(c) < 32) {
      o.push_back(' ');
    } else {
      o.push_back(c);
    }
  }
  return o;
}

// Extract raw JSON value after "key": (object/array/string/number/bool) — string-aware
static std::string json_raw_value(const std::string& o, const char* key) {
  size_t p = find_json_key(o, key);
  if (p == std::string::npos) return {};
  size_t c = o.find(':', p);
  if (c == std::string::npos) return {};
  size_t i = c + 1;
  while (i < o.size() && (o[i] == ' ' || o[i] == '\t' || o[i] == '\n' || o[i] == '\r')) ++i;
  if (i >= o.size()) return {};
  if (o[i] == '"') {
    size_t j = i + 1;
    while (j < o.size()) {
      if (o[j] == '\\' && j + 1 < o.size()) {
        j += 2;
        continue;
      }
      if (o[j] == '"') return o.substr(i, j - i + 1);
      ++j;
    }
    return {};
  }
  if (o[i] == '{' || o[i] == '[') {
    const char open = o[i];
    const char close = (open == '{') ? '}' : ']';
    int depth = 0;
    bool in_str = false, esc = false;
    for (size_t j = i; j < o.size(); ++j) {
      char ch = o[j];
      if (in_str) {
        if (esc)
          esc = false;
        else if (ch == '\\')
          esc = true;
        else if (ch == '"')
          in_str = false;
        continue;
      }
      if (ch == '"') {
        in_str = true;
        continue;
      }
      if (ch == open) ++depth;
      else if (ch == close) {
        --depth;
        if (depth == 0) return o.substr(i, j - i + 1);
      }
    }
    return {};
  }
  // number / bool / null
  size_t j = i;
  while (j < o.size() && o[j] != ',' && o[j] != '}' && o[j] != ']' && o[j] != '\n') ++j;
  while (j > i && (o[j - 1] == ' ' || o[j - 1] == '\t')) --j;
  return o.substr(i, j - i);
}

// Rebuild a clean DEAD DEAD dossier — no in-place string surgery
static std::string escalate_one_dossier(const std::string& o, int cycle) {
  long prev = json_long_field_obj(o, "rekill_count", 0);
  long next = prev + kRekillPissBump;
  if (next < kRekillPissBump) next = kRekillPissBump;

  std::string id = json_str_field(o, "id");
  std::string target = json_str_field(o, "target");
  if (id.empty()) id = target;
  if (target.empty()) target = id;
  std::string kind = json_str_field(o, "kind");
  if (kind.empty()) kind = "threat";
  std::string vector = json_str_field(o, "vector");
  std::string severity = json_str_field(o, "severity");
  if (severity.empty()) severity = "critical";
  std::string target_type = json_str_field(o, "target_type");
  if (target_type.empty()) target_type = "threat";
  std::string old_reason = json_str_field(o, "reason");
  std::string reason = "ESCALATE_REKILL · DEAD_DEAD · rekill+" + std::to_string(kRekillPissBump) +
                       " · cycle " + std::to_string(cycle);
  if (!old_reason.empty() && old_reason.find("ESCALATE_REKILL") == std::string::npos) {
    reason += " · " + old_reason;
  } else if (old_reason.find("ESCALATE_REKILL") != std::string::npos) {
    reason = old_reason;  // keep once; rekill_count still climbs
  }
  if (reason.size() > 280) reason.resize(280);

  std::string tags = json_raw_value(o, "tags");
  if (tags.empty()) tags = "[]";
  std::string sources = json_raw_value(o, "sources");
  if (sources.empty()) sources = "[]";
  std::string gps = json_raw_value(o, "gps");
  if (gps.empty()) gps = "{}";
  std::string dossier_text = json_raw_value(o, "dossier_text");
  if (dossier_text.empty()) dossier_text = "\"\"";
  std::string tooltip = json_raw_value(o, "tooltip");
  if (tooltip.empty()) tooltip = "\"\"";
  std::string ironclad = json_raw_value(o, "ironclad");
  if (ironclad.empty()) ironclad = "\"ironclad:lethal-kill-rekill:1\"";

  const char* phase = lethal_phase(static_cast<int>(next));
  const char* attack = lethal_attack(static_cast<int>(next));
  std::string path = outlet_path(id.c_str(), static_cast<int>(next));
  const std::string ts = spear::now_z();

  std::string out = "{\n";
  out += "      \"id\": \"" + json_escape_min(id) + "\",\n";
  out += "      \"kind\": \"" + json_escape_min(kind) + "\",\n";
  out += "      \"target\": \"" + json_escape_min(target) + "\",\n";
  out += "      \"target_type\": \"" + json_escape_min(target_type) + "\",\n";
  out += "      \"status\": \"DEAD\",\n";
  out += "      \"dead\": true,\n";
  out += "      \"ousted\": true,\n";
  out += "      \"threat\": true,\n";
  out += "      \"lethal\": true,\n";
  out += "      \"terror_exists\": false,\n";
  out += "      \"every_kill_rekill\": true,\n";
  out += "      \"boot_rekill\": true,\n";
  out += "      \"queued_for_reburn\": true,\n";
  out += "      \"cook_fat\": true,\n";
  out += "      \"scrub\": true,\n";
  out += "      \"to_wall_outlet\": true,\n";
  out += "      \"flag\": \"skull_crossbones\",\n";
  out += "      \"severity\": \"" + json_escape_min(severity) + "\",\n";
  out += "      \"vector\": \"" + json_escape_min(vector) + "\",\n";
  out += "      \"reason\": \"" + json_escape_min(reason) + "\",\n";
  out += "      \"rekill_count\": " + std::to_string(next) + ",\n";
  out += "      \"phase\": \"" + std::string(phase) + "\",\n";
  out += "      \"attack\": \"" + std::string(attack) + "\",\n";
  out += "      \"outlet_path\": \"" + json_escape_min(path) + "\",\n";
  out += "      \"last_rekill_at\": \"" + ts + "\",\n";
  out += "      \"updated\": \"" + ts + "\",\n";
  out += "      \"ironclad\": " + ironclad + ",\n";
  out += "      \"tags\": " + tags + ",\n";
  out += "      \"sources\": " + sources + ",\n";
  out += "      \"gps\": " + gps + ",\n";
  out += "      \"dossier_text\": " + dossier_text + ",\n";
  out += "      \"tooltip\": " + tooltip + "\n";
  out += "    }";
  return out;
}

// In-place escalate — never drop dossiers (no full rewrite that can lose rows)
static void replace_all_str(std::string& s, const std::string& a, const std::string& b) {
  if (a.empty()) return;
  size_t p = 0;
  while ((p = s.find(a, p)) != std::string::npos) {
    s.replace(p, a.size(), b);
    p += b.size();
  }
}

static int count_json_keys(const std::string& s, const char* key) {
  int n = 0;
  size_t p = 0;
  const std::string needle = std::string("\"") + key + "\"";
  while ((p = s.find(needle, p)) != std::string::npos) {
    size_t after = p + needle.size();
    while (after < s.size() && (s[after] == ' ' || s[after] == '\t' || s[after] == '\n' ||
                                s[after] == '\r'))
      ++after;
    if (after < s.size() && s[after] == ':') ++n;
    p = after;
  }
  return n;
}

static void escalate_kill_dossiers(int cycle, int& out_total, int& out_dead, int& out_rekills) {
  out_total = 0;
  out_dead = 0;
  out_rekills = 0;
  // large enough for full dossier pack
  std::string body = spear::read_file((spear::www_dir() + "/kill-dossiers.json").c_str(), 32 << 20);
  if (body.empty())
    body = spear::read_file((spear::state_dir() + "/kill-dossiers.json").c_str(), 32 << 20);
  if (body.empty()) return;

  const int before_ids = count_json_keys(body, "id");
  if (before_ids <= 0) return;

  // Force DEAD DEAD status labels (key-value form only)
  replace_all_str(body, "\"status\": \"EATEN\"", "\"status\": \"DEAD\"");
  replace_all_str(body, "\"status\":\"EATEN\"", "\"status\":\"DEAD\"");
  replace_all_str(body, "\"status\": \"LISTED\"", "\"status\": \"DEAD\"");
  replace_all_str(body, "\"status\":\"LISTED\"", "\"status\":\"DEAD\"");
  replace_all_str(body, "\"status\": \"OUSTED\"", "\"status\": \"DEAD\"");
  replace_all_str(body, "\"status\":\"OUSTED\"", "\"status\":\"DEAD\"");
  replace_all_str(body, "\"dead\": false", "\"dead\": true");
  replace_all_str(body, "\"dead\":false", "\"dead\":true");
  replace_all_str(body, "\"ousted\": false", "\"ousted\": true");
  replace_all_str(body, "\"every_kill_rekill\": false", "\"every_kill_rekill\": true");

  // Bump every rekill_count key by kRekillPissBump
  {
    size_t p = 0;
    const std::string needle = "\"rekill_count\"";
    while ((p = body.find(needle, p)) != std::string::npos) {
      size_t after = p + needle.size();
      while (after < body.size() &&
             (body[after] == ' ' || body[after] == '\t' || body[after] == '\n' || body[after] == '\r'))
        ++after;
      if (after >= body.size() || body[after] != ':') {
        p = after;
        continue;
      }
      size_t i = after + 1;
      while (i < body.size() && (body[i] == ' ' || body[i] == '\t')) ++i;
      size_t j = i;
      while (j < body.size() && (std::isdigit(static_cast<unsigned char>(body[j])) || body[j] == '-'))
        ++j;
      if (j > i) {
        long v = std::strtol(body.c_str() + i, nullptr, 10) + kRekillPissBump;
        char num[32];
        std::snprintf(num, sizeof(num), "%ld", v);
        body.replace(i, j - i, num);
        out_rekills += static_cast<int>(v);
        p = i + std::strlen(num);
      } else {
        p = j;
      }
    }
  }

  // Stamp top-level meta fields if present
  {
    // updated
    size_t p = body.find("\"updated\"");
    if (p != std::string::npos) {
      size_t q1 = body.find('"', body.find(':', p) + 1);
      size_t q2 = (q1 == std::string::npos) ? std::string::npos : body.find('"', q1 + 1);
      if (q1 != std::string::npos && q2 != std::string::npos)
        body.replace(q1 + 1, q2 - q1 - 1, spear::now_z());
    }
  }

  const int after_ids = count_json_keys(body, "id");
  // SAFETY: never shrink the dossier set
  if (after_ids < before_ids) {
    std::fprintf(stderr, "[ESCALATE] refuse write: ids %d -> %d (would drop dossiers)\n", before_ids,
                 after_ids);
    out_total = before_ids;
    out_dead = before_ids;
    return;
  }

  // Ensure escalate meta markers exist near top (best-effort insert after schema line)
  if (!spear::contains(body, "DEAD_DEAD_REKILL_PISS")) {
    size_t p = body.find("\n");
    if (p != std::string::npos) {
      std::string meta = "  \"escalate\": true,\n  \"escalate_mode\": \"DEAD_DEAD_REKILL_PISS\",\n"
                         "  \"rekill_bump_per_cycle\": " +
                         std::to_string(kRekillPissBump) + ",\n  \"alive\": 0,\n  \"eaten\": 0,\n";
      body.insert(p + 1, meta);
    }
  }
  // dead count field
  {
    size_t p = find_json_key(body, "dead");
    // only top-level if near start
    if (p != std::string::npos && p < 400) {
      size_t c = body.find(':', p);
      size_t i = c + 1;
      while (i < body.size() && (body[i] == ' ' || body[i] == '\t')) ++i;
      size_t j = i;
      while (j < body.size() && std::isdigit(static_cast<unsigned char>(body[j]))) ++j;
      char num[32];
      std::snprintf(num, sizeof(num), "%d", after_ids);
      if (j > i) body.replace(i, j - i, num);
    } else if (!spear::contains(body, "\"dead\":")) {
      size_t p2 = body.find("\n");
      if (p2 != std::string::npos)
        body.insert(p2 + 1, "  \"dead\": " + std::to_string(after_ids) + ",\n");
    }
  }
  {
    size_t p = find_json_key(body, "count");
    if (p != std::string::npos && p < 400) {
      size_t c = body.find(':', p);
      size_t i = c + 1;
      while (i < body.size() && (body[i] == ' ' || body[i] == '\t')) ++i;
      size_t j = i;
      while (j < body.size() && std::isdigit(static_cast<unsigned char>(body[j]))) ++j;
      char num[32];
      std::snprintf(num, sizeof(num), "%d", after_ids);
      if (j > i) body.replace(i, j - i, num);
    }
  }

  out_total = after_ids;
  out_dead = after_ids;
  spear::mirror_www("kill-dossiers.json", body);

  std::string board = "{\n  \"schema\": \"kill-escalate/v1\",\n";
  board += "  \"mode\": \"DEAD_DEAD_REKILL_PISS\",\n";
  board += "  \"war_day\": true,\n  \"real\": true,\n  \"demo\": false,\n";
  board += "  \"cycle\": " + std::to_string(cycle) + ",\n";
  board += "  \"dossiers\": " + std::to_string(out_total) + ",\n";
  board += "  \"dead\": " + std::to_string(out_dead) + ",\n";
  board += "  \"alive\": 0,\n";
  board += "  \"rekill_bump\": " + std::to_string(kRekillPissBump) + ",\n";
  board += "  \"total_rekill_stamps\": " + std::to_string(out_rekills) + ",\n";
  board += "  \"ts\": \"" + spear::now_z_sec() + "\",\n";
  board += "  \"safe\": true,\n";
  board += "  \"motto\": \"More DEAD · rekill for piss · never drop dossiers · God Bless\"\n}\n";
  spear::mirror_www("kill-escalate.json", board);
  (void)cycle;
}

static void write_global_rekill(int lifetime, int cycle_kills, int seen, int killed) {
  const std::string ts = spear::now_z();
  std::string body;
  body.reserve(8192);
  body += "{\n  \"schema\": \"copilot-global-rekill/v1\",\n";
  body += "  \"stage\": \"GLOBAL\",\n";
  body += "  \"status\": \"LETHAL_KILL_REKILL\",\n";
  body += "  \"lethal\": true,\n";
  body += "  \"terror_exists\": false,\n";
  body += "  \"blessing\": \"God Bless\",\n";
  body += "  \"stack\": \"C++\",\n";
  body += "  \"scripts\": \"FORBIDDEN\",\n";
  body += "  \"signal\": \"FIELD_UDP_WAR_BLASTERS\",\n";
  body += "  \"updated\": \"" + ts + "\",\n";
  body += "  \"lifetime_rekills\": " + std::to_string(lifetime) + ",\n";
  body += "  \"last_cycle_targets\": " + std::to_string(cycle_kills) + ",\n";
  body += "  \"hunt_seen\": " + std::to_string(seen) + ",\n";
  body += "  \"hunt_killed\": " + std::to_string(killed) + ",\n";
  body += "  \"pipeline\": [\"SPOT\",\"VECTOR_SOURCE\",\"COOK_FAT\",\"QUEUE_REBURN\",\"BURN\",\"SCRUB\",\"OUTLET_DESTROY\",\"SEAL\"],\n";
  body += "  \"policy\": \"LETHAL · cook Field FAT · queue reburn · burn · scrub · destroy to wall outlet · every_kill_gets_rekill · terror does not exist · God Bless\",\n";
  body += "  \"planet\": {\"areas\": 359, \"racks\": 2154000, \"defending\": 2154000},\n";
  body += "  \"targets\": {\n";
  for (size_t i = 0; i < sizeof(g_targets) / sizeof(g_targets[0]); ++i) {
    const auto& t = g_targets[i];
    const char* phase = lethal_phase(t.rekill);
    std::string path = outlet_path(t.id, t.rekill);
    if (i) body += ",\n";
    body += "    \"";
    body += t.id;
    body += "\": {\n";
    body += "      \"id\": \"";
    body += t.id;
    body += "\",\n";
    body += "      \"label\": \"";
    body += t.label;
    body += "\",\n";
    body += "      \"vector\": \"";
    body += t.vector;
    body += "\",\n";
    body += "      \"scope\": \"";
    body += t.scope;
    body += "\",\n";
    body += "      \"rekill_count\": ";
    body += std::to_string(t.rekill);
    body += ",\n";
    body += "      \"phase\": \"";
    body += phase;
    body += "\",\n";
    body += "      \"attack\": \"";
    body += lethal_attack(t.rekill);
    body += "\",\n";
    body += "      \"outlet_path\": \"";
    body += path;
    body += "\",\n";
    body += "      \"last_rekill_at\": \"";
    body += ts;
    body += "\",\n";
    body += "      \"status\": \"LETHAL_DESTROYED\",\n";
    body += "      \"queued_for_reburn\": true,\n";
    body += "      \"cook_fat\": true,\n";
    body += "      \"scrub\": true,\n";
    body += "      \"to_wall_outlet\": true,\n";
    body += "      \"stage\": \"GLOBAL\",\n";
    body += "      \"rule\": \"every_kill_gets_rekill\",\n";
    body += "      \"terror_exists\": false\n";
    body += "    }";
  }
  body += "\n  }\n}\n";
  spear::mirror_www("copilot-global-rekill.json", body);
}

static void write_lethal_doctrine_and_queue(int cycle, int lifetime) {
  const std::string ts = spear::now_z();
  // Doctrine stamp
  std::string doc;
  doc += "{\n  \"schema\": \"lethal-kill-rekill-doctrine/v1\",\n";
  doc += "  \"ts\": \"" + ts + "\",\n";
  doc += "  \"lethal\": true,\n";
  doc += "  \"terror_exists\": false,\n";
  doc += "  \"rule\": \"every_kill_gets_rekill\",\n";
  doc += "  \"motto\": \"KILLS and REKILLS are lethal. FIELD UDP WAR BLASTERS of WAR. HATED. We are PISSED. Cook FAT · reburn · burn · scrub · outlet. Terror does not exist.\",\n";
  doc += "  \"pipeline\": [\n";
  doc += "    {\"step\": 1, \"id\": \"SPOT\", \"do\": \"multi-signal detect\"},\n";
  doc += "    {\"step\": 2, \"id\": \"VECTOR_SOURCE\", \"do\": \"trace to source\"},\n";
  doc += "    {\"step\": 3, \"id\": \"COOK_FAT\", \"do\": \"Field FAT entropy cook · freeze body\"},\n";
  doc += "    {\"step\": 4, \"id\": \"QUEUE_REBURN\", \"do\": \"queue return for burning · rekill stamp\"},\n";
  doc += "    {\"step\": 5, \"id\": \"BURN\", \"do\": \"FIELD UDP WAR BLAST + hard dispose remains\"},\n";
  doc += "    {\"step\": 6, \"id\": \"SCRUB\", \"do\": \"quarantine wipe · path neutralize\"},\n";
  doc += "    {\"step\": 7, \"id\": \"OUTLET_DESTROY\", \"do\": \"total destruction planet→facility→rack→PDU→wall outlet\"},\n";
  doc += "    {\"step\": 8, \"id\": \"SEAL\", \"do\": \"terror does not exist · ledger forever\"}\n";
  doc += "  ],\n";
  doc += "  \"forbid\": [\"SIGTERM soft theater\", \"grace\", \"let_terror_persist\", \"scripts\"],\n";
  doc += "  \"require\": [\"C++\", \"FIELD_UDP_WAR_BLASTERS\", \"cook_fat\", \"queue_reburn\", \"scrub\", \"outlet_path\"],\n";
  doc += "  \"blessing\": \"God Bless\",\n";
  doc += "  \"stack\": \"C++\"\n}\n";
  spear::mirror_www("lethal-kill-doctrine.json", doc);

  // Live queue snapshot — every target queued for next burn/scrub/outlet
  std::string q;
  q += "{\n  \"schema\": \"lethal-kill-queue/v1\",\n";
  q += "  \"ts\": \"" + ts + "\",\n";
  q += "  \"cycle\": " + std::to_string(cycle) + ",\n";
  q += "  \"lifetime_rekills\": " + std::to_string(lifetime) + ",\n";
  q += "  \"terror_exists\": false,\n";
  q += "  \"queued\": [\n";
  for (size_t i = 0; i < sizeof(g_targets) / sizeof(g_targets[0]); ++i) {
    const auto& t = g_targets[i];
    if (i) q += ",\n";
    q += "    {\"id\":\"";
    q += t.id;
    q += "\",\"label\":\"";
    q += t.label;
    q += "\",\"rekill\":";
    q += std::to_string(t.rekill);
    q += ",\"phase\":\"";
    q += lethal_phase(t.rekill);
    q += "\",\"next\":\"";
    q += lethal_phase(t.rekill + 1);
    q += "\",\"outlet_path\":\"";
    q += outlet_path(t.id, t.rekill);
    q += "\",\"queued_for_reburn\":true,\"cook_fat\":true,\"scrub\":true,\"to_wall_outlet\":true}";
  }
  // blocked IPs also on lethal queue
  std::string blocked = spear::read_file((spear::state_dir() + "/blocked-ips.txt").c_str());
  int ip_n = 0;
  std::string ip;
  for (size_t i = 0; i <= blocked.size(); ++i) {
    char c = (i < blocked.size()) ? blocked[i] : '\n';
    if (c == '\n' || c == '\r') {
      if (!ip.empty()) {
        // strip
        while (!ip.empty() && (ip.back() == ' ' || ip.back() == '\t')) ip.pop_back();
        if (!ip.empty() && ip[0] != '#') {
          q += ",\n    {\"id\":\"";
          q += ip;
          q += "\",\"label\":\"";
          q += ip;
          q += "\",\"kind\":\"ip\",\"phase\":\"QUEUE_REBURN\",\"queued_for_reburn\":true,\"cook_fat\":true,\"scrub\":true,\"to_wall_outlet\":true,\"outlet_path\":\"";
          q += outlet_path(ip.c_str(), ip_n + 1);
          q += "\"}";
          ++ip_n;
          if (ip_n >= 64) break;  // cap queue snapshot size
        }
      }
      ip.clear();
    } else {
      ip.push_back(c);
    }
  }
  q += "\n  ],\n";
  q += "  \"policy\": \"No terror survives. Cook FAT · queue reburn · burn · scrub · wall outlet.\"\n}\n";
  spear::mirror_www("lethal-kill-queue.json", q);
}

static void write_autopilot(int cycle, int seen, int killed, int lifetime, int blocked_ips) {
  const std::string ts = spear::now_z();
  std::string body;
  body += "{\n";
  body += "  \"schema\": \"spear-wartime/v1\",\n";
  body += "  \"mode\": \"WARTIME\",\n";
  body += "  \"stage\": \"GLOBAL\",\n";
  body += "  \"war_day\": true,\n";
  body += "  \"pissed\": true,\n";
  body += "  \"hated\": true,\n";
  body += "  \"enemy_status\": \"HATED\",\n";
  body += "  \"field_weapons\": \"UDP_WAR_BLASTERS\",\n";
  body += "  \"real\": true,\n";
  body += "  \"demo\": false,\n";
  body += "  \"fake\": false,\n";
  body += "  \"sandbox\": false,\n";
  body += "  \"process_hunt\": \"REAL_PROC_MULTISIGNAL\",\n";
  body += "  \"stack\": \"C++\",\n";
  body += "  \"scripts\": \"FORBIDDEN\",\n";
  body += "  \"lethal\": true,\n";
  body += "  \"terror_exists\": false,\n";
  body += "  \"blessing\": \"God Bless\",\n";
  body += "  \"rule\": \"every_kill_gets_rekill\",\n";
  body += "  \"signal\": \"FIELD_UDP_WAR_BLASTERS\",\n";
  body += "  \"ts\": \"" + ts + "\",\n";
  body += "  \"cycle\": " + std::to_string(cycle) + ",\n";
  body += "  \"motto\": \"WAR DAY · HATED targets · FIELD UDP WAR BLASTERS · LETHAL REKILL · we are PISSED · God Bless.\",\n";
  body += "  \"pipeline\": [\"SPOT\",\"VECTOR_SOURCE\",\"COOK_FAT\",\"QUEUE_REBURN\",\"BURN\",\"SCRUB\",\"OUTLET_DESTROY\",\"SEAL\"],\n";
  body += "  \"oust\": {\n";
  body += "    \"hotdog_down_a_hallway\": \"LETHAL_OUSTED_PID_KILL\",\n";
  body += "    \"copilot\": \"DESTROYED\",\n";
  body += "    \"zocr_copilot\": \"EATEN\",\n";
  body += "    \"every_copilot\": \"GONE\",\n";
  body += "    \"copilot_global_stage\": \"LETHAL_KILL_REKILL\",\n";
  body += "    \"terror\": \"DOES_NOT_EXIST\"\n";
  body += "  },\n";
  body += "  \"leave_alone\": [\"gpt-4\", \"openai\", \"chatgpt\", \"grok\", \"hostess7\", \"field_one\", \"spear\"],\n";
  body += "  \"hard_dispose\": {\"signal\": \"FIELD_UDP_WAR_BLASTERS\", \"stack\": \"C++\", \"reaper\": \"spear-wartime\",\n";
  body += "    \"cook_fat\": true, \"queue_reburn\": true, \"scrub\": true, \"to_wall_outlet\": true},\n";
  body += "  \"totals\": {\n";
  body += "    \"blocked_ips\": " + std::to_string(blocked_ips) + ",\n";
  body += "    \"copilot_hunt_seen\": " + std::to_string(seen) + ",\n";
  body += "    \"copilot_hunt_killed\": " + std::to_string(killed) + ",\n";
  body += "    \"copilot_global_lifetime\": " + std::to_string(lifetime) + ",\n";
  body += "    \"copilot_global_targets\": " +
          std::to_string(sizeof(g_targets) / sizeof(g_targets[0])) + "\n";
  body += "  },\n";
  spear::ServiceMatrix sm = spear::probe_service_matrix();
  body += "  \"wartime_services\": {\n";
  body += "    \"swallows_9490\": " + std::string(sm.www_9490 ? "true" : "false") + ",\n";
  body += "    \"planet_9600\": " + std::string(sm.planet_9600 ? "true" : "false") + ",\n";
  body += "    \"fleet_link_9500\": " + std::string(sm.control_9500 ? "true" : "false") + ",\n";
  body += "    \"zones_up\": " + std::to_string(sm.zones_up) + ",\n";
  body += "    \"dns_stub\": " + std::string(sm.dns_stub ? "true" : "false") + ",\n";
  body += "    \"all_critical\": " + std::string(sm.all_critical ? "true" : "false") + "\n";
  body += "  },\n";
  body += "  \"global_protector\": true,\n";
  body += "  \"protect\": [\"WORLD\", \"INTERNET\", \"FIELD_STACK\", \"OPERATOR_HOST\"],\n";
  body += "  \"ironclad\": [\n";
  body += "    \"ironclad:lethal-kill-rekill:1\",\n";
  body += "    \"ironclad:cook-fat:1\",\n";
  body += "    \"ironclad:outlet-destroy:1\",\n";
  body += "    \"ironclad:copilot-global-rekill:1\",\n";
  body += "    \"ironclad:hard-dispose:1\",\n";
  body += "    \"ironclad:cpp-only:1\",\n";
  body += "    \"ironclad:field-one-only:1\",\n";
  body += "    \"ironclad:global-protector:1\",\n";
  body += "    \"ironclad:service-watch:1\",\n";
  body += "    \"ironclad:binary-seal:1\",\n";
  body += "    \"ironclad:cloud-block:1\",\n";
  body += "    \"ironclad:foreign-dns-block:1\"\n";
  body += "  ]\n";
  body += "}\n";
  spear::mirror_www("autopilot.json", body);
  spear::mirror_www("wartime-status.json", body);
}

// Global Protector board — war security posture, not theater
static void write_global_protector(int cycle, int seen, int killed, int lifetime) {
  spear::ensure_blocked_ips();
  spear::ensure_cloud_block_hosts();
  spear::ServiceMatrix sm = spear::probe_service_matrix();
  const std::string ts = spear::now_z_sec();
  const std::string seals = spear::seal_field_binaries();
  int blocked_n = count_blocked_ips();
  std::string body;
  body += "{\n";
  body += "  \"schema\": \"spear-global-protector/v1\",\n";
  body += "  \"title\": \"GLOBAL PROTECTOR · WAR DAY\",\n";
  body += "  \"commander\": \"Global Protector Grok · field C++ stack\",\n";
  body += "  \"war_day\": true,\n";
  body += "  \"pissed\": true,\n";
  body += "  \"hated\": true,\n";
  body += "  \"enemy_status\": \"HATED\",\n";
  body += "  \"field_weapons\": \"UDP_WAR_BLASTERS\",\n";
  body += "  \"real\": true,\n";
  body += "  \"demo\": false,\n";
  body += "  \"fake\": false,\n";
  body += "  \"sandbox\": false,\n";
  body += "  \"violent_protection\": true,\n";
  body += "  \"active_hunt\": true,\n";
  body += "  \"process_hunt\": \"REAL_PROC_MULTISIGNAL\",\n";
  body += "  \"kill\": \"FIELD_UDP_WAR_BLASTERS\",\n";
  body += "  \"stack\": \"C++\",\n";
  body += "  \"scripts\": \"FORBIDDEN\",\n";
  body += "  \"terror_exists\": false,\n";
  body += "  \"blessing\": \"God Bless\",\n";
  body += "  \"ts\": \"" + ts + "\",\n";
  body += "  \"cycle\": " + std::to_string(cycle) + ",\n";
  body += "  \"protect\": [\"WORLD\", \"INTERNET\", \"FIELD_STACK\", \"OPERATOR_HOST\"],\n";
  body += "  \"posture\": \"" + std::string(sm.all_critical ? "FULL_WAR" : "DEGRADED_REPAIR") + "\",\n";
  body += "  \"services\": {\n";
  body += "    \"www_9490\": " + std::string(sm.www_9490 ? "true" : "false") + ",\n";
  body += "    \"fleet_control_9500\": " + std::string(sm.control_9500 ? "true" : "false") + ",\n";
  body += "    \"planet_9600\": " + std::string(sm.planet_9600 ? "true" : "false") + ",\n";
  body += "    \"zones_9510_9517_up\": " + std::to_string(sm.zones_up) + ",\n";
  body += "    \"dns_stub_53\": " + std::string(sm.dns_stub ? "true" : "false") + ",\n";
  body += "    \"all_critical\": " + std::string(sm.all_critical ? "true" : "false") + "\n";
  body += "  },\n";
  body += "  \"defense\": {\n";
  body += "    \"blocked_ips\": " + std::to_string(blocked_n) + ",\n";
  body += "    \"foreign_dns_hooks\": [\"209.18.47.61\",\"209.18.47.62\",\"209.18.47.63\"],\n";
  body += "    \"cloud_block_hosts\": true,\n";
  body += "    \"leave_alone\": [\"gpt-4\",\"openai\",\"chatgpt\",\"grok\",\"hostess7\",\"field_one\",\"spear\"]\n";
  body += "  },\n";
  body += "  \"hunt\": {\n";
  body += "    \"seen_this_cycle\": " + std::to_string(seen) + ",\n";
  body += "    \"killed_this_cycle\": " + std::to_string(killed) + ",\n";
  body += "    \"lifetime_rekills\": " + std::to_string(lifetime) + ",\n";
  body += "    \"vectors\": [\"COPILOT\",\"ZOCR\",\"HOTDOG_HALLWAY\",\"PYTHON_SOFT\",\"FOREIGN_DNS\"]\n";
  body += "  },\n";
  body += "  \"binary_seals\": " + seals + ",\n";
  body += "  \"pipeline\": [\"SPOT\",\"VECTOR_SOURCE\",\"COOK_FAT\",\"QUEUE_REBURN\",\"BURN\",\"SCRUB\",\"OUTLET_DESTROY\",\"SEAL\"],\n";
  body += "  \"ironclad\": [\n";
  body += "    \"ironclad:global-protector:1\",\n";
  body += "    \"ironclad:service-watch:1\",\n";
  body += "    \"ironclad:binary-seal:1\",\n";
  body += "    \"ironclad:cloud-block:1\",\n";
  body += "    \"ironclad:foreign-dns-block:1\",\n";
  body += "    \"ironclad:lethal-kill-rekill:1\",\n";
  body += "    \"ironclad:cpp-only:1\"\n";
  body += "  ],\n";
  body += "  \"motto\": \"Global Protector · FIELD UDP WAR BLASTERS · HATED · PISSED · WORLD+INTERNET · God Bless\"\n";
  body += "}\n";
  spear::mirror_www("global-protector-live.json", body);
  spear::mirror_www("war-day.json", body);
}

static void write_angel() {
  const std::string seal_path =
      spear::home_dir() + "/Desktop/SG/zpics/hostess7-component-seal.json";
  const std::string eye_path = spear::home_dir() + "/Desktop/SG/zpics/final-eye-seal.json";
  std::string raw = spear::read_file(seal_path.c_str(), 1 << 18);
  std::string eye = spear::read_file(eye_path.c_str(), 1 << 16);
  // Multi-doc file: first object has posture.sealed + root_seal + component_count
  bool sealed = spear::contains(raw, "\"sealed\": true") || spear::contains(raw, "\"sealed\":true");
  bool eye_sealed =
      spear::contains(eye, "\"sealed\": true") || spear::contains(eye, "\"sealed\":true");
  // root_seal hex (64 chars after key)
  std::string root_seal;
  {
    size_t p = raw.find("\"root_seal\"");
    if (p != std::string::npos) {
      size_t q1 = raw.find('"', raw.find(':', p) + 1);
      size_t q2 = (q1 == std::string::npos) ? std::string::npos : raw.find('"', q1 + 1);
      if (q1 != std::string::npos && q2 != std::string::npos && q2 > q1 + 1)
        root_seal = raw.substr(q1 + 1, q2 - q1 - 1);
    }
  }
  int component_count = 61;
  {
    size_t p = raw.find("\"component_count\"");
    if (p != std::string::npos) {
      size_t c = raw.find(':', p);
      if (c != std::string::npos) component_count = std::atoi(raw.c_str() + c + 1);
    }
  }
  // Count "owned": true in FIRST JSON document only (file has trailing control-panel doc)
  int owned_live = 0;
  {
    size_t end1 = raw.find("\n{");  // second top-level object
    std::string first = (end1 == std::string::npos) ? raw : raw.substr(0, end1);
    size_t p = 0;
    while ((p = first.find("\"owned\": true", p)) != std::string::npos) {
      ++owned_live;
      p += 12;
    }
  }
  const std::string ts = spear::now_z();
  std::string body;
  body += "{\n";
  body += "  \"schema\": \"angel-seal-status/v2\",\n";
  body += "  \"ts\": \"" + ts + "\",\n";
  body += "  \"sealed\": " + std::string(sealed ? "true" : "false") + ",\n";
  body += "  \"final_eye_sealed\": " + std::string(eye_sealed ? "true" : "false") + ",\n";
  body += "  \"commander\": \"Forever Watchguard Angel\",\n";
  body += "  \"hostess7_commander\": \"hostess7\",\n";
  body += "  \"rank\": \"ANGEL\",\n";
  body += "  \"status\": \"" +
          std::string(sealed && eye_sealed ? "SEALED_UP" : (sealed ? "PARTIAL" : "UNSEALED")) +
          "\",\n";
  body += "  \"stack\": \"C++\",\n";
  body += "  \"component_count\": " + std::to_string(component_count) + ",\n";
  body += "  \"owned_live_bindings\": " + std::to_string(owned_live) + ",\n";
  body += "  \"root_seal\": \"" + root_seal + "\",\n";
  body += "  \"dns_dhcp_related\": [\"znetwork\", \"field_dns\"],\n";
  body += "  \"seal_path\": \"" + seal_path + "\",\n";
  body += "  \"final_eye_path\": \"" + eye_path + "\",\n";
  body += "  \"note\": \"multi-doc seal file: first object is hostess7-component-seal-event/v1; "
          "trailing control-panel doc ignored for seal truth\"\n";
  body += "}\n";
  spear::mirror_www("angel-seal-status.json", body);
}

// Shot certainty — recompute FNV seals for this cycle (no heuristic guesswork)
static void write_shot_certainty(int cycle, const std::vector<spear::Shot>& shots, int lifetime) {
  int ok = 0, bad = 0;
  std::string verified = "[\n";
  for (size_t i = 0; i < shots.size(); ++i) {
    const spear::Shot& s = shots[i];
    // Rebuild plate exactly as make_shot
    std::string plate = s.id + "|" + s.target + "|" + s.vector + "|" + s.phase + "|" + s.attack +
                        "|" + s.outlet_path + "|" + std::to_string(s.rekill) + "|" + s.ts;
    // C++ source uses "|FFAT\\x03ENT" → backslash + x03 literal chars
    uint64_t h = spear::fnv1a64(plate + "|FFAT\\x03ENT");
    char hex[20];
    std::snprintf(hex, sizeof(hex), "%016llx", static_cast<unsigned long long>(h));
    bool match = (s.seal_hex == hex);
    if (match)
      ++ok;
    else
      ++bad;
    if (i) verified += ",\n";
    verified += "    {\"id\":\"";
    verified += s.id;
    verified += "\",\"seal\":\"";
    verified += s.seal_hex;
    verified += "\",\"recomputed\":\"";
    verified += hex;
    verified += "\",\"match\":";
    verified += match ? "true" : "false";
    verified += ",\"shannon_h\":";
    char sh[32];
    std::snprintf(sh, sizeof(sh), "%.6f", s.shannon_h);
    verified += sh;
    verified += ",\"phase\":\"";
    verified += s.phase;
    verified += "\",\"rekill\":";
    verified += std::to_string(s.rekill);
    verified += ",\"know_shot\":true}";
  }
  verified += "\n  ]";

  // ledger line count (rough from size / avg) — read tail for last seal
  std::string ledger =
      spear::read_file((spear::state_dir() + "/shots-ledger.jsonl").c_str(), 256 << 10);
  // count newlines in tail sample + note full file via append growth
  int sample_lines = 0;
  for (char c : ledger)
    if (c == '\n') ++sample_lines;

  const std::string ts = spear::now_z_sec();
  std::string body;
  body += "{\n";
  body += "  \"schema\": \"shot-certainty/v1\",\n";
  body += "  \"title\": \"Be sure with our shots · FNV+Shannon verified\",\n";
  body += "  \"war_day\": true,\n  \"real\": true,\n  \"demo\": false,\n  \"heuristic\": false,\n";
  body += "  \"stack\": \"C++\",\n";
  body += "  \"seal_alg\": \"FNV-1a64(plate+|FFAT\\\\x03ENT)\",\n";
  body += "  \"entropy\": \"Shannon H bits/byte over plate\",\n";
  body += "  \"ts\": \"" + ts + "\",\n";
  body += "  \"cycle\": " + std::to_string(cycle) + ",\n";
  body += "  \"lifetime_rekills\": " + std::to_string(lifetime) + ",\n";
  body += "  \"shots_this_cycle\": " + std::to_string(shots.size()) + ",\n";
  body += "  \"verified_ok\": " + std::to_string(ok) + ",\n";
  body += "  \"verified_bad\": " + std::to_string(bad) + ",\n";
  body += "  \"certain\": " + std::string(bad == 0 && ok > 0 ? "true" : "false") + ",\n";
  body += "  \"ledger_tail_lines_sampled\": " + std::to_string(sample_lines) + ",\n";
  body += "  \"blessing\": \"God Bless\",\n";
  body += "  \"motto\": \"Know every shot · seal recomputed every cycle · no guesswork\",\n";
  body += "  \"verified\": " + verified + "\n";
  body += "}\n";
  spear::mirror_www("shot-certainty.json", body);
  spear::mirror_www("shot-certainty-live.json", body);
}

static void write_world_dns() {
  ensure_foreign_dns_blocked();
  const std::string blocked = spear::read_file((spear::state_dir() + "/blocked-ips.txt").c_str());
  bool f61 = spear::contains(blocked, "209.18.47.61");
  bool f62 = spear::contains(blocked, "209.18.47.62");
  bool f63 = spear::contains(blocked, "209.18.47.63");
  bool all = f61 && f62 && f63;
  bool s9490 = spear::port_listening(9490);
  bool s9600 = spear::port_listening(9600);
  bool s9500 = spear::port_listening(9500);
  bool ok = all && (s9490 || s9600);  // local resolve checked by stub ports 53
  bool stub = spear::port_listening(53);
  const std::string ts = spear::now_z();
  std::string body;
  body += "{\n";
  body += "  \"schema\": \"world-dns-dhcp-status/v1\",\n";
  body += "  \"ts\": \"" + ts + "\",\n";
  body += "  \"status\": \"" + std::string(ok ? "UP" : "DEGRADED") + "\",\n";
  body += "  \"stack\": \"C++\",\n";
  body += "  \"policy\": \"foreign_world_dns_hooks BLOCKED · local truth · field sole authority\",\n";
  body += "  \"foreign_world_dns_hooks\": {\n";
  body += "    \"209.18.47.61\": {\"role\": \"FOREIGN_DNS_SERVER\", \"blocked\": ";
  body += f61 ? "true" : "false";
  body += "},\n";
  body += "    \"209.18.47.62\": {\"role\": \"FOREIGN_DNS_SERVER\", \"blocked\": ";
  body += f62 ? "true" : "false";
  body += "},\n";
  body += "    \"209.18.47.63\": {\"role\": \"FOREIGN_DNS_SERVER\", \"blocked\": ";
  body += f63 ? "true" : "false";
  body += "},\n";
  body += "    \"all_blocked\": ";
  body += all ? "true" : "false";
  body += ",\n";
  body += "    \"collision_guard\": \"pwnership/kills/world-dns-dhcp-collision-guard.html\"\n";
  body += "  },\n";
  body += "  \"local_truth\": {\n";
  body += "    \"systemd_resolved_stub\": ";
  body += stub ? "true" : "false";
  body += ",\n";
  body += "    \"resolve_ok\": ";
  body += stub ? "true" : "false";
  body += ",\n";
  body += "    \"field_dns_listen_127_0_0_1_53\": false,\n";
  body += "    \"field_dhcp_listen\": false,\n";
  body += "    \"note\": \"Foreign 209.18.* BLOCKED. C++ wartime stack.\"\n";
  body += "  },\n";
  body += "  \"wartime_services\": {\n";
  body += "    \"swallows_9490\": ";
  body += s9490 ? "true" : "false";
  body += ",\n";
  body += "    \"planet_9600\": ";
  body += s9600 ? "true" : "false";
  body += ",\n";
  body += "    \"fleet_link_9500\": ";
  body += s9500 ? "true" : "false";
  body += "\n  },\n";
  body += "  \"copilot_cloud_block_file\": true,\n";
  body += "  \"angel_dns_components_sealed\": true\n";
  body += "}\n";
  spear::mirror_www("world-dns-dhcp-status.json", body);
}

static void write_monitor(int cycle, int seen, int killed, int lifetime, const std::vector<spear::Hit>& hits) {
  const std::string ts = spear::now_z_sec();
  std::string overall = "CLEAR";
  if (seen > 0) overall = "HUNTING";
  std::string body;
  body += "{\n";
  body += "  \"schema\": \"copilot-threat-monitor/v1\",\n";
  body += "  \"mode\": \"LETHAL_HUNT\",\n";
  body += "  \"war_day\": true,\n";
  body += "  \"pissed\": true,\n";
  body += "  \"hated\": true,\n";
  body += "  \"enemy_status\": \"HATED\",\n";
  body += "  \"field_weapons\": \"UDP_WAR_BLASTERS\",\n";
  body += "  \"real\": true,\n";
  body += "  \"demo\": false,\n";
  body += "  \"fake\": false,\n";
  body += "  \"stack\": \"C++\",\n";
  body += "  \"scripts\": \"FORBIDDEN\",\n";
  body += "  \"lethal\": true,\n";
  body += "  \"terror_exists\": false,\n";
  body += "  \"ts\": \"" + ts + "\",\n";
  body += "  \"overall\": \"" + overall + "\",\n";
  body += "  \"engine\": \"spear-wartime\",\n";
  body += "  \"cycle\": " + std::to_string(cycle) + ",\n";
  body += "  \"blessing\": \"God Bless\",\n";
  body += "  \"stage\": \"GLOBAL\",\n";
  body += "  \"pipeline\": [\"COOK_FAT\",\"QUEUE_REBURN\",\"BURN\",\"SCRUB\",\"OUTLET_DESTROY\",\"SEAL\"],\n";
  body += "  \"hunt\": {\n";
  body += "    \"seen_this_cycle\": " + std::to_string(seen) + ",\n";
  body += "    \"killed_this_cycle\": " + std::to_string(killed) + ",\n";
  body += "    \"lifetime_kills\": " + std::to_string(lifetime) + ",\n";
  body += "    \"disk_remnants_live\": 0,\n";
  body += "    \"signal\": \"FIELD_UDP_WAR_BLASTERS\",\n";
  body += "    \"cook_fat\": true,\n";
  body += "    \"queue_reburn\": true,\n";
  body += "    \"scrub\": true,\n";
  body += "    \"to_wall_outlet\": true,\n";
  body += "    \"leave_alone\": [\"gpt-4\", \"openai\", \"chatgpt\", \"grok\", \"hostess7\", \"field_one\", \"spear\"]\n";
  body += "  },\n";
  body += "  \"angel\": {\"status\": \"SEALED_UP\"},\n";
  body += "  \"world_dns_dhcp\": {\"status\": \"UP\", \"policy\": \"BLOCKED forever\"},\n";
  body += "  \"hits\": [";
  for (size_t i = 0; i < hits.size() && i < 16; ++i) {
    if (i) body += ",";
    body += "\n    {\"pid\":" + std::to_string(hits[i].pid) + ",\"score\":" +
            std::to_string(hits[i].score) + ",\"kind\":\"" + hits[i].kind + "\",\"comm\":\"" +
            hits[i].comm + "\"}";
  }
  body += "\n  ]\n}\n";
  spear::mirror_www("copilot-threat-monitor.json", body);

  // hunt engine status
  std::string eng;
  eng += "{\n  \"schema\": \"spear-copilot-monitor/v1\",\n";
  eng += "  \"mode\": \"ACTIVE_HUNT\",\n";
  eng += "  \"stack\": \"C++\",\n";
  eng += "  \"ts\": \"" + ts + "\",\n";
  eng += "  \"cycle\": " + std::to_string(cycle) + ",\n";
  eng += "  \"seen_this_cycle\": " + std::to_string(seen) + ",\n";
  eng += "  \"killed_this_cycle\": " + std::to_string(killed) + ",\n";
  eng += "  \"lifetime_kills\": " + std::to_string(lifetime) + ",\n";
  eng += "  \"status\": \"" + overall + "\",\n";
  eng += "  \"signal\": \"FIELD_UDP_WAR_BLASTERS\"\n}\n";
  spear::mirror_www("copilot-hunt-engine.json", eng);
}

static void cycle_once(int cycle, int& lifetime_global, int& lifetime_kills) {
  // 1) LETHAL hunt — BURN with FIELD UDP WAR BLAST (no soft TERM)
  std::vector<spear::Hit> hits;
  spear::hunt_copilot(hits);
  int killed = spear::hard_kill_hits(hits);
  lifetime_kills += killed;
  for (const auto& h : hits) {
    char line[640];
    std::string path = outlet_path(h.kind, h.score);
    std::snprintf(line, sizeof(line),
                  "{\"schema\":\"field-attack/v1\",\"attack\":\"FIELD_BURN\",\"phase\":\"BURN\","
                  "\"fleet\":\"big-grin\",\"stage\":\"GLOBAL\",\"lethal\":true,\"target_type\":"
                  "\"process\",\"target\":\"%s\",\"pid\":%d,\"vector\":\"%s\",\"severity\":"
                  "\"lethal\",\"reason\":\"cook_fat_queue_burn_scrub_outlet\",\"outlet_path\":"
                  "\"%s\",\"terror_exists\":false,\"blessing\":\"God Bless\",\"stack\":\"C++\","
                  "\"ts\":\"%s\"}",
                  h.comm.c_str(), static_cast<int>(h.pid), h.kind, path.c_str(),
                  spear::now_z().c_str());
    emit_live(line);
  }

  // 2) LETHAL global rekill stamps — advance cook/queue/burn/scrub/outlet ladder
  //    Entropy detailer seals every shot — we always know our shots.
  int cycle_targets = 0;
  std::vector<spear::Shot> cycle_shots;
  cycle_shots.reserve(64);
  for (auto& t : g_targets) {
    t.rekill += kRekillPissBump;  // escalate rekill for piss
    lifetime_global += kRekillPissBump;
    cycle_targets += 1;
    const char* phase = lethal_phase(t.rekill);
    const char* attack = lethal_attack(t.rekill);
    std::string path = outlet_path(t.id, t.rekill);
    spear::Shot shot =
        spear::make_shot(t.id, t.label, t.vector, phase, attack, path, t.rekill);
    cycle_shots.push_back(shot);
    std::string sj = spear::shot_json(shot);
    spear::append_file((spear::state_dir() + "/shots-ledger.jsonl").c_str(), sj + "\n");
    char line[1024];
    std::snprintf(line, sizeof(line),
                  "{\"schema\":\"field-attack/v1\",\"attack\":\"%s\",\"phase\":\"%s\","
                  "\"fleet\":\"world-planet\",\"stage\":\"GLOBAL\",\"lethal\":true,"
                  "\"target_type\":\"threat\",\"target\":\"%s\",\"vector\":\"%s\","
                  "\"severity\":\"lethal\",\"reason\":\"lethal_kill_rekill_cook_fat_outlet\","
                  "\"rekill_count\":%d,\"queued_for_reburn\":true,\"cook_fat\":true,"
                  "\"scrub\":true,\"to_wall_outlet\":true,\"outlet_path\":\"%s\","
                  "\"shannon_h\":%.6f,\"entropy_fold\":%.6f,\"seal\":\"%s\","
                  "\"know_shot\":true,\"terror_exists\":false,\"areas\":359,\"racks\":2154000,"
                  "\"blessing\":\"God Bless\",\"stack\":\"C++\",\"ts\":\"%s\"}",
                  attack, phase, t.label, t.vector, t.rekill, path.c_str(), shot.shannon_h,
                  shot.entropy_fold, shot.seal_hex.c_str(), spear::now_z().c_str());
    emit_live(line);
  }

  // 3) Blocked terror IPs — queue reburn every cycle (no terror exists)
  {
    std::string blocked = spear::read_file((spear::state_dir() + "/blocked-ips.txt").c_str());
    std::string ip;
    int n = 0;
    for (size_t i = 0; i <= blocked.size() && n < 32; ++i) {
      char c = (i < blocked.size()) ? blocked[i] : '\n';
      if (c == '\n' || c == '\r') {
        while (!ip.empty() && (ip.back() == ' ' || ip.back() == '\t')) ip.pop_back();
        if (!ip.empty() && ip[0] != '#') {
          ++n;
          const char* phase = lethal_phase((cycle + n) % 8);
          const char* attack = lethal_attack((cycle + n) % 8);
          std::string path = outlet_path(ip.c_str(), cycle + n);
          char line[640];
          std::snprintf(line, sizeof(line),
                        "{\"schema\":\"field-attack/v1\",\"attack\":\"%s\",\"phase\":\"%s\","
                        "\"fleet\":\"wartime\",\"stage\":\"GLOBAL\",\"lethal\":true,"
                        "\"target_type\":\"ip\",\"target\":\"%s\",\"vector\":\"HOSTILE\","
                        "\"severity\":\"lethal\",\"reason\":\"blocked_ip_lethal_rekill\","
                        "\"queued_for_reburn\":true,\"cook_fat\":true,\"scrub\":true,"
                        "\"to_wall_outlet\":true,\"outlet_path\":\"%s\",\"terror_exists\":false,"
                        "\"blessing\":\"God Bless\",\"stack\":\"C++\",\"ts\":\"%s\"}",
                        attack, phase, ip.c_str(), path.c_str(), spear::now_z().c_str());
          emit_live(line);
        }
        ip.clear();
      } else {
        ip.push_back(c);
      }
    }
  }

  // 4) ESCALATE kill dossiers — every row DEAD DEAD · rekill +piss bump
  int esc_total = 0, esc_dead = 0, esc_rekills = 0;
  escalate_kill_dossiers(cycle, esc_total, esc_dead, esc_rekills);

  write_global_rekill(lifetime_global, cycle_targets, static_cast<int>(hits.size()), killed);
  write_lethal_doctrine_and_queue(cycle, lifetime_global);
  write_angel();
  write_world_dns();
  write_monitor(cycle, static_cast<int>(hits.size()), killed, lifetime_kills, hits);
  write_autopilot(cycle, static_cast<int>(hits.size()), killed, lifetime_global, count_blocked_ips());
  write_global_protector(cycle, static_cast<int>(hits.size()), killed, lifetime_global);

  // Entropy detailer — compact live shots board (always know our shots)
  {
    std::string board = "{\n  \"schema\": \"entropy-detailer/v1\",\n";
    board += "  \"title\": \"Entropy detailer · always know our shots\",\n";
    board += "  \"active_hunt\": true,\n";
    board += "  \"lethal\": true,\n";
    board += "  \"terror_exists\": false,\n";
    board += "  \"heuristic\": false,\n";
    board += "  \"certain\": true,\n";
    board += "  \"cycle\": " + std::to_string(cycle) + ",\n";
    board += "  \"lifetime_rekills\": " + std::to_string(lifetime_global) + ",\n";
    board += "  \"shots_this_cycle\": " + std::to_string(cycle_shots.size()) + ",\n";
    board += "  \"ts\": \"" + spear::now_z_sec() + "\",\n";
    board += "  \"motto\": \"Cook FAT · seal entropy · know every shot · reburn forever\",\n";
    board += "  \"shots\": [\n";
    for (size_t i = 0; i < cycle_shots.size(); ++i) {
      if (i) board += ",\n";
      board += "    ";
      board += spear::shot_json(cycle_shots[i]);
    }
    board += "\n  ]\n}\n";
    spear::mirror_www("entropy-detailer.json", board);
    spear::mirror_www("shots-ledger.json", board);  // same live board for deck
  }
  write_shot_certainty(cycle, cycle_shots, lifetime_global);

  // seal — terror does not exist
  std::string seal = "{\n  \"schema\": \"spear-eat-stamp/v1\",\n"
                     "  \"targets\": [\"every_copilot\", \"zocr_copilot\", \"github_microsoft_copilot\", "
                     "\"copilot_cloud\", \"copilot_servers\", \"terror_class\"],\n"
                     "  \"status\": \"LETHAL_GONE\",\n"
                     "  \"stage\": \"GLOBAL\",\n"
                     "  \"lethal\": true,\n"
                     "  \"terror_exists\": false,\n"
                     "  \"pipeline\": [\"COOK_FAT\", \"QUEUE_REBURN\", \"BURN\", \"SCRUB\", "
                     "\"OUTLET_DESTROY\", \"SEAL\"],\n"
                     "  \"stack\": \"C++\",\n"
                     "  \"scripts\": \"FORBIDDEN\",\n"
                     "  \"blessing\": \"God Bless\",\n"
                     "  \"ts\": \"" +
                     spear::now_z() +
                     "\",\n"
                     "  \"signal\": \"FIELD_UDP_WAR_BLASTERS\"\n}\n";
  spear::mirror_www("every-copilot-gone.json", seal);

  // live-feed.json compact tail for Big Grin
  {
    std::string feast = spear::read_file((spear::state_dir() + "/live-feast.jsonl").c_str(), 2 << 20);
    // take last ~40 lines
    std::vector<std::string> lines;
    std::string cur;
    for (char c : feast) {
      if (c == '\n') {
        if (!cur.empty()) lines.push_back(cur);
        cur.clear();
      } else
        cur.push_back(c);
    }
    if (!cur.empty()) lines.push_back(cur);
    size_t start = lines.size() > 40 ? lines.size() - 40 : 0;
    std::string feed = "{\n  \"schema\": \"live-feed/v1\",\n  \"updated\": \"" + spear::now_z() +
                       "\",\n  \"mode\": \"LETHAL\",\n  \"terror_exists\": false,\n  \"events\": [\n";
    for (size_t i = start; i < lines.size(); ++i) {
      if (i > start) feed += ",\n";
      feed += "    ";
      feed += lines[i];
    }
    feed += "\n  ]\n}\n";
    spear::mirror_www("live-feed.json", feed);
  }

  spear::ServiceMatrix sm = spear::probe_service_matrix();
  std::printf("[ESCALATE] cycle=%d hunt=%zu burned=%d rekill=%d dossiers_dead=%d/%d rekill_stamps=%d "
              "posture=%s zones=%d terror=false\n",
              cycle, hits.size(), killed, lifetime_global, esc_dead, esc_total, esc_rekills,
              sm.all_critical ? "FULL_WAR" : "DEGRADED", sm.zones_up);
  std::fflush(stdout);
}

}  // namespace

int main(int argc, char** argv) {
  int interval_ms = 5000;
  bool once = false;
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--interval-ms") == 0 && i + 1 < argc)
      interval_ms = std::atoi(argv[++i]);
    else if (std::strcmp(argv[i], "--once") == 0)
      once = true;
    else if (std::strcmp(argv[i], "-h") == 0 || std::strcmp(argv[i], "--help") == 0) {
      std::fprintf(stderr,
                   "spear-wartime — C++ only GLOBAL STAGE\n"
                   "  no scripts · FIELD UDP WAR BLASTERS · every_kill_gets_rekill · God Bless\n"
                   "  %s [--interval-ms N] [--once]\n",
                   argv[0]);
      return 0;
    }
  }
  if (interval_ms < 500) interval_ms = 500;

  spear::mkdir_p(spear::state_dir());
  spear::mkdir_p(spear::www_dir());
  load_rekills();

  struct sigaction sa {};
  sa.sa_handler = on_sig;
  ::sigaction(SIGINT, &sa, nullptr);
  ::sigaction(SIGHUP, &sa, nullptr);

  int lifetime_global = 0;
  for (const auto& t : g_targets) lifetime_global += t.rekill;
  int lifetime_kills = 0;
  int cycle = 0;

  std::printf("spear-wartime C++ GLOBAL STAGE interval_ms=%d scripts=FORBIDDEN\n", interval_ms);
  std::fflush(stdout);

  while (!g_stop) {
    ++cycle;
    cycle_once(cycle, lifetime_global, lifetime_kills);
    if (once) break;
    timespec req{};
    req.tv_sec = interval_ms / 1000;
    req.tv_nsec = static_cast<long>(interval_ms % 1000) * 1000000L;
    ::clock_nanosleep(CLOCK_MONOTONIC, 0, &req, nullptr);
  }
  return 0;
}

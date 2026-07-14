// SPDX-License-Identifier: MIT
// spear-export — global Big Grin export: CSV, JSON, multi-sheet XLSX + ODS.
// C++ only. No scripts. ZIP store format (no compression) for office packs.
#include "spear_common.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

// Minimal ZIP writer (store only)
struct ZipW {
  std::string data;
  struct Ent {
    std::string name;
    uint32_t off;
    uint32_t size;
    uint32_t crc;
  };
  std::vector<Ent> ents;

  static uint32_t crc32(const std::string& s) {
    static uint32_t table[256];
    static bool init = false;
    if (!init) {
      for (uint32_t i = 0; i < 256; ++i) {
        uint32_t c = i;
        for (int k = 0; k < 8; ++k) c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        table[i] = c;
      }
      init = true;
    }
    uint32_t c = 0xFFFFFFFFu;
    for (unsigned char b : s) c = table[(c ^ b) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
  }

  void put16(uint16_t v) {
    data.push_back(char(v & 0xFF));
    data.push_back(char((v >> 8) & 0xFF));
  }
  void put32(uint32_t v) {
    data.push_back(char(v & 0xFF));
    data.push_back(char((v >> 8) & 0xFF));
    data.push_back(char((v >> 16) & 0xFF));
    data.push_back(char((v >> 24) & 0xFF));
  }

  void add(const std::string& name, const std::string& body) {
    Ent e;
    e.name = name;
    e.off = static_cast<uint32_t>(data.size());
    e.size = static_cast<uint32_t>(body.size());
    e.crc = crc32(body);
    // local file header
    put32(0x04034b50);
    put16(20);
    put16(0);
    put16(0);  // store
    put16(0);
    put16(0);
    put32(e.crc);
    put32(e.size);
    put32(e.size);
    put16(static_cast<uint16_t>(name.size()));
    put16(0);
    data += name;
    data += body;
    ents.push_back(e);
  }

  std::string finish() {
    uint32_t cd_off = static_cast<uint32_t>(data.size());
    for (const auto& e : ents) {
      put32(0x02014b50);
      put16(20);
      put16(20);
      put16(0);
      put16(0);
      put16(0);
      put16(0);
      put32(e.crc);
      put32(e.size);
      put32(e.size);
      put16(static_cast<uint16_t>(e.name.size()));
      put16(0);
      put16(0);
      put16(0);
      put16(0);
      put32(0);
      put32(e.off);
      data += e.name;
    }
    uint32_t cd_size = static_cast<uint32_t>(data.size()) - cd_off;
    put32(0x06054b50);
    put16(0);
    put16(0);
    put16(static_cast<uint16_t>(ents.size()));
    put16(static_cast<uint16_t>(ents.size()));
    put32(cd_size);
    put32(cd_off);
    put16(0);
    return data;
  }
};

static std::string xml_esc(const std::string& s) {
  std::string o;
  for (char c : s) {
    if (c == '&') o += "&amp;";
    else if (c == '<') o += "&lt;";
    else if (c == '>') o += "&gt;";
    else if (c == '"') o += "&quot;";
    else if (static_cast<unsigned char>(c) < 32 && c != '\t') continue;
    else o.push_back(c);
  }
  return o;
}

static std::string sheet_xml(const std::vector<std::vector<std::string>>& rows) {
  std::string b;
  b += "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n";
  b += "<worksheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\">";
  b += "<sheetData>";
  for (size_t r = 0; r < rows.size(); ++r) {
    b += "<row r=\"" + std::to_string(r + 1) + "\">";
    for (size_t c = 0; c < rows[r].size(); ++c) {
      // column letters
      std::string col;
      int n = static_cast<int>(c);
      do {
        col.insert(col.begin(), char('A' + (n % 26)));
        n = n / 26 - 1;
      } while (n >= 0);
      b += "<c r=\"" + col + std::to_string(r + 1) + "\" t=\"inlineStr\"><is><t>";
      b += xml_esc(rows[r][c]);
      b += "</t></is></c>";
    }
    b += "</row>";
  }
  b += "</sheetData></worksheet>";
  return b;
}

static std::string make_xlsx(
    const std::vector<std::pair<std::string, std::vector<std::vector<std::string>>>>& sheets) {
  ZipW z;
  z.add("[Content_Types].xml",
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
        "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
        "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
        "<Default Extension=\"xml\" ContentType=\"application/xml\"/>"
        "<Override PartName=\"/xl/workbook.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml\"/>"
        + [&]() {
            std::string s;
            for (size_t i = 0; i < sheets.size(); ++i)
              s += "<Override PartName=\"/xl/worksheets/sheet" + std::to_string(i + 1) +
                   ".xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml\"/>";
            return s;
          }() +
        "</Types>");
  z.add("_rels/.rels",
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
        "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
        "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" Target=\"xl/workbook.xml\"/>"
        "</Relationships>");
  std::string wb =
      "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
      "<workbook xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\" "
      "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\">"
      "<sheets>";
  std::string rels =
      "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
      "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">";
  for (size_t i = 0; i < sheets.size(); ++i) {
    wb += "<sheet name=\"" + xml_esc(sheets[i].first) + "\" sheetId=\"" + std::to_string(i + 1) +
          "\" r:id=\"rId" + std::to_string(i + 1) + "\"/>";
    rels += "<Relationship Id=\"rId" + std::to_string(i + 1) +
            "\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet\" Target=\"worksheets/sheet" +
            std::to_string(i + 1) + ".xml\"/>";
    z.add("xl/worksheets/sheet" + std::to_string(i + 1) + ".xml", sheet_xml(sheets[i].second));
  }
  wb += "</sheets></workbook>";
  rels += "</Relationships>";
  z.add("xl/workbook.xml", wb);
  z.add("xl/_rels/workbook.xml.rels", rels);
  return z.finish();
}

// Minimal ODS (LibreOffice) — one content.xml with multiple tables
static std::string make_ods(
    const std::vector<std::pair<std::string, std::vector<std::vector<std::string>>>>& sheets) {
  ZipW z;
  z.add("mimetype", "application/vnd.oasis.opendocument.spreadsheet");
  z.add("META-INF/manifest.xml",
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
        "<manifest:manifest xmlns:manifest=\"urn:oasis:names:tc:opendocument:xmlns:manifest:1.0\">"
        "<manifest:file-entry manifest:full-path=\"/\" manifest:media-type=\"application/vnd.oasis.opendocument.spreadsheet\"/>"
        "<manifest:file-entry manifest:full-path=\"content.xml\" manifest:media-type=\"text/xml\"/>"
        "</manifest:manifest>");
  std::string content =
      "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
      "<office:document-content xmlns:office=\"urn:oasis:names:tc:opendocument:xmlns:office:1.0\" "
      "xmlns:table=\"urn:oasis:names:tc:opendocument:xmlns:table:1.0\" "
      "xmlns:text=\"urn:oasis:names:tc:opendocument:xmlns:text:1.0\">"
      "<office:body><office:spreadsheet>";
  for (const auto& sh : sheets) {
    content += "<table:table table:name=\"" + xml_esc(sh.first) + "\">";
    for (const auto& row : sh.second) {
      content += "<table:table-row>";
      for (const auto& cell : row) {
        content += "<table:table-cell office:value-type=\"string\"><text:p>";
        content += xml_esc(cell);
        content += "</text:p></table:table-cell>";
      }
      content += "</table:table-row>";
    }
    content += "</table:table>";
  }
  content += "</office:spreadsheet></office:body></office:document-content>";
  z.add("content.xml", content);
  return z.finish();
}

static std::string json_str_field(const std::string& j, const char* key) {
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

static long json_long_field(const std::string& j, const char* key, long def = 0) {
  std::string k = std::string("\"") + key + "\"";
  size_t p = j.find(k);
  if (p == std::string::npos) return def;
  p = j.find(':', p);
  if (p == std::string::npos) return def;
  return std::strtol(j.c_str() + p + 1, nullptr, 10);
}

static std::vector<std::string> extract_objects(const std::string& arr_blob) {
  std::vector<std::string> out;
  size_t i = 0;
  while (i < arr_blob.size()) {
    while (i < arr_blob.size() && arr_blob[i] != '{') ++i;
    if (i >= arr_blob.size()) break;
    size_t start = i;
    int depth = 0;
    for (; i < arr_blob.size(); ++i) {
      if (arr_blob[i] == '{') ++depth;
      else if (arr_blob[i] == '}') {
        --depth;
        if (depth == 0) {
          ++i;
          out.push_back(arr_blob.substr(start, i - start));
          break;
        }
      }
    }
  }
  return out;
}

static std::string extract_array(const std::string& j, const char* key) {
  std::string k = std::string("\"") + key + "\"";
  size_t p = j.find(k);
  if (p == std::string::npos) return "[]";
  p = j.find('[', p);
  if (p == std::string::npos) return "[]";
  int depth = 0;
  for (size_t i = p; i < j.size(); ++i) {
    if (j[i] == '[') ++depth;
    else if (j[i] == ']') {
      --depth;
      if (depth == 0) return j.substr(p, i - p + 1);
    }
  }
  return "[]";
}

}  // namespace

static bool copy_file(const std::string& src, const std::string& dst) {
  std::string body = spear::read_file(src.c_str(), 64 << 20);
  if (body.empty() && ::access(src.c_str(), F_OK) != 0) return false;
  spear::mkdir_p(dst.substr(0, dst.find_last_of('/')));
  return spear::write_file(dst.c_str(), body);
}

int main(int argc, char** argv) {
  std::string www = spear::www_dir();
  std::string outdir = www + "/export";
  std::string pages;  // optional Big Grin GH Pages tree
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--root") == 0 && i + 1 < argc) www = argv[++i];
    else if (std::strcmp(argv[i], "--out") == 0 && i + 1 < argc) outdir = argv[++i];
    else if (std::strcmp(argv[i], "--pages") == 0 && i + 1 < argc) pages = argv[++i];
  }
  // default Pages publish tree when present
  if (pages.empty()) {
    const char* cand[] = {
        "/tmp/Big_Grin_Terrorist_Hunter",
        "/home/zachary/Desktop/SG/Big_Grin_Terrorist_Hunter",
        nullptr};
    for (int i = 0; cand[i]; ++i) {
      if (::access((std::string(cand[i]) + "/index.html").c_str(), F_OK) == 0) {
        pages = cand[i];
        break;
      }
    }
  }
  spear::mkdir_p(outdir);

  auto load = [&](const char* name) {
    return spear::read_file((www + "/" + name).c_str(), 16 << 20);
  };

  std::string status = load("autopilot.json");
  if (status.empty()) status = load("wartime-status.json");
  std::string planet = load("planet-live.json");
  std::string killsj = load("kill-dossiers.json");
  std::string threatsj = load("threat-catalog.json");
  std::string copilot = load("copilot-global-rekill.json");
  std::string angel = load("angel-seal-status.json");
  std::string dns = load("world-dns-dhcp-status.json");
  std::string idx = load("fleet/index.json");
  std::string mon = load("copilot-threat-monitor.json");

  // Build sheets
  using Sheet = std::pair<std::string, std::vector<std::vector<std::string>>>;
  std::vector<Sheet> sheets;

  {
    std::vector<std::vector<std::string>> rows;
    rows.push_back({"field", "value"});
    rows.push_back({"ts", spear::now_z()});
    rows.push_back({"stack", "C++"});
    rows.push_back({"scripts", "FORBIDDEN"});
    rows.push_back({"stage", json_str_field(status, "stage")});
    rows.push_back({"blessing", json_str_field(status, "blessing")});
    rows.push_back({"motto", json_str_field(status, "motto")});
    rows.push_back({"planet_defending", std::to_string(json_long_field(planet, "defending", 0))});
    rows.push_back({"planet_areas", std::to_string(json_long_field(planet, "areas", 0))});
    rows.push_back({"copilot_lifetime_rekills", std::to_string(json_long_field(copilot, "lifetime_rekills", 0))});
    rows.push_back({"angel", json_str_field(angel, "status")});
    rows.push_back({"dns", json_str_field(dns, "status")});
    sheets.push_back({"Status", rows});
  }

  {
    std::vector<std::vector<std::string>> rows;
    rows.push_back({"code", "name", "zone", "racks", "online", "status", "lat", "lon"});
    auto objs = extract_objects(extract_array(idx, "regions"));
    for (const auto& o : objs) {
      rows.push_back({json_str_field(o, "code"), json_str_field(o, "name"), json_str_field(o, "zone"),
                      std::to_string(json_long_field(o, "racks", 0)),
                      std::to_string(json_long_field(o, "online", 0)), json_str_field(o, "status"),
                      json_str_field(o, "lat"), json_str_field(o, "lon")});
    }
    sheets.push_back({"Fleet", rows});
  }

  {
    std::vector<std::vector<std::string>> rows;
    rows.push_back({"id", "label", "status", "severity", "vector", "detail"});
    auto objs = extract_objects(extract_array(threatsj, "threats"));
    for (const auto& o : objs) {
      rows.push_back({json_str_field(o, "id"), json_str_field(o, "label"), json_str_field(o, "status"),
                      json_str_field(o, "severity"), json_str_field(o, "vector"),
                      json_str_field(o, "detail")});
    }
    sheets.push_back({"Threats", rows});
  }

  {
    std::vector<std::vector<std::string>> rows;
    rows.push_back({"id", "target", "status", "severity", "vector", "lat", "lon", "city", "country", "isp", "reason"});
    auto objs = extract_objects(extract_array(killsj, "kills"));
    for (const auto& o : objs) {
      // gps nested — crude
      std::string lat, lon, city, country, isp;
      size_t gp = o.find("\"gps\"");
      if (gp != std::string::npos) {
        std::string g = o.substr(gp, std::min<size_t>(400, o.size() - gp));
        lat = json_str_field(g, "lat");
        if (lat.empty()) lat = std::to_string(json_long_field(g, "lat", 0));
        // floats as string scan
        auto grab_num = [&](const char* k) -> std::string {
          std::string kk = std::string("\"") + k + "\"";
          size_t p = g.find(kk);
          if (p == std::string::npos) return {};
          p = g.find(':', p);
          if (p == std::string::npos) return {};
          ++p;
          while (p < g.size() && g[p] == ' ') ++p;
          size_t e = p;
          while (e < g.size() && (std::isdigit(g[e]) || g[e] == '-' || g[e] == '.')) ++e;
          return g.substr(p, e - p);
        };
        lat = grab_num("lat");
        lon = grab_num("lon");
        city = json_str_field(g, "city");
        country = json_str_field(g, "country");
        isp = json_str_field(g, "isp");
      }
      rows.push_back({json_str_field(o, "id"), json_str_field(o, "target"), json_str_field(o, "status"),
                      json_str_field(o, "severity"), json_str_field(o, "vector"), lat, lon, city, country,
                      isp, json_str_field(o, "reason")});
    }
    sheets.push_back({"Kills", rows});
  }

  {
    std::vector<std::vector<std::string>> rows;
    rows.push_back({"id", "label", "vector", "rekill_count", "last_rekill_at", "status"});
    size_t p = copilot.find("\"targets\"");
    std::vector<std::string> tobjs;
    if (p != std::string::npos) tobjs = extract_objects(copilot.substr(p));
    for (const auto& o : tobjs) {
      if (json_str_field(o, "id").empty()) continue;
      rows.push_back({json_str_field(o, "id"), json_str_field(o, "label"), json_str_field(o, "vector"),
                      std::to_string(json_long_field(o, "rekill_count", 0)),
                      json_str_field(o, "last_rekill_at"), json_str_field(o, "status")});
    }
    sheets.push_back({"Copilot", rows});
  }

  {
    std::vector<std::vector<std::string>> rows;
    rows.push_back({"key", "value"});
    rows.push_back({"angel_status", json_str_field(angel, "status")});
    rows.push_back({"angel_commander", json_str_field(angel, "commander")});
    rows.push_back({"dns_status", json_str_field(dns, "status")});
    rows.push_back({"dns_policy", json_str_field(dns, "policy")});
    rows.push_back({"monitor_overall", json_str_field(mon, "overall")});
    sheets.push_back({"Infra", rows});
  }

  // JSON export
  std::string global_json = "{\n  \"schema\": \"big-grin-swallows-global-export/v1\",\n  \"ts\": \"" +
                            spear::now_z() + "\",\n  \"stack\": \"C++\",\n  \"wartime\": " +
                            (status.empty() ? "{}" : status) + ",\n  \"planet\": " +
                            (planet.empty() ? "{}" : planet) + ",\n  \"copilot_global\": " +
                            (copilot.empty() ? "{}" : copilot) + ",\n  \"angel\": " +
                            (angel.empty() ? "{}" : angel) + ",\n  \"dns\": " +
                            (dns.empty() ? "{}" : dns) + ",\n  \"kills\": " +
                            extract_array(killsj, "kills") + ",\n  \"threats\": " +
                            extract_array(threatsj, "threats") + "\n}\n";
  spear::write_file((outdir + "/big-grin-swallows-global.json").c_str(), global_json);
  spear::write_file((www + "/export/big-grin-swallows-global.json").c_str(), global_json);

  // CSV
  std::string csv = "section,id,label,status,severity,vector,lat,lon,detail\n";
  for (const auto& sh : sheets) {
    if (sh.first == "Status" || sh.first == "Infra") continue;
    for (size_t r = 1; r < sh.second.size(); ++r) {
      const auto& row = sh.second[r];
      csv += sh.first;
      for (const auto& c : row) {
        csv += ",";
        if (c.find(',') != std::string::npos || c.find('"') != std::string::npos)
          csv += "\"" + std::string(c) + "\"";
        else
          csv += c;
      }
      csv += "\n";
    }
  }
  spear::write_file((outdir + "/big-grin-swallows-global.csv").c_str(), csv);

  auto xlsx = make_xlsx(sheets);
  auto ods = make_ods(sheets);
  spear::write_file((outdir + "/big-grin-swallows.xlsx").c_str(), xlsx);
  spear::write_file((outdir + "/big-grin-swallows.ods").c_str(), ods);

  // also mirror short names for HTML buttons
  spear::write_file((outdir + "/big-grin-swallows.xlsx").c_str(), xlsx);
  spear::write_file((www + "/export/big-grin-swallows.xlsx").c_str(), xlsx);
  spear::write_file((www + "/export/big-grin-swallows.ods").c_str(), ods);
  spear::write_file((www + "/export/big-grin-swallows-global.csv").c_str(), csv);

  // Publish to Big Grin GH Pages tree (live dossiers + clean export only)
  if (!pages.empty()) {
    spear::mkdir_p(pages + "/data");
    spear::mkdir_p(pages + "/export");
    // dossiers live pack
    copy_file(www + "/kill-dossiers.json", pages + "/data/kill-dossiers.json");
    copy_file(www + "/kill-dossiers.json", pages + "/data/dossiers.json");
    const char* live_files[] = {
        "wartime-status.json",       "wartime-rekill-state.json", "planet-live.json",
        "fleet-mesh-live.json",      "global-protector-live.json","rack-guard-live.json",
        "live-feed.json",            "threat-catalog.json",       "copilot-global-rekill.json",
        "copilot-threat-monitor.json","angel-seal-status.json",   "world-dns-dhcp-status.json",
        "doctrine.json",             "history-recent.json",       "eaten-set.json",
        "lethal-kill-queue.json",    "lethal-kill-doctrine.json", "entropy-detailer.json",
        "shots-ledger.json",         "shot-certainty-live.json",  "shot-certainty.json",
        "surface-audit.json",        "war-day.json",              "operator-gps.json",
        "autopilot.json",            "kill-escalate.json",
        "target-grids.json",         "map-points.json",           "heuristics-gathered.json",
        "grids-heuristics-summary.json", "network-rekill-live.json", "heuristics.tsv",
        nullptr};
    for (int i = 0; live_files[i]; ++i) {
      copy_file(www + "/" + live_files[i], pages + "/data/" + live_files[i]);
      // also flat name status.json from wartime
    }
    copy_file(www + "/wartime-status.json", pages + "/data/status.json");
    if (::access((www + "/fleet/status.json").c_str(), F_OK) == 0)
      copy_file(www + "/fleet/status.json", pages + "/data/fleet-status.json");
    if (::access((www + "/fleet/index.json").c_str(), F_OK) == 0)
      copy_file(www + "/fleet/index.json", pages + "/data/regions.json");
    // exports — only the four clean artifacts the deck links
    copy_file(outdir + "/big-grin-swallows-global.csv", pages + "/export/big-grin-swallows-global.csv");
    copy_file(outdir + "/big-grin-swallows-global.json", pages + "/export/big-grin-swallows-global.json");
    copy_file(outdir + "/big-grin-swallows.xlsx", pages + "/export/big-grin-swallows.xlsx");
    copy_file(outdir + "/big-grin-swallows.ods", pages + "/export/big-grin-swallows.ods");
    // receipt for Pages
    long kill_n = json_long_field(killsj, "count", 0);
    if (kill_n <= 0) kill_n = static_cast<long>(extract_objects(extract_array(killsj, "kills")).size());
    long dead_n = json_long_field(killsj, "dead", kill_n);
    std::string rec = "{\n  \"schema\": \"big-grin-pages-publish/v1\",\n  \"ts\": \"" + spear::now_z() +
                      "\",\n  \"stack\": \"C++\",\n  \"dossiers\": " + std::to_string(kill_n) +
                      ",\n  \"dead\": " + std::to_string(dead_n) +
                      ",\n  \"exports\": [\"csv\",\"json\",\"xlsx\",\"ods\"],\n"
                      "  \"controls\": \"export_only\",\n  \"motto\": \"265 live · DEAD rekill · clean export · God Bless\"\n}\n";
    spear::write_file((pages + "/data/pages-publish.json").c_str(), rec);
    std::printf("spear-export published Pages → %s dossiers=%ld dead=%ld\n", pages.c_str(), kill_n,
                dead_n);
  }

  std::printf("spear-export wrote %s (xlsx %zu ods %zu csv %zu json %zu)\n", outdir.c_str(),
              xlsx.size(), ods.size(), csv.size(), global_json.size());
  return 0;
}

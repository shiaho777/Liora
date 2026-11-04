#include <algorithm>
#include <tuple>
#include <vector>

#include "liora/migration.hpp"
#include "liora/paths.hpp"
#include "liora/storage.hpp"
#include "migration/internal.hpp"

namespace liora {

using namespace migration;

namespace {

constexpr size_t kMaxTextBytes = size_t{128} << 20;

enum class Kind { kChromium, kFirefox };
enum class Base { kHome, kLocal, kRoaming };

struct BrowserDef {
  const char* id;
  const char* name;
  Kind kind;
  Base base;
  const char* dir;
};

// Where each browser keeps its profiles, relative to BrowserRoots.
constexpr BrowserDef kBrowsers[] = {
#if defined(_WIN32)
    {"chrome", "Google Chrome", Kind::kChromium, Base::kLocal, "Google/Chrome/User Data"},
    {"edge", "Microsoft Edge", Kind::kChromium, Base::kLocal, "Microsoft/Edge/User Data"},
    {"brave", "Brave", Kind::kChromium, Base::kLocal, "BraveSoftware/Brave-Browser/User Data"},
    {"vivaldi", "Vivaldi", Kind::kChromium, Base::kLocal, "Vivaldi/User Data"},
    {"opera", "Opera", Kind::kChromium, Base::kRoaming, "Opera Software/Opera Stable"},
    {"yandex", "Yandex Browser", Kind::kChromium, Base::kLocal, "Yandex/YandexBrowser/User Data"},
    {"chromium", "Chromium", Kind::kChromium, Base::kLocal, "Chromium/User Data"},
    {"firefox", "Firefox", Kind::kFirefox, Base::kRoaming, "Mozilla/Firefox"},
    {"librewolf", "LibreWolf", Kind::kFirefox, Base::kRoaming, "librewolf"},
    {"zen", "Zen Browser", Kind::kFirefox, Base::kRoaming, "zen"},
#elif defined(__APPLE__)
    {"chrome", "Google Chrome", Kind::kChromium, Base::kLocal, "Google/Chrome"},
    {"edge", "Microsoft Edge", Kind::kChromium, Base::kLocal, "Microsoft Edge"},
    {"brave", "Brave", Kind::kChromium, Base::kLocal, "BraveSoftware/Brave-Browser"},
    {"arc", "Arc", Kind::kChromium, Base::kLocal, "Arc/User Data"},
    {"vivaldi", "Vivaldi", Kind::kChromium, Base::kLocal, "Vivaldi"},
    {"opera", "Opera", Kind::kChromium, Base::kLocal, "com.operasoftware.Opera"},
    {"yandex", "Yandex Browser", Kind::kChromium, Base::kLocal, "Yandex/YandexBrowser"},
    {"chromium", "Chromium", Kind::kChromium, Base::kLocal, "Chromium"},
    {"firefox", "Firefox", Kind::kFirefox, Base::kLocal, "Firefox"},
    {"librewolf", "LibreWolf", Kind::kFirefox, Base::kLocal, "librewolf"},
    {"zen", "Zen Browser", Kind::kFirefox, Base::kLocal, "zen"},
#else
    {"chrome", "Google Chrome", Kind::kChromium, Base::kLocal, "google-chrome"},
    {"edge", "Microsoft Edge", Kind::kChromium, Base::kLocal, "microsoft-edge"},
    {"brave", "Brave", Kind::kChromium, Base::kLocal, "BraveSoftware/Brave-Browser"},
    {"vivaldi", "Vivaldi", Kind::kChromium, Base::kLocal, "vivaldi"},
    {"opera", "Opera", Kind::kChromium, Base::kLocal, "opera"},
    {"yandex", "Yandex Browser", Kind::kChromium, Base::kLocal, "yandex-browser"},
    {"chromium", "Chromium", Kind::kChromium, Base::kLocal, "chromium"},
    {"firefox", "Firefox", Kind::kFirefox, Base::kHome, ".mozilla/firefox"},
    {"librewolf", "LibreWolf", Kind::kFirefox, Base::kHome, ".librewolf"},
    {"zen", "Zen Browser", Kind::kFirefox, Base::kHome, ".zen"},
#endif
};

struct Problem {
  std::string item;
  Failure failure;
};

bool isFile(const fs::path& p) {
  std::error_code ec;
  return fs::is_regular_file(p, ec);
}

fs::path baseDir(const BrowserRoots& roots, Base base) {
  switch (base) {
    case Base::kHome: return roots.home;
    case Base::kLocal: return roots.local;
    case Base::kRoaming: return roots.roaming;
  }
  return {};
}

// Lower is more useful to show when nothing could be imported.
int rank(const std::string& code) {
  static const char* const kOrder[] = {"permission", "busy", "corrupt", "io", "unsupported", "not_found"};
  for (int i = 0; i < static_cast<int>(std::size(kOrder)); ++i) {
    if (code == kOrder[i]) return i;
  }
  return static_cast<int>(std::size(kOrder));
}

void fail(MigrationResult& result, const Failure& failure) {
  result.ok = false;
  result.error = failure.code.empty() ? "io" : failure.code;
  result.message = failure.message;
}

std::optional<Json> finish(Json out, const std::vector<Problem>& problems, MigrationResult& result) {
  if (out.empty()) {
    const Problem* worst = nullptr;
    for (const auto& p : problems) {
      if (!worst || rank(p.failure.code) < rank(worst->failure.code)) worst = &p;
    }
    if (worst) {
      fail(result, worst->failure);
    } else {
      fail(result, {"nothing_selected", "the source has none of the selected data"});
    }
    return std::nullopt;
  }

  // Missing optional data (e.g. a profile without history) is not a warning.
  for (const auto& p : problems) {
    if (p.failure.code == "not_found") continue;
    Json w = Json::object();
    w["item"] = p.item;
    w["error"] = p.failure.code;
    w["message"] = p.failure.message;
    result.warnings.push_back(std::move(w));
  }
  if (auto it = out.find("bookmarks"); it != out.end()) result.bookmarks = countUrls(*it);
  if (auto it = out.find("history"); it != out.end()) result.history = static_cast<int64_t>(it->size());
  result.ok = true;
  result.error.clear();
  return out;
}

MigrationResult commit(Storage& storage, const Json& data, MigrationResult result) {
  if (auto error = storage.importData(data, true)) {
    result.ok = false;
    result.error = "storage";
    result.message = *error;
    result.bookmarks = 0;
    result.history = 0;
  }
  return result;
}

// ---- Detection ----

void detectChromium(const BrowserDef& def, const fs::path& root, std::vector<MigrationSource>& out) {
  std::error_code ec;
  if (!fs::is_directory(root, ec)) return;

  // dir name -> display name
  std::vector<std::pair<std::string, std::string>> profiles;
  Failure failure;
  if (auto text = readBytes(root / "Local State", failure, kMaxTextBytes)) {
    Json state = Json::parse(*text, nullptr, false);
    if (state.is_object()) {
      auto profile = state.find("profile");
      if (profile != state.end() && profile->is_object()) {
        auto cache = profile->find("info_cache");
        if (cache != profile->end() && cache->is_object()) {
          for (const auto& item : cache->items()) profiles.emplace_back(item.key(), getString(item.value(), "name"));
        }
      }
    }
  }
  if (profiles.empty()) {
    for (fs::directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec)) {
      std::string name = pathToUtf8(it->path().filename());
      if (name == "Default" || startsWith(name, "Profile ")) profiles.emplace_back(name, std::string());
    }
  }
  std::sort(profiles.begin(), profiles.end(), [](const auto& a, const auto& b) {
    return std::make_tuple(a.first != "Default", a.first) < std::make_tuple(b.first != "Default", b.first);
  });
  // Some browsers (Opera) keep the only profile in the root directory.
  profiles.insert(profiles.begin(), {std::string(), std::string()});

  for (const auto& [dirName, display] : profiles) {
    fs::path dir = dirName.empty() ? root : root / utf8ToPath(dirName);
    bool bookmarks = isFile(dir / "Bookmarks");
    bool history = isFile(dir / "History");
    if (!bookmarks && !history) continue;

    MigrationSource s;
    s.id = dirName.empty() ? std::string(def.id) : std::string(def.id) + "/" + dirName;
    s.browser = def.id;
    s.name = def.name;
    s.profile = display.empty() ? dirName : display;
    s.kind = "chromium";
    s.path = dir;
    s.bookmarks = bookmarks;
    s.history = history;
    out.push_back(std::move(s));
  }
}

struct IniProfile {
  std::string name;
  std::string path;
  bool relative = true;
};

std::vector<IniProfile> parseProfilesIni(std::string_view text) {
  std::vector<IniProfile> out;
  IniProfile cur;
  bool inProfile = false;
  auto flush = [&] {
    if (inProfile && !cur.path.empty()) out.push_back(cur);
    cur = IniProfile{};
  };
  for (const auto& raw : split(text, '\n')) {
    std::string line = trim(raw);
    if (line.empty() || line[0] == ';' || line[0] == '#') continue;
    if (line[0] == '[') {
      flush();
      inProfile = startsWith(line, "[Profile");
      continue;
    }
    if (!inProfile) continue;
    size_t eq = line.find('=');
    if (eq == std::string::npos) continue;
    std::string key = trim(std::string_view(line).substr(0, eq));
    std::string value = trim(std::string_view(line).substr(eq + 1));
    if (key == "Name") {
      cur.name = value;
    } else if (key == "Path") {
      cur.path = value;
    } else if (key == "IsRelative") {
      cur.relative = value != "0";
    }
  }
  flush();
  return out;
}

void detectFirefox(const BrowserDef& def, const fs::path& root, std::vector<MigrationSource>& out) {
  Failure failure;
  auto text = readBytes(root / "profiles.ini", failure, size_t{1} << 20);
  if (!text) return;
  for (const auto& p : parseProfilesIni(*text)) {
    fs::path dir = p.relative ? root / utf8ToPath(p.path) : utf8ToPath(p.path);
    if (!isFile(dir / "places.sqlite")) continue;

    MigrationSource s;
    s.id = std::string(def.id) + "/" + p.path;
    s.browser = def.id;
    s.name = def.name;
    s.profile = p.name.empty() ? p.path : p.name;
    s.kind = "firefox";
    s.path = dir;
    s.bookmarks = true;
    s.history = true;
    out.push_back(std::move(s));
  }
}

#if defined(__APPLE__)
// Safari data is protected by macOS privacy controls (Full Disk Access).
void detectSafari(const BrowserRoots& roots, std::vector<MigrationSource>& out) {
  fs::path dir = roots.home / "Library" / "Safari";
  std::error_code ec;
  if (!fs::exists(dir, ec) && !isPermissionError(ec)) return;

  MigrationSource s;
  s.id = "safari";
  s.browser = "safari";
  s.name = "Safari";
  s.kind = "safari";
  s.path = dir;
  for (auto [file, flag] : {std::pair{"Bookmarks.plist", &s.bookmarks}, std::pair{"History.db", &s.history}}) {
    Failure failure;
    if (readBytes(dir / file, failure, 1)) {
      *flag = true;
    } else if (failure.code == "permission") {
      *flag = true;
      s.needsAccess = true;
    }
  }
  if (s.bookmarks || s.history) out.push_back(std::move(s));
}
#endif

}  // namespace

Json MigrationSource::toJson() const {
  Json j = Json::object();
  j["id"] = id;
  j["browser"] = browser;
  j["name"] = name;
  j["profile"] = profile;
  j["kind"] = kind;
  j["path"] = pathToUtf8(path);
  j["bookmarks"] = bookmarks;
  j["history"] = history;
  j["needsAccess"] = needsAccess;
  return j;
}

MigrationOptions MigrationOptions::fromJson(const Json& j) {
  MigrationOptions o;
  if (!j.is_object()) return o;
  if (auto it = j.find("bookmarks"); it != j.end() && it->is_boolean()) o.bookmarks = it->get<bool>();
  if (auto it = j.find("history"); it != j.end() && it->is_boolean()) o.history = it->get<bool>();
  if (auto it = j.find("historyLimit"); it != j.end() && it->is_number_integer()) {
    o.historyLimit = it->get<int64_t>();
  }
  return o;
}

Json MigrationResult::toJson() const {
  Json j = Json::object();
  j["ok"] = ok;
  j["error"] = error;
  j["message"] = message;
  j["bookmarks"] = bookmarks;
  j["history"] = history;
  j["warnings"] = warnings;
  return j;
}

BrowserRoots BrowserRoots::system() {
  return {paths::homeDir(), paths::localAppData(), paths::roamingAppData()};
}

std::vector<MigrationSource> Migrator::detect(const BrowserRoots& roots) {
  std::vector<MigrationSource> out;
#if defined(__APPLE__)
  detectSafari(roots, out);
#endif
  for (const auto& def : kBrowsers) {
    fs::path root = baseDir(roots, def.base);
    if (root.empty()) continue;
    root /= utf8ToPath(def.dir);
    if (def.kind == Kind::kChromium) {
      detectChromium(def, root, out);
    } else {
      detectFirefox(def, root, out);
    }
  }
  return out;
}

std::optional<MigrationSource> Migrator::find(std::string_view id, const BrowserRoots& roots) {
  for (auto& s : detect(roots)) {
    if (s.id == id) return std::move(s);
  }
  return std::nullopt;
}

std::optional<Json> Migrator::read(const MigrationSource& source, const MigrationOptions& options,
                                   MigrationResult& result) {
  result = MigrationResult{};
  if (!options.bookmarks && !options.history) {
    fail(result, {"nothing_selected", "nothing to import"});
    return std::nullopt;
  }

  Json out = Json::object();
  std::vector<Problem> problems;
  auto run = [&](const char* item, auto&& fn) {
    Failure failure;
    if (auto data = fn(failure)) {
      out[item] = std::move(*data);
    } else {
      problems.push_back({item, failure});
    }
  };

  if (source.kind == "chromium") {
    if (options.bookmarks) {
      run("bookmarks", [&](Failure& f) -> std::optional<Json> {
        auto text = readBytes(source.path / "Bookmarks", f, kMaxTextBytes);
        if (!text) return std::nullopt;
        return parseChromiumBookmarks(*text, f);
      });
    }
    if (options.history) {
      run("history", [&](Failure& f) -> std::optional<Json> {
        auto db = Database::open(source.path / "History", f);
        if (!db) return std::nullopt;
        return readChromiumHistory(*db, options.historyLimit, f);
      });
    }
  } else if (source.kind == "firefox") {
    // Bookmarks and history share places.sqlite: copy it once.
    Failure openFailure;
    auto db = Database::open(source.path / "places.sqlite", openFailure);
    if (options.bookmarks) {
      run("bookmarks", [&](Failure& f) -> std::optional<Json> {
        if (!db) {
          f = openFailure;
          return std::nullopt;
        }
        return readFirefoxBookmarks(*db, f);
      });
    }
    if (options.history) {
      run("history", [&](Failure& f) -> std::optional<Json> {
        if (!db) {
          f = openFailure;
          return std::nullopt;
        }
        return readFirefoxHistory(*db, options.historyLimit, f);
      });
    }
  } else if (source.kind == "safari") {
    if (options.bookmarks) {
      run("bookmarks", [&](Failure& f) -> std::optional<Json> {
        auto bytes = readBytes(source.path / "Bookmarks.plist", f, kMaxTextBytes);
        if (!bytes) return std::nullopt;
        return parseSafariBookmarks(*bytes, f);
      });
    }
    if (options.history) {
      run("history", [&](Failure& f) -> std::optional<Json> {
        auto db = Database::open(source.path / "History.db", f);
        if (!db) return std::nullopt;
        return readSafariHistory(*db, options.historyLimit, f);
      });
    }
  } else {
    fail(result, {"unsupported", "unknown source kind: " + source.kind});
    return std::nullopt;
  }
  return finish(std::move(out), problems, result);
}

std::optional<Json> Migrator::readFile(const fs::path& file, const MigrationOptions& options,
                                       MigrationResult& result) {
  result = MigrationResult{};
  if (!options.bookmarks && !options.history) {
    fail(result, {"nothing_selected", "nothing to import"});
    return std::nullopt;
  }

  Failure failure;
  auto head = readBytes(file, failure, 16);
  if (!head) {
    fail(result, failure);
    return std::nullopt;
  }

  Json out = Json::object();
  std::vector<Problem> problems;
  auto take = [&](const char* item, std::optional<Json> data, const Failure& f) {
    if (data) {
      out[item] = std::move(*data);
    } else {
      problems.push_back({item, f});
    }
  };

  if (*head == std::string_view("SQLite format 3\0", 16)) {
    auto db = Database::open(file, failure);
    if (!db) {
      fail(result, failure);
      return std::nullopt;
    }
    Failure f;
    if (db->hasTable("moz_places") && db->hasTable("moz_bookmarks")) {
      if (options.bookmarks) take("bookmarks", readFirefoxBookmarks(*db, f), f);
      if (options.history) take("history", readFirefoxHistory(*db, options.historyLimit, f), f);
    } else if (db->hasTable("urls") && db->hasTable("visits")) {
      if (options.history) take("history", readChromiumHistory(*db, options.historyLimit, f), f);
    } else if (db->hasTable("history_items") && db->hasTable("history_visits")) {
      if (options.history) take("history", readSafariHistory(*db, options.historyLimit, f), f);
    } else {
      fail(result, {"unsupported", "the database is not a known browser format"});
      return std::nullopt;
    }
    return finish(std::move(out), problems, result);
  }

  // Every other supported format holds bookmarks only.
  if (!options.bookmarks) {
    fail(result, {"nothing_selected", "the file contains bookmarks only"});
    return std::nullopt;
  }
  auto data = readBytes(file, failure, kMaxTextBytes);
  if (!data) {
    fail(result, failure);
    return std::nullopt;
  }

  Failure f;
  if (startsWith(*data, "bplist00")) {
    take("bookmarks", parseSafariBookmarks(*data, f), f);
    return finish(std::move(out), problems, result);
  }

  std::string_view text = *data;
  if (startsWith(text, "\xEF\xBB\xBF")) text.remove_prefix(3);
  std::string start = trim(text.substr(0, 64));
  if (startsWith(start, "{")) {
    take("bookmarks", parseChromiumBookmarks(text, f), f);
    return finish(std::move(out), problems, result);
  }

  std::string lower = toLower(text.substr(0, 4096));
  if (lower.find("netscape-bookmark-file") != std::string::npos || lower.find("<dl") != std::string::npos) {
    auto tree = parseBookmarksHtml(text);
    take("bookmarks", std::move(tree), {"corrupt", "the bookmarks file cannot be parsed"});
    return finish(std::move(out), problems, result);
  }

  fail(result, {"unsupported", "the file is not a known bookmarks or history format"});
  return std::nullopt;
}

MigrationResult Migrator::migrate(Storage& storage, const MigrationSource& source, const MigrationOptions& options) {
  MigrationResult result;
  auto data = read(source, options, result);
  return data ? commit(storage, *data, std::move(result)) : result;
}

MigrationResult Migrator::migrateFile(Storage& storage, const fs::path& file, const MigrationOptions& options) {
  MigrationResult result;
  auto data = readFile(file, options, result);
  return data ? commit(storage, *data, std::move(result)) : result;
}

}  // namespace liora

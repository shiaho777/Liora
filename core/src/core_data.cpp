#include "liora/core.hpp"

#include "liora/migration.hpp"

namespace liora {

namespace {

Json wrapBookmarks(const Json& tree) { return Json{{"bookmarks", tree}}; }

}  // namespace

void Core::registerBackupSections() {
  backup_.registerSection({"settings", [this] { return settings_.exportData(); },
                           [this](const Json& data, bool merge) -> std::optional<std::string> {
                             if (auto error = settings_.importData(data, merge)) return error;
                             reloadRules();
                             persistSettings();
                             return std::nullopt;
                           },
                           [](const Json& data) {
                             int64_t sites = data.contains("sites") && data["sites"].is_object()
                                                 ? static_cast<int64_t>(data["sites"].size())
                                                 : 0;
                             int64_t values = data.contains("global") && data["global"].is_object()
                                                   ? static_cast<int64_t>(data["global"].size())
                                                   : 0;
                             return Json{{"values", values}, {"sites", sites}};
                           }});

  backup_.registerSection({"bookmarks", [this] { return storage_.exportData()["bookmarks"]; },
                           [this](const Json& data, bool merge) -> std::optional<std::string> {
                             return storage_.importData(wrapBookmarks(data), merge);
                           },
                           [](const Json& data) { return Json{{"count", 0}, {"tree", data.is_object()}}; }});

  backup_.registerSection({"history", [this] { return storage_.exportData()["history"]; },
                           [this](const Json& data, bool merge) -> std::optional<std::string> {
                             return storage_.importData(Json{{"history", data}}, merge);
                           },
                           [](const Json& data) {
                             return Json{{"entries", data.is_array() ? data.size() : 0}};
                           }});

  backup_.registerSection({"permissions", [this] { return storage_.exportData()["permissions"]; },
                           [this](const Json& data, bool merge) -> std::optional<std::string> {
                             return storage_.importData(Json{{"permissions", data}}, merge);
                           },
                           [](const Json& data) {
                             return Json{{"entries", data.is_array() ? data.size() : 0}};
                           }});
}

void Core::registerDataMethods() {
  // ---- Bookmarks ----

  rpc_.add("bookmarks.tree", [this](const Json&) { return storage_.bookmarkTree(); });

  rpc_.add("bookmarks.add", [this](const Json& params) {
    std::string url = params::string(params, "url");
    int64_t parent = params::optionalInteger(params, "parent").value_or(Storage::kOtherBookmarks);
    std::string title = params::optionalString(params, "title").value_or("");
    auto id = storage_.addBookmark(parent, title, url);
    if (!id) rpcFail(kRpcInvalidParams, "cannot add the bookmark to that folder");
    rpc_.notify("bookmarks.changed", nullptr);
    return Json{{"id", *id}};
  });

  rpc_.add("bookmarks.addFolder", [this](const Json& params) {
    int64_t parent = params::optionalInteger(params, "parent").value_or(Storage::kOtherBookmarks);
    auto id = storage_.addFolder(parent, params::optionalString(params, "title").value_or(""));
    if (!id) rpcFail(kRpcInvalidParams, "cannot create the folder there");
    rpc_.notify("bookmarks.changed", nullptr);
    return Json{{"id", *id}};
  });

  rpc_.add("bookmarks.update", [this](const Json& params) {
    int64_t id = params::integer(params, "id");
    auto title = params::optionalString(params, "title");
    auto url = params::optionalString(params, "url");
    if (!storage_.updateBookmark(id, title, url)) rpcFail(kRpcNotFound, "bookmark not found");
    rpc_.notify("bookmarks.changed", nullptr);
    return Json{{"ok", true}};
  });

  rpc_.add("bookmarks.move", [this](const Json& params) {
    int64_t id = params::integer(params, "id");
    int64_t parent = params::integer(params, "parent");
    auto position = params::optionalInteger(params, "position");
    std::optional<int64_t> pos;
    if (position) pos = *position;
    if (!storage_.moveBookmark(id, parent, pos)) rpcFail(kRpcInvalidParams, "cannot move the bookmark there");
    rpc_.notify("bookmarks.changed", nullptr);
    return Json{{"ok", true}};
  });

  rpc_.add("bookmarks.remove", [this](const Json& params) {
    if (!storage_.removeBookmark(params::integer(params, "id"))) rpcFail(kRpcNotFound, "bookmark not found");
    rpc_.notify("bookmarks.changed", nullptr);
    return Json{{"ok", true}};
  });

  rpc_.add("bookmarks.search", [this](const Json& params) {
    int limit = static_cast<int>(params::optionalInteger(params, "limit").value_or(50));
    Json out = Json::array();
    for (const auto& b : storage_.searchBookmarks(params::string(params, "query"), limit)) {
      out.push_back(b.toJson());
    }
    return out;
  });

  rpc_.add("bookmarks.forUrl", [this](const Json& params) {
    Json out = Json::array();
    for (const auto& b : storage_.bookmarksForUrl(params::string(params, "url"))) out.push_back(b.toJson());
    return out;
  });

  // ---- History ----

  rpc_.add("history.search", [this](const Json& params) {
    int limit = static_cast<int>(params::optionalInteger(params, "limit").value_or(50));
    auto before = params::optionalInteger(params, "before");
    Json out = Json::array();
    for (const auto& h : storage_.searchHistory(params::string(params, "query"), limit, before.value_or(0))) {
      out.push_back(h.toJson());
    }
    return out;
  });

  rpc_.add("history.suggest", [this](const Json& params) {
    int limit = static_cast<int>(params::optionalInteger(params, "limit").value_or(6));
    Json out = Json::array();
    for (const auto& h : storage_.suggest(params::string(params, "input"), limit)) out.push_back(h.toJson());
    return out;
  });

  rpc_.add("history.top", [this](const Json& params) {
    int limit = static_cast<int>(params::optionalInteger(params, "limit").value_or(8));
    Json out = Json::array();
    for (const auto& h : storage_.topSites(limit)) out.push_back(h.toJson());
    return out;
  });

  rpc_.add("history.add", [this](const Json& params) {
    storage_.addVisit(params::string(params, "url"), params::optionalString(params, "title").value_or(""),
                      params::boolean(params, "typed", false), params::optionalInteger(params, "time").value_or(0));
    rpc_.notify("history.changed", nullptr);
    return Json{{"ok", true}};
  });

  // ---- Favicons ----
  // Cached locally so tabs and the start page show real icons without any
  // third-party icon service. Only small inline images are accepted.

  rpc_.add("favicon.set", [this](const Json& params) {
    std::string host = params::string(params, "host");
    std::string icon = params::optionalString(params, "icon").value_or("");
    if (host.empty()) rpcFail(kRpcInvalidParams, "host is required");
    if (icon.size() > 200000 || (!icon.empty() && icon.rfind("data:image/", 0) != 0)) {
      rpcFail(kRpcInvalidParams, "icon must be a small data:image URL");
    }
    storage_.setFavicon(host, icon);
    return Json{{"ok", true}};
  });

  rpc_.add("favicon.get", [this](const Json& params) {
    auto icon = storage_.favicon(params::string(params, "host"));
    return Json{{"icon", icon.value_or("")}};
  });

  rpc_.add("favicon.list", [this](const Json& params) {
    int limit = static_cast<int>(params::optionalInteger(params, "limit").value_or(512));
    Json out = Json::array();
    for (const auto& f : storage_.favicons(limit)) out.push_back(f.toJson());
    return out;
  });

  rpc_.add("history.setTitle", [this](const Json& params) {
    storage_.setTitle(params::string(params, "url"), params::string(params, "title"));
    return Json{{"ok", true}};
  });

  rpc_.add("history.remove", [this](const Json& params) {
    if (!storage_.removeHistory(params::string(params, "url"))) rpcFail(kRpcNotFound, "entry not found");
    rpc_.notify("history.changed", nullptr);
    return Json{{"ok", true}};
  });

  rpc_.add("history.clear", [this](const Json& params) {
    storage_.clearHistory(params::optionalInteger(params, "from").value_or(0),
                          params::optionalInteger(params, "to").value_or(0));
    rpc_.notify("history.changed", nullptr);
    return Json{{"ok", true}};
  });

  // ---- Permissions ----

  rpc_.add("permissions.list", [this](const Json& params) {
    Json out = Json::array();
    for (const auto& p : storage_.permissions(params::optionalString(params, "origin").value_or(""))) {
      out.push_back(p.toJson());
    }
    return out;
  });

  rpc_.add("permissions.set", [this](const Json& params) {
    storage_.setPermission(params::string(params, "origin"), params::string(params, "name"),
                           params::optionalString(params, "value").value_or(""));
    rpc_.notify("permissions.changed", nullptr);
    return Json{{"ok", true}};
  });

  // ---- Downloads ----

  rpc_.add("downloads.list", [this](const Json& params) {
    int limit = static_cast<int>(params::optionalInteger(params, "limit").value_or(100));
    Json out = Json::array();
    for (const auto& record : storage_.downloads(limit)) out.push_back(record.toJson());
    return out;
  });

  rpc_.add("downloads.clear", [this](const Json&) {
    storage_.clearDownloads();
    rpc_.notify("downloads.changed", nullptr);
    return Json{{"ok", true}};
  });

  rpc_.add("downloads.remove", [this](const Json& params) {
    if (!storage_.removeDownload(params::string(params, "id"))) rpcFail(kRpcNotFound, "download not found");
    return Json{{"ok", true}};
  });

  // ---- Backup ----

  rpc_.add("backup.sections", [this](const Json&) {
    Json out = Json::array();
    for (const auto& name : backup_.sectionNames()) out.push_back(name);
    return out;
  });

  rpc_.add("backup.create", [this](const Json& params) {
    auto sections = params::stringList(params, "sections");
    if (auto path = params::optionalString(params, "path"); path && !path->empty()) {
      std::string error;
      if (!backup_.write(utf8ToPath(*path), sections, &error)) rpcFail(kRpcInternalError, error);
      return Json{{"path", *path}};
    }
    return backup_.create(sections);
  });

  rpc_.add("backup.inspect", [this](const Json& params) {
    std::string path = params::string(params, "path");
    std::string error;
    auto envelope = Backup::read(utf8ToPath(path), &error);
    if (!envelope) rpcFail(kRpcInvalidParams, error);
    return backup_.inspect(*envelope);
  });

  rpc_.add("backup.restore", [this](const Json& params) {
    std::string path = params::string(params, "path");
    bool merge = params::boolean(params, "merge", true);
    auto sections = params::stringList(params, "sections");
    std::string error;
    auto envelope = Backup::read(utf8ToPath(path), &error);
    if (!envelope) rpcFail(kRpcInvalidParams, error);
    if (auto restoreError = backup_.restore(*envelope, sections, merge)) rpcFail(kRpcInvalidParams, *restoreError);
    persistSettings();
    rpc_.notify("core.reloaded", nullptr);
    return Json{{"ok", true}};
  });

  rpc_.add("backup.list", [this](const Json&) {
    Json out = Json::array();
    for (const auto& path : Backup::list(paths_.backups)) {
      out.push_back({{"path", pathToUtf8(path)}, {"name", pathToUtf8(path.filename())}});
    }
    return out;
  });

  rpc_.add("backup.now", [this](const Json&) {
    std::string error;
    auto path = backup_.autoBackup(paths_.backups, 7, "manual", &error);
    if (!path) rpcFail(kRpcInternalError, error);
    return Json{{"path", pathToUtf8(*path)}};
  });

  // ---- Migration ----

  rpc_.add("migrate.detect", [](const Json&) {
    Json out = Json::array();
    for (const auto& source : Migrator::detect()) out.push_back(source.toJson());
    return out;
  });

  rpc_.add("migrate.preview", [](const Json& params) {
    std::string path = params::string(params, "path");
    MigrationOptions options = MigrationOptions::fromJson(params);
    MigrationResult result;
    auto data = Migrator::readFile(utf8ToPath(path), options, result);
    Json out = result.toJson();
    out["preview"] = data.has_value();
    return out;
  });

  rpc_.add("migrate.file", [this](const Json& params) {
    std::string path = params::string(params, "path");
    MigrationOptions options = MigrationOptions::fromJson(params);
    MigrationResult result = Migrator::migrateFile(storage_, utf8ToPath(path), options);
    if (result.ok) rpc_.notify("core.reloaded", nullptr);
    return result.toJson();
  });

  rpc_.add("migrate.run", [this](const Json& params) {
    std::string id = params::string(params, "id");
    MigrationOptions options = MigrationOptions::fromJson(params);
    auto source = Migrator::find(id);
    if (!source) rpcFail(kRpcNotFound, "that browser profile is no longer available");
    MigrationResult result = Migrator::migrate(storage_, *source, options);
    if (result.ok) rpc_.notify("core.reloaded", nullptr);
    return result.toJson();
  });

  rpc_.add("migrate.exportHtml", [this](const Json& params) {
    std::string path = params::string(params, "path");
    std::string html = Migrator::bookmarksToHtml(storage_.bookmarkTree());
    std::string error;
    if (!writeFileAtomic(utf8ToPath(path), html, &error)) rpcFail(kRpcInternalError, error);
    return Json{{"path", path}};
  });

  // ---- Session ----

  rpc_.add("session.save", [this](const Json& params) {
    Json session = {
        {"version", 1},
        {"savedAt", nowMs()},
        {"active", params::optionalInteger(params, "active").value_or(0)},
        {"tabs", params::optional(params, "tabs").value_or(Json::array())},
        {"groups", params::optional(params, "groups").value_or(Json::array())},
        {"containers", params::optional(params, "containers").value_or(Json::array())},
        {"split", params::optional(params, "split").value_or(Json())},
    };
    if (!session["tabs"].is_array()) rpcFail(kRpcInvalidParams, "tabs must be a list");
    if (!session["groups"].is_array()) rpcFail(kRpcInvalidParams, "groups must be a list");
    if (!session["containers"].is_array()) rpcFail(kRpcInvalidParams, "containers must be a list");
    if (!session["split"].is_null() && !session["split"].is_object()) {
      rpcFail(kRpcInvalidParams, "split must be an object or null");
    }
    std::string error;
    if (!writeFileAtomic(paths_.root / "session.json", session.dump(), &error)) {
      rpcFail(kRpcInternalError, error);
    }
    return Json{{"ok", true}};
  });

  rpc_.add("session.load", [this](const Json&) {
    auto text = readFile(paths_.root / "session.json");
    if (!text) return Json{{"tabs", Json::array()}, {"active", 0}};
    Json session = Json::parse(*text, nullptr, false);
    if (session.is_discarded() || !session.is_object()) {
      return Json{{"tabs", Json::array()}, {"active", 0}};
    }
    return session;
  });

  rpc_.add("session.clear", [this](const Json&) {
    std::error_code ec;
    fs::remove(paths_.root / "session.json", ec);
    return Json{{"ok", true}};
  });
}

}  // namespace liora

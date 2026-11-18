#include "liora/storage.hpp"

#include <sqlite3.h>

#include <algorithm>
#include <system_error>

#include "sqlite_stmt.hpp"

namespace liora {

namespace {

using detail::Stmt;

constexpr const char* kSchema = R"sql(
CREATE TABLE IF NOT EXISTS bookmarks(
  id INTEGER PRIMARY KEY,
  parent INTEGER REFERENCES bookmarks(id) ON DELETE CASCADE,
  folder INTEGER NOT NULL DEFAULT 0,
  title TEXT NOT NULL DEFAULT '',
  url TEXT NOT NULL DEFAULT '',
  position INTEGER NOT NULL DEFAULT 0,
  created INTEGER NOT NULL DEFAULT 0,
  modified INTEGER NOT NULL DEFAULT 0);
CREATE INDEX IF NOT EXISTS bookmarks_parent ON bookmarks(parent, position);
CREATE INDEX IF NOT EXISTS bookmarks_url ON bookmarks(url);

CREATE TABLE IF NOT EXISTS history(
  id INTEGER PRIMARY KEY,
  url TEXT NOT NULL UNIQUE,
  title TEXT NOT NULL DEFAULT '',
  visit_count INTEGER NOT NULL DEFAULT 0,
  typed_count INTEGER NOT NULL DEFAULT 0,
  last_visit INTEGER NOT NULL DEFAULT 0);
CREATE INDEX IF NOT EXISTS history_last ON history(last_visit);

CREATE TABLE IF NOT EXISTS visits(
  id INTEGER PRIMARY KEY,
  url_id INTEGER NOT NULL REFERENCES history(id) ON DELETE CASCADE,
  time INTEGER NOT NULL,
  typed INTEGER NOT NULL DEFAULT 0);
CREATE INDEX IF NOT EXISTS visits_time ON visits(time);
CREATE INDEX IF NOT EXISTS visits_url ON visits(url_id);

CREATE TABLE IF NOT EXISTS permissions(
  origin TEXT NOT NULL,
  name TEXT NOT NULL,
  value TEXT NOT NULL,
  modified INTEGER NOT NULL DEFAULT 0,
  PRIMARY KEY(origin, name));

INSERT OR IGNORE INTO bookmarks(id, parent, folder, title, position) VALUES
  (1, NULL, 1, 'Bookmarks bar', 0),
  (2, NULL, 1, 'Other bookmarks', 1),
  (3, NULL, 1, 'Mobile bookmarks', 2);

PRAGMA user_version = 1;
)sql";

// Added in schema version 2. Kept separate so existing databases migrate by
// running this idempotent block at startup.
constexpr const char* kDownloadsSchema = R"sql(
CREATE TABLE IF NOT EXISTS downloads(
  id TEXT PRIMARY KEY,
  filename TEXT NOT NULL DEFAULT '',
  path TEXT NOT NULL DEFAULT '',
  bytes INTEGER NOT NULL DEFAULT 0,
  total INTEGER NOT NULL DEFAULT 0,
  state TEXT NOT NULL DEFAULT '',
  error TEXT NOT NULL DEFAULT '',
  created INTEGER NOT NULL DEFAULT 0,
  updated INTEGER NOT NULL DEFAULT 0);
CREATE INDEX IF NOT EXISTS downloads_created ON downloads(created);
)sql";

// Added in schema version 3. Idempotent like the downloads block, so older
// databases pick it up at startup without a manual migration.
constexpr const char* kFaviconsSchema = R"sql(
CREATE TABLE IF NOT EXISTS favicons(
  host TEXT PRIMARY KEY,
  data_url TEXT NOT NULL DEFAULT '',
  updated INTEGER NOT NULL DEFAULT 0);
CREATE INDEX IF NOT EXISTS favicons_updated ON favicons(updated);
)sql";

const std::string kBookmarkCols =
    "SELECT id, IFNULL(parent, 0), folder, title, url, position, created, modified FROM bookmarks ";
const std::string kHistoryCols =
    "SELECT id, url, title, visit_count, typed_count, last_visit FROM history ";

Bookmark readBookmark(const Stmt& st) {
  Bookmark b;
  b.id = st.i64(0);
  b.parent = st.i64(1);
  b.folder = st.i64(2) != 0;
  b.title = st.text(3);
  b.url = st.text(4);
  b.position = st.i64(5);
  b.created = st.i64(6);
  b.modified = st.i64(7);
  return b;
}

HistoryEntry readHistory(const Stmt& st) {
  HistoryEntry h;
  h.id = st.i64(0);
  h.url = st.text(1);
  h.title = st.text(2);
  h.visitCount = st.i64(3);
  h.typedCount = st.i64(4);
  h.lastVisit = st.i64(5);
  return h;
}

std::string likeEscape(std::string_view s) {
  std::string out;
  out.reserve(s.size() + 2);
  for (char c : s) {
    if (c == '%' || c == '_' || c == '\\') out += '\\';
    out += c;
  }
  return out;
}

const char* rootKey(int64_t id) {
  switch (id) {
    case Storage::kBookmarkBar: return "bar";
    case Storage::kOtherBookmarks: return "other";
    case Storage::kMobileBookmarks: return "mobile";
    default: return "";
  }
}

std::string jsonString(const Json& j, const char* key) {
  auto it = j.find(key);
  return it != j.end() && it->is_string() ? it->get<std::string>() : std::string();
}

int64_t jsonInt(const Json& j, const char* key) {
  auto it = j.find(key);
  return it != j.end() && it->is_number_integer() ? it->get<int64_t>() : 0;
}

}  // namespace

Json Bookmark::toJson() const {
  Json j = {{"id", id}, {"parent", parent}, {"type", folder ? "folder" : "url"}, {"title", title},
            {"position", position}, {"created", created}, {"modified", modified}};
  if (!folder) j["url"] = url;
  return j;
}

Json HistoryEntry::toJson() const {
  return {{"id", id},           {"url", url},           {"title", title},
          {"visitCount", visitCount}, {"typedCount", typedCount}, {"lastVisit", lastVisit}};
}

Json Permission::toJson() const {
  return {{"origin", origin}, {"name", name}, {"value", value}, {"modified", modified}};
}

Json DownloadRecord::toJson() const {
  return {{"id", id},       {"filename", filename}, {"path", path},   {"state", state},
          {"error", error}, {"bytes", bytes},       {"total", total}, {"created", created},
          {"updated", updated}};
}

Storage::Storage(fs::path file) : file_(std::move(file)) {}

Storage::~Storage() { close(); }

bool Storage::exec(const char* sql, std::string* error) {
  char* msg = nullptr;
  if (sqlite3_exec(db_, sql, nullptr, nullptr, &msg) != SQLITE_OK) {
    if (error) *error = msg ? msg : "sqlite error";
    sqlite3_free(msg);
    return false;
  }
  return true;
}

bool Storage::open(std::string* error) {
  std::lock_guard lock(mutex_);
  if (db_) return true;
  std::error_code ec;
  if (file_.has_parent_path()) fs::create_directories(file_.parent_path(), ec);

  std::string path = pathToUtf8(file_);
  if (sqlite3_open_v2(path.c_str(), &db_, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX,
                      nullptr) != SQLITE_OK) {
    if (error) *error = db_ ? sqlite3_errmsg(db_) : "cannot open database";
    sqlite3_close(db_);
    db_ = nullptr;
    return false;
  }
  sqlite3_busy_timeout(db_, 5000);
  if (!exec("PRAGMA journal_mode = WAL; PRAGMA synchronous = NORMAL; PRAGMA foreign_keys = ON;", error)) {
    close();
    return false;
  }

  int64_t version = 0;
  {
    Stmt st(db_, "PRAGMA user_version");
    if (st.step()) version = st.i64(0);
  }
  if (version > 3) {
    if (error) *error = "database was created by a newer version of Liora";
    close();
    return false;
  }
  if (version < 1 && !exec(kSchema, error)) {
    close();
    return false;
  }
  if (!exec(kDownloadsSchema, error)) {
    close();
    return false;
  }
  if (!exec(kFaviconsSchema, error)) {
    close();
    return false;
  }
  exec("PRAGMA user_version = 3");
  return true;
}

void Storage::close() {
  std::lock_guard lock(mutex_);
  if (db_) {
    sqlite3_close_v2(db_);
    db_ = nullptr;
  }
  txDepth_ = 0;
}

bool Storage::isOpen() const {
  std::lock_guard lock(mutex_);
  return db_ != nullptr;
}

bool Storage::transaction(const std::function<bool()>& fn) {
  std::lock_guard lock(mutex_);
  if (!db_) return false;
  if (!exec("SAVEPOINT liora_tx")) return false;
  ++txDepth_;
  bool ok = false;
  try {
    ok = fn();
  } catch (...) {
    --txDepth_;
    exec("ROLLBACK TO liora_tx; RELEASE liora_tx;");
    throw;
  }
  --txDepth_;
  if (ok) return exec("RELEASE liora_tx");
  exec("ROLLBACK TO liora_tx; RELEASE liora_tx;");
  return false;
}

// ---- Bookmarks ----

bool Storage::isRootFolder(int64_t id) { return id >= kBookmarkBar && id <= kMobileBookmarks; }

int64_t Storage::childCount(int64_t parent, int64_t exclude) const {
  Stmt st(db_, "SELECT COUNT(*) FROM bookmarks WHERE parent = ? AND id <> ?");
  st.bind(1, parent).bind(2, exclude);
  return st.step() ? st.i64(0) : 0;
}

std::optional<int64_t> Storage::addNode(int64_t parent, bool folder, std::string_view title, std::string_view url,
                                        std::optional<int64_t> position, int64_t created) {
  std::lock_guard lock(mutex_);
  if (!db_) return std::nullopt;
  auto p = bookmark(parent);
  if (!p || !p->folder) return std::nullopt;

  std::optional<int64_t> id;
  transaction([&] {
    int64_t count = childCount(parent);
    int64_t pos = position ? std::clamp(*position, int64_t{0}, count) : count;
    if (!Stmt(db_, "UPDATE bookmarks SET position = position + 1 WHERE parent = ? AND position >= ?")
             .bind(1, parent)
             .bind(2, pos)
             .exec())
      return false;
    int64_t now = nowMs();
    Stmt st(db_,
            "INSERT INTO bookmarks(parent, folder, title, url, position, created, modified) "
            "VALUES(?, ?, ?, ?, ?, ?, ?)");
    st.bind(1, parent)
        .bind(2, int64_t{folder ? 1 : 0})
        .bind(3, title)
        .bind(4, folder ? std::string_view() : url)
        .bind(5, pos)
        .bind(6, created > 0 ? created : now)
        .bind(7, now);
    if (!st.exec()) return false;
    id = sqlite3_last_insert_rowid(db_);
    return true;
  });
  return id;
}

std::optional<int64_t> Storage::addBookmark(int64_t parent, std::string_view title, std::string_view url,
                                            std::optional<int64_t> position, int64_t created) {
  if (trim(url).empty()) return std::nullopt;
  return addNode(parent, false, title, trim(url), position, created);
}

std::optional<int64_t> Storage::addFolder(int64_t parent, std::string_view title, std::optional<int64_t> position,
                                          int64_t created) {
  return addNode(parent, true, title, {}, position, created);
}

bool Storage::updateBookmark(int64_t id, std::optional<std::string> title, std::optional<std::string> url) {
  std::lock_guard lock(mutex_);
  auto b = bookmark(id);
  if (!b) return false;
  if (url && (b->folder || trim(*url).empty())) return false;
  Stmt st(db_, "UPDATE bookmarks SET title = ?, url = ?, modified = ? WHERE id = ?");
  st.bind(1, title ? *title : b->title)
      .bind(2, url ? trim(*url) : b->url)
      .bind(3, nowMs())
      .bind(4, id);
  return st.exec();
}

bool Storage::moveBookmark(int64_t id, int64_t parent, std::optional<int64_t> position) {
  std::lock_guard lock(mutex_);
  if (isRootFolder(id)) return false;
  auto item = bookmark(id);
  auto target = bookmark(parent);
  if (!item || !target || !target->folder) return false;
  // A folder cannot be moved into itself or one of its descendants.
  for (int64_t p = parent; p != 0;) {
    if (p == id) return false;
    auto node = bookmark(p);
    if (!node) break;
    p = node->parent;
  }

  return transaction([&] {
    if (!Stmt(db_, "UPDATE bookmarks SET position = position - 1 WHERE parent = ? AND position > ?")
             .bind(1, item->parent)
             .bind(2, item->position)
             .exec())
      return false;
    int64_t count = childCount(parent, id);
    int64_t pos = position ? std::clamp(*position, int64_t{0}, count) : count;
    if (!Stmt(db_, "UPDATE bookmarks SET position = position + 1 WHERE parent = ? AND position >= ? AND id <> ?")
             .bind(1, parent)
             .bind(2, pos)
             .bind(3, id)
             .exec())
      return false;
    return Stmt(db_, "UPDATE bookmarks SET parent = ?, position = ?, modified = ? WHERE id = ?")
        .bind(1, parent)
        .bind(2, pos)
        .bind(3, nowMs())
        .bind(4, id)
        .exec();
  });
}

bool Storage::removeBookmark(int64_t id) {
  std::lock_guard lock(mutex_);
  if (isRootFolder(id)) return false;
  auto item = bookmark(id);
  if (!item) return false;
  return transaction([&] {
    if (!Stmt(db_, "DELETE FROM bookmarks WHERE id = ?").bind(1, id).exec()) return false;
    return Stmt(db_, "UPDATE bookmarks SET position = position - 1 WHERE parent = ? AND position > ?")
        .bind(1, item->parent)
        .bind(2, item->position)
        .exec();
  });
}

std::optional<Bookmark> Storage::bookmark(int64_t id) const {
  std::lock_guard lock(mutex_);
  Stmt st(db_, kBookmarkCols + "WHERE id = ?");
  st.bind(1, id);
  if (!st.step()) return std::nullopt;
  return readBookmark(st);
}

std::vector<Bookmark> Storage::children(int64_t parent) const {
  std::lock_guard lock(mutex_);
  std::vector<Bookmark> out;
  Stmt st(db_, kBookmarkCols + "WHERE parent = ? ORDER BY position");
  st.bind(1, parent);
  while (st.step()) out.push_back(readBookmark(st));
  return out;
}

std::vector<Bookmark> Storage::searchBookmarks(std::string_view query, int limit) const {
  std::lock_guard lock(mutex_);
  std::vector<Bookmark> out;
  std::string pattern = "%" + likeEscape(trim(query)) + "%";
  Stmt st(db_, kBookmarkCols +
                   "WHERE folder = 0 AND (title LIKE ?1 ESCAPE '\\' OR url LIKE ?1 ESCAPE '\\') "
                   "ORDER BY modified DESC LIMIT ?2");
  st.bind(1, pattern).bind(2, int64_t{limit});
  while (st.step()) out.push_back(readBookmark(st));
  return out;
}

std::vector<Bookmark> Storage::bookmarksForUrl(std::string_view url) const {
  std::lock_guard lock(mutex_);
  std::vector<Bookmark> out;
  Stmt st(db_, kBookmarkCols + "WHERE folder = 0 AND url = ?");
  st.bind(1, url);
  while (st.step()) out.push_back(readBookmark(st));
  return out;
}

Json Storage::nodeJson(const Bookmark& b) const {
  Json j = b.toJson();
  if (b.folder) {
    Json kids = Json::array();
    for (const auto& c : children(b.id)) kids.push_back(nodeJson(c));
    j["children"] = std::move(kids);
  }
  return j;
}

Json Storage::bookmarkTree() const {
  std::lock_guard lock(mutex_);
  Json tree = Json::object();
  for (int64_t id : {kBookmarkBar, kOtherBookmarks, kMobileBookmarks}) {
    if (auto b = bookmark(id)) tree[rootKey(id)] = nodeJson(*b);
  }
  return tree;
}

// ---- History ----

void Storage::addVisit(std::string_view url, std::string_view title, bool typed, int64_t time) {
  std::lock_guard lock(mutex_);
  if (!db_ || url.empty()) return;
  if (time <= 0) time = nowMs();
  transaction([&] {
    Stmt up(db_,
            "INSERT INTO history(url, title, visit_count, typed_count, last_visit) VALUES(?1, ?2, 1, ?3, ?4) "
            "ON CONFLICT(url) DO UPDATE SET visit_count = visit_count + 1, "
            "typed_count = typed_count + excluded.typed_count, "
            "last_visit = MAX(last_visit, excluded.last_visit), "
            "title = CASE WHEN excluded.title <> '' THEN excluded.title ELSE title END");
    up.bind(1, url).bind(2, title).bind(3, int64_t{typed ? 1 : 0}).bind(4, time);
    if (!up.exec()) return false;
    return Stmt(db_, "INSERT INTO visits(url_id, time, typed) SELECT id, ?2, ?3 FROM history WHERE url = ?1")
        .bind(1, url)
        .bind(2, time)
        .bind(3, int64_t{typed ? 1 : 0})
        .exec();
  });
}

void Storage::setTitle(std::string_view url, std::string_view title) {
  std::lock_guard lock(mutex_);
  if (title.empty()) return;
  Stmt(db_, "UPDATE history SET title = ? WHERE url = ?").bind(1, title).bind(2, url).exec();
}

void Storage::mergeHistory(const HistoryEntry& e) {
  std::lock_guard lock(mutex_);
  if (!db_ || e.url.empty()) return;
  int64_t visits = std::max<int64_t>(e.visitCount, 1);
  int64_t last = e.lastVisit > 0 ? e.lastVisit : nowMs();
  transaction([&] {
    Stmt up(db_,
            "INSERT INTO history(url, title, visit_count, typed_count, last_visit) VALUES(?1, ?2, ?3, ?4, ?5) "
            "ON CONFLICT(url) DO UPDATE SET visit_count = MAX(visit_count, excluded.visit_count), "
            "typed_count = MAX(typed_count, excluded.typed_count), "
            "last_visit = MAX(last_visit, excluded.last_visit), "
            "title = CASE WHEN title = '' THEN excluded.title ELSE title END");
    up.bind(1, e.url).bind(2, e.title).bind(3, visits).bind(4, std::max<int64_t>(e.typedCount, 0)).bind(5, last);
    if (!up.exec()) return false;
    // One visit row keeps time-range clearing meaningful for imported entries.
    // Skipped when it already exists so repeated imports stay idempotent.
    return Stmt(db_,
                "INSERT INTO visits(url_id, time, typed) SELECT h.id, ?2, ?3 FROM history h WHERE h.url = ?1 "
                "AND NOT EXISTS (SELECT 1 FROM visits v WHERE v.url_id = h.id AND v.time = ?2)")
        .bind(1, e.url)
        .bind(2, last)
        .bind(3, int64_t{e.typedCount > 0 ? 1 : 0})
        .exec();
  });
}

std::vector<HistoryEntry> Storage::searchHistory(std::string_view query, int limit, int64_t before) const {
  std::lock_guard lock(mutex_);
  std::vector<HistoryEntry> out;
  std::string pattern = "%" + likeEscape(trim(query)) + "%";
  Stmt st(db_, kHistoryCols +
                   "WHERE (title LIKE ?1 ESCAPE '\\' OR url LIKE ?1 ESCAPE '\\') AND (?2 = 0 OR last_visit < ?2) "
                   "ORDER BY last_visit DESC LIMIT ?3");
  st.bind(1, pattern).bind(2, before).bind(3, int64_t{limit});
  while (st.step()) out.push_back(readHistory(st));
  return out;
}

std::vector<HistoryEntry> Storage::topSites(int limit) const {
  std::lock_guard lock(mutex_);
  std::vector<HistoryEntry> out;
  // Frecency: typed visits count double, score decays with weeks since last visit.
  Stmt st(db_, kHistoryCols +
                   "ORDER BY (visit_count + 2.0 * typed_count) / (1.0 + MAX(?1 - last_visit, 0) / 604800000.0) DESC, "
                   "last_visit DESC LIMIT ?2");
  st.bind(1, nowMs()).bind(2, int64_t{limit});
  while (st.step()) out.push_back(readHistory(st));
  return out;
}

std::vector<HistoryEntry> Storage::suggest(std::string_view input, int limit) const {
  std::lock_guard lock(mutex_);
  std::vector<HistoryEntry> out;
  std::string q = likeEscape(toLower(trim(input)));
  if (q.empty()) return out;
  Stmt st(db_, kHistoryCols +
                   "WHERE url LIKE 'https://' || ?1 || '%' ESCAPE '\\' "
                   "OR url LIKE 'http://' || ?1 || '%' ESCAPE '\\' "
                   "OR url LIKE 'https://www.' || ?1 || '%' ESCAPE '\\' "
                   "OR url LIKE 'http://www.' || ?1 || '%' ESCAPE '\\' "
                   "OR title LIKE '%' || ?1 || '%' ESCAPE '\\' "
                   "ORDER BY (url LIKE '%://' || ?1 || '%' ESCAPE '\\' "
                   "OR url LIKE '%://www.' || ?1 || '%' ESCAPE '\\') DESC, "
                   "typed_count DESC, visit_count DESC, last_visit DESC LIMIT ?2");
  st.bind(1, q).bind(2, int64_t{limit});
  while (st.step()) out.push_back(readHistory(st));
  return out;
}

bool Storage::removeHistory(std::string_view url) {
  std::lock_guard lock(mutex_);
  return Stmt(db_, "DELETE FROM history WHERE url = ?").bind(1, url).exec();
}

void Storage::clearHistory(int64_t from, int64_t to) {
  std::lock_guard lock(mutex_);
  if (!db_) return;
  if (from <= 0 && to <= 0) {
    exec("DELETE FROM history");
    return;
  }
  if (to <= 0) to = INT64_MAX;
  transaction([&] {
    if (!Stmt(db_, "DELETE FROM visits WHERE time >= ? AND time < ?").bind(1, from).bind(2, to).exec())
      return false;
    return exec(
        "DELETE FROM history WHERE id NOT IN (SELECT url_id FROM visits);"
        "UPDATE history SET "
        "visit_count = (SELECT COUNT(*) FROM visits WHERE url_id = history.id), "
        "typed_count = (SELECT COUNT(*) FROM visits WHERE url_id = history.id AND typed = 1), "
        "last_visit = (SELECT MAX(time) FROM visits WHERE url_id = history.id);");
  });
}

// ---- Site permissions ----

void Storage::setPermission(std::string_view origin, std::string_view name, std::string_view value) {
  std::lock_guard lock(mutex_);
  if (origin.empty() || name.empty()) return;
  if (value.empty()) {
    Stmt(db_, "DELETE FROM permissions WHERE origin = ? AND name = ?").bind(1, origin).bind(2, name).exec();
    return;
  }
  Stmt(db_,
       "INSERT INTO permissions(origin, name, value, modified) VALUES(?, ?, ?, ?) "
       "ON CONFLICT(origin, name) DO UPDATE SET value = excluded.value, modified = excluded.modified")
      .bind(1, origin)
      .bind(2, name)
      .bind(3, value)
      .bind(4, nowMs())
      .exec();
}

std::optional<std::string> Storage::permission(std::string_view origin, std::string_view name) const {
  std::lock_guard lock(mutex_);
  Stmt st(db_, "SELECT value FROM permissions WHERE origin = ? AND name = ?");
  st.bind(1, origin).bind(2, name);
  if (!st.step()) return std::nullopt;
  return st.text(0);
}

std::vector<Permission> Storage::permissions(std::string_view origin) const {
  std::lock_guard lock(mutex_);
  std::vector<Permission> out;
  Stmt st(db_,
          "SELECT origin, name, value, modified FROM permissions WHERE ?1 = '' OR origin = ?1 "
          "ORDER BY origin, name");
  st.bind(1, origin);
  while (st.step()) out.push_back({st.text(0), st.text(1), st.text(2), st.i64(3)});
  return out;
}

// ---- Downloads ----

void Storage::saveDownload(const DownloadRecord& record) {
  std::lock_guard lock(mutex_);
  if (!db_ || record.id.empty()) return;
  int64_t now = nowMs();
  Stmt st(db_,
          "INSERT INTO downloads(id, filename, path, bytes, total, state, error, created, updated) "
          "VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9) "
          "ON CONFLICT(id) DO UPDATE SET filename = excluded.filename, path = excluded.path, "
          "bytes = excluded.bytes, total = excluded.total, state = excluded.state, "
          "error = excluded.error, updated = excluded.updated");
  st.bind(1, record.id)
      .bind(2, record.filename)
      .bind(3, record.path)
      .bind(4, record.bytes)
      .bind(5, record.total)
      .bind(6, record.state)
      .bind(7, record.error)
      .bind(8, record.created > 0 ? record.created : now)
      .bind(9, now);
  st.exec();
}

std::vector<DownloadRecord> Storage::downloads(int limit) const {
  std::lock_guard lock(mutex_);
  std::vector<DownloadRecord> out;
  Stmt st(db_,
          "SELECT id, filename, path, bytes, total, state, error, created, updated FROM downloads "
          "ORDER BY created DESC LIMIT ?");
  st.bind(1, int64_t{limit > 0 ? limit : 100});
  while (st.step()) {
    out.push_back({st.text(0), st.text(1), st.text(2), st.text(5), st.text(6),
                   st.i64(3), st.i64(4), st.i64(7), st.i64(8)});
  }
  return out;
}

void Storage::clearDownloads() {
  std::lock_guard lock(mutex_);
  if (!db_) return;
  exec("DELETE FROM downloads");
}

bool Storage::removeDownload(std::string_view id) {
  std::lock_guard lock(mutex_);
  if (!db_) return false;
  return Stmt(db_, "DELETE FROM downloads WHERE id = ?").bind(1, id).exec();
}

// ---- Favicons ----

Json Favicon::toJson() const {
  return {{"host", host}, {"dataUrl", dataUrl}, {"updated", updated}};
}

void Storage::setFavicon(std::string_view host, std::string_view dataUrl) {
  std::lock_guard lock(mutex_);
  if (!db_ || host.empty()) return;
  if (dataUrl.empty()) {
    Stmt(db_, "DELETE FROM favicons WHERE host = ?").bind(1, host).exec();
    return;
  }
  Stmt st(db_,
          "INSERT INTO favicons(host, data_url, updated) VALUES(?1, ?2, ?3) "
          "ON CONFLICT(host) DO UPDATE SET data_url = excluded.data_url, updated = excluded.updated");
  st.bind(1, host).bind(2, dataUrl).bind(3, nowMs()).exec();
  // Keep the cache bounded: the least recently refreshed entries go first.
  exec("DELETE FROM favicons WHERE host NOT IN "
       "(SELECT host FROM favicons ORDER BY updated DESC LIMIT 512)");
}

std::optional<std::string> Storage::favicon(std::string_view host) const {
  std::lock_guard lock(mutex_);
  if (!db_) return std::nullopt;
  Stmt st(db_, "SELECT data_url FROM favicons WHERE host = ?");
  st.bind(1, host);
  if (!st.step()) return std::nullopt;
  return st.text(0);
}

std::vector<Favicon> Storage::favicons(int limit) const {
  std::lock_guard lock(mutex_);
  std::vector<Favicon> out;
  if (!db_) return out;
  Stmt st(db_, "SELECT host, data_url, updated FROM favicons ORDER BY updated DESC LIMIT ?");
  st.bind(1, int64_t{limit > 0 ? limit : 512});
  while (st.step()) out.push_back({st.text(0), st.text(1), st.i64(2)});
  return out;
}

// ---- Backup ----

Json Storage::exportData() const {
  std::lock_guard lock(mutex_);
  Json history = Json::array();
  {
    Stmt st(db_, kHistoryCols + "ORDER BY last_visit DESC");
    while (st.step()) {
      Json h = readHistory(st).toJson();
      h.erase("id");
      history.push_back(std::move(h));
    }
  }
  Json perms = Json::array();
  for (const auto& p : permissions()) perms.push_back(p.toJson());
  return {{"version", 1}, {"bookmarks", bookmarkTree()}, {"history", std::move(history)}, {"permissions", std::move(perms)}};
}

bool Storage::importChildren(int64_t parent, const Json& children, bool merge) {
  if (!children.is_array()) return true;
  for (const auto& node : children) {
    if (!node.is_object()) continue;
    std::string title = jsonString(node, "title");
    int64_t created = jsonInt(node, "created");
    if (jsonString(node, "type") == "folder") {
      std::optional<int64_t> folder;
      if (merge) {
        for (const auto& c : this->children(parent)) {
          if (c.folder && c.title == title) {
            folder = c.id;
            break;
          }
        }
      }
      if (!folder) folder = addFolder(parent, title, std::nullopt, created);
      if (!folder) return false;
      auto it = node.find("children");
      if (it != node.end() && !importChildren(*folder, *it, merge)) return false;
    } else {
      std::string url = jsonString(node, "url");
      if (url.empty()) continue;
      if (merge) {
        bool exists = false;
        for (const auto& b : bookmarksForUrl(url)) exists = exists || b.parent == parent;
        if (exists) continue;
      }
      if (!addBookmark(parent, title, url, std::nullopt, created)) return false;
    }
  }
  return true;
}

std::optional<std::string> Storage::importData(const Json& data, bool merge) {
  std::lock_guard lock(mutex_);
  if (!db_) return "storage is not open";
  if (!data.is_object()) return "backup data is not an object";

  std::optional<std::string> error;
  bool ok = transaction([&] {
    if (!merge && !exec("DELETE FROM bookmarks WHERE parent IS NOT NULL; DELETE FROM history; DELETE FROM permissions;")) {
      error = "cannot clear existing data";
      return false;
    }

    if (auto it = data.find("bookmarks"); it != data.end() && it->is_object()) {
      for (int64_t root : {kBookmarkBar, kOtherBookmarks, kMobileBookmarks}) {
        auto node = it->find(rootKey(root));
        if (node == it->end() || !node->is_object()) continue;
        auto kids = node->find("children");
        if (kids != node->end() && !importChildren(root, *kids, merge)) {
          error = "cannot import bookmarks";
          return false;
        }
      }
    }

    if (auto it = data.find("history"); it != data.end() && it->is_array()) {
      for (const auto& h : *it) {
        if (!h.is_object()) continue;
        HistoryEntry e;
        e.url = jsonString(h, "url");
        e.title = jsonString(h, "title");
        e.visitCount = jsonInt(h, "visitCount");
        e.typedCount = jsonInt(h, "typedCount");
        e.lastVisit = jsonInt(h, "lastVisit");
        mergeHistory(e);
      }
    }

    if (auto it = data.find("permissions"); it != data.end() && it->is_array()) {
      for (const auto& p : *it) {
        if (!p.is_object()) continue;
        setPermission(jsonString(p, "origin"), jsonString(p, "name"), jsonString(p, "value"));
      }
    }
    return true;
  });
  if (!ok && !error) error = "import failed";
  return ok ? std::nullopt : error;
}

}  // namespace liora

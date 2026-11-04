#include <sqlite3.h>

#include <algorithm>
#include <cerrno>
#include <fstream>
#include <iterator>

#include "liora/paths.hpp"
#include "migration/internal.hpp"
#include "sqlite_stmt.hpp"

namespace liora::migration {

namespace {

constexpr int64_t kChromiumEpochOffsetMs = 11644473600000;  // 1601-01-01 -> 1970-01-01
constexpr double kAppleEpochOffsetS = 978307200.0;          // 1970-01-01 -> 2001-01-01

// Another process holds the file open or locked. On Windows this happens while
// the source browser is running; it must be checked before permission errors
// because the sharing violation maps to errc::permission_denied there.
bool isBusyError(const std::error_code& ec) {
#ifdef _WIN32
  return ec.category() == std::system_category() && (ec.value() == 32 || ec.value() == 33);
#else
  return ec == std::errc::device_or_resource_busy || ec == std::errc::text_file_busy;
#endif
}

Failure failureFor(const std::error_code& ec, const fs::path& file) {
  std::string name = pathToUtf8(file);
  if (ec == std::errc::no_such_file_or_directory) return {"not_found", name + " does not exist"};
  if (isBusyError(ec)) return {"busy", name + " is in use by another application"};
  if (isPermissionError(ec)) return {"permission", "access to " + name + " was denied"};
  return {"io", name + ": " + ec.message()};
}

}  // namespace

int64_t chromiumTimeToMs(int64_t micros) {
  if (micros <= 0) return 0;
  return std::max<int64_t>(micros / 1000 - kChromiumEpochOffsetMs, 0);
}

int64_t appleTimeToMs(double seconds) {
  if (seconds <= 0) return 0;
  return static_cast<int64_t>((seconds + kAppleEpochOffsetS) * 1000.0);
}

bool isPermissionError(const std::error_code& ec) {
  return ec == std::errc::permission_denied || ec == std::errc::operation_not_permitted;
}

std::optional<std::string> readBytes(const fs::path& file, Failure& failure, size_t limit) {
  std::error_code ec;
  auto status = fs::status(file, ec);
  if (ec) {
    failure = failureFor(ec, file);
    return std::nullopt;
  }
  if (!fs::is_regular_file(status)) {
    failure = {"not_found", pathToUtf8(file) + " is not a file"};
    return std::nullopt;
  }
  errno = 0;
  std::ifstream in(file, std::ios::binary);
  if (!in) {
    failure = failureFor(std::error_code(errno ? errno : EACCES, std::generic_category()), file);
    return std::nullopt;
  }
  std::string data;
  char buf[64 * 1024];
  while (in && data.size() < limit) {
    size_t want = std::min(sizeof(buf), limit - data.size());
    in.read(buf, static_cast<std::streamsize>(want));
    data.append(buf, static_cast<size_t>(in.gcount()));
  }
  if (in.bad()) {
    failure = {"io", "cannot read " + pathToUtf8(file)};
    return std::nullopt;
  }
  return data;
}

Database::~Database() {
  if (db_) sqlite3_close_v2(db_);
  if (!dir_.empty()) {
    std::error_code ec;
    fs::remove_all(dir_, ec);
  }
}

std::unique_ptr<Database> Database::open(const fs::path& file, Failure& failure) {
  std::error_code ec;
  auto status = fs::status(file, ec);
  if (ec || !fs::is_regular_file(status)) {
    failure = ec ? failureFor(ec, file) : Failure{"not_found", pathToUtf8(file) + " does not exist"};
    return nullptr;
  }

  std::unique_ptr<Database> db(new Database);
  db->dir_ = paths::tempDir() / newId("migrate-");
  fs::create_directories(db->dir_, ec);
  if (ec) {
    failure = {"io", "cannot create a temporary directory: " + ec.message()};
    return nullptr;
  }

  fs::path copy = db->dir_ / "db.sqlite";
  fs::copy_file(file, copy, ec);
  if (ec) {
    failure = failureFor(ec, file);
    return nullptr;
  }
  for (const char* suffix : {"-wal", "-journal"}) {
    fs::path side = file;
    side += suffix;
    fs::path target = copy;
    target += suffix;
    // Optional: without it the snapshot is only slightly older.
    if (fs::exists(side, ec)) fs::copy_file(side, target, ec);
  }

  if (sqlite3_open_v2(pathToUtf8(copy).c_str(), &db->db_, SQLITE_OPEN_READWRITE, nullptr) != SQLITE_OK) {
    failure = {"corrupt", "cannot open " + pathToUtf8(file) + ": " + sqlite3_errmsg(db->db_)};
    return nullptr;
  }
  return db;
}

bool Database::hasTable(std::string_view name) const {
  detail::Stmt st(db_, "SELECT 1 FROM sqlite_master WHERE type = 'table' AND name = ?");
  st.bind(1, name);
  return st.step();
}

std::string getString(const Json& j, const char* key) {
  if (!j.is_object()) return {};
  auto it = j.find(key);
  return it != j.end() && it->is_string() ? it->get<std::string>() : std::string();
}

bool importableUrl(std::string_view url) {
  std::string lower = toLower(url.substr(0, 8));
  return startsWith(lower, "http://") || startsWith(lower, "https://") || startsWith(lower, "ftp://") ||
         startsWith(lower, "file://");
}

Json folderNode(std::string_view title, int64_t created) {
  Json node = Json::object();
  node["type"] = "folder";
  node["title"] = std::string(title);
  node["created"] = created;
  node["children"] = Json::array();
  return node;
}

Json urlNode(std::string_view title, std::string_view url, int64_t created) {
  Json node = Json::object();
  node["type"] = "url";
  node["title"] = std::string(title.empty() ? url : title);
  node["url"] = std::string(url);
  node["created"] = created;
  return node;
}

Json emptyRoots() {
  Json roots = Json::object();
  roots["bar"] = folderNode("");
  roots["other"] = folderNode("");
  roots["mobile"] = folderNode("");
  return roots;
}

int64_t countUrls(const Json& node) {
  int64_t n = 0;
  if (node.is_array()) {
    for (const auto& c : node) n += countUrls(c);
    return n;
  }
  if (!node.is_object()) return 0;
  if (auto type = node.find("type"); type != node.end()) {
    if (*type == "url") return 1;
    auto kids = node.find("children");
    return kids == node.end() ? 0 : countUrls(*kids);
  }
  for (const auto& item : node.items()) n += countUrls(item.value());
  return n;
}

std::optional<Json> readHistoryQuery(const Database& db, const std::string& sql, int64_t limit, TimeBase base,
                                     Failure& failure) {
  detail::Stmt st(db.get(), sql);
  if (!st) {
    failure = {"corrupt", std::string("unexpected history database: ") + sqlite3_errmsg(db.get())};
    return std::nullopt;
  }
  st.bind(1, limit > 0 ? limit : int64_t{-1});

  Json out = Json::array();
  while (st.step()) {
    std::string url = st.text(0);
    if (!importableUrl(url)) continue;
    int64_t last = 0;
    switch (base) {
      case TimeBase::kChromium: last = chromiumTimeToMs(st.i64(4)); break;
      case TimeBase::kUnixMicros: last = std::max<int64_t>(st.i64(4) / 1000, 0); break;
      case TimeBase::kAppleSeconds: last = appleTimeToMs(st.f64(4)); break;
    }
    Json h = Json::object();
    h["url"] = std::move(url);
    h["title"] = st.text(1);
    h["visitCount"] = std::max<int64_t>(st.i64(2), 1);
    h["typedCount"] = std::max<int64_t>(st.i64(3), 0);
    h["lastVisit"] = last;
    out.push_back(std::move(h));
  }
  return out;
}

}  // namespace liora::migration

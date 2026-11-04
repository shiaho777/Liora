#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>

#include "liora/json.hpp"
#include "liora/migration.hpp"
#include "liora/util.hpp"

struct sqlite3;

namespace liora::migration {

// Why one part of a migration failed. code uses MigrationResult::error values.
struct Failure {
  std::string code;
  std::string message;
};

// A private copy of a database another application may hold open or lock.
// The -wal / -journal files are copied too and the copy is opened read-write,
// so SQLite applies pending changes to the copy, never to the original.
// The copy is deleted on destruction.
class Database {
 public:
  ~Database();
  Database(const Database&) = delete;
  Database& operator=(const Database&) = delete;

  static std::unique_ptr<Database> open(const fs::path& file, Failure& failure);

  sqlite3* get() const { return db_; }
  bool hasTable(std::string_view name) const;

 private:
  Database() = default;

  sqlite3* db_ = nullptr;
  fs::path dir_;
};

enum class TimeBase {
  kChromium,      // microseconds since 1601-01-01
  kUnixMicros,    // microseconds since 1970-01-01 (Firefox)
  kAppleSeconds,  // seconds since 2001-01-01 (Safari)
};

int64_t chromiumTimeToMs(int64_t micros);
int64_t appleTimeToMs(double seconds);

// True when the OS denied access, including macOS privacy protection.
bool isPermissionError(const std::error_code& ec);
// Reads a file (at most limit bytes) and classifies failures as
// not_found / permission / busy / io.
std::optional<std::string> readBytes(const fs::path& file, Failure& failure, size_t limit = SIZE_MAX);

std::string getString(const Json& j, const char* key);

// Only web and file URLs are imported; javascript:, place:, about: etc. are skipped.
bool importableUrl(std::string_view url);
Json folderNode(std::string_view title, int64_t created = 0);
Json urlNode(std::string_view title, std::string_view url, int64_t created = 0);
// {"bar": folder, "other": folder, "mobile": folder}, the Storage::importData layout.
Json emptyRoots();
int64_t countUrls(const Json& node);

// The query takes the row limit as parameter 1 and returns:
// url, title, visit count, typed count, last visit time.
std::optional<Json> readHistoryQuery(const Database& db, const std::string& sql, int64_t limit, TimeBase base,
                                     Failure& failure);

std::optional<Json> parseChromiumBookmarks(std::string_view text, Failure& failure);
std::optional<Json> readChromiumHistory(const Database& db, int64_t limit, Failure& failure);

std::optional<Json> readFirefoxBookmarks(const Database& db, Failure& failure);
std::optional<Json> readFirefoxHistory(const Database& db, int64_t limit, Failure& failure);

std::optional<Json> parseSafariBookmarks(std::string_view bytes, Failure& failure);
std::optional<Json> readSafariHistory(const Database& db, int64_t limit, Failure& failure);

}  // namespace liora::migration

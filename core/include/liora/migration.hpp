#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "liora/json.hpp"
#include "liora/util.hpp"

namespace liora {

class Storage;

// A browser profile found on this device.
struct MigrationSource {
  std::string id;       // stable, e.g. "chrome/Default"
  std::string browser;  // "chrome", "firefox", "safari", ...
  std::string name;     // display name of the browser
  std::string profile;  // display name of the profile
  std::string kind;     // "chromium" | "firefox" | "safari"
  fs::path path;        // profile directory
  bool bookmarks = false;
  bool history = false;
  // The data exists but the OS blocks access (e.g. macOS Full Disk Access).
  bool needsAccess = false;

  Json toJson() const;
};

struct MigrationOptions {
  bool bookmarks = true;
  bool history = true;
  // Most recent entries to import. <= 0 means no limit.
  int64_t historyLimit = 100000;

  static MigrationOptions fromJson(const Json& j);
};

struct MigrationResult {
  bool ok = false;
  // "", "not_found", "permission", "busy", "unsupported", "corrupt", "io",
  // "nothing_selected", "storage"
  std::string error;
  std::string message;
  int64_t bookmarks = 0;
  int64_t history = 0;
  // Items that failed while others succeeded: [{item, error, message}].
  Json warnings = Json::array();

  Json toJson() const;
};

// Base directories browsers store profiles in. Injectable for tests.
struct BrowserRoots {
  fs::path home;
  fs::path local;
  fs::path roaming;

  static BrowserRoots system();
};

class Migrator {
 public:
  static std::vector<MigrationSource> detect(const BrowserRoots& roots = BrowserRoots::system());
  static std::optional<MigrationSource> find(std::string_view id,
                                             const BrowserRoots& roots = BrowserRoots::system());

  // Reads a profile into the Storage::importData format. Files are copied
  // first, so the source browser may keep running.
  static std::optional<Json> read(const MigrationSource& source, const MigrationOptions& options,
                                  MigrationResult& result);
  // Detects the format of a single file: bookmarks HTML, Chromium "Bookmarks"
  // JSON, Safari Bookmarks.plist, or a Chromium / Firefox / Safari database.
  static std::optional<Json> readFile(const fs::path& file, const MigrationOptions& options,
                                      MigrationResult& result);

  // Merges into storage. Safe to repeat: existing entries are not duplicated.
  static MigrationResult migrate(Storage& storage, const MigrationSource& source,
                                 const MigrationOptions& options);
  static MigrationResult migrateFile(Storage& storage, const fs::path& file, const MigrationOptions& options);

  // Netscape bookmarks HTML, the format every browser can import and export.
  // parse returns {"bar": {...}, "other": {...}, "mobile": {...}}.
  static std::optional<Json> parseBookmarksHtml(std::string_view html);
  // tree: Storage::bookmarkTree()
  static std::string bookmarksToHtml(const Json& tree);
};

}  // namespace liora

#pragma once

#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "liora/json.hpp"
#include "liora/util.hpp"

struct sqlite3;

namespace liora {

struct Bookmark {
  int64_t id = 0;
  int64_t parent = 0;
  bool folder = false;
  std::string title;
  std::string url;
  int64_t position = 0;
  int64_t created = 0;
  int64_t modified = 0;

  Json toJson() const;
};

struct HistoryEntry {
  int64_t id = 0;
  std::string url;
  std::string title;
  int64_t visitCount = 0;
  int64_t typedCount = 0;
  int64_t lastVisit = 0;

  Json toJson() const;
};

struct Permission {
  std::string origin;
  std::string name;
  std::string value;
  int64_t modified = 0;

  Json toJson() const;
};

// Kernel-independent user data: bookmarks, history and site permissions.
// All methods are thread safe.
class Storage {
 public:
  static constexpr int64_t kBookmarkBar = 1;
  static constexpr int64_t kOtherBookmarks = 2;
  static constexpr int64_t kMobileBookmarks = 3;

  explicit Storage(fs::path file);
  ~Storage();
  Storage(const Storage&) = delete;
  Storage& operator=(const Storage&) = delete;

  bool open(std::string* error = nullptr);
  void close();
  bool isOpen() const;

  // Runs fn in a (nestable) transaction. Rolls back when fn returns false.
  bool transaction(const std::function<bool()>& fn);

  // ---- Bookmarks ----
  std::optional<int64_t> addBookmark(int64_t parent, std::string_view title, std::string_view url,
                                     std::optional<int64_t> position = {}, int64_t created = 0);
  std::optional<int64_t> addFolder(int64_t parent, std::string_view title,
                                   std::optional<int64_t> position = {}, int64_t created = 0);
  bool updateBookmark(int64_t id, std::optional<std::string> title, std::optional<std::string> url);
  bool moveBookmark(int64_t id, int64_t parent, std::optional<int64_t> position = {});
  bool removeBookmark(int64_t id);
  std::optional<Bookmark> bookmark(int64_t id) const;
  std::vector<Bookmark> children(int64_t parent) const;
  std::vector<Bookmark> searchBookmarks(std::string_view query, int limit = 50) const;
  std::vector<Bookmark> bookmarksForUrl(std::string_view url) const;
  // {"bar": node, "other": node, "mobile": node}
  Json bookmarkTree() const;
  static bool isRootFolder(int64_t id);

  // ---- History ----
  void addVisit(std::string_view url, std::string_view title, bool typed = false, int64_t time = 0);
  void setTitle(std::string_view url, std::string_view title);
  // Adds counts from an imported entry (backup restore, migration).
  void mergeHistory(const HistoryEntry& entry);
  std::vector<HistoryEntry> searchHistory(std::string_view query, int limit = 50, int64_t before = 0) const;
  std::vector<HistoryEntry> topSites(int limit = 8) const;
  // Address bar completion: host/path prefix or title substring.
  std::vector<HistoryEntry> suggest(std::string_view input, int limit = 6) const;
  bool removeHistory(std::string_view url);
  // Removes visits in [from, to). to == 0 means "until now".
  void clearHistory(int64_t from = 0, int64_t to = 0);

  // ---- Site permissions ----
  // An empty value removes the entry.
  void setPermission(std::string_view origin, std::string_view name, std::string_view value);
  std::optional<std::string> permission(std::string_view origin, std::string_view name) const;
  std::vector<Permission> permissions(std::string_view origin = {}) const;

  // ---- Backup ----
  Json exportData() const;
  std::optional<std::string> importData(const Json& data, bool merge);

 private:
  std::optional<int64_t> addNode(int64_t parent, bool folder, std::string_view title, std::string_view url,
                                 std::optional<int64_t> position, int64_t created);
  int64_t childCount(int64_t parent, int64_t exclude = 0) const;
  Json nodeJson(const Bookmark& b) const;
  bool importChildren(int64_t parent, const Json& children, bool merge);
  bool exec(const char* sql, std::string* error = nullptr);

  fs::path file_;
  sqlite3* db_ = nullptr;
  mutable std::recursive_mutex mutex_;
  int txDepth_ = 0;
};

}  // namespace liora

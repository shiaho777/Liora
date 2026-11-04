#include <sqlite3.h>

#include <algorithm>
#include <unordered_map>
#include <vector>

#include "migration/internal.hpp"
#include "sqlite_stmt.hpp"

namespace liora::migration {

namespace {

constexpr int64_t kTypeBookmark = 1;
constexpr int64_t kTypeFolder = 2;

struct Item {
  int64_t id = 0;
  int64_t type = 0;
  std::string title;
  std::string url;
  int64_t created = 0;
};

using ChildMap = std::unordered_map<int64_t, std::vector<Item>>;

void buildChildren(const ChildMap& map, int64_t parent, Json& dst, int depth) {
  // Depth also stops parent cycles in a damaged database.
  if (depth > 64) return;
  auto it = map.find(parent);
  if (it == map.end()) return;
  for (const auto& item : it->second) {
    if (item.type == kTypeFolder) {
      Json folder = folderNode(item.title, item.created);
      buildChildren(map, item.id, folder["children"], depth + 1);
      dst.push_back(std::move(folder));
    } else if (item.type == kTypeBookmark && importableUrl(item.url)) {
      dst.push_back(urlNode(item.title, item.url, item.created));
    }
  }
}

}  // namespace

std::optional<Json> readFirefoxBookmarks(const Database& db, Failure& failure) {
  if (!db.hasTable("moz_bookmarks") || !db.hasTable("moz_places")) {
    failure = {"corrupt", "the places database has no bookmark tables"};
    return std::nullopt;
  }
  detail::Stmt st(db.get(),
                  "SELECT b.id, b.type, b.parent, b.title, b.dateAdded, b.guid, p.url "
                  "FROM moz_bookmarks b LEFT JOIN moz_places p ON p.id = b.fk "
                  "ORDER BY b.parent, b.position");
  if (!st) {
    failure = {"corrupt", std::string("unexpected places database: ") + sqlite3_errmsg(db.get())};
    return std::nullopt;
  }

  ChildMap children;
  std::unordered_map<std::string, int64_t> rootIds;
  while (st.step()) {
    Item item;
    item.id = st.i64(0);
    item.type = st.i64(1);
    item.title = st.text(3);
    item.created = std::max<int64_t>(st.i64(4) / 1000, 0);
    item.url = st.text(6);
    std::string guid = st.text(5);
    if (guid == "toolbar_____" || guid == "menu________" || guid == "unfiled_____" || guid == "mobile______") {
      rootIds[guid] = item.id;
    }
    int64_t parent = st.i64(2);
    if (parent != item.id) children[parent].push_back(std::move(item));
  }

  Json out = emptyRoots();
  static constexpr std::pair<const char*, const char*> kRoots[] = {
      {"toolbar_____", "bar"}, {"menu________", "other"}, {"unfiled_____", "other"}, {"mobile______", "mobile"}};
  for (const auto& [guid, to] : kRoots) {
    auto it = rootIds.find(guid);
    if (it != rootIds.end()) buildChildren(children, it->second, out[to]["children"], 0);
  }
  return out;
}

std::optional<Json> readFirefoxHistory(const Database& db, int64_t limit, Failure& failure) {
  if (!db.hasTable("moz_places")) {
    failure = {"corrupt", "the places database has no moz_places table"};
    return std::nullopt;
  }
  return readHistoryQuery(db,
                          "SELECT url, title, visit_count, typed, last_visit_date FROM moz_places "
                          "WHERE hidden = 0 AND visit_count > 0 AND last_visit_date IS NOT NULL "
                          "ORDER BY last_visit_date DESC LIMIT ?1",
                          limit, TimeBase::kUnixMicros, failure);
}

}  // namespace liora::migration

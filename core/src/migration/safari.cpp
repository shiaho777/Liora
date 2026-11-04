#include "liora/plist.hpp"
#include "migration/internal.hpp"

namespace liora::migration {

namespace {

constexpr const char* kTypeList = "WebBookmarkTypeList";
constexpr const char* kTypeLeaf = "WebBookmarkTypeLeaf";

std::string leafTitle(const Json& node) {
  auto dict = node.find("URIDictionary");
  if (dict != node.end() && dict->is_object()) {
    std::string title = getString(*dict, "title");
    if (!title.empty()) return title;
  }
  return getString(node, "Title");
}

void convertChildren(const Json& src, Json& dst, int depth) {
  if (depth > 64 || !src.is_array()) return;
  for (const auto& node : src) {
    if (!node.is_object()) continue;
    std::string type = getString(node, "WebBookmarkType");
    if (type == kTypeList) {
      Json folder = folderNode(getString(node, "Title"));
      if (auto kids = node.find("Children"); kids != node.end()) convertChildren(*kids, folder["children"], depth + 1);
      dst.push_back(std::move(folder));
    } else if (type == kTypeLeaf) {
      std::string url = getString(node, "URLString");
      if (importableUrl(url)) dst.push_back(urlNode(leafTitle(node), url));
    }
    // WebBookmarkTypeProxy entries (History, Reading List shortcuts) carry no bookmarks.
  }
}

}  // namespace

std::optional<Json> parseSafariBookmarks(std::string_view bytes, Failure& failure) {
  if (!startsWith(bytes, "bplist00")) {
    failure = {"unsupported", "only binary Safari bookmark files are supported"};
    return std::nullopt;
  }
  std::string error;
  auto root = plist::parseBinary(bytes, &error);
  if (!root || !root->is_object()) {
    failure = {"corrupt", "cannot parse Safari bookmarks: " + (error.empty() ? std::string("not a dictionary") : error)};
    return std::nullopt;
  }
  auto top = root->find("Children");
  if (top == root->end() || !top->is_array()) {
    failure = {"corrupt", "Safari bookmarks have no top-level children"};
    return std::nullopt;
  }

  Json out = emptyRoots();
  for (const auto& node : *top) {
    if (!node.is_object()) continue;
    std::string type = getString(node, "WebBookmarkType");
    std::string title = getString(node, "Title");
    auto kids = node.find("Children");
    if (type == kTypeList && title == "BookmarksBar") {
      if (kids != node.end()) convertChildren(*kids, out["bar"]["children"], 0);
    } else if (type == kTypeList && title == "BookmarksMenu") {
      if (kids != node.end()) convertChildren(*kids, out["other"]["children"], 0);
    } else if (type == kTypeList && title == "com.apple.ReadingList") {
      Json folder = folderNode("Reading List");
      if (kids != node.end()) convertChildren(*kids, folder["children"], 1);
      if (!folder["children"].empty()) out["other"]["children"].push_back(std::move(folder));
    } else {
      Json single = Json::array({node});
      convertChildren(single, out["other"]["children"], 0);
    }
  }
  return out;
}

std::optional<Json> readSafariHistory(const Database& db, int64_t limit, Failure& failure) {
  if (!db.hasTable("history_items") || !db.hasTable("history_visits")) {
    failure = {"corrupt", "the Safari history database has no history tables"};
    return std::nullopt;
  }
  return readHistoryQuery(db,
                          "SELECT i.url, "
                          "COALESCE((SELECT t.title FROM history_visits t WHERE t.history_item = i.id "
                          "AND t.title IS NOT NULL AND t.title != '' ORDER BY t.visit_time DESC LIMIT 1), ''), "
                          "i.visit_count, 0, MAX(v.visit_time) AS last "
                          "FROM history_items i JOIN history_visits v ON v.history_item = i.id "
                          "GROUP BY i.id ORDER BY last DESC LIMIT ?1",
                          limit, TimeBase::kAppleSeconds, failure);
}

}  // namespace liora::migration

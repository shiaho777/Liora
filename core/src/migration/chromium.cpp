#include <cstdlib>

#include "migration/internal.hpp"

namespace liora::migration {

namespace {

// Chromium stores times as decimal strings of microseconds since 1601.
int64_t chromiumTimeField(const Json& node, const char* key) {
  auto it = node.find(key);
  if (it == node.end()) return 0;
  if (it->is_number_integer()) return chromiumTimeToMs(it->get<int64_t>());
  if (!it->is_string()) return 0;
  const std::string& s = it->get_ref<const std::string&>();
  char* end = nullptr;
  long long v = std::strtoll(s.c_str(), &end, 10);
  if (end == s.c_str()) return 0;
  return chromiumTimeToMs(static_cast<int64_t>(v));
}

void convertChildren(const Json& src, Json& dst, int depth) {
  // Guards against malformed files with absurd nesting.
  if (depth > 64 || !src.is_array()) return;
  for (const auto& node : src) {
    if (!node.is_object()) continue;
    std::string type = getString(node, "type");
    std::string title = getString(node, "name");
    int64_t created = chromiumTimeField(node, "date_added");
    if (type == "folder") {
      Json folder = folderNode(title, created);
      if (auto kids = node.find("children"); kids != node.end()) convertChildren(*kids, folder["children"], depth + 1);
      dst.push_back(std::move(folder));
    } else if (type == "url") {
      std::string url = getString(node, "url");
      if (importableUrl(url)) dst.push_back(urlNode(title, url, created));
    }
  }
}

}  // namespace

std::optional<Json> parseChromiumBookmarks(std::string_view text, Failure& failure) {
  Json doc = Json::parse(text, nullptr, false);
  if (doc.is_discarded() || !doc.is_object()) {
    failure = {"corrupt", "the bookmarks file is not valid JSON"};
    return std::nullopt;
  }
  auto roots = doc.find("roots");
  if (roots == doc.end() || !roots->is_object()) {
    failure = {"corrupt", "the bookmarks file has no roots"};
    return std::nullopt;
  }

  Json out = emptyRoots();
  static constexpr std::pair<const char*, const char*> kRoots[] = {
      {"bookmark_bar", "bar"}, {"other", "other"}, {"synced", "mobile"}};
  for (const auto& [from, to] : kRoots) {
    auto node = roots->find(from);
    if (node == roots->end() || !node->is_object()) continue;
    if (auto kids = node->find("children"); kids != node->end()) convertChildren(*kids, out[to]["children"], 0);
  }
  return out;
}

std::optional<Json> readChromiumHistory(const Database& db, int64_t limit, Failure& failure) {
  if (!db.hasTable("urls")) {
    failure = {"corrupt", "the history database has no urls table"};
    return std::nullopt;
  }
  return readHistoryQuery(db,
                          "SELECT url, title, visit_count, typed_count, last_visit_time FROM urls "
                          "WHERE hidden = 0 AND last_visit_time > 0 "
                          "ORDER BY last_visit_time DESC LIMIT ?1",
                          limit, TimeBase::kChromium, failure);
}

}  // namespace liora::migration

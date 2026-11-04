#include <algorithm>
#include <cstdlib>
#include <vector>

#include "migration/internal.hpp"

namespace liora {

using namespace migration;

namespace {

bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f'; }
bool isAlnum(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
}

void appendUtf8(std::string& out, uint32_t cp) {
  if (cp == 0 || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) cp = 0xFFFD;
  if (cp < 0x80) {
    out += static_cast<char>(cp);
  } else if (cp < 0x800) {
    out += static_cast<char>(0xC0 | (cp >> 6));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  } else if (cp < 0x10000) {
    out += static_cast<char>(0xE0 | (cp >> 12));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  } else {
    out += static_cast<char>(0xF0 | (cp >> 18));
    out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  }
}

std::string decodeEntities(std::string_view s) {
  std::string out;
  out.reserve(s.size());
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] != '&') {
      out += s[i];
      continue;
    }
    size_t semi = s.find(';', i + 1);
    if (semi == std::string_view::npos || semi - i > 10) {
      out += '&';
      continue;
    }
    std::string_view name = s.substr(i + 1, semi - i - 1);
    if (name.size() > 1 && name[0] == '#') {
      bool hex = name[1] == 'x' || name[1] == 'X';
      std::string digits(name.substr(hex ? 2 : 1));
      bool valid = !digits.empty() && std::all_of(digits.begin(), digits.end(), [&](char c) {
        return (c >= '0' && c <= '9') || (hex && ((c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')));
      });
      if (!valid) {
        out += '&';
        continue;
      }
      unsigned long cp = std::strtoul(digits.c_str(), nullptr, hex ? 16 : 10);
      appendUtf8(out, static_cast<uint32_t>(std::min<unsigned long>(cp, 0x110000)));
    } else if (name == "amp") {
      out += '&';
    } else if (name == "lt") {
      out += '<';
    } else if (name == "gt") {
      out += '>';
    } else if (name == "quot") {
      out += '"';
    } else if (name == "apos") {
      out += '\'';
    } else if (name == "nbsp") {
      out += "\xC2\xA0";
    } else {
      out += '&';
      continue;
    }
    i = semi;
  }
  return out;
}

std::string escapeHtml(std::string_view s) {
  std::string out;
  out.reserve(s.size());
  for (char c : s) {
    switch (c) {
      case '&': out += "&amp;"; break;
      case '<': out += "&lt;"; break;
      case '>': out += "&gt;"; break;
      case '"': out += "&quot;"; break;
      default: out += c;
    }
  }
  return out;
}

struct Tag {
  std::string name;  // lower case; closing tags start with '/'
  std::vector<std::pair<std::string, std::string>> attrs;

  std::string attr(std::string_view key) const {
    for (const auto& [k, v] : attrs) {
      if (k == key) return v;
    }
    return {};
  }
};

// html[pos] is '<'. Returns the position after the closing '>'.
size_t parseTag(std::string_view html, size_t pos, Tag& tag) {
  size_t n = html.size();
  size_t i = pos + 1;
  bool closing = i < n && html[i] == '/';
  if (closing) ++i;
  size_t start = i;
  while (i < n && isAlnum(html[i])) ++i;
  tag.name = (closing ? "/" : "") + toLower(html.substr(start, i - start));
  tag.attrs.clear();

  while (i < n && html[i] != '>') {
    if (isSpace(html[i]) || html[i] == '/') {
      ++i;
      continue;
    }
    size_t keyStart = i;
    while (i < n && !isSpace(html[i]) && html[i] != '=' && html[i] != '>') ++i;
    std::string key = toLower(html.substr(keyStart, i - keyStart));
    while (i < n && isSpace(html[i])) ++i;
    std::string value;
    if (i < n && html[i] == '=') {
      ++i;
      while (i < n && isSpace(html[i])) ++i;
      if (i < n && (html[i] == '"' || html[i] == '\'')) {
        char quote = html[i++];
        size_t end = html.find(quote, i);
        if (end == std::string_view::npos) end = n;
        value = decodeEntities(html.substr(i, end - i));
        i = end < n ? end + 1 : n;
      } else {
        size_t valueStart = i;
        while (i < n && !isSpace(html[i]) && html[i] != '>') ++i;
        value = decodeEntities(html.substr(valueStart, i - valueStart));
      }
    }
    if (!key.empty()) tag.attrs.emplace_back(std::move(key), std::move(value));
  }
  return i < n ? i + 1 : n;
}

// ADD_DATE is Unix seconds; some exporters write milliseconds or microseconds.
int64_t parseDate(const std::string& value) {
  if (value.empty()) return 0;
  long long v = std::strtoll(value.c_str(), nullptr, 10);
  if (v <= 0) return 0;
  if (v > 100000000000000LL) return v / 1000;
  if (v > 100000000000LL) return v;
  return v * 1000;
}

struct OpenFolder {
  Json folder;
  // "root" top-level list, "bar" toolbar folder, "unfiled" Firefox "Other Bookmarks",
  // "inline" a list without a heading, "" an ordinary folder.
  std::string role;
};

int64_t intField(const Json& j, const char* key) {
  auto it = j.find(key);
  return it != j.end() && it->is_number_integer() ? it->get<int64_t>() : 0;
}

std::string dateAttrs(const Json& node) {
  std::string out;
  if (int64_t created = intField(node, "created"); created > 0) out += " ADD_DATE=\"" + std::to_string(created / 1000) + "\"";
  if (int64_t modified = intField(node, "modified"); modified > 0) {
    out += " LAST_MODIFIED=\"" + std::to_string(modified / 1000) + "\"";
  }
  return out;
}

void writeList(std::string& out, const Json& children, int depth);

void writeNode(std::string& out, const Json& node, int depth) {
  if (!node.is_object() || depth > 64) return;
  std::string indent(static_cast<size_t>(depth) * 4, ' ');
  std::string type = getString(node, "type");
  std::string title = getString(node, "title");
  if (type == "folder") {
    out += indent + "<DT><H3" + dateAttrs(node) + ">" + escapeHtml(title) + "</H3>\n";
    auto kids = node.find("children");
    writeList(out, kids != node.end() ? *kids : Json::array(), depth);
  } else if (type == "url") {
    std::string url = getString(node, "url");
    out += indent + "<DT><A HREF=\"" + escapeHtml(url) + "\"" + dateAttrs(node) + ">" +
           escapeHtml(title.empty() ? url : title) + "</A>\n";
  }
}

void writeList(std::string& out, const Json& children, int depth) {
  std::string indent(static_cast<size_t>(depth) * 4, ' ');
  out += indent + "<DL><p>\n";
  if (children.is_array()) {
    for (const auto& c : children) writeNode(out, c, depth + 1);
  }
  out += indent + "</DL><p>\n";
}

const Json& childrenOf(const Json& tree, const char* root) {
  static const Json kEmpty = Json::array();
  if (!tree.is_object()) return kEmpty;
  auto node = tree.find(root);
  if (node == tree.end() || !node->is_object()) return kEmpty;
  auto kids = node->find("children");
  return kids != node->end() && kids->is_array() ? *kids : kEmpty;
}

}  // namespace

std::optional<Json> Migrator::parseBookmarksHtml(std::string_view html) {
  const std::string lower = toLower(html);
  const size_t n = html.size();
  Json out = emptyRoots();
  std::vector<OpenFolder> stack;
  std::optional<OpenFolder> pending;
  bool sawList = false;

  // A heading not followed by a list is an empty folder.
  auto flushPending = [&] {
    if (pending && !stack.empty()) stack.back().folder["children"].push_back(std::move(pending->folder));
    pending.reset();
  };
  auto closeTop = [&] {
    OpenFolder open = std::move(stack.back());
    stack.pop_back();
    Json& kids = open.folder["children"];
    if (open.role == "root" || open.role == "bar" || open.role == "unfiled" ||
        (open.role == "inline" && stack.empty())) {
      Json& dst = out[open.role == "bar" ? "bar" : "other"]["children"];
      for (auto& k : kids) dst.push_back(std::move(k));
    } else if (open.role == "inline") {
      Json& dst = stack.back().folder["children"];
      for (auto& k : kids) dst.push_back(std::move(k));
    } else if (!stack.empty()) {
      stack.back().folder["children"].push_back(std::move(open.folder));
    } else {
      out["other"]["children"].push_back(std::move(open.folder));
    }
  };

  size_t i = 0;
  while ((i = lower.find('<', i)) != std::string::npos) {
    if (lower.compare(i, 4, "<!--") == 0) {
      size_t end = lower.find("-->", i + 4);
      i = end == std::string::npos ? n : end + 3;
      continue;
    }
    Tag tag;
    size_t next = parseTag(html, i, tag);

    if (tag.name == "dl") {
      sawList = true;
      if (pending) {
        stack.push_back(std::move(*pending));
        pending.reset();
      } else {
        stack.push_back({folderNode(""), stack.empty() ? "root" : "inline"});
      }
    } else if (tag.name == "/dl") {
      flushPending();
      if (!stack.empty()) closeTop();
    } else if (tag.name == "h3") {
      flushPending();
      size_t end = lower.find("</h3", next);
      if (end == std::string::npos) end = n;
      std::string title = trim(decodeEntities(html.substr(next, end - next)));
      OpenFolder folder{folderNode(title, parseDate(tag.attr("add_date"))), ""};
      if (stack.size() == 1) {
        if (toLower(tag.attr("personal_toolbar_folder")) == "true") folder.role = "bar";
        else if (toLower(tag.attr("unfiled_bookmarks_folder")) == "true") folder.role = "unfiled";
      }
      pending = std::move(folder);
      next = end;
    } else if (tag.name == "a") {
      flushPending();
      size_t end = lower.find("</a", next);
      if (end == std::string::npos) end = n;
      std::string url = trim(tag.attr("href"));
      if (!stack.empty() && importableUrl(url)) {
        std::string title = trim(decodeEntities(html.substr(next, end - next)));
        stack.back().folder["children"].push_back(urlNode(title, url, parseDate(tag.attr("add_date"))));
      }
      next = end;
    }
    i = next;
  }
  flushPending();
  while (!stack.empty()) closeTop();

  if (!sawList) return std::nullopt;
  return out;
}

std::string Migrator::bookmarksToHtml(const Json& tree) {
  std::string out =
      "<!DOCTYPE NETSCAPE-Bookmark-file-1>\n"
      "<!-- This is an automatically generated file.\n"
      "     It will be read and overwritten.\n"
      "     DO NOT EDIT! -->\n"
      "<META HTTP-EQUIV=\"Content-Type\" CONTENT=\"text/html; charset=UTF-8\">\n"
      "<TITLE>Bookmarks</TITLE>\n"
      "<H1>Bookmarks</H1>\n"
      "<DL><p>\n";

  Json bar = tree.is_object() && tree.contains("bar") ? tree["bar"] : Json::object();
  std::string barTitle = getString(bar, "title");
  out += "    <DT><H3" + dateAttrs(bar) + " PERSONAL_TOOLBAR_FOLDER=\"true\">" +
         escapeHtml(barTitle.empty() ? "Bookmarks bar" : barTitle) + "</H3>\n";
  writeList(out, childrenOf(tree, "bar"), 1);

  for (const auto& node : childrenOf(tree, "other")) writeNode(out, node, 1);

  const Json& mobile = childrenOf(tree, "mobile");
  if (!mobile.empty()) {
    out += "    <DT><H3>Mobile bookmarks</H3>\n";
    writeList(out, mobile, 1);
  }
  out += "</DL><p>\n";
  return out;
}

}  // namespace liora

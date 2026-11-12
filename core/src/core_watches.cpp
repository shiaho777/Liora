#include "liora/core.hpp"

#include <atomic>

namespace liora {

namespace {

Json* findWatch(Json& watches, const std::string& id) {
  for (auto& watch : watches) {
    if (watch.value("id", "") == id) return &watch;
  }
  return nullptr;
}

std::string watchId() {
  static std::atomic<uint64_t> sequence{1};
  return "watch-" + std::to_string(nowMs()) + "-" + std::to_string(sequence.fetch_add(1));
}

std::string sanitizeUtf8(std::string_view input) {
  std::string out;
  out.reserve(input.size());
  for (size_t i = 0; i < input.size();) {
    const unsigned char c = static_cast<unsigned char>(input[i]);
    size_t length = 0;
    if (c < 0x80) {
      out.push_back(static_cast<char>(c));
      i += 1;
      continue;
    }
    if (c >= 0xC2 && c <= 0xDF) length = 2;
    else if (c >= 0xE0 && c <= 0xEF) length = 3;
    else if (c >= 0xF0 && c <= 0xF4) length = 4;
    else {
      out.push_back('?');
      i += 1;
      continue;
    }
    if (i + length > input.size()) {
      out.push_back('?');
      i += 1;
      continue;
    }
    bool valid = true;
    for (size_t j = 1; j < length; ++j) {
      const unsigned char next = static_cast<unsigned char>(input[i + j]);
      if (next < 0x80 || next > 0xBF) {
        valid = false;
        break;
      }
    }
    if (valid && length == 3) {
      const unsigned char second = static_cast<unsigned char>(input[i + 1]);
      if (c == 0xE0 && second < 0xA0) valid = false;
      if (c == 0xED && second > 0x9F) valid = false;
    }
    if (valid && length == 4) {
      const unsigned char second = static_cast<unsigned char>(input[i + 1]);
      if (c == 0xF0 && second < 0x90) valid = false;
      if (c == 0xF4 && second > 0x8F) valid = false;
    }
    if (!valid) {
      out.push_back('?');
      i += 1;
      continue;
    }
    out.append(input.substr(i, length));
    i += length;
  }
  return out;
}

std::string cleanText(const std::string& value, size_t limit) {
  const std::string text = trim(value);
  const std::string safe = sanitizeUtf8(text);
  // Truncation can split a multi-byte sequence; sanitize once more so the
  // in-memory record is JSON-safe as well as the file written to disk.
  return sanitizeUtf8(safe.substr(0, limit));
}

}  // namespace

void Core::loadWatches() {
  std::lock_guard lock(watchesMutex_);
  auto text = readFile(paths_.root / "watches.json");
  if (!text) return;
  Json doc = Json::parse(*text, nullptr, false);
  if (doc.is_discarded() || !doc.is_object()) return;
  auto it = doc.find("watches");
  if (it == doc.end() || !it->is_array()) return;
  watches_ = *it;
  for (auto& watch : watches_) {
    for (const char* key : {"url", "title", "keyword", "lastSummary", "lastError"}) {
      if (!watch.contains(key) || !watch[key].is_string()) continue;
      watch[key] = sanitizeUtf8(watch[key].get<std::string>());
    }
  }
}

void Core::saveWatchesLocked() {
  Json doc = {{"version", 1}, {"watches", watches_}};
  std::string error;
  if (!writeFileAtomic(paths_.root / "watches.json",
                       doc.dump(2, ' ', false, Json::error_handler_t::replace), &error)) {
    rpc_.notify("core.warning", {{"source", "watches"}, {"message", error}});
  }
}

void Core::registerWatchMethods() {
  rpc_.setNamespaceDescription("watch", "Page change monitoring");

  rpc_.add("watch.list", [this](const Json&) {
    std::lock_guard lock(watchesMutex_);
    return watches_;
  });

  rpc_.add("watch.add", [this](const Json& params) {
    std::string url = trim(params::string(params, "url"));
    if (url.size() > 4096 || !(url.rfind("https://", 0) == 0 || url.rfind("http://", 0) == 0)) {
      rpcFail(kRpcInvalidParams, "the watch URL must start with http:// or https://");
    }
    std::string title = cleanText(params::optionalString(params, "title").value_or(url), 240);
    if (title.empty()) title = url;
    std::string keyword = cleanText(params::optionalString(params, "keyword").value_or(""), 200);
    int interval = static_cast<int>(params::optionalInteger(params, "intervalMinutes").value_or(60));
    if (interval < 5 || interval > 10080) {
      rpcFail(kRpcInvalidParams, "the interval must be between 5 minutes and 7 days");
    }

    std::lock_guard lock(watchesMutex_);
    for (const auto& watch : watches_) {
      if (watch.value("url", "") == url && watch.value("keyword", "") == keyword) return watch;
    }

    Json record = {
        {"id", watchId()},
        {"url", url},
        {"title", title},
        {"keyword", keyword},
        {"intervalMinutes", interval},
        {"createdAt", nowMs()},
        {"lastCheckedAt", 0},
        {"lastChangedAt", 0},
        {"lastSeenAt", 0},
        {"lastHash", ""},
        {"status", "watching"},
        {"lastError", ""},
        {"lastSummary", ""},
    };
    watches_.push_back(record);
    saveWatchesLocked();
    return record;
  });

  rpc_.add("watch.remove", [this](const Json& params) {
    std::string id = params::string(params, "id");
    std::lock_guard lock(watchesMutex_);
    if (!findWatch(watches_, id)) rpcFail(kRpcNotFound, "unknown watch: " + id);
    Json next = Json::array();
    for (const auto& watch : watches_) {
      if (watch.value("id", "") != id) next.push_back(watch);
    }
    watches_ = std::move(next);
    saveWatchesLocked();
    return Json{{"ok", true}};
  });

  rpc_.add("watch.touch", [this](const Json& params) {
    std::string id = params::string(params, "id");
    std::lock_guard lock(watchesMutex_);
    Json* watch = findWatch(watches_, id);
    if (!watch) rpcFail(kRpcNotFound, "unknown watch: " + id);

    const int64_t checkedAt = params::optionalInteger(params, "checkedAt").value_or(nowMs());
    const std::string hash = params::optionalString(params, "hash").value_or("");
    const std::string error = cleanText(params::optionalString(params, "error").value_or(""), 500);
    const std::string summary = cleanText(params::optionalString(params, "summary").value_or(""), 500);
    const bool changed = params.value("changed", false);

    (*watch)["lastCheckedAt"] = checkedAt;
    (*watch)["lastSummary"] = summary;
    if (!hash.empty()) (*watch)["lastHash"] = hash;
    if (!error.empty()) {
      (*watch)["status"] = "error";
      (*watch)["lastError"] = error;
    } else if (changed) {
      (*watch)["status"] = "changed";
      (*watch)["lastChangedAt"] = checkedAt;
      (*watch)["lastError"] = "";
    } else {
      (*watch)["lastError"] = "";
      if ((*watch).value("status", "") == "error") (*watch)["status"] = "watching";
    }
    saveWatchesLocked();
    return *watch;
  });

  rpc_.add("watch.seen", [this](const Json& params) {
    std::string id = params::string(params, "id");
    std::lock_guard lock(watchesMutex_);
    Json* watch = findWatch(watches_, id);
    if (!watch) rpcFail(kRpcNotFound, "unknown watch: " + id);
    (*watch)["status"] = "watching";
    (*watch)["lastSeenAt"] = nowMs();
    saveWatchesLocked();
    return *watch;
  });
}

}  // namespace liora

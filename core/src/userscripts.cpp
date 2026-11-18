#include "liora/userscripts.hpp"

#include <algorithm>
#include <cctype>

#include "liora/settings.hpp"
#include "liora/util.hpp"

namespace liora {

namespace {

bool validHost(std::string_view host) {
  if (host.empty() || host.size() > 253) return false;
  auto labels = split(host, '.', false);
  if (labels.size() < 2) return false;
  for (const auto& label : labels) {
    if (label.empty() || label.size() > 63) return false;
    if (label.front() == '-' || label.back() == '-') return false;
    for (char c : label) {
      auto u = static_cast<unsigned char>(c);
      if (!std::isalnum(u) && c != '-' && c != '_') return false;
    }
  }
  const auto& tld = labels.back();
  bool numeric = std::all_of(tld.begin(), tld.end(),
                             [](char c) { return std::isdigit(static_cast<unsigned char>(c)); });
  return !numeric;
}

// Code is stored verbatim but bounded so a runaway paste cannot bloat the
// settings document or the injected payload.
constexpr size_t kMaxCodeBytes = 256 * 1024;
constexpr size_t kMaxNameLen = 120;
constexpr size_t kMaxPatterns = 50;

}  // namespace

std::string UserScript::runAtName(ScriptRunAt runAt) {
  switch (runAt) {
    case ScriptRunAt::kStart: return "start";
    case ScriptRunAt::kEnd: return "end";
    case ScriptRunAt::kIdle: return "idle";
  }
  return "end";
}

std::optional<ScriptRunAt> UserScript::runAtFromName(std::string_view name) {
  if (name == "start") return ScriptRunAt::kStart;
  if (name == "end") return ScriptRunAt::kEnd;
  if (name == "idle") return ScriptRunAt::kIdle;
  return std::nullopt;
}

Json UserScript::toJson() const {
  Json patterns = Json::array();
  for (const auto& m : matches) patterns.push_back(m);
  return {{"id", id},
          {"name", name},
          {"enabled", enabled},
          {"matches", patterns},
          {"runAt", runAtName(runAt)},
          {"code", code},
          {"created", created},
          {"modified", modified}};
}

std::optional<UserScript> UserScript::fromJson(const Json& j, std::string* error) {
  auto fail = [&](std::string msg) -> std::optional<UserScript> {
    if (error) *error = std::move(msg);
    return std::nullopt;
  };
  if (!j.is_object()) return fail("a script must be an object");

  UserScript s;
  s.id = j.value("id", "");
  s.name = trim(j.value("name", ""));
  if (s.name.empty()) return fail("a script needs a name");
  if (s.name.size() > kMaxNameLen) return fail("the name is too long");
  s.enabled = j.value("enabled", true);

  auto runAt = runAtFromName(j.value("runAt", "end"));
  if (!runAt) return fail("runAt must be start, end or idle");
  s.runAt = *runAt;

  s.code = j.value("code", "");
  if (s.code.empty()) return fail("a script needs code");
  if (s.code.size() > kMaxCodeBytes) return fail("the script is larger than 256 KB");

  auto matches = j.find("matches");
  if (matches == j.end() || !matches->is_array() || matches->empty()) {
    return fail("a script needs at least one match pattern");
  }
  for (const auto& m : *matches) {
    if (!m.is_string()) return fail("match patterns must be strings");
    auto pattern = UserScriptSet::normalizePattern(m.get<std::string>(), error);
    if (!pattern) return std::nullopt;
    s.matches.push_back(*pattern);
  }
  if (s.matches.size() > kMaxPatterns) return fail("too many match patterns");
  std::sort(s.matches.begin(), s.matches.end());
  s.matches.erase(std::unique(s.matches.begin(), s.matches.end()), s.matches.end());

  s.created = j.value("created", int64_t{0});
  s.modified = j.value("modified", int64_t{0});
  return s;
}

std::optional<std::string> UserScriptSet::normalizePattern(std::string_view input,
                                                           std::string* error) {
  auto fail = [&](std::string msg) -> std::optional<std::string> {
    if (error) *error = std::move(msg);
    return std::nullopt;
  };
  std::string s = trim(input);
  if (s == "*") return "*";
  std::string prefix;
  if (startsWith(s, "=")) {
    prefix = "=";
    s.erase(0, 1);
  } else if (startsWith(s, "*.")) {
    prefix = "*.";
    s.erase(0, 2);
  } else if (startsWith(s, ".")) {
    s.erase(0, 1);
  }
  if (s.find('*') != std::string::npos) return fail("wildcards are only allowed as a leading '*.' or '*'");
  std::string host = Settings::normalizeSite(s);
  if (!validHost(host)) return fail("'" + std::string(trim(input)) + "' is not a valid domain");
  return prefix + host;
}

bool UserScriptSet::patternMatches(std::string_view pattern, std::string_view rawHost) {
  if (pattern == "*") return true;
  std::string host = Settings::normalizeSite(rawHost);
  if (host.empty()) return false;
  if (startsWith(pattern, "=")) return pattern.substr(1) == host;
  if (startsWith(pattern, "*.")) {
    std::string_view parent = pattern.substr(2);
    return endsWith(host, parent) && host.size() > parent.size() &&
           host[host.size() - parent.size() - 1] == '.';
  }
  // Bare domain: the domain itself and all subdomains.
  return host == pattern || (endsWith(host, pattern) && host.size() > pattern.size() &&
                             host[host.size() - pattern.size() - 1] == '.');
}

std::optional<std::string> UserScriptSet::load(const Json& scripts) {
  if (!scripts.is_array()) return "scripts must be a list";
  std::vector<UserScript> next;
  for (const auto& item : scripts) {
    std::string error;
    auto script = UserScript::fromJson(item, &error);
    if (!script) return error;
    next.push_back(std::move(*script));
  }
  return replace(std::move(next));
}

Json UserScriptSet::toJson() const {
  Json out = Json::array();
  for (const auto& s : scripts_) out.push_back(s.toJson());
  return out;
}

const UserScript* UserScriptSet::find(std::string_view id) const {
  for (const auto& s : scripts_) {
    if (s.id == id) return &s;
  }
  return nullptr;
}

std::vector<const UserScript*> UserScriptSet::match(std::string_view host) const {
  std::vector<const UserScript*> out;
  for (const auto& s : scripts_) {
    if (!s.enabled) continue;
    for (const auto& pattern : s.matches) {
      if (patternMatches(pattern, host)) {
        out.push_back(&s);
        break;
      }
    }
  }
  return out;
}

std::optional<std::string> UserScriptSet::replace(std::vector<UserScript> scripts) {
  std::vector<std::string> ids;
  for (const auto& s : scripts) {
    if (s.id.empty()) return "a script needs an id";
    ids.push_back(s.id);
  }
  std::sort(ids.begin(), ids.end());
  if (std::adjacent_find(ids.begin(), ids.end()) != ids.end()) return "duplicate script id";
  scripts_ = std::move(scripts);
  return std::nullopt;
}

std::optional<std::string> UserScriptSet::add(UserScript script) {
  if (script.id.empty()) script.id = newId("script");
  if (find(script.id)) return "duplicate script id";
  if (script.created == 0) script.created = nowMs();
  script.modified = nowMs();
  auto next = scripts_;
  next.push_back(std::move(script));
  return replace(std::move(next));
}

std::optional<std::string> UserScriptSet::update(UserScript script) {
  auto* existing = const_cast<UserScript*>(find(script.id));
  if (!existing) return "unknown script: " + script.id;
  script.created = existing->created;
  script.modified = nowMs();
  auto next = scripts_;
  for (auto& s : next) {
    if (s.id == script.id) {
      s = script;
      break;
    }
  }
  return replace(std::move(next));
}

bool UserScriptSet::remove(std::string_view id) {
  auto next = scripts_;
  auto it = std::remove_if(next.begin(), next.end(),
                           [&](const UserScript& s) { return s.id == id; });
  if (it == next.end()) return false;
  next.erase(it, next.end());
  scripts_ = std::move(next);
  return true;
}

std::optional<std::string> UserScriptSet::setEnabled(std::string_view id, bool enabled) {
  auto* existing = const_cast<UserScript*>(find(id));
  if (!existing) return "unknown script: " + std::string(id);
  auto next = scripts_;
  for (auto& s : next) {
    if (s.id == id) {
      s.enabled = enabled;
      s.modified = nowMs();
      break;
    }
  }
  return replace(std::move(next));
}

}  // namespace liora
#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "liora/json.hpp"

namespace liora {

// When a user script runs relative to the page's own scripts.
enum class ScriptRunAt { kStart, kEnd, kIdle };

// A user-authored script injected into matching pages. This is Liora's own
// customization layer (the Via-style userscript model), not Chrome extensions.
//
// Domain patterns reuse the rule syntax:
//   x.com     the domain and all of its subdomains
//   *.x.com   subdomains only
//   =x.com    the domain only
//   *         every site
struct UserScript {
  std::string id;
  std::string name;
  bool enabled = true;
  std::vector<std::string> matches;
  ScriptRunAt runAt = ScriptRunAt::kEnd;
  std::string code;
  int64_t created = 0;
  int64_t modified = 0;

  Json toJson() const;
  static std::optional<UserScript> fromJson(const Json& j, std::string* error = nullptr);
  static std::string runAtName(ScriptRunAt runAt);
  static std::optional<ScriptRunAt> runAtFromName(std::string_view name);
};

// Not thread-safe; the owner serializes access.
class UserScriptSet {
 public:
  std::optional<std::string> load(const Json& scripts);
  Json toJson() const;

  const std::vector<UserScript>& scripts() const { return scripts_; }
  const UserScript* find(std::string_view id) const;

  // Enabled scripts whose patterns match the host, in insertion order.
  std::vector<const UserScript*> match(std::string_view host) const;

  std::optional<std::string> add(UserScript script);
  std::optional<std::string> update(UserScript script);
  bool remove(std::string_view id);
  std::optional<std::string> setEnabled(std::string_view id, bool enabled);

  // Normalizes one pattern; "*" means every site.
  static std::optional<std::string> normalizePattern(std::string_view input,
                                                     std::string* error = nullptr);
  static bool patternMatches(std::string_view pattern, std::string_view host);

 private:
  std::optional<std::string> replace(std::vector<UserScript> scripts);
  std::vector<UserScript> scripts_;
};

}  // namespace liora
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "liora/json.hpp"
#include "liora/util.hpp"

namespace liora {

enum ScopeLevel : uint8_t {
  kScopeGlobal = 1,
  kScopeSite = 2,
  kScopeTab = 4,
};

// Where a setting is read or written. The most specific non-empty field wins:
// tab, then site, then global.
struct Scope {
  std::string site;
  int64_t tab = 0;
};

struct SettingDef {
  std::string key;
  Json defaultValue;
  uint8_t scopes = kScopeGlobal;
  std::vector<std::string> choices;
  // Settings marked advanced are hidden in simple mode.
  bool advanced = false;
};

class Settings {
 public:
  using Listener = std::function<void(const std::string& key, const Scope& scope)>;

  explicit Settings(fs::path file);

  bool load(std::string* error = nullptr);
  bool save(std::string* error = nullptr);

  // Resolves tab override, then site override (walking up parent domains),
  // then the global value, then the schema default.
  Json get(std::string_view key, const Scope& scope = {}) const;

  template <class T>
  T value(std::string_view key, const Scope& scope = {}) const {
    return get(key, scope).get<T>();
  }

  // Returns an error message when the key is unknown, the value has the wrong
  // type, or the key cannot be overridden at the requested scope.
  std::optional<std::string> set(std::string_view key, const Json& value, const Scope& scope = {});
  void reset(std::string_view key, const Scope& scope = {});
  void clearTab(int64_t tab);
  void clearSite(std::string_view site);

  // Schema, current global values and the list of sites with overrides.
  Json describe() const;
  Json siteOverrides(std::string_view site) const;

  Json exportData() const;
  std::optional<std::string> importData(const Json& data, bool merge);

  int subscribe(Listener listener);
  void unsubscribe(int id);

  static const std::vector<SettingDef>& schema();
  static const SettingDef* find(std::string_view key);
  static std::string normalizeSite(std::string_view site);

 private:
  std::optional<std::string> validate(const SettingDef& def, const Json& value) const;
  void notify(const std::string& key, const Scope& scope);

  fs::path file_;
  mutable std::mutex mutex_;
  Json global_ = Json::object();
  std::map<std::string, Json> sites_;
  std::map<int64_t, Json> tabs_;
  std::map<int, Listener> listeners_;
  int nextListener_ = 1;
};

}  // namespace liora

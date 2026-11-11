#include "liora/settings.hpp"

#include <algorithm>

namespace liora {

namespace {

constexpr uint8_t kAll = kScopeGlobal | kScopeSite | kScopeTab;
constexpr uint8_t kGlobalSite = kScopeGlobal | kScopeSite;

std::vector<SettingDef> buildSchema() {
  std::vector<SettingDef> s;
  auto add = [&](std::string key, Json def, uint8_t scopes = kScopeGlobal,
                 std::vector<std::string> choices = {}, bool advanced = false) {
    s.push_back({std::move(key), std::move(def), scopes, std::move(choices), advanced});
  };

  add("ui.mode", "simple", kScopeGlobal, {"simple", "standard", "advanced"});
  add("ui.theme", "system", kScopeGlobal, {"system", "light", "dark"});
  add("ui.accent", "#3b6cf6");
  add("ui.density", "comfortable", kScopeGlobal, {"compact", "comfortable"});
  add("ui.toolbar", "top", kScopeGlobal, {"top", "bottom"});
  add("ui.tabs", "strip", kScopeGlobal, {"strip", "vertical", "hidden"});
  add("ui.language", "auto");
  add("ui.showHomeButton", false);

  add("search.engine", "duckduckgo");
  add("search.suggest", true);
  add("home.page", "liora://newtab");
  add("startup.restore", true);

  add("kernel.default", "system", kAll);
  add("kernel.cef.singleProcess", false, kScopeGlobal, {}, true);

  add("dns.mode", "auto", kScopeGlobal, {"auto", "off", "provider", "custom"});
  add("dns.provider", "cloudflare");
  add("dns.protocol", "doh", kScopeGlobal, {"doh", "dot", "doq"}, true);
  add("dns.custom", Json::array(), kScopeGlobal, {}, true);
  add("dns.customId", "", kScopeGlobal, {}, true);

  add("net.rulesEnabled", true);
  add("net.rules", Json::array(), kScopeGlobal, {}, true);
  add("net.echOuter", "cloudflare-ech.com", kScopeGlobal, {}, true);

  add("privacy.thirdPartyCookies", "block", kGlobalSite, {"allow", "block"});
  add("privacy.httpsUpgrade", true, kGlobalSite);
  add("privacy.doNotTrack", false, kScopeGlobal, {}, true);

  add("content.javascript", true, kAll, {}, true);
  add("content.images", true, kAll);
  add("content.autoplay", "block-audible", kGlobalSite, {"allow", "block-audible", "block"});
  add("content.zoom", 100, kGlobalSite);
  add("content.userAgent", "default", kAll, {"default", "desktop", "mobile"});
  add("content.adblock", true, kGlobalSite);

  add("downloads.dir", "");
  add("downloads.ask", false);

  add("extensions.enabled", true, kScopeGlobal, {}, true);
  add("components.autoUpdate", true, kScopeGlobal, {}, true);
  add("components.channel", "stable", kScopeGlobal, {"stable", "beta"}, true);
  add("components.catalogUrl", "", kScopeGlobal, {}, true);
  add("diagnostics.crashReports", false);
  return s;
}

bool sameKind(const Json& a, const Json& b) {
  if (a.is_number() && b.is_number()) return true;
  return a.type() == b.type();
}

}  // namespace

const std::vector<SettingDef>& Settings::schema() {
  static const std::vector<SettingDef> s = buildSchema();
  return s;
}

const SettingDef* Settings::find(std::string_view key) {
  for (const auto& def : schema()) {
    if (def.key == key) return &def;
  }
  return nullptr;
}

std::string Settings::normalizeSite(std::string_view site) {
  std::string s = toLower(trim(site));
  if (auto pos = s.find("://"); pos != std::string::npos) s = s.substr(pos + 3);
  if (auto pos = s.find_first_of("/?#"); pos != std::string::npos) s.resize(pos);
  if (auto at = s.rfind('@'); at != std::string::npos) s = s.substr(at + 1);
  if (!s.empty() && s.front() != '[') {
    if (auto colon = s.rfind(':'); colon != std::string::npos) s.resize(colon);
  }
  while (!s.empty() && s.back() == '.') s.pop_back();
  if (s.empty()) return s;
  if (auto ascii = hostToAscii(s)) return *ascii;
  return {};
}

Settings::Settings(fs::path file) : file_(std::move(file)) {}

bool Settings::load(std::string* error) {
  auto text = readFile(file_);
  if (!text) return true;
  Json data = Json::parse(*text, nullptr, false);
  if (data.is_discarded() || !data.is_object()) {
    if (error) *error = "settings file is not valid JSON";
    return false;
  }
  auto err = importData(data, false);
  if (err) {
    if (error) *error = *err;
    return false;
  }
  return true;
}

bool Settings::save(std::string* error) {
  Json data = exportData();
  return writeFileAtomic(file_, data.dump(2), error);
}

Json Settings::get(std::string_view key, const Scope& scope) const {
  const SettingDef* def = find(key);
  if (def == nullptr) return nullptr;
  std::string k(key);
  std::lock_guard lock(mutex_);

  if (scope.tab != 0 && (def->scopes & kScopeTab)) {
    if (auto it = tabs_.find(scope.tab); it != tabs_.end() && it->second.contains(k)) {
      return it->second[k];
    }
  }
  if (!scope.site.empty() && (def->scopes & kScopeSite)) {
    std::string host = normalizeSite(scope.site);
    while (!host.empty()) {
      if (auto it = sites_.find(host); it != sites_.end() && it->second.contains(k)) {
        return it->second[k];
      }
      auto dot = host.find('.');
      if (dot == std::string::npos) break;
      host = host.substr(dot + 1);
    }
  }
  if (global_.contains(k)) return global_[k];
  return def->defaultValue;
}

std::optional<std::string> Settings::validate(const SettingDef& def, const Json& value) const {
  if (!sameKind(def.defaultValue, value)) return "wrong type for " + def.key;
  if (!def.choices.empty()) {
    auto s = value.get<std::string>();
    if (std::find(def.choices.begin(), def.choices.end(), s) == def.choices.end()) {
      return "invalid value for " + def.key + ": " + s;
    }
  }
  if (def.key == "content.zoom") {
    double z = value.get<double>();
    if (z < 25 || z > 500) return "zoom must be between 25 and 500";
  }
  return std::nullopt;
}

std::optional<std::string> Settings::set(std::string_view key, const Json& value, const Scope& scope) {
  const SettingDef* def = find(key);
  if (def == nullptr) return "unknown setting: " + std::string(key);
  if (auto err = validate(*def, value)) return err;
  std::string k(key);
  {
    std::lock_guard lock(mutex_);
    if (scope.tab != 0) {
      if (!(def->scopes & kScopeTab)) return k + " cannot be set per tab";
      tabs_[scope.tab][k] = value;
    } else if (!scope.site.empty()) {
      if (!(def->scopes & kScopeSite)) return k + " cannot be set per site";
      std::string host = normalizeSite(scope.site);
      if (host.empty()) return "invalid site: " + scope.site;
      sites_[host][k] = value;
    } else {
      if (value == def->defaultValue) {
        global_.erase(k);
      } else {
        global_[k] = value;
      }
    }
  }
  notify(k, scope);
  return std::nullopt;
}

void Settings::reset(std::string_view key, const Scope& scope) {
  std::string k(key);
  {
    std::lock_guard lock(mutex_);
    if (scope.tab != 0) {
      if (auto it = tabs_.find(scope.tab); it != tabs_.end()) it->second.erase(k);
    } else if (!scope.site.empty()) {
      std::string host = normalizeSite(scope.site);
      if (auto it = sites_.find(host); it != sites_.end()) {
        it->second.erase(k);
        if (it->second.empty()) sites_.erase(it);
      }
    } else {
      global_.erase(k);
    }
  }
  notify(k, scope);
}

void Settings::clearTab(int64_t tab) {
  std::lock_guard lock(mutex_);
  tabs_.erase(tab);
}

void Settings::clearSite(std::string_view site) {
  std::string host = normalizeSite(site);
  {
    std::lock_guard lock(mutex_);
    sites_.erase(host);
  }
  notify("*", Scope{host, 0});
}

Json Settings::describe() const {
  Json out = Json::object();
  Json items = Json::array();
  std::lock_guard lock(mutex_);
  for (const auto& def : schema()) {
    Json scopes = Json::array();
    if (def.scopes & kScopeGlobal) scopes.push_back("global");
    if (def.scopes & kScopeSite) scopes.push_back("site");
    if (def.scopes & kScopeTab) scopes.push_back("tab");
    Json item = {
        {"key", def.key},
        {"default", def.defaultValue},
        {"value", global_.contains(def.key) ? global_[def.key] : def.defaultValue},
        {"scopes", scopes},
        {"advanced", def.advanced},
    };
    if (!def.choices.empty()) item["choices"] = def.choices;
    items.push_back(std::move(item));
  }
  out["items"] = std::move(items);
  Json sites = Json::array();
  for (const auto& [host, values] : sites_) sites.push_back({{"site", host}, {"values", values}});
  out["sites"] = std::move(sites);
  return out;
}

Json Settings::siteOverrides(std::string_view site) const {
  std::string host = normalizeSite(site);
  std::lock_guard lock(mutex_);
  if (auto it = sites_.find(host); it != sites_.end()) return it->second;
  return Json::object();
}

Json Settings::exportData() const {
  std::lock_guard lock(mutex_);
  Json sites = Json::object();
  for (const auto& [host, values] : sites_) sites[host] = values;
  return {{"version", 1}, {"global", global_}, {"sites", sites}};
}

std::optional<std::string> Settings::importData(const Json& data, bool merge) {
  if (!data.is_object()) return "settings must be an object";
  Json global = Json::object();
  std::map<std::string, Json> sites;
  if (merge) {
    std::lock_guard lock(mutex_);
    global = global_;
    sites = sites_;
  }
  // Unknown keys and invalid values are skipped so that settings written by a
  // newer version still load.
  if (auto it = data.find("global"); it != data.end() && it->is_object()) {
    for (const auto& [k, v] : it->items()) {
      const SettingDef* def = find(k);
      if (def && !validate(*def, v)) global[k] = v;
    }
  }
  if (auto it = data.find("sites"); it != data.end() && it->is_object()) {
    for (const auto& [host, values] : it->items()) {
      std::string h = normalizeSite(host);
      if (h.empty() || !values.is_object()) continue;
      for (const auto& [k, v] : values.items()) {
        const SettingDef* def = find(k);
        if (def && (def->scopes & kScopeSite) && !validate(*def, v)) sites[h][k] = v;
      }
    }
  }
  {
    std::lock_guard lock(mutex_);
    global_ = std::move(global);
    sites_ = std::move(sites);
  }
  notify("*", {});
  return std::nullopt;
}

int Settings::subscribe(Listener listener) {
  std::lock_guard lock(mutex_);
  int id = nextListener_++;
  listeners_[id] = std::move(listener);
  return id;
}

void Settings::unsubscribe(int id) {
  std::lock_guard lock(mutex_);
  listeners_.erase(id);
}

void Settings::notify(const std::string& key, const Scope& scope) {
  std::vector<Listener> copy;
  {
    std::lock_guard lock(mutex_);
    for (const auto& [id, l] : listeners_) copy.push_back(l);
  }
  for (const auto& l : copy) l(key, scope);
}

}  // namespace liora

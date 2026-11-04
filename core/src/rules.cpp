#include "liora/rules.hpp"

#include <algorithm>
#include <cctype>

#include "embedded.hpp"
#include "liora/dns.hpp"
#include "liora/settings.hpp"
#include "liora/util.hpp"

namespace liora {

namespace {

constexpr const char* kAllowedAlpn[] = {"h3", "h2", "http/1.1"};

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

bool validVariantId(std::string_view id) {
  if (id.empty() || id.size() > 64) return false;
  return std::all_of(id.begin(), id.end(), [](char c) {
    auto u = static_cast<unsigned char>(c);
    return std::islower(u) || std::isdigit(u) || c == '-';
  });
}

std::optional<std::vector<std::string>> stringArray(const Json& j) {
  if (!j.is_array()) return std::nullopt;
  std::vector<std::string> out;
  for (const auto& item : j) {
    if (!item.is_string()) return std::nullopt;
    out.push_back(item.get<std::string>());
  }
  return out;
}

void dedupe(std::vector<std::string>& v) {
  std::vector<std::string> out;
  for (auto& s : v) {
    if (std::find(out.begin(), out.end(), s) == out.end()) out.push_back(std::move(s));
  }
  v = std::move(out);
}

}  // namespace

std::string RuleSet::h3Name(H3Mode mode) {
  switch (mode) {
    case H3Mode::kOff: return "off";
    case H3Mode::kPrefer: return "prefer";
    case H3Mode::kForce: return "force";
  }
  return "prefer";
}

std::optional<H3Mode> RuleSet::h3FromName(std::string_view name) {
  if (name == "off") return H3Mode::kOff;
  if (name == "prefer") return H3Mode::kPrefer;
  if (name == "force") return H3Mode::kForce;
  return std::nullopt;
}

std::optional<std::string> RuleSet::normalizePattern(std::string_view input, std::string* error) {
  auto fail = [&](std::string msg) -> std::optional<std::string> {
    if (error) *error = std::move(msg);
    return std::nullopt;
  };
  std::string s = trim(input);
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
  if (s.find('*') != std::string::npos) return fail("wildcards are only allowed as a leading '*.'");
  std::string host = Settings::normalizeSite(s);
  if (!validHost(host)) return fail("'" + std::string(trim(input)) + "' is not a valid domain");
  return prefix + host;
}

Json NetRule::toJson() const {
  Json j = {
      {"id", id},
      {"name", name},
      {"enabled", enabled},
      {"domains", domains},
      {"h3", RuleSet::h3Name(h3)},
      {"ech", ech},
      {"echSource", echSource},
      {"alpn", alpn},
      {"dns", dns},
  };
  if (!presetId.empty()) j["preset"] = presetId;
  return j;
}

std::optional<NetRule> NetRule::fromJson(const Json& j, std::string* error) {
  auto fail = [&](std::string msg) -> std::optional<NetRule> {
    if (error) *error = std::move(msg);
    return std::nullopt;
  };
  if (!j.is_object()) return fail("rule must be an object");

  auto str = [&](const char* key, std::string def, bool& ok) -> std::string {
    auto it = j.find(key);
    if (it == j.end() || it->is_null()) return def;
    if (!it->is_string()) {
      ok = false;
      return {};
    }
    return it->get<std::string>();
  };
  auto boolean = [&](const char* key, bool def, bool& ok) -> bool {
    auto it = j.find(key);
    if (it == j.end() || it->is_null()) return def;
    if (!it->is_boolean()) {
      ok = false;
      return def;
    }
    return it->get<bool>();
  };

  NetRule r;
  bool ok = true;
  r.id = str("id", "", ok);
  r.name = trim(str("name", "", ok));
  r.presetId = str("preset", "", ok);
  r.enabled = boolean("enabled", true, ok);
  r.ech = boolean("ech", true, ok);
  std::string h3 = str("h3", "prefer", ok);
  r.echSource = trim(str("echSource", "", ok));
  r.dns = trim(str("dns", "", ok));
  if (!ok) return fail("rule field has the wrong type");

  if (r.id.empty()) r.id = newId("rule-");

  auto mode = RuleSet::h3FromName(h3);
  if (!mode) return fail("h3 must be off, prefer or force");
  r.h3 = *mode;

  auto domains = stringArray(j.value("domains", Json::array()));
  if (!domains) return fail("domains must be a list of strings");
  for (const auto& d : *domains) {
    std::string err;
    auto pattern = RuleSet::normalizePattern(d, &err);
    if (!pattern) return fail(err);
    r.domains.push_back(*pattern);
  }
  dedupe(r.domains);
  if (r.domains.empty()) return fail("a rule needs at least one domain");
  if (r.name.empty()) r.name = r.domains.front();

  if (auto it = j.find("alpn"); it != j.end() && !it->is_null()) {
    auto alpn = stringArray(*it);
    if (!alpn) return fail("alpn must be a list of strings");
    r.alpn.clear();
    for (auto& a : *alpn) {
      a = toLower(trim(a));
      bool known = std::any_of(std::begin(kAllowedAlpn), std::end(kAllowedAlpn),
                               [&](const char* v) { return a == v; });
      if (!known) return fail("unsupported alpn value '" + a + "'");
      r.alpn.push_back(a);
    }
    dedupe(r.alpn);
  }
  if (r.h3 == H3Mode::kForce) {
    r.alpn = {"h3"};
  } else if (r.h3 == H3Mode::kOff) {
    r.alpn.erase(std::remove(r.alpn.begin(), r.alpn.end(), "h3"), r.alpn.end());
  }
  if (r.alpn.empty()) r.alpn = {"h2", "http/1.1"};

  if (!r.echSource.empty()) {
    std::string host = Settings::normalizeSite(r.echSource);
    if (!validHost(host)) return fail("echSource is not a valid domain");
    r.echSource = host;
  }

  if (!r.dns.empty()) {
    if (r.dns.find("://") != std::string::npos) {
      std::string err;
      if (!DnsCatalog::parseCustom(r.dns, &err)) return fail("dns: " + err);
    } else if (!validVariantId(r.dns)) {
      return fail("dns must be a provider id or a DoH / DoT / DoQ address");
    }
  }
  return r;
}

std::optional<std::string> RuleSet::replace(std::vector<NetRule> rules) {
  std::unordered_map<std::string, size_t> index;
  std::unordered_map<std::string, size_t> ids;
  for (size_t i = 0; i < rules.size(); ++i) {
    const auto& r = rules[i];
    if (!ids.emplace(r.id, i).second) return "duplicate rule id '" + r.id + "'";
    if (!r.enabled) continue;
    for (const auto& d : r.domains) {
      auto [it, inserted] = index.emplace(d, i);
      if (!inserted) {
        return "'" + d + "' is already covered by rule '" + rules[it->second].name + "'";
      }
    }
  }
  rules_ = std::move(rules);
  index_ = std::move(index);
  return std::nullopt;
}

std::optional<std::string> RuleSet::load(const Json& rules) {
  if (!rules.is_array()) return "rules must be a list";
  std::vector<NetRule> parsed;
  for (size_t i = 0; i < rules.size(); ++i) {
    std::string err;
    auto r = NetRule::fromJson(rules[i], &err);
    if (!r) return "rule " + std::to_string(i + 1) + ": " + err;
    parsed.push_back(std::move(*r));
  }
  return replace(std::move(parsed));
}

Json RuleSet::toJson() const {
  Json out = Json::array();
  for (const auto& r : rules_) out.push_back(r.toJson());
  return out;
}

const NetRule* RuleSet::find(std::string_view id) const {
  for (const auto& r : rules_) {
    if (r.id == id) return &r;
  }
  return nullptr;
}

const NetRule* RuleSet::match(std::string_view rawHost) const {
  if (index_.empty()) return nullptr;
  std::string host = Settings::normalizeSite(rawHost);
  if (host.empty()) return nullptr;

  auto lookup = [&](const std::string& key) -> const NetRule* {
    auto it = index_.find(key);
    return it == index_.end() ? nullptr : &rules_[it->second];
  };

  if (auto r = lookup("=" + host)) return r;
  if (auto r = lookup(host)) return r;
  std::string_view rest = host;
  for (auto pos = rest.find('.'); pos != std::string_view::npos; pos = rest.find('.')) {
    rest.remove_prefix(pos + 1);
    std::string parent(rest);
    if (auto r = lookup("*." + parent)) return r;
    if (auto r = lookup(parent)) return r;
  }
  return nullptr;
}

std::optional<std::string> RuleSet::add(NetRule rule) {
  auto next = rules_;
  next.push_back(std::move(rule));
  return replace(std::move(next));
}

std::optional<std::string> RuleSet::update(NetRule rule) {
  auto next = rules_;
  auto it = std::find_if(next.begin(), next.end(), [&](const NetRule& r) { return r.id == rule.id; });
  if (it == next.end()) return "rule not found";
  *it = std::move(rule);
  return replace(std::move(next));
}

bool RuleSet::remove(std::string_view id) {
  auto next = rules_;
  auto it = std::find_if(next.begin(), next.end(), [&](const NetRule& r) { return r.id == id; });
  if (it == next.end()) return false;
  next.erase(it);
  return !replace(std::move(next)).has_value();
}

std::optional<std::string> RuleSet::setEnabled(std::string_view id, bool enabled) {
  const NetRule* current = find(id);
  if (!current) return "rule not found";
  NetRule copy = *current;
  copy.enabled = enabled;
  return update(std::move(copy));
}

const Json& RuleSet::presets() {
  static const Json list = [] {
    Json data = Json::parse(embedded::rulePresets(), nullptr, false);
    if (data.is_discarded() || !data.contains("presets") || !data["presets"].is_array()) {
      return Json::array();
    }
    return data["presets"];
  }();
  return list;
}

std::optional<NetRule> RuleSet::fromPreset(std::string_view presetId, std::string* error) {
  for (const auto& p : presets()) {
    if (p.value("id", std::string()) != presetId) continue;
    Json j = p.value("rule", Json::object());
    j["preset"] = std::string(presetId);
    j["domains"] = p.value("domains", Json::array());
    const Json name = p.value("name", Json::object());
    if (name.is_object() && name.contains("en") && name["en"].is_string()) j["name"] = name["en"];
    return NetRule::fromJson(j, error);
  }
  if (error) *error = "unknown preset '" + std::string(presetId) + "'";
  return std::nullopt;
}

}  // namespace liora

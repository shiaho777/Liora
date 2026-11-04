#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "liora/json.hpp"

namespace liora {

enum class H3Mode { kOff, kPrefer, kForce };

// Per-domain network rule. Only hosts matched by an enabled rule get the
// customized behavior; everything else uses the kernel defaults.
//
// Domain patterns:
//   x.com     the domain and all of its subdomains
//   *.x.com   subdomains only
//   =x.com    the domain only
struct NetRule {
  std::string id;
  std::string name;
  bool enabled = true;
  std::string presetId;
  std::vector<std::string> domains;
  H3Mode h3 = H3Mode::kPrefer;
  bool ech = true;
  // Host whose HTTPS record supplies the ECHConfigList. Empty means the global
  // net.echOuter setting.
  std::string echSource;
  // ALPN values injected into the synthesized HTTPS record.
  std::vector<std::string> alpn{"h3", "h2"};
  // Empty: global DNS. Otherwise a catalog variant id or a custom address.
  std::string dns;

  Json toJson() const;
  static std::optional<NetRule> fromJson(const Json& j, std::string* error = nullptr);
};

// Not thread-safe; the owner serializes access.
class RuleSet {
 public:
  std::optional<std::string> load(const Json& rules);
  Json toJson() const;

  const std::vector<NetRule>& rules() const { return rules_; }
  const NetRule* find(std::string_view id) const;
  const NetRule* match(std::string_view host) const;

  std::optional<std::string> add(NetRule rule);
  std::optional<std::string> update(NetRule rule);
  bool remove(std::string_view id);
  std::optional<std::string> setEnabled(std::string_view id, bool enabled);

  static const Json& presets();
  static std::optional<NetRule> fromPreset(std::string_view presetId, std::string* error = nullptr);

  // Normalizes one pattern to its canonical form ("x.com", "*.x.com", "=x.com").
  static std::optional<std::string> normalizePattern(std::string_view input, std::string* error = nullptr);

  static std::string h3Name(H3Mode mode);
  static std::optional<H3Mode> h3FromName(std::string_view name);

 private:
  std::optional<std::string> replace(std::vector<NetRule> rules);

  std::vector<NetRule> rules_;
  std::unordered_map<std::string, size_t> index_;
};

}  // namespace liora

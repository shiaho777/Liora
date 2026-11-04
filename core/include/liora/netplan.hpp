#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "liora/dns.hpp"
#include "liora/json.hpp"
#include "liora/rules.hpp"

namespace liora {

class Settings;

// Effective behavior for one host matched by a rule.
struct HostPlan {
  std::string host;
  std::string ruleId;
  std::string ruleName;
  H3Mode h3 = H3Mode::kPrefer;
  bool ech = false;
  std::string echSource;
  std::vector<std::string> alpn;
  // nullopt: the host uses the global DNS plan.
  std::optional<DnsEndpoint> dns;

  Json toJson() const;
};

// Everything the shell needs to configure the active kernel and liora-net.
struct NetConfig {
  DnsPlan dns;
  // True when the local liora-net resolver must sit in front of the kernel:
  // DoT/DoQ upstreams, per-rule DNS, or synthesized HTTPS records.
  bool needsNet = false;
  // host:443 entries for Chromium's --origin-to-force-quic-on. Only exact
  // hosts can be listed; subdomains rely on the injected alpn=h3.
  std::vector<std::string> forceQuicOrigins;
  // Configuration document consumed by liora-net.
  Json net = Json::object();
  std::vector<std::string> warnings;

  Json toJson() const;
};

class NetPlanner {
 public:
  static std::optional<HostPlan> forHost(const RuleSet& rules, const DnsCatalog& catalog,
                                         const Settings& settings, std::string_view host);
  static NetConfig build(const RuleSet& rules, const DnsCatalog& catalog, const Settings& settings);

  // A rule's dns field: empty (global), a catalog variant id, or a custom
  // address accepted by DnsCatalog::parseCustom.
  static std::optional<DnsEndpoint> resolveDns(const DnsCatalog& catalog, const Settings& settings,
                                               std::string_view spec, std::string* error = nullptr);

  static Json endpointJson(const DnsEndpoint& endpoint);
};

}  // namespace liora

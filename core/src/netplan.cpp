#include "liora/netplan.hpp"

#include <algorithm>

#include "liora/settings.hpp"
#include "liora/util.hpp"

namespace liora {

namespace {

std::string echOuter(const Settings& settings, const NetRule& rule) {
  if (!rule.echSource.empty()) return rule.echSource;
  auto v = settings.get("net.echOuter");
  std::string s = v.is_string() ? v.get<std::string>() : std::string();
  return s.empty() ? std::string("cloudflare-ech.com") : s;
}

std::vector<std::string> effectiveAlpn(const NetRule& rule) {
  std::vector<std::string> alpn = rule.alpn;
  if (rule.h3 == H3Mode::kForce) return {"h3"};
  if (rule.h3 == H3Mode::kOff) {
    alpn.erase(std::remove(alpn.begin(), alpn.end(), "h3"), alpn.end());
  } else if (std::find(alpn.begin(), alpn.end(), "h3") == alpn.end()) {
    alpn.insert(alpn.begin(), "h3");
  }
  if (alpn.empty()) alpn = {"h2", "http/1.1"};
  return alpn;
}

// Strips the pattern prefix; returns the host only for patterns that cover the
// bare domain ("x.com" and "=x.com").
std::optional<std::string> exactHost(const std::string& pattern) {
  if (startsWith(pattern, "*.")) return std::nullopt;
  if (startsWith(pattern, "=")) return pattern.substr(1);
  return pattern;
}

}  // namespace

Json NetPlanner::endpointJson(const DnsEndpoint& e) {
  return {
      {"id", e.id},
      {"name", e.name},
      {"protocol", DnsCatalog::protocolName(e.protocol)},
      {"address", e.address},
      {"bootstrap", e.bootstrap},
  };
}

Json HostPlan::toJson() const {
  Json j = {
      {"host", host},
      {"rule", ruleId},
      {"ruleName", ruleName},
      {"h3", RuleSet::h3Name(h3)},
      {"ech", ech},
      {"echSource", echSource},
      {"alpn", alpn},
      {"dns", nullptr},
  };
  if (dns) j["dns"] = NetPlanner::endpointJson(*dns);
  return j;
}

Json NetConfig::toJson() const {
  return {
      {"dns", dns.toJson()},
      {"needsNet", needsNet},
      {"forceQuicOrigins", forceQuicOrigins},
      {"net", net},
      {"warnings", warnings},
  };
}

std::optional<DnsEndpoint> NetPlanner::resolveDns(const DnsCatalog& catalog, const Settings& settings,
                                                  std::string_view spec, std::string* error) {
  std::string s = trim(spec);
  if (s.empty()) return std::nullopt;
  if (const auto* variant = catalog.find(s)) {
    auto protoName = settings.get("dns.protocol");
    auto proto = DnsCatalog::protocolFromName(protoName.is_string() ? protoName.get<std::string>() : "doh")
                     .value_or(DnsProtocol::kDoh);
    if (auto ep = catalog.endpointFor(variant->id, proto)) return ep;
    // The variant may not offer the preferred protocol; DoH is always present.
    if (auto ep = catalog.endpointFor(variant->id, DnsProtocol::kDoh)) return ep;
    if (error) *error = "provider '" + s + "' has no usable endpoint";
    return std::nullopt;
  }
  return DnsCatalog::parseCustom(s, error);
}

std::optional<HostPlan> NetPlanner::forHost(const RuleSet& rules, const DnsCatalog& catalog,
                                            const Settings& settings, std::string_view host) {
  auto enabled = settings.get("net.rulesEnabled");
  if (enabled.is_boolean() && !enabled.get<bool>()) return std::nullopt;
  std::string normalized = Settings::normalizeSite(host);
  if (normalized.empty()) return std::nullopt;
  const NetRule* rule = rules.match(normalized);
  if (!rule) return std::nullopt;

  HostPlan plan;
  plan.host = normalized;
  plan.ruleId = rule->id;
  plan.ruleName = rule->name;
  plan.h3 = rule->h3;
  plan.ech = rule->ech;
  if (rule->ech) plan.echSource = echOuter(settings, *rule);
  plan.alpn = effectiveAlpn(*rule);
  plan.dns = resolveDns(catalog, settings, rule->dns);
  return plan;
}

NetConfig NetPlanner::build(const RuleSet& rules, const DnsCatalog& catalog, const Settings& settings) {
  NetConfig cfg;
  cfg.dns = catalog.plan(settings);
  cfg.needsNet = cfg.dns.needsNet;

  Json upstream = nullptr;
  if (cfg.dns.endpoint) upstream = endpointJson(*cfg.dns.endpoint);

  Json ruleList = Json::array();
  auto enabled = settings.get("net.rulesEnabled");
  bool rulesOn = !enabled.is_boolean() || enabled.get<bool>();

  if (rulesOn) {
    for (const auto& rule : rules.rules()) {
      if (!rule.enabled || rule.domains.empty()) continue;

      std::string dnsError;
      auto dns = resolveDns(catalog, settings, rule.dns, &dnsError);
      if (!rule.dns.empty() && !dns) {
        cfg.warnings.push_back("rule '" + rule.name + "': " + dnsError + "; using global DNS");
      }

      auto alpn = effectiveAlpn(rule);
      Json r = {
          {"id", rule.id},
          {"patterns", rule.domains},
          {"h3", RuleSet::h3Name(rule.h3)},
          {"alpn", alpn},
          {"ech", rule.ech},
          {"echSource", rule.ech ? echOuter(settings, rule) : std::string()},
          {"dns", dns ? endpointJson(*dns) : Json(nullptr)},
      };
      ruleList.push_back(std::move(r));

      if (rule.h3 == H3Mode::kForce) {
        bool coversSubdomains = false;
        for (const auto& pattern : rule.domains) {
          if (auto host = exactHost(pattern)) {
            std::string origin = *host + ":443";
            if (std::find(cfg.forceQuicOrigins.begin(), cfg.forceQuicOrigins.end(), origin) ==
                cfg.forceQuicOrigins.end()) {
              cfg.forceQuicOrigins.push_back(origin);
            }
          }
          if (!startsWith(pattern, "=")) coversSubdomains = true;
        }
        if (coversSubdomains) {
          cfg.warnings.push_back("rule '" + rule.name +
                                 "': QUIC is forced on listed hosts only; subdomains prefer h3 "
                                 "via the HTTPS record and may still fall back to TCP");
        }
      }
    }
  }

  if (!ruleList.empty()) cfg.needsNet = true;

  cfg.net = {
      {"version", 1},
      {"mode", cfg.dns.mode},
      {"upstream", upstream},
      {"rules", ruleList},
  };
  return cfg;
}

}  // namespace liora

#include "harness.hpp"

#include <filesystem>

#include "liora/dns.hpp"
#include "liora/netplan.hpp"
#include "liora/rules.hpp"
#include "liora/settings.hpp"

using namespace liora;

namespace {

struct TempDir {
  fs::path path;
  TempDir() {
    path = fs::temp_directory_path() / ("liora-test-" + newId());
    fs::create_directories(path);
  }
  ~TempDir() {
    std::error_code ec;
    fs::remove_all(path, ec);
  }
  fs::path operator/(std::string_view name) const { return path / utf8ToPath(name); }
};

NetRule makeRule(std::vector<std::string> domains, H3Mode h3 = H3Mode::kPrefer) {
  Json j = {{"name", "test"},
            {"domains", domains},
            {"h3", RuleSet::h3Name(h3)},
            {"ech", true},
            {"echSource", "cloudflare-ech.com"}};
  std::string error;
  auto rule = NetRule::fromJson(j, &error);
  CHECK(rule.has_value());
  return *rule;
}

}  // namespace

LIORA_TEST(dns_catalog_parses_builtin_providers) {
  DnsCatalog catalog;
  CHECK(catalog.variants().size() >= 15);
  CHECK(catalog.find("cloudflare") != nullptr);
  const DnsVariant* adguard = catalog.find("adguard");
  CHECK(adguard != nullptr);
  CHECK_EQ(adguard->doh, std::string("https://dns.adguard-dns.com/dns-query"));

  Json described = catalog.describe();
  CHECK(described["categories"].is_array());
  CHECK(described["providers"].is_array());
}

LIORA_TEST(dns_parse_custom_accepts_supported_forms) {
  auto doh = DnsCatalog::parseCustom("https://dns.example/dns-query");
  CHECK(doh && doh->protocol == DnsProtocol::kDoh);
  CHECK_EQ(doh->address, std::string("https://dns.example/dns-query"));

  // A bare host gets the conventional path.
  auto bare = DnsCatalog::parseCustom("https://dns.example");
  CHECK(bare);
  CHECK_EQ(bare->address, std::string("https://dns.example/dns-query"));

  auto dot = DnsCatalog::parseCustom("tls://dns.example:853");
  CHECK(dot && dot->protocol == DnsProtocol::kDot);
  CHECK_EQ(dot->address, std::string("dns.example:853"));

  auto doq = DnsCatalog::parseCustom("quic://dns.example");
  CHECK(doq && doq->protocol == DnsProtocol::kDoq);

  // A bare address is treated as DoT.
  auto plain = DnsCatalog::parseCustom("dns.example");
  CHECK(plain && plain->protocol == DnsProtocol::kDot);
}

LIORA_TEST(dns_parse_custom_rejects_bad_input) {
  std::string error;
  CHECK(!DnsCatalog::parseCustom("http://dns.example/dns-query", &error));
  CHECK(error.find("https") != std::string::npos);
  CHECK(!DnsCatalog::parseCustom("https://dns.example:99999/dns-query").has_value());
  CHECK(!DnsCatalog::parseCustom("tls://dns.example/path").has_value());
  CHECK(!DnsCatalog::parseCustom("ftp://dns.example").has_value());
  CHECK(!DnsCatalog::parseCustom("").has_value());
}

LIORA_TEST(dns_plan_follows_settings) {
  TempDir dir;
  Settings settings(dir / "settings.json");
  DnsCatalog catalog;

  // Default is automatic: let the kernel upgrade to DoH when it can.
  CHECK_EQ(catalog.plan(settings).mode, std::string("automatic"));

  CHECK(!settings.set("dns.mode", "provider"));
  CHECK(!settings.set("dns.provider", "adguard"));
  DnsPlan plan = catalog.plan(settings);
  CHECK_EQ(plan.mode, std::string("secure"));
  CHECK_EQ(plan.dohTemplates, std::string("https://dns.adguard-dns.com/dns-query"));
  CHECK(!plan.needsNet);

  // DoT has no kernel-side template; it must go through liora-net.
  CHECK(!settings.set("dns.protocol", "dot"));
  DnsPlan dot = catalog.plan(settings);
  CHECK(dot.needsNet);
  CHECK(dot.dohTemplates.empty());

  CHECK(!settings.set("dns.mode", "off"));
  CHECK_EQ(catalog.plan(settings).mode, std::string("system"));
}

LIORA_TEST(dns_plan_for_custom_entry) {
  TempDir dir;
  Settings settings(dir / "settings.json");
  DnsCatalog catalog;
  CHECK(!settings.set("dns.mode", "custom"));
  CHECK(!settings.set("dns.customId", "custom-x"));
  CHECK(!settings.set("dns.custom",
                      Json::array({{{"id", "custom-x"}, {"name", "Mine"}, {"address", "https://dns.example/q"}}})));
  DnsPlan plan = catalog.plan(settings);
  CHECK_EQ(plan.mode, std::string("secure"));
  CHECK(plan.endpoint.has_value());
  CHECK_EQ(plan.endpoint->address, std::string("https://dns.example/q"));
}

LIORA_TEST(rules_normalize_patterns) {
  CHECK_EQ(*RuleSet::normalizePattern("X.com"), std::string("x.com"));
  CHECK_EQ(*RuleSet::normalizePattern("*.x.com"), std::string("*.x.com"));
  CHECK_EQ(*RuleSet::normalizePattern("=x.com"), std::string("=x.com"));
  CHECK_EQ(*RuleSet::normalizePattern(".x.com"), std::string("x.com"));
  CHECK_EQ(*RuleSet::normalizePattern("https://x.com/path"), std::string("x.com"));

  // A bare top-level domain would capture the whole internet.
  CHECK(!RuleSet::normalizePattern("com").has_value());
  CHECK(!RuleSet::normalizePattern("127.0.0.1").has_value());
  CHECK(!RuleSet::normalizePattern("x.*.com").has_value());
  CHECK(!RuleSet::normalizePattern("").has_value());
}

LIORA_TEST(rules_match_precedence) {
  RuleSet rules;
  CHECK(!rules.add(makeRule({"x.com"})));
  CHECK(!rules.add(makeRule({"*.cdn.com"})));
  CHECK(!rules.add(makeRule({"=exact.com"})));

  // "=host" matches the host itself but not its subdomains.
  CHECK(rules.match("exact.com") != nullptr);
  CHECK(rules.match("sub.exact.com") == nullptr);

  // Longest match wins over the broader parent rule.
  const NetRule* wildcard = rules.match("a.cdn.com");
  CHECK(wildcard != nullptr);
  CHECK_EQ(wildcard->domains.front(), std::string("*.cdn.com"));

  CHECK(rules.match("cdn.com") == nullptr);
  CHECK(rules.match("x.com") != nullptr);
  CHECK(rules.match("deep.sub.x.com") != nullptr);
  CHECK(rules.match("notx.com") == nullptr);
}

LIORA_TEST(rules_reject_duplicates_and_support_disable) {
  RuleSet rules;
  CHECK(!rules.add(makeRule({"x.com"})));
  CHECK(rules.add(makeRule({"x.com"})).has_value());

  const NetRule* rule = rules.match("x.com");
  CHECK(rule != nullptr);
  std::string id = rule->id;

  CHECK(!rules.setEnabled(id, false));
  CHECK(rules.match("x.com") == nullptr);

  // Disabling frees the domain for another rule.
  CHECK(!rules.add(makeRule({"x.com"})));
  const NetRule* replacement = rules.match("x.com");
  CHECK(replacement != nullptr);
  CHECK_NE(replacement->id, id);

  // Removing the disabled rule leaves the replacement in place.
  CHECK(rules.remove(id));
  CHECK(rules.match("x.com") != nullptr);
}

LIORA_TEST(rules_force_quic_sets_alpn) {
  std::string error;
  Json j = {{"name", "force"}, {"domains", {"x.com"}}, {"h3", "force"}};
  auto rule = NetRule::fromJson(j, &error);
  CHECK(rule.has_value());
  CHECK_EQ(rule->alpn.size(), size_t{1});
  CHECK_EQ(rule->alpn.front(), std::string("h3"));

  Json off = {{"name", "off"}, {"domains", {"x.com"}}, {"h3", "off"}};
  auto noH3 = NetRule::fromJson(off, &error);
  CHECK(noH3.has_value());
  for (const auto& a : noH3->alpn) CHECK(a != "h3");
}

LIORA_TEST(rules_presets_apply) {
  std::string error;
  auto rule = RuleSet::fromPreset("x", &error);
  CHECK(rule.has_value());
  CHECK_EQ(rule->presetId, std::string("x"));
  CHECK(rule->domains.size() >= 3);
  CHECK(rule->ech);
  CHECK(!RuleSet::fromPreset("missing", &error).has_value());
}

LIORA_TEST(netplan_builds_force_quic_origins_and_warnings) {
  TempDir dir;
  Settings settings(dir / "settings.json");
  DnsCatalog catalog;
  RuleSet rules;
  CHECK(!rules.add(makeRule({"x.com"}, H3Mode::kForce)));

  NetConfig config = NetPlanner::build(rules, catalog, settings);
  CHECK(config.needsNet);
  CHECK_EQ(config.forceQuicOrigins.size(), size_t{1});
  CHECK_EQ(config.forceQuicOrigins.front(), std::string("x.com:443"));
  // Subdomains cannot be forced, which the warning states.
  CHECK(!config.warnings.empty());
  CHECK_EQ(config.net["rules"].size(), size_t{1});
}

LIORA_TEST(netplan_rule_dns_overrides_global) {
  TempDir dir;
  Settings settings(dir / "settings.json");
  DnsCatalog catalog;
  RuleSet rules;
  Json j = {{"name", "with dns"}, {"domains", {"private.example"}}, {"h3", "prefer"}, {"dns", "quad9"}};
  std::string error;
  auto rule = NetRule::fromJson(j, &error);
  CHECK(rule.has_value());
  CHECK(!rules.add(*rule));

  auto plan = NetPlanner::forHost(rules, catalog, settings, "private.example");
  CHECK(plan.has_value());
  CHECK(plan->dns.has_value());
  CHECK_EQ(plan->dns->id, std::string("quad9"));

  auto other = NetPlanner::forHost(rules, catalog, settings, "public.example");
  CHECK(!other.has_value());
}

LIORA_TEST(netplan_respects_rules_switch) {
  TempDir dir;
  Settings settings(dir / "settings.json");
  DnsCatalog catalog;
  RuleSet rules;
  CHECK(!rules.add(makeRule({"x.com"})));
  CHECK(!settings.set("net.rulesEnabled", false));

  CHECK(!NetPlanner::forHost(rules, catalog, settings, "x.com").has_value());
  NetConfig config = NetPlanner::build(rules, catalog, settings);
  CHECK_EQ(config.net["rules"].size(), size_t{0});
  CHECK(!config.needsNet);
}

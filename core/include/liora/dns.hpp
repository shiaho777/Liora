#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "liora/json.hpp"

namespace liora {

class Settings;

enum class DnsProtocol { kDoh, kDot, kDoq };

struct DnsEndpoint {
  std::string id;
  std::string name;
  DnsProtocol protocol = DnsProtocol::kDoh;
  // DoH: full https URL (may contain an RFC 8484 {?dns} template).
  // DoT / DoQ: host or host:port.
  std::string address;
  std::vector<std::string> bootstrap;
};

struct DnsVariant {
  std::string id;
  std::string providerId;
  std::string providerName;
  Json label;
  Json desc;
  std::vector<std::string> categories;
  std::string doh;
  std::string dot;
  std::string doq;
  std::vector<std::string> bootstrap;
};

// What the shell hands to the active kernel and to liora-net.
struct DnsPlan {
  // "system": OS resolver. "automatic": upgrade to DoH when the system
  // resolver supports it. "secure": encrypted DNS only, no plaintext fallback.
  std::string mode = "automatic";
  // Chromium-style DoH template list, space separated. Empty unless the kernel
  // itself can resolve with the chosen endpoint.
  std::string dohTemplates;
  // Set when the selected protocol is DoT or DoQ, which kernels cannot do
  // natively; resolution must be routed through liora-net.
  bool needsNet = false;
  std::optional<DnsEndpoint> endpoint;
  // Human readable state for the settings panel.
  std::string summary;

  Json toJson() const;
};

class DnsCatalog {
 public:
  DnsCatalog();

  // Replaces the built-in list with a newer one (downloaded catalog update).
  std::optional<std::string> loadOverride(std::string_view json);

  const std::vector<DnsVariant>& variants() const { return variants_; }
  const DnsVariant* find(std::string_view id) const;

  // Categories plus providers grouped for the settings panel.
  Json describe() const;

  // Builds the effective plan from dns.* settings.
  DnsPlan plan(const Settings& settings) const;
  // Plan for a single endpoint; used by per-domain rules.
  static DnsPlan planFor(const DnsEndpoint& endpoint, bool strict);

  // Validates and normalizes a user-entered address. Accepts
  //   https://host/path, tls://host[:port], quic://host[:port], host[:port] (DoT)
  static std::optional<DnsEndpoint> parseCustom(std::string_view input, std::string* error = nullptr);
  std::optional<DnsEndpoint> endpointFor(std::string_view variantId, DnsProtocol protocol) const;

  static std::string protocolName(DnsProtocol p);
  static std::optional<DnsProtocol> protocolFromName(std::string_view name);

 private:
  std::optional<std::string> parse(std::string_view json);

  Json categories_ = Json::array();
  std::vector<DnsVariant> variants_;
};

}  // namespace liora

#include "liora/dns.hpp"

#include <algorithm>
#include <charconv>

#include "embedded.hpp"
#include "liora/settings.hpp"
#include "liora/util.hpp"

namespace liora {

namespace {

std::vector<std::string> stringList(const Json& j) {
  std::vector<std::string> out;
  if (!j.is_array()) return out;
  for (const auto& v : j) {
    if (v.is_string()) out.push_back(v.get<std::string>());
  }
  return out;
}

std::string stringField(const Json& j, const char* key) {
  auto it = j.find(key);
  if (it == j.end() || !it->is_string()) return {};
  return it->get<std::string>();
}

bool validHostChars(std::string_view host) {
  if (host.empty() || host.size() > 253) return false;
  for (const auto& label : split(host, '.', false)) {
    if (label.empty() || label.size() > 63) return false;
    if (label.front() == '-' || label.back() == '-') return false;
    for (char c : label) {
      bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
      if (!ok) return false;
    }
  }
  return true;
}

bool validIpv6(std::string_view s) {
  if (s.empty() || s.size() > 45) return false;
  for (char c : s) {
    bool ok = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || c == ':' || c == '.';
    if (!ok) return false;
  }
  return s.find(':') != std::string_view::npos;
}

// Splits "host", "host:port" or "[v6]:port" and normalizes the host.
// Returns false when either part is invalid.
bool parseAuthority(std::string_view in, std::string& host, int& port, std::string* error) {
  port = 0;
  std::string_view h = in;
  std::string_view p;
  if (!in.empty() && in.front() == '[') {
    auto close = in.find(']');
    if (close == std::string_view::npos) {
      if (error) *error = "unterminated IPv6 address";
      return false;
    }
    h = in.substr(1, close - 1);
    auto rest = in.substr(close + 1);
    if (!rest.empty()) {
      if (rest.front() != ':') {
        if (error) *error = "unexpected text after IPv6 address";
        return false;
      }
      p = rest.substr(1);
    }
    if (!validIpv6(h)) {
      if (error) *error = "invalid IPv6 address";
      return false;
    }
    host = "[" + std::string(h) + "]";
  } else {
    if (auto colon = in.rfind(':'); colon != std::string_view::npos) {
      h = in.substr(0, colon);
      p = in.substr(colon + 1);
    }
    auto ascii = hostToAscii(toLower(h));
    if (!ascii || !validHostChars(*ascii)) {
      if (error) *error = "invalid host name";
      return false;
    }
    host = *ascii;
  }
  if (!p.empty()) {
    int value = 0;
    auto [ptr, ec] = std::from_chars(p.data(), p.data() + p.size(), value);
    if (ec != std::errc() || ptr != p.data() + p.size() || value < 1 || value > 65535) {
      if (error) *error = "invalid port";
      return false;
    }
    port = value;
  }
  return true;
}

std::string withPort(const std::string& host, int port) {
  return port ? host + ":" + std::to_string(port) : host;
}

}  // namespace

Json DnsPlan::toJson() const {
  Json j = {
      {"mode", mode},
      {"dohTemplates", dohTemplates},
      {"needsNet", needsNet},
      {"summary", summary},
  };
  if (endpoint) {
    j["endpoint"] = {
        {"id", endpoint->id},
        {"name", endpoint->name},
        {"protocol", DnsCatalog::protocolName(endpoint->protocol)},
        {"address", endpoint->address},
        {"bootstrap", endpoint->bootstrap},
    };
  } else {
    j["endpoint"] = nullptr;
  }
  return j;
}

DnsCatalog::DnsCatalog() { parse(embedded::dnsProviders()); }

std::optional<std::string> DnsCatalog::loadOverride(std::string_view json) {
  DnsCatalog next;
  next.categories_ = Json::array();
  next.variants_.clear();
  if (auto err = next.parse(json)) return err;
  if (next.variants_.empty()) return "catalog has no providers";
  categories_ = std::move(next.categories_);
  variants_ = std::move(next.variants_);
  return std::nullopt;
}

std::optional<std::string> DnsCatalog::parse(std::string_view json) {
  Json data = Json::parse(json, nullptr, false);
  if (data.is_discarded() || !data.is_object()) return "catalog is not valid JSON";
  auto providers = data.find("providers");
  if (providers == data.end() || !providers->is_array()) return "catalog has no provider list";

  std::vector<DnsVariant> variants;
  for (const auto& p : *providers) {
    if (!p.is_object()) continue;
    std::string pid = stringField(p, "id");
    std::string pname = stringField(p, "name");
    auto vs = p.find("variants");
    if (pid.empty() || vs == p.end() || !vs->is_array()) continue;
    for (const auto& v : *vs) {
      if (!v.is_object()) continue;
      DnsVariant d;
      d.id = stringField(v, "id");
      if (d.id.empty()) continue;
      d.providerId = pid;
      d.providerName = pname;
      d.label = v.value("label", Json::object());
      d.desc = v.value("desc", Json::object());
      d.categories = stringList(v.value("categories", Json::array()));
      d.doh = stringField(v, "doh");
      d.dot = stringField(v, "dot");
      d.doq = stringField(v, "doq");
      d.bootstrap = stringList(v.value("bootstrap", Json::array()));
      if (d.doh.empty() && d.dot.empty() && d.doq.empty()) continue;
      variants.push_back(std::move(d));
    }
  }
  categories_ = data.value("categories", Json::array());
  variants_ = std::move(variants);
  return std::nullopt;
}

const DnsVariant* DnsCatalog::find(std::string_view id) const {
  for (const auto& v : variants_) {
    if (v.id == id) return &v;
  }
  return nullptr;
}

Json DnsCatalog::describe() const {
  Json providers = Json::array();
  for (const auto& v : variants_) {
    Json* group = nullptr;
    for (auto& g : providers) {
      if (g["id"] == v.providerId) group = &g;
    }
    if (!group) {
      providers.push_back({{"id", v.providerId}, {"name", v.providerName}, {"variants", Json::array()}});
      group = &providers.back();
    }
    Json protocols = Json::array();
    if (!v.doh.empty()) protocols.push_back("doh");
    if (!v.dot.empty()) protocols.push_back("dot");
    if (!v.doq.empty()) protocols.push_back("doq");
    (*group)["variants"].push_back({
        {"id", v.id},
        {"label", v.label},
        {"desc", v.desc},
        {"categories", v.categories},
        {"protocols", protocols},
        {"doh", v.doh},
        {"dot", v.dot},
        {"doq", v.doq},
    });
  }
  return {{"categories", categories_}, {"providers", providers}};
}

std::optional<DnsEndpoint> DnsCatalog::endpointFor(std::string_view variantId, DnsProtocol protocol) const {
  const DnsVariant* v = find(variantId);
  if (!v) return std::nullopt;
  const std::string* address = nullptr;
  switch (protocol) {
    case DnsProtocol::kDoh: address = &v->doh; break;
    case DnsProtocol::kDot: address = &v->dot; break;
    case DnsProtocol::kDoq: address = &v->doq; break;
  }
  if (!address || address->empty()) return std::nullopt;
  DnsEndpoint e;
  e.id = v->id;
  e.name = v->providerName;
  if (auto label = v->label.find("en"); label != v->label.end() && label->is_string()) {
    e.name += " " + label->get<std::string>();
  }
  e.protocol = protocol;
  e.address = *address;
  e.bootstrap = v->bootstrap;
  return e;
}

DnsPlan DnsCatalog::planFor(const DnsEndpoint& endpoint, bool strict) {
  DnsPlan plan;
  plan.mode = strict ? "secure" : "automatic";
  plan.endpoint = endpoint;
  if (endpoint.protocol == DnsProtocol::kDoh) {
    plan.dohTemplates = endpoint.address;
  } else {
    plan.needsNet = true;
  }
  plan.summary = endpoint.name + " (" + protocolName(endpoint.protocol) + ")";
  return plan;
}

DnsPlan DnsCatalog::plan(const Settings& settings) const {
  std::string mode = settings.get("dns.mode").get<std::string>();

  if (mode == "off") {
    DnsPlan p;
    p.mode = "system";
    p.summary = "system resolver";
    return p;
  }

  if (mode == "provider") {
    std::string id = settings.get("dns.provider").get<std::string>();
    auto protocol = protocolFromName(settings.get("dns.protocol").get<std::string>()).value_or(DnsProtocol::kDoh);
    auto endpoint = endpointFor(id, protocol);
    // A provider may not offer the chosen protocol; DoH is the common ground.
    if (!endpoint && protocol != DnsProtocol::kDoh) endpoint = endpointFor(id, DnsProtocol::kDoh);
    if (endpoint) return planFor(*endpoint, true);
  }

  if (mode == "custom") {
    std::string id = settings.get("dns.customId").get<std::string>();
    for (const auto& entry : settings.get("dns.custom")) {
      if (!entry.is_object() || stringField(entry, "id") != id) continue;
      if (auto endpoint = parseCustom(stringField(entry, "address"))) {
        endpoint->id = id;
        if (auto name = stringField(entry, "name"); !name.empty()) endpoint->name = name;
        return planFor(*endpoint, true);
      }
    }
  }

  // "auto", or a provider / custom entry that no longer resolves.
  DnsPlan p;
  p.mode = "automatic";
  p.summary = mode == "auto" ? "automatic" : "automatic (selected server unavailable)";
  return p;
}

std::optional<DnsEndpoint> DnsCatalog::parseCustom(std::string_view input, std::string* error) {
  std::string text = trim(input);
  if (text.empty()) {
    if (error) *error = "address is empty";
    return std::nullopt;
  }

  DnsEndpoint e;
  std::string lower = toLower(text);
  std::string_view rest;

  if (startsWith(lower, "https://")) {
    e.protocol = DnsProtocol::kDoh;
    rest = std::string_view(text).substr(8);
    auto slash = rest.find('/');
    std::string_view authority = rest.substr(0, slash);
    std::string path = slash == std::string_view::npos ? "/dns-query" : std::string(rest.substr(slash));
    if (authority.find('@') != std::string_view::npos) {
      if (error) *error = "credentials in DNS URLs are not supported";
      return std::nullopt;
    }
    std::string host;
    int port = 0;
    if (!parseAuthority(authority, host, port, error)) return std::nullopt;
    if (path.find_first_of(" \t\r\n") != std::string::npos) {
      if (error) *error = "invalid path";
      return std::nullopt;
    }
    e.address = "https://" + withPort(host, port) + path;
  } else if (startsWith(lower, "http://")) {
    if (error) *error = "plain HTTP is not encrypted; use https://";
    return std::nullopt;
  } else {
    if (startsWith(lower, "tls://")) {
      e.protocol = DnsProtocol::kDot;
      rest = std::string_view(text).substr(6);
    } else if (startsWith(lower, "quic://")) {
      e.protocol = DnsProtocol::kDoq;
      rest = std::string_view(text).substr(7);
    } else if (lower.find("://") != std::string::npos) {
      if (error) *error = "unsupported scheme; use https://, tls:// or quic://";
      return std::nullopt;
    } else {
      e.protocol = DnsProtocol::kDot;
      rest = text;
    }
    while (!rest.empty() && rest.back() == '/') rest.remove_suffix(1);
    if (rest.find('/') != std::string_view::npos) {
      if (error) *error = "DoT and DoQ addresses cannot have a path";
      return std::nullopt;
    }
    std::string host;
    int port = 0;
    if (!parseAuthority(rest, host, port, error)) return std::nullopt;
    e.address = withPort(host, port);
  }

  e.id = "custom-" + sha256Hex(e.address).substr(0, 12);
  e.name = e.address;
  return e;
}

std::string DnsCatalog::protocolName(DnsProtocol p) {
  switch (p) {
    case DnsProtocol::kDoh: return "doh";
    case DnsProtocol::kDot: return "dot";
    case DnsProtocol::kDoq: return "doq";
  }
  return "doh";
}

std::optional<DnsProtocol> DnsCatalog::protocolFromName(std::string_view name) {
  if (name == "doh") return DnsProtocol::kDoh;
  if (name == "dot") return DnsProtocol::kDot;
  if (name == "doq") return DnsProtocol::kDoq;
  return std::nullopt;
}

}  // namespace liora

#include "liora/kernel.hpp"

#include <algorithm>

#include "liora/settings.hpp"
#include "liora/util.hpp"

namespace liora {

namespace {

constexpr std::pair<uint32_t, const char*> kCapList[] = {
    {kCapSecureDns, "secureDns"}, {kCapHttpsRecords, "httpsRecords"},
    {kCapEch, "ech"},             {kCapForceQuic, "forceQuic"},
    {kCapProxy, "proxy"},         {kCapExtensions, "extensions"},
    {kCapDevTools, "devtools"},   {kCapIsolatedProfiles, "isolatedProfiles"},
    {kCapContentControls, "contentControls"},
};

std::string chromiumMode(const std::string& mode) {
  if (mode == "system" || mode == "off") return "off";
  if (mode == "secure") return "secure";
  return "automatic";
}

void addOnce(std::vector<std::string>& list, const char* value) {
  if (std::find(list.begin(), list.end(), value) == list.end()) list.emplace_back(value);
}

}  // namespace

Json KernelInfo::toJson() const {
  return {{"id", id},
          {"name", name},
          {"version", version},
          {"component", component},
          {"caps", KernelManager::capNames(caps)},
          {"available", available},
          {"netNeedsRestart", netNeedsRestart}};
}

Json KernelNetSetup::toJson() const {
  return {{"kernel", kernel},     {"dnsMode", dnsMode},       {"dohTemplates", dohTemplates},
          {"proxy", proxy},       {"switches", switches},     {"unsupported", unsupported},
          {"usesNet", usesNet},   {"netMissing", netMissing}};
}

KernelManager::KernelManager() {
  KernelInfo system;
  system.id = kSystem;
  system.name = "System WebView";
  system.caps = kCapProxy;
  system.available = true;
  kernels_.push_back(std::move(system));

  KernelInfo cef;
  cef.id = kCef;
  cef.name = "Chromium (CEF)";
  cef.component = "kernel-cef";
  // No extension runtime exists yet, so kCapExtensions stays off: the panel
  // must not offer a capability the kernel cannot deliver.
  cef.caps = kCapSecureDns | kCapHttpsRecords | kCapEch | kCapForceQuic | kCapProxy | kCapDevTools |
             kCapIsolatedProfiles;
  cef.netNeedsRestart = true;
  kernels_.push_back(std::move(cef));
}

void KernelManager::registerKernel(KernelInfo info) {
  std::lock_guard lock(mutex_);
  for (auto& k : kernels_) {
    if (k.id == info.id) {
      k = std::move(info);
      return;
    }
  }
  kernels_.push_back(std::move(info));
}

bool KernelManager::setAvailable(std::string_view id, bool available, std::string_view version) {
  std::lock_guard lock(mutex_);
  for (auto& k : kernels_) {
    if (k.id != id) continue;
    k.available = available;
    if (!version.empty()) k.version = std::string(version);
    return true;
  }
  return false;
}

std::optional<KernelInfo> KernelManager::find(std::string_view id) const {
  std::lock_guard lock(mutex_);
  for (const auto& k : kernels_) {
    if (k.id == id) return k;
  }
  return std::nullopt;
}

std::vector<KernelInfo> KernelManager::list() const {
  std::lock_guard lock(mutex_);
  return kernels_;
}

void KernelManager::attach(std::shared_ptr<IKernel> kernel) {
  if (!kernel) return;
  KernelInfo info = kernel->info();
  info.available = true;
  std::string id = info.id;
  registerKernel(std::move(info));
  std::lock_guard lock(mutex_);
  instances_[id] = std::move(kernel);
}

void KernelManager::detach(std::string_view id) {
  std::lock_guard lock(mutex_);
  if (auto it = instances_.find(id); it != instances_.end()) instances_.erase(it);
}

std::shared_ptr<IKernel> KernelManager::instance(std::string_view id) const {
  std::lock_guard lock(mutex_);
  auto it = instances_.find(id);
  return it == instances_.end() ? nullptr : it->second;
}

std::string KernelManager::resolve(const Settings& settings, const Scope& scope) const {
  Json value = settings.get("kernel.default", scope);
  if (value.is_string()) {
    if (auto k = find(value.get<std::string>()); k && k->available) return k->id;
  }
  return kSystem;
}

KernelNetSetup KernelManager::netSetup(const KernelInfo& kernel, const NetConfig& config,
                                       const NetRuntime& runtime) {
  KernelNetSetup s;
  s.kernel = kernel.id;

  Json rules = config.net.is_object() ? config.net.value("rules", Json::array()) : Json::array();
  bool anyEch = false;
  bool anyForce = false;
  for (const auto& r : rules) {
    anyEch = anyEch || r.value("ech", false);
    anyForce = anyForce || r.value("h3", std::string()) == "force";
  }
  bool encrypted = config.dns.endpoint.has_value();
  bool native = kernel.has(kCapSecureDns);
  bool proxy = kernel.has(kCapProxy);

  // Kernels without their own DoH need liora-net for any encrypted DNS.
  bool needNet = config.needsNet || (!native && encrypted);

  if (native) {
    bool netReady = runtime.running && runtime.dohPort != 0 && !runtime.spki.empty();
    if (needNet && netReady) {
      s.usesNet = true;
      s.dnsMode = "secure";
      s.dohTemplates = "https://127.0.0.1:" + std::to_string(runtime.dohPort) + "/dns-query";
      // Trusts only the local resolver's key; other certificates are still verified.
      s.switches.push_back("--ignore-certificate-errors-spki-list=" + runtime.spki);
    } else {
      s.netMissing = needNet;
      s.dnsMode = chromiumMode(config.dns.mode);
      s.dohTemplates = config.dns.dohTemplates;
      // A DoT/DoQ choice has no DoH template; strict mode would break every lookup.
      if (s.dnsMode == "secure" && s.dohTemplates.empty()) s.dnsMode = "automatic";
    }
  } else {
    s.dnsMode = "system";
    if (needNet && proxy) {
      if (runtime.running && runtime.proxyPort != 0) {
        s.usesNet = true;
        s.proxy = "http://127.0.0.1:" + std::to_string(runtime.proxyPort);
      } else {
        s.netMissing = true;
      }
    }
  }

  if (kernel.has(kCapForceQuic) && !config.forceQuicOrigins.empty()) {
    s.switches.push_back("--enable-quic");
    s.switches.push_back("--origin-to-force-quic-on=" + join(config.forceQuicOrigins, ","));
  }

  if ((encrypted || !rules.empty()) && !native && !proxy) addOnce(s.unsupported, "dns");
  if (anyEch && !kernel.has(kCapEch)) addOnce(s.unsupported, "ech");
  if (anyForce && !kernel.has(kCapForceQuic)) addOnce(s.unsupported, "forceQuic");
  if (!rules.empty() && !kernel.has(kCapHttpsRecords)) addOnce(s.unsupported, "httpsRecords");
  return s;
}

std::vector<std::string> KernelManager::capNames(uint32_t caps) {
  std::vector<std::string> out;
  for (const auto& [bit, name] : kCapList) {
    if (caps & bit) out.emplace_back(name);
  }
  return out;
}

Json KernelManager::toJson() const {
  Json out = Json::array();
  for (const auto& k : list()) out.push_back(k.toJson());
  return out;
}

}  // namespace liora

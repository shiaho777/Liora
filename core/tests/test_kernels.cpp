#include "harness.hpp"

#include <filesystem>

#include "liora/kernel.hpp"
#include "liora/settings.hpp"

using namespace liora;

namespace {

KernelInfo findKernel(const KernelManager& kernels, const char* id) {
  auto info = kernels.find(id);
  CHECK(info.has_value());
  return *info;
}

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

}  // namespace

LIORA_TEST(kitesurf_description_is_registered) {
  KernelManager kernels;
  KernelInfo info = findKernel(kernels, KernelManager::kKitesurf);
  CHECK_EQ(info.id, std::string("kitesurf"));
  CHECK(!info.available);  // attached only when a local engine answers
  CHECK(info.component.empty());
  CHECK(!info.netNeedsRestart);
}

LIORA_TEST(kitesurf_caps_match_the_engine) {
  KernelManager kernels;
  KernelInfo info = findKernel(kernels, KernelManager::kKitesurf);
  // DevTools over CDP and a fresh isolate per page are real.
  CHECK(info.has(kCapDevTools));
  CHECK(info.has(kCapIsolatedProfiles));
  // The engine's outbound fetch has no proxy or DNS hook, and media, WebGL
  // and extensions are out of scope upstream: none of it may be advertised.
  CHECK(!info.has(kCapProxy));
  CHECK(!info.has(kCapSecureDns));
  CHECK(!info.has(kCapHttpsRecords));
  CHECK(!info.has(kCapEch));
  CHECK(!info.has(kCapForceQuic));
  CHECK(!info.has(kCapExtensions));
  CHECK(!info.has(kCapContentControls));
}

LIORA_TEST(kitesurf_net_setup_reports_unsupported_dns) {
  KernelManager kernels;
  KernelInfo info = findKernel(kernels, KernelManager::kKitesurf);
  NetConfig config;
  DnsEndpoint endpoint;
  endpoint.id = "custom";
  endpoint.address = "https://dns.example/dns-query";
  config.dns.endpoint = endpoint;
  NetRuntime runtime;
  runtime.running = true;
  runtime.proxyPort = 9999;
  KernelNetSetup setup = KernelManager::netSetup(info, config, runtime);
  // Without a proxy capability the resolver cannot serve this kernel; the
  // interface must say so instead of pretending DNS is applied.
  CHECK_EQ(setup.dnsMode, std::string("system"));
  CHECK(!setup.usesNet);
  CHECK(setup.proxy.empty());
  bool found = false;
  for (const auto& item : setup.unsupported) {
    if (item == "dns") found = true;
  }
  CHECK(found);
}

LIORA_TEST(resolve_falls_back_when_kitesurf_is_not_attached) {
  TempDir dir;
  KernelManager kernels;
  Settings settings(dir / "settings.json");
  CHECK(settings.load());
  CHECK(!settings.set("kernel.default", std::string("kitesurf")));
  Scope scope;
  CHECK_EQ(kernels.resolve(settings, scope), std::string(KernelManager::kSystem));
}
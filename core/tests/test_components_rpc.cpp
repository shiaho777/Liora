#include "harness.hpp"

#include <filesystem>

#include "liora/components.hpp"
#include "liora/core.hpp"
#include "liora/kernel.hpp"
#include "liora/rpc.hpp"

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

const char* kCatalog = R"({
  "version": 1,
  "components": [
    {
      "id": "kernel-cef", "name": "Chromium kernel", "kind": "kernel",
      "versions": [
        {
          "version": "1.0.0",
          "builds": [
            {
              "os": "macos", "arch": "arm64", "url": "https://example.invalid/cef-1.0.0.zip", "size": 2,
              "files": [{"path": "cef/libcef.dylib", "sha256": "PLACEHOLDER", "size": 2}]
            }
          ]
        }
      ]
    }
  ]
})";

std::string catalogWithHash(const std::string& hash) {
  std::string json = kCatalog;
  auto pos = json.find("PLACEHOLDER");
  json.replace(pos, std::string("PLACEHOLDER").size(), hash);
  return json;
}

}  // namespace

LIORA_TEST(components_plan_and_commit_verifies_files) {
  TempDir dir;
  ComponentManager manager(dir / "components");
  std::string payload = "hi";
  CHECK(!manager.loadCatalog(catalogWithHash(sha256Hex(payload))).has_value());

  std::string error;
  auto plan = manager.plan("kernel-cef", "", "macos", "arm64", &error);
  CHECK(plan.has_value());
  CHECK_EQ(plan->url, std::string("https://example.invalid/cef-1.0.0.zip"));
  CHECK_EQ(plan->files.size(), size_t{1});

  fs::create_directories(plan->stagingDir / "cef");
  CHECK(writeFileAtomic(plan->stagingDir / "cef" / "libcef.dylib", payload));

  CHECK(!manager.commit(*plan).has_value());
  auto current = manager.current("kernel-cef");
  CHECK(current.has_value());
  CHECK_EQ(current->version, std::string("1.0.0"));
  CHECK(manager.available("kernel-cef"));

  // The package moved out of staging into the versioned directory.
  CHECK(!fs::exists(plan->stagingDir));
}

LIORA_TEST(components_reject_tampered_or_incomplete_packages) {
  TempDir dir;
  ComponentManager manager(dir / "components");
  CHECK(!manager.loadCatalog(catalogWithHash(sha256Hex("hi"))).has_value());
  std::string error;
  auto plan = manager.plan("kernel-cef", "", "macos", "arm64", &error);
  CHECK(plan.has_value());

  // Wrong contents.
  fs::create_directories(plan->stagingDir / "cef");
  CHECK(writeFileAtomic(plan->stagingDir / "cef" / "libcef.dylib", "EVIL"));
  CHECK(manager.commit(*plan).has_value());
  CHECK(!manager.current("kernel-cef").has_value());

  // Right contents but a path that escapes the staging directory.
  std::error_code ec;
  fs::remove_all(plan->stagingDir, ec);
  auto escape = *plan;
  escape.files[0].path = "../escape.dylib";
  CHECK(writeFileAtomic(plan->stagingDir / ".." / "escape.dylib", "hi"));
  CHECK(manager.commit(escape).has_value());
}

LIORA_TEST(components_unknown_platform_has_no_plan) {
  TempDir dir;
  ComponentManager manager(dir / "components");
  CHECK(!manager.loadCatalog(catalogWithHash(sha256Hex("hi"))).has_value());
  std::string error;
  CHECK(!manager.plan("kernel-cef", "", "linux", "x64", &error).has_value());
  CHECK(error.find("linux") != std::string::npos);
}

LIORA_TEST(components_activate_rollback_and_remove) {
  TempDir dir;
  ComponentManager manager(dir / "components");

  Json catalog = Json::parse(catalogWithHash(sha256Hex("aa")), nullptr, false);
  Json second = catalog["components"][0]["versions"][0];
  second["version"] = "2.0.0";
  second["builds"][0]["files"][0]["sha256"] = sha256Hex("bb");
  catalog["components"][0]["versions"].push_back(second);
  CHECK(!manager.loadCatalog(catalog.dump()).has_value());

  for (const auto& [version, payload] : {std::pair{"1.0.0", "aa"}, std::pair{"2.0.0", "bb"}}) {
    std::string error;
    auto plan = manager.plan("kernel-cef", version, "macos", "arm64", &error);
    CHECK(plan.has_value());
    fs::create_directories(plan->stagingDir / "cef");
    CHECK(writeFileAtomic(plan->stagingDir / "cef" / "libcef.dylib", payload));
    CHECK(!manager.commit(*plan).has_value());
  }

  CHECK_EQ(manager.current("kernel-cef")->version, std::string("2.0.0"));
  CHECK(manager.activate("kernel-cef", "1.0.0"));
  CHECK_EQ(manager.current("kernel-cef")->version, std::string("1.0.0"));
  CHECK(manager.rollback("kernel-cef"));
  CHECK_EQ(manager.current("kernel-cef")->version, std::string("2.0.0"));

  CHECK(manager.remove("kernel-cef", "1.0.0"));
  CHECK_EQ(manager.installed().size(), size_t{1});
}

LIORA_TEST(kernel_manager_resolution_and_setup) {
  KernelManager kernels;
  TempDir dir;
  Settings settings(dir / "settings.json");

  CHECK_EQ(kernels.resolve(settings, Scope{}), std::string("system"));
  // cef is known but not installed, so it must not be selected.
  CHECK(!settings.set("kernel.default", "cef"));
  CHECK_EQ(kernels.resolve(settings, Scope{}), std::string("system"));
  CHECK(kernels.setAvailable("cef", true, "126.0"));
  CHECK_EQ(kernels.resolve(settings, Scope{}), std::string("cef"));
  // Per-site overrides work too.
  CHECK(!settings.set("kernel.default", "system", Scope{"example.com", 0}));
  CHECK_EQ(kernels.resolve(settings, Scope{"example.com", 0}), std::string("system"));
  CHECK_EQ(kernels.resolve(settings, Scope{}), std::string("cef"));
}

LIORA_TEST(kernel_setup_points_cef_at_the_local_resolver) {
  KernelManager kernels;
  auto cef = kernels.find("cef");
  CHECK(cef.has_value());

  NetConfig config;
  config.dns.mode = "secure";
  config.needsNet = true;
  NetRuntime runtime;
  runtime.running = true;
  runtime.dohPort = 5300;
  runtime.spki = "abc=";

  KernelNetSetup setup = KernelManager::netSetup(*cef, config, runtime);
  CHECK(setup.usesNet);
  CHECK_EQ(setup.dnsMode, std::string("secure"));
  CHECK_EQ(setup.dohTemplates, std::string("https://127.0.0.1:5300/dns-query"));
  CHECK_EQ(setup.switches.size(), size_t{1});
  CHECK(setup.switches[0].find("--ignore-certificate-errors-spki-list=abc=") == 0);
  CHECK(setup.unsupported.empty());
}

LIORA_TEST(kernel_setup_falls_back_when_net_is_not_running) {
  KernelManager kernels;
  auto cef = kernels.find("cef");
  NetConfig config;
  config.dns.mode = "secure";
  config.dns.dohTemplates = "https://dns.example/dns-query";
  config.needsNet = true;

  KernelNetSetup setup = KernelManager::netSetup(*cef, config, NetRuntime{});
  CHECK(setup.netMissing);
  CHECK(!setup.usesNet);
  // Without liora-net the kernel still gets a working DoH template.
  CHECK_EQ(setup.dohTemplates, std::string("https://dns.example/dns-query"));
}

LIORA_TEST(kernel_setup_reports_what_system_webview_cannot_do) {
  KernelManager kernels;
  auto system = kernels.find("system");
  CHECK(system.has_value());

  NetConfig config;
  config.dns.mode = "secure";
  config.dns.dohTemplates = "https://dns.example/dns-query";
  config.needsNet = true;
  config.net["rules"] = Json::array({{{"h3", "force"}, {"ech", true}}});

  NetRuntime runtime;
  runtime.running = true;
  runtime.proxyPort = 5301;

  KernelNetSetup setup = KernelManager::netSetup(*system, config, runtime);
  CHECK(setup.usesNet);
  CHECK_EQ(setup.proxy, std::string("http://127.0.0.1:5301"));
  CHECK_EQ(setup.dnsMode, std::string("system"));
  CHECK(setup.unsupported.size() >= 2);
}

LIORA_TEST(rpc_returns_structured_errors) {
  RpcRouter rpc;
  rpc.add("boom", [](const Json&) -> Json { rpcFail(kRpcPermission, "nope"); });
  rpc.add("echo", [](const Json& params) { return params; });

  Json ok = rpc.handle(Json{{"jsonrpc", "2.0"}, {"id", 1}, {"method", "echo"}, {"params", {{"a", 1}}}});
  CHECK_EQ(ok["id"], 1);
  CHECK_EQ(ok["result"]["a"], 1);

  Json missing = rpc.handle(Json{{"jsonrpc", "2.0"}, {"id", 2}, {"method", "missing"}});
  CHECK_EQ(missing["error"]["code"].get<int>(), int(kRpcMethodNotFound));

  Json failure = rpc.handle(Json{{"jsonrpc", "2.0"}, {"id", 3}, {"method", "boom"}});
  CHECK_EQ(failure["error"]["code"].get<int>(), int(kRpcPermission));
  CHECK_EQ(failure["error"]["message"], std::string("nope"));

  Json bad = rpc.handle(Json{{"jsonrpc", "2.0"}, {"id", 4}, {"method", "echo"}});
  CHECK_EQ(bad["result"].is_object(), true);

  Json invalid = rpc.handle(Json::array());
  CHECK_EQ(invalid["error"]["code"].get<int>(), int(kRpcInvalidRequest));
}

LIORA_TEST(rpc_notifications_have_no_response) {
  RpcRouter rpc;
  int calls = 0;
  rpc.add("fire", [&](const Json&) {
    ++calls;
    return Json{{"ok", true}};
  });
  Json response = rpc.handle(Json{{"jsonrpc", "2.0"}, {"method", "fire"}});
  CHECK(response.is_null());
  CHECK_EQ(calls, 1);
}

LIORA_TEST(rpc_batch) {
  RpcRouter rpc;
  rpc.add("one", [](const Json&) { return Json(1); });
  Json response = rpc.handleDocument(Json::array({
      Json{{"jsonrpc", "2.0"}, {"id", "a"}, {"method", "one"}},
      Json{{"jsonrpc", "2.0"}, {"id", "b"}, {"method", "one"}},
  }));
  CHECK_EQ(response.size(), size_t{2});
}

LIORA_TEST(core_end_to_end_over_rpc) {
  TempDir dir;
  Core::Options options;
  options.paths = CorePaths::forProfile(dir.path);
  Core core(options);
  std::string error;
  CHECK(!core.start());

  Json info = core.handleJson(Json{{"jsonrpc", "2.0"}, {"id", 1}, {"method", "core.info"}});
  CHECK_EQ(info["result"]["platform"].is_string(), true);

  // DNS catalog from the embedded data file.
  Json catalog = core.handleJson(Json{{"jsonrpc", "2.0"}, {"id", 2}, {"method", "dns.catalog"}});
  CHECK(catalog["result"]["providers"].size() > 5);
  CHECK(catalog["result"]["categories"].size() >= 5);

  // Switching provider is visible in net.plan.
  core.handleJson(Json{{"jsonrpc", "2.0"}, {"id", 3}, {"method", "settings.set"},
                       {"params", {{"key", "dns.mode"}, {"value", "provider"}}}});
  core.handleJson(Json{{"jsonrpc", "2.0"}, {"id", 4}, {"method", "settings.set"},
                       {"params", {{"key", "dns.provider"}, {"value", "quad9"}}}});
  Json plan = core.handleJson(Json{{"jsonrpc", "2.0"}, {"id", 5}, {"method", "net.plan"}});
  CHECK_EQ(plan["result"]["dns"]["dohTemplates"], std::string("https://dns.quad9.net/dns-query"));

  // Rules affect the net config and the per-host lookup.
  Json added = core.handleJson(Json{{"jsonrpc", "2.0"}, {"id", 6}, {"method", "rules.add"},
                                    {"params", {{"preset", "x"}}}});
  CHECK(added["result"].is_object());
  Json host = core.handleJson(Json{{"jsonrpc", "2.0"}, {"id", 7}, {"method", "rules.match"},
                                   {"params", {{"host", "x.com"}}}});
  CHECK_EQ(host["result"]["matched"], true);

  // Bookmarks survive a restart because they are not owned by a kernel.
  core.handleJson(Json{{"jsonrpc", "2.0"}, {"id", 8}, {"method", "bookmarks.add"},
                       {"params", {{"title", "Example"}, {"url", "https://example.com"}}}});
  core.stop();

  Core reopened(options);
  CHECK(!reopened.start());
  Json tree = reopened.handleJson(Json{{"jsonrpc", "2.0"}, {"id", 9}, {"method", "bookmarks.tree"}});
  CHECK_EQ(tree["result"]["other"]["children"].size(), size_t{1});
  reopened.stop();
}

LIORA_TEST(core_permissions_over_rpc) {
  TempDir dir;
  Core::Options options;
  options.paths = CorePaths::forProfile(dir.path);
  options.skipAutoBackup = true;
  Core core(options);
  CHECK(!core.start());

  core.storage().setPermission("https://meet.example", "camera", "allow");

  Json listed = core.handleJson(Json{{"jsonrpc", "2.0"}, {"id", 1}, {"method", "permissions.list"},
                                     {"params", Json::object()}});
  CHECK_EQ(listed["result"].size(), size_t{1});
  CHECK_EQ(listed["result"][0]["origin"], std::string("https://meet.example"));
  CHECK_EQ(listed["result"][0]["name"], std::string("camera"));
  CHECK_EQ(listed["result"][0]["value"], std::string("allow"));

  // An empty value is how the interface revokes a remembered decision.
  core.handleJson(Json{{"jsonrpc", "2.0"},
                       {"id", 2},
                       {"method", "permissions.set"},
                       {"params", {{"origin", "https://meet.example"}, {"name", "camera"}, {"value", ""}}}});

  Json cleared = core.handleJson(Json{{"jsonrpc", "2.0"}, {"id", 3}, {"method", "permissions.list"},
                                      {"params", Json::object()}});
  CHECK_EQ(cleared["result"].size(), size_t{0});
  core.stop();
}

LIORA_TEST(core_download_history_over_rpc) {
  TempDir dir;
  Core::Options options;
  options.paths = CorePaths::forProfile(dir.path);
  options.skipAutoBackup = true;
  Core core(options);
  CHECK(!core.start());

  DownloadRecord record;
  record.id = "download-1";
  record.filename = "report.pdf";
  record.path = "/Users/example/Downloads/report.pdf";
  record.state = "finished";
  record.bytes = 4096;
  record.created = nowMs();
  core.storage().saveDownload(record);

  Json listed = core.handleJson(
      Json{{"jsonrpc", "2.0"}, {"id", 1}, {"method", "downloads.list"}, {"params", Json::object()}});
  CHECK_EQ(listed["result"].size(), size_t{1});
  CHECK_EQ(listed["result"][0]["filename"], std::string("report.pdf"));
  CHECK_EQ(listed["result"][0]["bytes"], int64_t{4096});

  core.handleJson(Json{{"jsonrpc", "2.0"}, {"id", 2}, {"method", "downloads.clear"}, {"params", Json::object()}});
  Json cleared = core.handleJson(
      Json{{"jsonrpc", "2.0"}, {"id", 3}, {"method", "downloads.list"}, {"params", Json::object()}});
  CHECK_EQ(cleared["result"].size(), size_t{0});
  core.stop();
}

LIORA_TEST(core_net_runtime_roundtrip) {
  TempDir dir;
  Core::Options options;
  options.paths = CorePaths::forProfile(dir.path);
  options.skipAutoBackup = true;
  Core core(options);
  CHECK(!core.start());

  Json set = core.handleJson(Json{
      {"jsonrpc", "2.0"},
      {"id", 1},
      {"method", "net.setRuntime"},
      {"params",
       {{"running", true},
        {"dohPort", 5300},
        {"proxyPort", 5301},
        {"diagPort", 5302},
        {"spki", "abc="},
        {"appliedConfig", "{\"rules\":[]}"}}}});
  CHECK_EQ(set["result"]["ok"], true);

  Json got =
      core.handleJson(Json{{"jsonrpc", "2.0"}, {"id", 2}, {"method", "net.runtime"}});
  CHECK_EQ(got["result"]["proxyPort"].get<int>(), 5301);
  CHECK_EQ(got["result"]["diagPort"].get<int>(), 5302);
  CHECK_EQ(got["result"]["appliedConfig"].get<std::string>(), std::string("{\"rules\":[]}"));
  core.stop();
}

LIORA_TEST(core_net_applied_snapshot_survives_restart) {
  TempDir dir;
  Core::Options options;
  options.paths = CorePaths::forProfile(dir.path);
  options.skipAutoBackup = true;
  Core core(options);
  CHECK(!core.start());

  Json recorded = core.handleJson(Json{
      {"jsonrpc", "2.0"},
      {"id", 1},
      {"method", "net.markApplied"},
      {"params",
       {{"proxy", "http://127.0.0.1:5300"},
        {"provider", "Quad9"},
        {"protocol", "doh"},
        {"mode", "secure"},
        {"at", 1700000000000LL}}}});
  CHECK_EQ(recorded["result"]["provider"].get<std::string>(), std::string("Quad9"));

  Json live =
      core.handleJson(Json{{"jsonrpc", "2.0"}, {"id", 2}, {"method", "net.lastApplied"}});
  CHECK_EQ(live["result"]["proxy"].get<std::string>(), std::string("http://127.0.0.1:5300"));
  core.stop();

  // The snapshot is on disk, so it survives a restart.
  Core reopened(options);
  CHECK(!reopened.start());
  Json restored =
      reopened.handleJson(Json{{"jsonrpc", "2.0"}, {"id", 3}, {"method", "net.lastApplied"}});
  CHECK_EQ(restored["result"]["provider"].get<std::string>(), std::string("Quad9"));
  CHECK_EQ(restored["result"]["protocol"].get<std::string>(), std::string("doh"));
  CHECK_EQ(restored["result"]["at"].get<int64_t>(), int64_t{1700000000000LL});
  reopened.stop();
}

LIORA_TEST(core_reports_bad_requests_as_json) {
  TempDir dir;
  Core::Options options;
  options.paths = CorePaths::forProfile(dir.path);
  options.skipAutoBackup = true;
  Core core(options);
  CHECK(!core.start());
  std::string response = core.handle("{not json");
  CHECK(response.find("-32700") != std::string::npos);
  CHECK(response.find("invalid JSON") != std::string::npos);
  core.stop();
}

#include "liora/core.hpp"

#include <algorithm>

#include "embedded.hpp"
#include "liora/paths.hpp"

namespace liora {

namespace {

Json customDnsEntry(const DnsEndpoint& endpoint, const Json& name) {
  return {{"id", endpoint.id},
          {"name", name.is_string() ? name.get<std::string>() : endpoint.name},
          {"address", endpoint.address},
          {"protocol", DnsCatalog::protocolName(endpoint.protocol)}};
}

}  // namespace

CorePaths CorePaths::forProfile(const fs::path& root) {
  CorePaths p;
  p.root = root;
  p.settings = root / "settings.json";
  p.database = root / "liora.db";
  p.components = root / "components";
  p.backups = root / "backups";
  return p;
}

std::string Core::appVersion() {
#ifdef LIORA_VERSION
  return LIORA_VERSION;
#else
  return "0.0.0";
#endif
}

Core::Core(Options options)
    : options_(std::move(options)),
      paths_(options_.paths),
      settings_(paths_.settings),
      storage_(paths_.database),
      components_(paths_.components) {
  dns_ = DnsCatalog();
  registerCoreMethods();
  registerDataMethods();
  registerComponentMethods();
  registerBackupSections();
}

Core::~Core() { stop(); }

std::optional<std::string> Core::start() {
  std::string error;
  if (!settings_.load(&error)) return "settings: " + error;
  reloadRules();
  if (!storage_.open(&error)) return "database: " + error;

  if (!options_.builtinCatalog.empty()) {
    if (auto catalogError = components_.loadCatalog(options_.builtinCatalog)) {
      // A broken built-in catalog must not stop the browser from starting.
      rpc_.notify("core.warning", {{"source", "components"}, {"message", *catalogError}});
    }
  }

  fs::path catalog = paths_.root / "components.json";
  if (fs::exists(catalog)) components_.loadCatalogFile(catalog);

  if (auto text = readFile(paths_.root / "net-applied.json")) {
    Json doc = Json::parse(*text, nullptr, false);
    if (doc.is_object()) {
      std::lock_guard lock(appliedMutex_);
      applied_ = std::move(doc);
    }
  }
  if (auto text = readFile(paths_.root / "net-resolver.json")) {
    Json doc = Json::parse(*text, nullptr, false);
    if (doc.is_object()) {
      std::lock_guard lock(resolverMutex_);
      resolverStatus_ = std::move(doc);
    }
  }

  // Keep a daily automatic backup without asking.
  if (!options_.skipAutoBackup) {
    std::string backupError;
    backup_.autoBackup(paths_.backups, 7, "auto", &backupError);
  }
  started_ = true;
  return std::nullopt;
}

void Core::stop() {
  if (!started_) return;
  settings_.save();
  storage_.close();
  started_ = false;
}

void Core::setNotificationSink(RpcRouter::Sink sink) { rpc_.setSink(std::move(sink)); }

void Core::setNetRuntime(const NetRuntime& runtime) {
  std::lock_guard lock(netMutex_);
  net_ = runtime;
}

NetRuntime Core::netRuntime() const {
  std::lock_guard lock(netMutex_);
  return net_;
}

void Core::attachKernel(std::shared_ptr<IKernel> kernel) { kernels_.attach(std::move(kernel)); }

void Core::detachKernel(std::string_view id) { kernels_.detach(id); }

KernelNetSetup Core::applyNetwork(std::string_view kernelId) {
  auto info = kernels_.find(kernelId);
  if (!info) {
    KernelInfo fallback;
    fallback.id = std::string(kernelId);
    info = fallback;
  }
  return KernelManager::netSetup(*info, currentNetConfig(), netRuntime());
}

NetConfig Core::currentNetConfig() { return NetPlanner::build(rules_, dns_, settings_); }

std::optional<std::string> Core::saveRules() {
  Json rules = rules_.toJson();
  if (auto error = settings_.set("net.rules", rules)) return error;
  persistSettings();
  rpc_.notify("net.rulesChanged", nullptr);
  return std::nullopt;
}

void Core::reloadRules() {
  Json stored = settings_.get("net.rules");
  if (auto error = rules_.load(stored)) {
    // Keep the previous rules rather than dropping the user's configuration.
    rpc_.notify("core.warning", {{"source", "rules"}, {"message", *error}});
  }
}

void Core::persistSettings() {
  std::string error;
  if (!settings_.save(&error)) {
    rpc_.notify("core.warning", {{"source", "settings"}, {"message", error}});
  }
}

Scope Core::scopeFrom(const Json& params) const {
  Scope scope;
  if (auto site = params::optionalString(params, "site"); site && !site->empty()) {
    scope.site = Settings::normalizeSite(*site);
  }
  if (auto tab = params::optionalInteger(params, "tab"); tab) scope.tab = *tab;
  return scope;
}

void Core::registerCoreMethods() {
  rpc_.setNamespaceDescription("core", "Application lifecycle and diagnostics");
  rpc_.setNamespaceDescription("settings", "Layered settings");
  rpc_.setNamespaceDescription("dns", "DNS provider catalog and custom servers");
  rpc_.setNamespaceDescription("rules", "Per-domain network rules");
  rpc_.setNamespaceDescription("net", "Effective network configuration");
  rpc_.setNamespaceDescription("bookmarks", "Bookmarks");
  rpc_.setNamespaceDescription("history", "Browsing history");
  rpc_.setNamespaceDescription("backup", "Backups");
  rpc_.setNamespaceDescription("migrate", "Importing data from other browsers");
  rpc_.setNamespaceDescription("session", "Tabs restored on the next start");
  rpc_.setNamespaceDescription("diagnostics", "Saved diagnostic reports");
  rpc_.setNamespaceDescription("kernels", "Browser kernels");
  rpc_.setNamespaceDescription("components", "Downloadable components");
  rpc_.setNamespaceDescription("permissions", "Remembered site permissions");
  rpc_.setNamespaceDescription("downloads", "Download history");

  rpc_.add("core.info", [this](const Json&) {
    NetConfig net = NetPlanner::build(rules_, dns_, settings_);
    return Json{{"version", appVersion()},
                {"profile", pathToUtf8(paths_.root)},
                {"platform", ComponentManager::platformOs()},
                {"arch", ComponentManager::platformArch()},
                {"netNeedsNet", net.needsNet},
                {"netSummary", net.dns.summary}};
  });

  rpc_.add("core.ping", [](const Json&) { return Json{{"ok", true}, {"time", nowMs()}}; });

  rpc_.add("diagnostics.recent", [this](const Json& params) {
    std::string configured = settings_.get("downloads.dir").get<std::string>();
    fs::path dir = configured.empty() ? paths::homeDir() / "Downloads" : utf8ToPath(configured);
    int limit = static_cast<int>(params::optionalInteger(params, "limit").value_or(20));
    if (limit <= 0) limit = 20;

    std::vector<std::pair<fs::file_time_type, Json>> found;
    std::error_code ec;
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
      if (!it->is_regular_file(ec)) continue;
      std::string name = pathToUtf8(it->path().filename());
      if (!startsWith(name, "liora-diagnostic-") || !endsWith(name, ".txt")) continue;
      auto modified = it->last_write_time(ec);
      if (ec) continue;
      // file_clock has no portable epoch conversion we rely on; derive the
      // wall-clock time from its age instead.
      auto age = std::chrono::duration_cast<std::chrono::milliseconds>(
          fs::file_time_type::clock::now() - modified);
      Json entry = {{"name", name},
                    {"path", pathToUtf8(it->path())},
                    {"size", static_cast<int64_t>(it->file_size(ec))},
                    {"modified", nowMs() - age.count()},
                    {"kind", "report"}};
      found.emplace_back(modified, std::move(entry));
    }
    std::sort(found.begin(), found.end(), [](const auto& a, const auto& b) {
      if (a.first != b.first) return a.first > b.first;
      return a.second.value("name", std::string()) > b.second.value("name", std::string());
    });
    Json out = Json::array();
    for (const auto& item : found) {
      if (static_cast<int>(out.size()) >= limit) break;
      out.push_back(item.second);
    }
    return out;
  });

  rpc_.add("rpc.discover", [this](const Json&) { return rpc_.describe(); });

  // ---- Settings ----

  rpc_.add("settings.describe", [this](const Json& params) {
    Json out = settings_.describe();
    std::string site = params::optionalString(params, "site").value_or("");
    if (!site.empty()) out["overrides"] = settings_.siteOverrides(site);
    return out;
  });

  rpc_.add("settings.get", [this](const Json& params) {
    Scope scope = scopeFrom(params);
    Json out = Json::object();
    if (auto key = params::optionalString(params, "key"); key && !key->empty()) {
      if (!Settings::find(*key)) rpcFail(kRpcNotFound, "unknown setting: " + *key);
      out[*key] = settings_.get(*key, scope);
      return out;
    }
    // Whole groups: {"prefix": "dns"} returns every dns.* value.
    std::string prefix = params::optionalString(params, "prefix").value_or("");
    for (const auto& def : Settings::schema()) {
      if (prefix.empty() || startsWith(def.key, prefix + ".")) out[def.key] = settings_.get(def.key, scope);
    }
    return out;
  });

  rpc_.add("settings.set", [this](const Json& params) {
    Scope scope = scopeFrom(params);
    if (auto values = params::optional(params, "values"); values && values->is_object()) {
      for (const auto& [key, value] : values->items()) {
        if (auto error = settings_.set(key, value, scope)) rpcFail(kRpcInvalidParams, *error);
      }
    } else {
      std::string key = params::string(params, "key");
      auto value = params::optional(params, "value");
      if (!value) rpcFail(kRpcInvalidParams, "missing parameter: value");
      if (auto error = settings_.set(key, *value, scope)) rpcFail(kRpcInvalidParams, *error);
    }
    persistSettings();
    rpc_.notify("settings.changed", {{"scope", scope.site}, {"tab", scope.tab}});
    return Json{{"ok", true}};
  });

  rpc_.add("settings.reset", [this](const Json& params) {
    Scope scope = scopeFrom(params);
    settings_.reset(params::string(params, "key"), scope);
    persistSettings();
    return Json{{"ok", true}};
  });

  rpc_.add("settings.clearSite", [this](const Json& params) {
    settings_.clearSite(params::string(params, "site"));
    persistSettings();
    return Json{{"ok", true}};
  });

  // ---- DNS ----

  rpc_.add("dns.catalog", [this](const Json&) {
    Json out = dns_.describe();
    out["custom"] = settings_.get("dns.custom");
    out["active"] = dns_.plan(settings_).toJson();
    return out;
  });

  rpc_.add("dns.validate", [](const Json& params) {
    std::string address = params::string(params, "address");
    std::string error;
    auto endpoint = DnsCatalog::parseCustom(address, &error);
    if (!endpoint) return Json{{"valid", false}, {"error", error}};
    return Json{{"valid", true},
                {"normalized", endpoint->address},
                {"protocol", DnsCatalog::protocolName(endpoint->protocol)},
                {"id", endpoint->id}};
  });

  rpc_.add("dns.custom.add", [this](const Json& params) {
    std::string address = params::string(params, "address");
    std::string error;
    auto endpoint = DnsCatalog::parseCustom(address, &error);
    if (!endpoint) rpcFail(kRpcInvalidParams, error);
    Json list = settings_.get("dns.custom");
    for (const auto& entry : list) {
      if (entry.value("address", "") == endpoint->address) {
        rpcFail(kRpcConflict, "this server is already in the list");
      }
    }
    Json entry = customDnsEntry(*endpoint, params::optional(params, "name").value_or(Json()));
    list.push_back(entry);
    if (auto setError = settings_.set("dns.custom", list)) rpcFail(kRpcInternalError, *setError);
    persistSettings();
    return entry;
  });

  rpc_.add("dns.custom.remove", [this](const Json& params) {
    std::string id = params::string(params, "id");
    Json list = settings_.get("dns.custom");
    Json out = Json::array();
    bool found = false;
    for (const auto& entry : list) {
      if (entry.value("id", "") == id) {
        found = true;
        continue;
      }
      out.push_back(entry);
    }
    if (!found) rpcFail(kRpcNotFound, "custom server not found");
    if (auto error = settings_.set("dns.custom", out)) rpcFail(kRpcInternalError, *error);
    if (settings_.get("dns.customId").get<std::string>() == id) settings_.set("dns.customId", "");
    persistSettings();
    return Json{{"ok", true}};
  });

  rpc_.add("dns.plan", [this](const Json&) { return dns_.plan(settings_).toJson(); });

  // ---- Rules ----

  rpc_.add("rules.list", [this](const Json&) {
    return Json{{"rules", rules_.toJson()},
                {"enabled", settings_.get("net.rulesEnabled")},
                {"echOuter", settings_.get("net.echOuter")}};
  });

  rpc_.add("rules.presets", [](const Json&) { return RuleSet::presets(); });

  rpc_.add("rules.add", [this](const Json& params) {
    std::string error;
    std::optional<NetRule> rule;
    if (auto preset = params::optionalString(params, "preset"); preset && !preset->empty()) {
      rule = RuleSet::fromPreset(*preset, &error);
    } else {
      rule = NetRule::fromJson(params, &error);
    }
    if (!rule) rpcFail(kRpcInvalidParams, error);
    if (auto addError = rules_.add(*rule)) rpcFail(kRpcConflict, *addError);
    if (auto saveError = saveRules()) rpcFail(kRpcInternalError, *saveError);
    return rule->toJson();
  });

  rpc_.add("rules.update", [this](const Json& params) {
    std::string error;
    auto rule = NetRule::fromJson(params, &error);
    if (!rule) rpcFail(kRpcInvalidParams, error);
    if (auto updateError = rules_.update(*rule)) rpcFail(kRpcNotFound, *updateError);
    if (auto saveError = saveRules()) rpcFail(kRpcInternalError, *saveError);
    return rule->toJson();
  });

  rpc_.add("rules.remove", [this](const Json& params) {
    if (!rules_.remove(params::string(params, "id"))) rpcFail(kRpcNotFound, "rule not found");
    if (auto error = saveRules()) rpcFail(kRpcInternalError, *error);
    return Json{{"ok", true}};
  });

  rpc_.add("rules.enable", [this](const Json& params) {
    std::string id = params::string(params, "id");
    bool enabled = params::boolean(params, "enabled", true);
    if (auto error = rules_.setEnabled(id, enabled)) rpcFail(kRpcNotFound, *error);
    if (auto error = saveRules()) rpcFail(kRpcInternalError, *error);
    return Json{{"ok", true}};
  });

  rpc_.add("rules.match", [this](const Json& params) {
    Scope scope = scopeFrom(params);
    auto plan = NetPlanner::forHost(rules_, dns_, settings_, params::string(params, "host"));
    if (!plan) return Json{{"matched", false}};
    Json out = plan->toJson();
    out["matched"] = true;
    out["kernel"] = kernels_.resolve(settings_, scope);
    return out;
  });

  // ---- Net ----

  rpc_.add("net.plan", [this](const Json&) { return currentNetConfig().toJson(); });

  rpc_.add("net.forHost", [this](const Json& params) {
    auto plan = NetPlanner::forHost(rules_, dns_, settings_, params::string(params, "host"));
    if (!plan) return Json{{"matched", false}, {"host", params::string(params, "host")}};
    Json out = plan->toJson();
    out["matched"] = true;
    return out;
  });

  // The configuration document for liora-net, plus how to point the kernel at it.
  rpc_.add("net.config", [this](const Json& params) {
    NetConfig config = NetPlanner::build(rules_, dns_, settings_);
    std::string kernelId = params::optionalString(params, "kernel").value_or("");
    Json out{{"config", config.net},
             {"needsNet", config.needsNet},
             {"warnings", config.warnings},
             {"kernels", Json::array()}};
    if (!kernelId.empty()) {
      auto info = kernels_.find(kernelId);
      if (!info) rpcFail(kRpcNotFound, "unknown kernel: " + kernelId);
      out["kernels"].push_back(KernelManager::netSetup(*info, config, netRuntime()).toJson());
    } else {
      for (const auto& info : kernels_.list()) {
        out["kernels"].push_back(KernelManager::netSetup(info, config, netRuntime()).toJson());
      }
    }
    return out;
  });

  rpc_.add("net.resolverStatus", [this](const Json&) {
    std::lock_guard lock(resolverMutex_);
    return resolverStatus_;
  });

  rpc_.add("net.markResolverRestart", [this](const Json& params) {
    Json record;
    {
      std::lock_guard lock(resolverMutex_);
      int64_t count = resolverStatus_.value("count", int64_t{0});
      record = {{"at", params::optionalInteger(params, "at").value_or(nowMs())},
                {"count", count + 1}};
      resolverStatus_ = record;
    }
    std::string error;
    if (!writeFileAtomic(paths_.root / "net-resolver.json", record.dump(), &error)) {
      rpcFail(kRpcInternalError, error);
    }
    return record;
  });

  rpc_.add("net.lastApplied", [this](const Json&) {
    std::lock_guard lock(appliedMutex_);
    return applied_;
  });

  rpc_.add("net.markApplied", [this](const Json& params) {
    Json record = {
        {"at", params::optionalInteger(params, "at").value_or(nowMs())},
        {"proxy", params::optionalString(params, "proxy").value_or("")},
        {"provider", params::optionalString(params, "provider").value_or("")},
        {"protocol", params::optionalString(params, "protocol").value_or("")},
        {"mode", params::optionalString(params, "mode").value_or("")},
        {"summary", params::optionalString(params, "summary").value_or("")},
    };
    {
      std::lock_guard lock(appliedMutex_);
      applied_ = record;
    }
    std::string error;
    if (!writeFileAtomic(paths_.root / "net-applied.json", record.dump(), &error)) {
      rpcFail(kRpcInternalError, error);
    }
    return record;
  });

  rpc_.add("net.runtime", [this](const Json&) {
    NetRuntime runtime = netRuntime();
    return Json{{"running", runtime.running},
                {"dohPort", runtime.dohPort},
                {"proxyPort", runtime.proxyPort},
                {"diagPort", runtime.diagPort},
                {"spki", runtime.spki},
                {"appliedConfig", runtime.appliedConfig}};
  });

  rpc_.add("net.setRuntime", [this](const Json& params) {
    NetRuntime runtime;
    runtime.running = params::boolean(params, "running", false);
    runtime.dohPort = static_cast<uint16_t>(params::optionalInteger(params, "dohPort").value_or(0));
    runtime.proxyPort = static_cast<uint16_t>(params::optionalInteger(params, "proxyPort").value_or(0));
    runtime.diagPort = static_cast<uint16_t>(params::optionalInteger(params, "diagPort").value_or(0));
    runtime.spki = params::optionalString(params, "spki").value_or("");
    runtime.appliedConfig = params::optionalString(params, "appliedConfig").value_or("");
    setNetRuntime(runtime);
    rpc_.notify("net.runtimeChanged", nullptr);
    return Json{{"ok", true}};
  });
}

Json Core::handleJson(const Json& request) { return rpc_.handleDocument(request); }

std::string Core::handle(std::string_view request) {
  Json document = Json::parse(request, nullptr, false);
  if (document.is_discarded()) {
    return Json{{"jsonrpc", "2.0"},
                {"error", RpcError{kRpcParseError, "invalid JSON"}.toJson()},
                {"id", nullptr}}
        .dump();
  }
  Json response = rpc_.handleDocument(document);
  // Notifications produce no response.
  if (response.is_null()) return {};
  return response.dump();
}

}  // namespace liora

#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "liora/backup.hpp"
#include "liora/components.hpp"
#include "liora/dns.hpp"
#include "liora/json.hpp"
#include "liora/kernel.hpp"
#include "liora/migration.hpp"
#include "liora/netplan.hpp"
#include "liora/rpc.hpp"
#include "liora/rules.hpp"
#include "liora/settings.hpp"
#include "liora/storage.hpp"
#include "liora/util.hpp"

namespace liora {

// Where Core keeps a profile.
struct CorePaths {
  fs::path root;
  fs::path settings;
  fs::path database;
  fs::path components;
  fs::path backups;

  static CorePaths forProfile(const fs::path& root);
};

// The platform-independent brain of Liora: settings, DNS, rules, user data,
// backups, migration, kernels and components, all exposed over JSON-RPC.
//
// The shell creates one Core per profile and forwards messages from the WebUI.
// Core never performs network or UI work itself.
class Core {
 public:
  struct Options {
    CorePaths paths;
    // Component catalog shipped with the app.
    std::string builtinCatalog;
    // Set by tests to keep the clock deterministic.
    bool skipAutoBackup = false;
    // Optional Ed25519 public key (hex or base64) for the component catalog.
    // Empty means catalog signatures are not enforced.
    std::string updateKey;
  };

  explicit Core(Options options);
  ~Core();
  Core(const Core&) = delete;
  Core& operator=(const Core&) = delete;

  // Loads settings and opens the database. Returns an error message on failure.
  std::optional<std::string> start();
  void stop();

  // A JSON-RPC request document (single or batch) as text; returns the response.
  std::string handle(std::string_view request);
  Json handleJson(const Json& request);

  // Outgoing notifications (settings changed, kernel state, ...) go here.
  void setNotificationSink(RpcRouter::Sink sink);

  // The shell reports the addresses of the running liora-net service.
  void setNetRuntime(const NetRuntime& runtime);
  NetRuntime netRuntime() const;

  // The shell registers its native kernels (system WebView, downloaded CEF).
  void attachKernel(std::shared_ptr<IKernel> kernel);
  void detachKernel(std::string_view id);

  // Applies current network settings to a kernel. Returns what to change.
  KernelNetSetup applyNetwork(std::string_view kernelId);

  Settings& settings() { return settings_; }
  DnsCatalog& dns() { return dns_; }
  Storage& storage() { return storage_; }
  KernelManager& kernels() { return kernels_; }
  ComponentManager& components() { return components_; }
  Backup& backup() { return backup_; }

  // Rules live in the settings document but are validated by RuleSet.
  std::optional<std::string> saveRules();
  void reloadRules();

  const CorePaths& paths() const { return paths_; }
  static std::string appVersion();

 private:
  void registerCoreMethods();
  void registerDataMethods();
  void registerComponentMethods();
  void registerBackupSections();
  // Persists settings and rules when they changed.
  void persistSettings();

  Scope scopeFrom(const Json& params) const;
  NetConfig currentNetConfig();

  Options options_;
  CorePaths paths_;
  Settings settings_;
  DnsCatalog dns_;
  RuleSet rules_;
  Storage storage_;
  Backup backup_;
  KernelManager kernels_;
  ComponentManager components_;
  RpcRouter rpc_;
  mutable std::mutex netMutex_;
  NetRuntime net_;
  // Last network configuration the shell applied to kernel views. Persisted so
  // the DNS panel can show it after navigation or a restart.
  mutable std::mutex appliedMutex_;
  Json applied_ = Json::object();
  // Last successful liora-net start and how many times it has run.
  mutable std::mutex resolverMutex_;
  Json resolverStatus_ = Json::object();
  bool started_ = false;
};

}  // namespace liora

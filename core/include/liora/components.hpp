#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "liora/json.hpp"
#include "liora/util.hpp"

namespace liora {

// A file inside a component package, as listed in the signed catalog.
struct ComponentFile {
  std::string path;  // relative, forward slashes
  std::string sha256;
  int64_t size = 0;

  Json toJson() const;
};

// One platform-specific build of a component version.
struct ComponentBuild {
  std::string os;  // "macos" | "windows" | "linux" | "android" | "ios"
  std::string arch;  // "arm64" | "x64" | "universal"
  std::string url;
  int64_t size = 0;
  std::vector<ComponentFile> files;

  bool matches(std::string_view platformOs, std::string_view platformArch) const;
  Json toJson() const;
};

struct ComponentVersion {
  std::string version;
  std::string minApp;  // minimum Liora version
  std::vector<ComponentBuild> builds;

  Json toJson() const;
};

struct ComponentEntry {
  std::string id;
  std::string name;
  std::string kind;  // "kernel" | "net" | "migration" | "language" | "plugin" | "rules"
  std::string description;
  bool required = false;
  std::vector<ComponentVersion> versions;

  const ComponentVersion* version(std::string_view v) const;
  const ComponentVersion* latest() const;
  Json toJson() const;
};

enum class InstallState { kAbsent, kInstalled, kStaged };

struct InstalledComponent {
  std::string id;
  std::string version;
  std::string path;
  int64_t installedAt = 0;
  bool current = false;

  Json toJson() const;
};

// Everything the shell needs to fetch and unpack a package.
struct InstallPlan {
  std::string id;
  std::string version;
  std::string url;
  int64_t size = 0;
  // Signed by the update key; the shell must not install without it.
  std::string signature;
  // Every file that must exist in the staging directory after unpacking.
  std::vector<ComponentFile> files;
  fs::path stagingDir;

  Json toJson() const;
};

// Verifies a detached signature over `message`.
// Returns true when valid. Provided by the shell (Rust ed25519-dalek).
using SignatureVerifier = std::function<bool(std::string_view publicKey, std::string_view message,
                                            std::string_view signature)>;

// Installs and tracks downloadable components (kernels, liora-net, migration
// helpers, languages, plugins). Core never downloads or unpacks anything: the
// shell downloads `InstallPlan::url` into `stagingDir`, unpacks it, and calls
// commit(). Core verifies every file against the catalog before the switch, so
// a corrupted or tampered package never becomes active.
class ComponentManager {
 public:
  explicit ComponentManager(fs::path root);

  // Loads the component catalog. Unsigned catalogs are rejected when a verifier
  // is configured.
  std::optional<std::string> loadCatalog(std::string_view json);
  std::optional<std::string> loadCatalogFile(const fs::path& file);
  const std::vector<ComponentEntry>& catalog() const { return entries_; }
  const ComponentEntry* find(std::string_view id) const;
  Json describe() const;

  // A newer build of an installed component exists.
  std::vector<std::string> updates(std::string_view os, std::string_view arch) const;

  std::optional<InstallPlan> plan(std::string_view id, std::string_view version, std::string_view os,
                                 std::string_view arch, std::string* error = nullptr) const;
  std::optional<std::string> commit(const InstallPlan& plan);
  // File-by-file check used by commit(); exposed for tests.
  static std::optional<std::string> verifyTree(const fs::path& dir, const std::vector<ComponentFile>& files);

  bool activate(std::string_view id, std::string_view version);
  bool remove(std::string_view id, std::string_view version);
  // Switches to the newest installed version.
  bool rollback(std::string_view id);

  std::optional<InstalledComponent> current(std::string_view id) const;
  std::vector<InstalledComponent> installed() const;
  fs::path componentPath(std::string_view id, std::string_view version) const;

  void setVerifier(SignatureVerifier verifier);
  void setPublicKey(std::string key);
  // When true, a catalog without a valid signature is rejected. Enabled
  // automatically once a verifier and public key are set.
  void requireSignature(bool require);
  bool signatureRequired() const;

  // A component this build depends on at startup (e.g. the CEF kernel).
  bool available(std::string_view id) const;

  static std::string platformOs();
  static std::string platformArch();

 private:
  bool loadIndex();
  bool saveIndex(std::string* error = nullptr) const;
  std::optional<std::string> checkCatalogSignature(const Json& doc) const;
  void syncCurrentMarker(InstalledComponent& item) const;

  fs::path root_;
  mutable std::mutex mutex_;
  std::vector<ComponentEntry> entries_;
  std::vector<InstalledComponent> installed_;
  SignatureVerifier verifier_;
  std::string publicKey_;
  std::string catalogSignature_;
  bool requireSignature_ = false;
};

}  // namespace liora

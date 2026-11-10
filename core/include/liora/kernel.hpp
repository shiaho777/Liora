#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "liora/json.hpp"
#include "liora/netplan.hpp"

namespace liora {

class Settings;
struct Scope;

// What a kernel can do. The UI shows a feature only when the active kernel has
// the matching bit, and labels it as unavailable otherwise.
enum KernelCap : uint32_t {
  kCapSecureDns = 1u << 0,     // native DoH with custom templates
  kCapHttpsRecords = 1u << 1,  // uses HTTPS/SVCB records (alpn, ech) from DNS
  kCapEch = 1u << 2,
  kCapForceQuic = 1u << 3,  // QUIC without TCP fallback for listed origins
  kCapProxy = 1u << 4,      // can route traffic through a local proxy
  kCapExtensions = 1u << 5,  // Chrome extensions, including MV3
  kCapDevTools = 1u << 6,
  kCapIsolatedProfiles = 1u << 7,
};

struct KernelInfo {
  std::string id;
  std::string name;
  std::string version;
  // Component that provides the kernel; empty for kernels built into the app.
  std::string component;
  uint32_t caps = 0;
  bool available = false;
  // Network switches are startup arguments; changing them restarts the kernel.
  bool netNeedsRestart = false;

  bool has(uint32_t cap) const { return (caps & cap) == cap; }
  Json toJson() const;
};

// Endpoints of the running liora-net service.
struct NetRuntime {
  bool running = false;
  uint16_t dohPort = 0;
  uint16_t proxyPort = 0;
  // Loopback HTTP endpoint for the interface's diagnostics.
  uint16_t diagPort = 0;
  // base64 SHA-256 of the local DoH certificate's SubjectPublicKeyInfo.
  std::string spki;
  // JSON of the network config the kernel views were last loaded with. The
  // shell updates it on launch and on an explicit restart, not when it merely
  // refreshes the resolver, so the interface can show unapplied changes.
  std::string appliedConfig;
};

// How the shell configures one kernel for a NetConfig.
struct KernelNetSetup {
  std::string kernel;
  // Chromium secure DNS mode ("off", "automatic", "secure"), or "system" for
  // kernels whose resolver cannot be configured.
  std::string dnsMode = "automatic";
  std::string dohTemplates;
  // Empty or "http://127.0.0.1:port"; applied with the kernel's proxy API.
  std::string proxy;
  // Full startup switches, e.g. "--origin-to-force-quic-on=x.com:443".
  std::vector<std::string> switches;
  // Configured features this kernel cannot apply: "dns", "ech", "forceQuic", "httpsRecords".
  std::vector<std::string> unsupported;
  // liora-net is required but not running; DNS falls back to kernel defaults.
  bool netMissing = false;
  bool usesNet = false;

  Json toJson() const;
};

enum class DownloadState { kStarted, kProgress, kFinished, kFailed };

struct DownloadEvent {
  std::string id;
  std::string filename;
  std::string path;
  int64_t tab = 0;
  int64_t received = 0;
  int64_t total = 0;
  DownloadState state = DownloadState::kStarted;
  std::string error;
};

// Summary of the TLS session the kernel actually established.
struct SecurityInfo {
  std::string host;
  std::string subject;
  bool valid = false;
  // Unix milliseconds; 0 when unknown.
  int64_t notAfter = 0;
  bool https = true;
};

struct ViewOptions {
  void* parent = nullptr;  // native parent window or view
  int64_t tab = 0;
  std::string url;
  bool incognito = false;
  // Empty means the platform default (the user's Downloads folder).
  std::string downloadDir;
  bool askWhereToSave = false;
  // Asked before granting camera or microphone. Called on the main thread;
  // returning true grants the request for this navigation.
  std::function<bool(const std::string& origin, const std::string& kind)> askPermission;
};

struct ViewEvents {
  std::function<void(const std::string& url)> onUrl;
  std::function<void(const std::string& title)> onTitle;
  std::function<void(bool loading, double progress)> onLoading;
  std::function<void(bool canBack, bool canForward)> onHistory;
  // Returns true when the shell opened the window itself.
  std::function<bool(const std::string& url, bool userGesture)> onOpenWindow;
  // A message from a liora:// page to the bridge.
  std::function<void(const std::string& json)> onMessage;
  // Navigation failed before the page could load. Code uses the platform's
  // error code (NSURLErrorDomain on macOS) so the interface can explain it.
  std::function<void(const std::string& url, int code, const std::string& message)> onError;
  // Download progress. The kernel reports started, periodic progress, then
  // finished or failed.
  std::function<void(const DownloadEvent& event)> onDownload;
  // TLS details for the page that just loaded.
  std::function<void(const SecurityInfo& info)> onSecurity;
  // The kernel view is now attached and can be laid out. Kernels that create
  // their native view asynchronously use this to request a fresh layout.
  std::function<void()> onReady;
};

class IKernelView {
 public:
  virtual ~IKernelView() = default;
  virtual void load(const std::string& url) = 0;
  virtual void back() = 0;
  virtual void forward() = 0;
  virtual void reload(bool bypassCache) = 0;
  virtual void stop() = 0;
  // Bounds are in the host view's coordinate space (AppKit: bottom-left origin).
  virtual void setBounds(int x, int y, int width, int height) = 0;
  virtual void setVisible(bool visible) = 0;
  virtual void setZoom(double percent) = 0;
  virtual void postMessage(const std::string& json) = 0;
  virtual void* nativeHandle() const = 0;
};

// Implemented by each shell for each kernel it can host.
class IKernel {
 public:
  virtual ~IKernel() = default;
  virtual KernelInfo info() const = 0;
  virtual std::unique_ptr<IKernelView> createView(const ViewOptions& options, ViewEvents events) = 0;
  // Switches that need a restart take effect at the next start.
  virtual void applyNet(const KernelNetSetup& setup) = 0;
  virtual void shutdown() = 0;
};

class KernelManager {
 public:
  static constexpr const char* kSystem = "system";
  static constexpr const char* kCef = "cef";

  KernelManager();

  // Registers or replaces a kernel description. Shells call this at startup to
  // report the real capabilities of the platform's system WebView.
  void registerKernel(KernelInfo info);
  bool setAvailable(std::string_view id, bool available, std::string_view version = {});
  std::optional<KernelInfo> find(std::string_view id) const;
  std::vector<KernelInfo> list() const;

  void attach(std::shared_ptr<IKernel> kernel);
  void detach(std::string_view id);
  std::shared_ptr<IKernel> instance(std::string_view id) const;

  // kernel.default at the scope (tab, then site, then global), falling back to
  // the system kernel when the chosen one is not installed.
  std::string resolve(const Settings& settings, const Scope& scope) const;

  static KernelNetSetup netSetup(const KernelInfo& kernel, const NetConfig& config, const NetRuntime& runtime);
  static std::vector<std::string> capNames(uint32_t caps);

  Json toJson() const;

 private:
  mutable std::mutex mutex_;
  std::vector<KernelInfo> kernels_;
  std::map<std::string, std::shared_ptr<IKernel>, std::less<>> instances_;
};

}  // namespace liora

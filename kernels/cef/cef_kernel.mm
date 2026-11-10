#include "liora/cef_kernel.hpp"

#include <atomic>
#include <cmath>
#include <mutex>

#import <Cocoa/Cocoa.h>

#include "include/cef_app.h"
#include "include/cef_browser.h"
#include "include/cef_client.h"
#include "include/cef_life_span_handler.h"
#include "include/cef_load_handler.h"
#include "include/cef_version.h"
#include "include/wrapper/cef_helpers.h"
#include "include/wrapper/cef_library_loader.h"

namespace liora::cef {
namespace {

// Shared with the client so callbacks never touch a destroyed view.
struct Bridge {
  ViewEvents events;
  std::atomic<bool> alive{true};

  ~Bridge() { alive = false; }
};

void fireLoading(const std::shared_ptr<Bridge>& bridge, bool loading, double progress) {
  if (bridge && bridge->alive && bridge->events.onLoading) bridge->events.onLoading(loading, progress);
}

void fireUrl(const std::shared_ptr<Bridge>& bridge, const std::string& url) {
  if (bridge && bridge->alive && bridge->events.onUrl) bridge->events.onUrl(url);
}

class KernelClient : public CefClient, public CefLifeSpanHandler, public CefLoadHandler {
 public:
  explicit KernelClient(std::shared_ptr<Bridge> bridge) : bridge_(std::move(bridge)) {}

  CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }
  CefRefPtr<CefLoadHandler> GetLoadHandler() override { return this; }

  void OnAfterCreated(CefRefPtr<CefBrowser> browser) override {
    CEF_REQUIRE_UI_THREAD();
    browser_ = browser;
  }

  void OnBeforeClose(CefRefPtr<CefBrowser> browser) override {
    CEF_REQUIRE_UI_THREAD();
    browser_ = nullptr;
  }

  void OnLoadStart(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                   TransitionType transition) override {
    if (!frame || !frame->IsMain()) return;
    fireLoading(bridge_, true, 0.0);
    fireUrl(bridge_, frame->GetURL().ToString());
  }

  void OnLoadEnd(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, int httpStatusCode) override {
    if (!frame || !frame->IsMain()) return;
    fireLoading(bridge_, false, 1.0);
    fireUrl(bridge_, frame->GetURL().ToString());
    if (bridge_ && bridge_->alive && bridge_->events.onHistory) {
      bridge_->events.onHistory(browser->CanGoBack(), browser->CanGoForward());
    }
  }

  void OnLoadError(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                   ErrorCode errorCode, const CefString& errorText,
                   const CefString& failedUrl) override {
    if (!frame || !frame->IsMain()) return;
    fireLoading(bridge_, false, 0.0);
  }

  CefRefPtr<CefBrowser> browser() const { return browser_; }

 private:
  std::shared_ptr<Bridge> bridge_;
  CefRefPtr<CefBrowser> browser_;

  IMPLEMENT_REFCOUNTING(KernelClient);
};

class CefView final : public IKernelView {
 public:
  CefView(const ViewOptions& options, ViewEvents events) {
    bridge_ = std::make_shared<Bridge>();
    bridge_->events = std::move(events);
    client_ = new KernelClient(bridge_);

    CefWindowInfo info;
    NSView* parent = (__bridge NSView*)options.parent;
    info.SetAsChild((__bridge void*)parent, CefRect(0, 0, 800, 600));
    CefBrowserSettings settings;
    // Isolated profiles use a separate CefRequestContext; wired up with the
    // tab model rather than guessed here.
    CefBrowserHost::CreateBrowser(info, client_, CefString(options.url.c_str()), settings, nullptr, nullptr);
  }

  ~CefView() override {
    if (bridge_) bridge_->alive = false;
    if (auto browser = client_ ? client_->browser() : nullptr) {
      browser->GetHost()->CloseBrowser(true);
    }
  }

  void load(const std::string& url) override {
    if (auto frame = mainFrame()) frame->LoadURL(CefString(url.c_str()));
  }
  void back() override {
    if (auto b = this->browser()) b->GoBack();
  }
  void forward() override {
    if (auto b = this->browser()) b->GoForward();
  }
  void reload(bool bypassCache) override {
    if (auto b = this->browser()) {
      if (bypassCache) {
        b->ReloadIgnoreCache();
      } else {
        b->Reload();
      }
    }
  }
  void stop() override {
    if (auto b = this->browser()) b->StopLoad();
  }

  void setBounds(int x, int y, int width, int height) override {
    if (auto b = this->browser()) {
      if (auto handle = b->GetHost()->GetWindowHandle()) {
        NSView* view = (__bridge NSView*)handle;
        [view setFrame:NSMakeRect(x, y, width, height)];
      }
    }
  }

  void setVisible(bool visible) override {
    if (auto b = this->browser()) {
      if (auto handle = b->GetHost()->GetWindowHandle()) {
        NSView* view = (__bridge NSView*)handle;
        view.hidden = !visible;
      }
    }
  }

  void setZoom(double percent) override {
    if (auto b = this->browser()) {
      // Chromium zoom levels are logarithmic with a 1.2 step.
      const double level = std::log(percent / 100.0) / std::log(1.2);
      b->GetHost()->SetZoomLevel(level);
    }
  }

  void postMessage(const std::string& json) override {
    if (auto frame = mainFrame()) {
      std::string script = "window.__lioraKernelDeliver && window.__lioraKernelDeliver(" + json + ");";
      frame->ExecuteJavaScript(CefString(script.c_str()), frame->GetURL(), 0);
    }
  }

  void* nativeHandle() const override {
    auto self = const_cast<CefView*>(this);
    if (auto browser = self->browser()) return browser->GetHost()->GetWindowHandle();
    return nullptr;
  }

 private:
  CefRefPtr<CefBrowser> browser() {
    return client_ ? client_->browser() : nullptr;
  }
  CefRefPtr<CefFrame> mainFrame() {
    if (auto browser = this->browser()) return browser->GetMainFrame();
    return nullptr;
  }

  std::shared_ptr<Bridge> bridge_;
  CefRefPtr<KernelClient> client_;
};

class KernelApp : public CefApp, public CefBrowserProcessHandler {
 public:
  explicit KernelApp(std::vector<std::string> switches) : switches_(std::move(switches)) {}

  CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler() override { return this; }

  void OnBeforeCommandLineProcessing(const CefString& process_type,
                                     CefRefPtr<CefCommandLine> command_line) override {
    for (const auto& raw : switches_) {
      std::string item = raw;
      if (item.rfind("--", 0) == 0) item = item.substr(2);
      if (item.empty()) continue;
      const auto eq = item.find('=');
      if (eq == std::string::npos) {
        command_line->AppendSwitch(CefString(item.c_str()));
      } else {
        command_line->AppendSwitchWithValue(CefString(item.substr(0, eq).c_str()),
                                            CefString(item.substr(eq + 1).c_str()));
      }
    }
  }

  IMPLEMENT_REFCOUNTING(KernelApp);

 private:
  std::vector<std::string> switches_;
};

class CefKernel final : public IKernel {
 public:
  KernelInfo info() const override {
    KernelInfo info;
    info.id = KernelManager::kCef;
    info.name = "Chromium (CEF)";
    info.version = CEF_VERSION;
    info.component = "kernel-cef";
    info.caps = kCapSecureDns | kCapHttpsRecords | kCapEch | kCapForceQuic | kCapProxy | kCapExtensions |
                kCapDevTools | kCapIsolatedProfiles;
    info.available = true;
    // Chromium reads DNS and QUIC switches at startup only.
    info.netNeedsRestart = true;
    return info;
  }

  std::unique_ptr<IKernelView> createView(const ViewOptions& options, ViewEvents events) override {
    return std::make_unique<CefView>(options, std::move(events));
  }

  // Switches are consumed by KernelApp during CefInitialize; the shell must
  // restart the browser process to apply a change.
  void applyNet(const KernelNetSetup& setup) override {}
  void shutdown() override { liora::cef::shutdown(); }
};

std::mutex g_mutex;
bool g_initialized = false;
NSTimer* g_timer = nil;
// The framework must stay loaded for the lifetime of the process.
std::unique_ptr<CefScopedLibraryLoader> g_loader;

bool ensureLibraryLoaded(std::string* error) {
  if (g_loader) return true;
  auto loader = std::make_unique<CefScopedLibraryLoader>();
  if (!loader->LoadInMain()) {
    if (error) *error = "cannot load the Chromium Embedded Framework";
    return false;
  }
  g_loader = std::move(loader);
  return true;
}

}  // namespace

void pump() {
  CefDoMessageLoopWork();
}

int executeProcess(int argc, char** argv) {
  std::string error;
  if (!ensureLibraryLoaded(&error)) return -1;
  CefMainArgs args(argc, argv);
  CefRefPtr<KernelApp> app = new KernelApp({});
  return CefExecuteProcess(args, app, nullptr);
}

bool initialize(const Startup& startup, std::string* error) {
  std::lock_guard lock(g_mutex);
  if (g_initialized) return true;

  CefMainArgs args;
  CefSettings settings;
  CefString(&settings.cache_path).FromString(startup.cachePath);
  if (!startup.userAgent.empty()) CefString(&settings.user_agent).FromString(startup.userAgent);
  settings.external_message_pump = true;
  settings.no_sandbox = startup.noSandbox;
  settings.windowless_rendering_enabled = false;
  settings.log_severity = LOGSEVERITY_WARNING;

  CefRefPtr<KernelApp> app = new KernelApp(startup.switches);
  if (!ensureLibraryLoaded(error)) return false;
  if (!CefInitialize(args, settings, app, nullptr)) {
    if (error) *error = "CefInitialize failed";
    return false;
  }

  // Chromium has no run loop of its own here; drive it from the main thread.
  g_timer = [NSTimer scheduledTimerWithTimeInterval:1.0 / 120.0
                                            repeats:YES
                                              block:^(NSTimer* timer) {
                                                liora::cef::pump();
                                              }];
  g_initialized = true;
  return true;
}

void shutdown() {
  std::lock_guard lock(g_mutex);
  if (!g_initialized) return;
  if (g_timer) {
    [g_timer invalidate];
    g_timer = nil;
  }
  CefShutdown();
  g_initialized = false;
}

std::shared_ptr<IKernel> createKernel() { return std::make_shared<CefKernel>(); }

}  // namespace liora::cef

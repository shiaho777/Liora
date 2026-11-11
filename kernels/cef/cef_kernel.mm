#include "liora/cef_kernel.hpp"

#include <atomic>
#include <cmath>
#include <mutex>

#import <Cocoa/Cocoa.h>

#include "include/cef_app.h"
#include "include/cef_browser.h"
#include "include/cef_client.h"
#include "include/cef_context_menu_handler.h"
#include "include/cef_display_handler.h"
#include "include/cef_download_handler.h"
#include "include/cef_find_handler.h"
#include "include/cef_life_span_handler.h"
#include "include/cef_load_handler.h"
#include "include/cef_permission_handler.h"
#include "include/cef_version.h"
#include "include/wrapper/cef_helpers.h"
#include "include/wrapper/cef_library_loader.h"

namespace liora::cef {
namespace {

// Maps Chromium net errors onto the NSURLErrorDomain codes the interface
// already explains. 0 means the failure should be ignored (navigation replaced).
int nsErrorForCef(int code) {
  switch (code) {
    case ERR_ABORTED:
      return 0;
    case ERR_NAME_NOT_RESOLVED:
      return -1003;
    case ERR_CONNECTION_REFUSED:
      return -1004;
    case ERR_CONNECTION_RESET:
    case ERR_CONNECTION_CLOSED:
    case ERR_CONNECTION_ABORTED:
      return -1005;
    case ERR_INTERNET_DISCONNECTED:
      return -1009;
    case ERR_TIMED_OUT:
      return -1001;
    case ERR_CERT_DATE_INVALID:
      return -1201;
    case ERR_CERT_AUTHORITY_INVALID:
    case ERR_CERT_COMMON_NAME_INVALID:
    case ERR_CERT_INVALID:
      return -1202;
    case ERR_SSL_PROTOCOL_ERROR:
      return -1200;
    default:
      return -1005;
  }
}

NSString* defaultDownloadDirectory() {
  NSString* dir = [NSSearchPathForDirectoriesInDomains(NSDownloadsDirectory, NSUserDomainMask, YES)
      firstObject];
  return dir.length ? dir : NSTemporaryDirectory();
}

NSString* nsFromCef(const CefString& value) {
  return [NSString stringWithUTF8String:value.ToString().c_str()] ?: @"";
}

NSURL* uniqueFileURL(NSURL* url) {
  NSFileManager* files = [NSFileManager defaultManager];
  if (![files fileExistsAtPath:url.path]) return url;
  NSString* directory = url.URLByDeletingLastPathComponent.path;
  NSString* name = url.lastPathComponent.stringByDeletingPathExtension;
  NSString* ext = url.pathExtension;
  for (int i = 1; i < 1000; ++i) {
    NSString* candidate = [NSString
        stringWithFormat:@"%@ (%d)%@", name, i, ext.length ? [@"." stringByAppendingString:ext] : @""];
    NSURL* next = [NSURL fileURLWithPath:[directory stringByAppendingPathComponent:candidate]];
    if (![files fileExistsAtPath:next.path]) return next;
  }
  return url;
}

// Shared with the client so callbacks never touch a destroyed view.
struct Bridge {
  ViewEvents events;
  std::atomic<bool> alive{true};

  ~Bridge() { alive = false; }
};

void fireLoading(const std::shared_ptr<Bridge>& bridge, bool loading, double progress) {
  if (bridge && bridge->alive && bridge->events.onLoading) bridge->events.onLoading(loading, progress);
}

void fireHistory(const std::shared_ptr<Bridge>& bridge, bool canBack, bool canForward) {
  if (bridge && bridge->alive && bridge->events.onHistory) {
    bridge->events.onHistory(canBack, canForward);
  }
}

void fireTitle(const std::shared_ptr<Bridge>& bridge, const std::string& title) {
  if (bridge && bridge->alive && bridge->events.onTitle) bridge->events.onTitle(title);
}

void fireUrl(const std::shared_ptr<Bridge>& bridge, const std::string& url) {
  if (bridge && bridge->alive && bridge->events.onUrl) bridge->events.onUrl(url);
}

class KernelClient : public CefClient,
                     public CefLifeSpanHandler,
                     public CefLoadHandler,
                     public CefDisplayHandler,
                     public CefDownloadHandler,
                     public CefPermissionHandler,
                     public CefContextMenuHandler,
                     public CefFindHandler {
 public:
  KernelClient(std::shared_ptr<Bridge> bridge, const ViewOptions& options)
      : bridge_(std::move(bridge)), options_(options) {}

  CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }
  CefRefPtr<CefLoadHandler> GetLoadHandler() override { return this; }
  CefRefPtr<CefDisplayHandler> GetDisplayHandler() override { return this; }
  CefRefPtr<CefDownloadHandler> GetDownloadHandler() override { return this; }
  CefRefPtr<CefPermissionHandler> GetPermissionHandler() override { return this; }
  CefRefPtr<CefContextMenuHandler> GetContextMenuHandler() override { return this; }
  CefRefPtr<CefFindHandler> GetFindHandler() override { return this; }

  void OnFindResult(CefRefPtr<CefBrowser> browser,
                    int identifier,
                    int count,
                    const CefRect& selection_rect,
                    int active_match_ordinal,
                    bool final_update) override {
    if (bridge_ && bridge_->alive && bridge_->events.onFindResult) {
      bridge_->events.onFindResult(count, active_match_ordinal);
    }
  }

  enum MenuCommand {
    kMenuBack = 1000,
    kMenuForward,
    kMenuReload,
    kMenuCopy,
    kMenuPaste,
    kMenuSelectAll,
    kMenuDevTools,
  };

  void OnBeforeContextMenu(CefRefPtr<CefBrowser> browser,
                           CefRefPtr<CefFrame> frame,
                           CefRefPtr<CefContextMenuParams> params,
                           CefRefPtr<CefMenuModel> model) override {
    if (!model) return;
    model->Clear();
    model->AddItem(kMenuBack, "后退");
    model->AddItem(kMenuForward, "前进");
    model->AddItem(kMenuReload, "重新加载");
    model->AddSeparator();
    model->AddItem(kMenuCopy, "复制");
    model->AddItem(kMenuPaste, "粘贴");
    model->AddItem(kMenuSelectAll, "全选");
    model->AddSeparator();
    model->AddItem(kMenuDevTools, "开发者工具");
  }

  bool OnContextMenuCommand(CefRefPtr<CefBrowser> browser,
                            CefRefPtr<CefFrame> frame,
                            CefRefPtr<CefContextMenuParams> params,
                            int command_id,
                            EventFlags event_flags) override {
    switch (command_id) {
      case kMenuBack:
        if (browser) browser->GoBack();
        return true;
      case kMenuForward:
        if (browser) browser->GoForward();
        return true;
      case kMenuReload:
        if (browser) browser->Reload();
        return true;
      case kMenuCopy:
        if (frame) frame->Copy();
        return true;
      case kMenuPaste:
        if (frame) frame->Paste();
        return true;
      case kMenuSelectAll:
        if (frame) frame->SelectAll();
        return true;
      case kMenuDevTools:
        if (browser) {
          CefWindowInfo info;
          CefBrowserSettings settings;
          browser->GetHost()->ShowDevTools(info, nullptr, settings, CefPoint());
        }
        return true;
      default:
        return false;
    }
  }

  bool OnShowPermissionPrompt(CefRefPtr<CefBrowser> browser,
                              uint64_t prompt_id,
                              const CefString& requesting_origin,
                              uint32_t requested_permissions,
                              CefRefPtr<CefPermissionPromptCallback> callback) override {
    if (!callback) return false;
    const bool camera = (requested_permissions & CEF_PERMISSION_TYPE_CAMERA_STREAM) != 0;
    const bool microphone = (requested_permissions & CEF_PERMISSION_TYPE_MIC_STREAM) != 0;
    std::string kind;
    if (camera && microphone) kind = "camera+microphone";
    else if (camera) kind = "camera";
    else if (microphone) kind = "microphone";
    // Other prompts (geolocation, clipboard, ...) are not part of Liora's
    // permission UI yet; let CEF apply its default handling.
    if (kind.empty()) return false;

    if (!options_.askPermission) {
      callback->Continue(CEF_PERMISSION_RESULT_DENY);
      return true;
    }
    const std::string origin = requesting_origin.ToString();
    auto ask = options_.askPermission;
    dispatch_async(dispatch_get_main_queue(), ^{
      const bool allowed = ask(origin, kind);
      callback->Continue(allowed ? CEF_PERMISSION_RESULT_ACCEPT : CEF_PERMISSION_RESULT_DENY);
    });
    return true;
  }

  bool OnRequestMediaAccessPermission(
      CefRefPtr<CefBrowser> browser,
      CefRefPtr<CefFrame> frame,
      const CefString& requesting_origin,
      uint32_t requested_permissions,
      CefRefPtr<CefMediaAccessCallback> callback) override {
    if (!callback) return false;
    const bool audio =
        (requested_permissions & CEF_MEDIA_PERMISSION_DEVICE_AUDIO_CAPTURE) != 0;
    const bool video =
        (requested_permissions & CEF_MEDIA_PERMISSION_DEVICE_VIDEO_CAPTURE) != 0;
    std::string kind;
    if (audio && video) kind = "camera+microphone";
    else if (video) kind = "camera";
    else if (audio) kind = "microphone";
    if (kind.empty() || !options_.askPermission) {
      callback->Cancel();
      return true;
    }
    const std::string origin = requesting_origin.ToString();
    auto ask = options_.askPermission;
    dispatch_async(dispatch_get_main_queue(), ^{
      const bool allowed = ask(origin, kind);
      if (allowed) {
        callback->Continue(requested_permissions);
      } else {
        callback->Cancel();
      }
    });
    return true;
  }

  void OnTitleChange(CefRefPtr<CefBrowser> browser, const CefString& title) override {
    if (!bridge_ || !bridge_->alive) return;
    fireTitle(bridge_, title.ToString());
  }

  void OnFullscreenModeChange(CefRefPtr<CefBrowser> browser, bool fullscreen) override {
    if (bridge_ && bridge_->alive && bridge_->events.onFullscreen) {
      bridge_->events.onFullscreen(fullscreen);
    }
  }

  void OnAddressChange(CefRefPtr<CefBrowser> browser,
                       CefRefPtr<CefFrame> frame,
                       const CefString& url) override {
    if (!bridge_ || !bridge_->alive || !frame || !frame->IsMain()) return;
    fireUrl(bridge_, url.ToString());
  }
  // target="_blank": keep the popup in the same view until the shell owns a
  // full tab model for kernel-created windows.
  bool OnBeforePopup(CefRefPtr<CefBrowser> browser,
                     CefRefPtr<CefFrame> frame,
                     int popup_id,
                     const CefString& target_url,
                     const CefString& target_frame_name,
                     WindowOpenDisposition target_disposition,
                     bool user_gesture,
                     const CefPopupFeatures& popup_features,
                     CefWindowInfo& window_info,
                     CefRefPtr<CefClient>& client,
                     CefBrowserSettings& settings,
                     CefRefPtr<CefDictionaryValue>& extra_info,
                     bool* no_javascript_access) override {
    if (!frame || target_url.empty()) return false;
    if (bridge_ && bridge_->alive && bridge_->events.onOpenWindow &&
        bridge_->events.onOpenWindow(target_url.ToString(), user_gesture)) {
      return true;
    }
    // Fallback for shells without a tab model.
    frame->LoadURL(target_url);
    return true;
  }

  bool OnBeforeDownload(CefRefPtr<CefBrowser> browser,
                        CefRefPtr<CefDownloadItem> item,
                        const CefString& suggested_name,
                        CefRefPtr<CefBeforeDownloadCallback> callback) override {
    if (!item || !callback) return false;

    NSString* directory = options_.downloadDir.empty()
                              ? defaultDownloadDirectory()
                              : [NSString stringWithUTF8String:options_.downloadDir.c_str()];
    [[NSFileManager defaultManager] createDirectoryAtPath:directory
                              withIntermediateDirectories:YES
                                               attributes:nil
                                                    error:nil];
    NSString* name = suggested_name.empty() ? @"download" : nsFromCef(suggested_name);
    NSURL* target = [NSURL fileURLWithPath:[directory stringByAppendingPathComponent:name]];

    if (options_.askWhereToSave) {
      NSSavePanel* panel = [NSSavePanel savePanel];
      panel.nameFieldStringValue = target.lastPathComponent;
      panel.directoryURL = [NSURL fileURLWithPath:directory];
      if ([panel runModal] != NSModalResponseOK || !panel.URL) {
        callback->Continue("", false);
        return true;
      }
      target = panel.URL;
    } else {
      target = uniqueFileURL(target);
    }

    callback->Continue(std::string(target.path.UTF8String), false);
    if (bridge_ && bridge_->alive && bridge_->events.onDownload) {
      DownloadEvent event;
      event.id = std::to_string(item->GetId());
      event.filename = std::string(target.lastPathComponent.UTF8String);
      event.path = std::string(target.path.UTF8String);
      event.tab = options_.tab;
      event.total = item->GetTotalBytes();
      event.state = DownloadState::kStarted;
      bridge_->events.onDownload(event);
    }
    return true;
  }

  void OnDownloadUpdated(CefRefPtr<CefBrowser> browser,
                         CefRefPtr<CefDownloadItem> item,
                         CefRefPtr<CefDownloadItemCallback> callback) override {
    if (!item || !bridge_ || !bridge_->alive || !bridge_->events.onDownload) return;

    DownloadEvent event;
    event.id = std::to_string(item->GetId());
    event.path = item->GetFullPath().ToString();
    event.filename = item->GetSuggestedFileName().ToString();
    event.tab = options_.tab;
    event.received = item->GetReceivedBytes();
    event.total = item->GetTotalBytes();
    if (item->IsComplete()) {
      event.state = DownloadState::kFinished;
    } else if (item->IsCanceled()) {
      event.state = DownloadState::kFailed;
      event.error = "canceled";
    } else {
      event.state = DownloadState::kProgress;
    }
    bridge_->events.onDownload(event);
  }

  void OnAfterCreated(CefRefPtr<CefBrowser> browser) override {
    CEF_REQUIRE_UI_THREAD();
    browser_ = browser;
    if (onCreated) onCreated();
    if (bridge_ && bridge_->alive && bridge_->events.onReady) bridge_->events.onReady();
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
    // Chromium updates the navigation entry just after OnLoadEnd; querying
    // CanGoBack synchronously here returns false for the previous page.
    if (bridge_ && bridge_->alive && bridge_->events.onHistory) {
      CefRefPtr<CefBrowser> current = browser;
      auto bridge = bridge_;
      dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(150 * NSEC_PER_MSEC)),
                     dispatch_get_main_queue(), ^{
                       if (bridge->alive && current) {
                         fireHistory(bridge, current->CanGoBack(), current->CanGoForward());
                       }
                     });
    }
  }

  void OnLoadError(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                   ErrorCode errorCode, const CefString& errorText,
                   const CefString& failedUrl) override {
    if (!frame || !frame->IsMain()) return;
    fireLoading(bridge_, false, 0.0);
    const int mapped = nsErrorForCef(static_cast<int>(errorCode));
    if (mapped != 0 && bridge_ && bridge_->alive && bridge_->events.onError) {
      bridge_->events.onError(failedUrl.ToString(), mapped, errorText.ToString());
    }
  }

  CefRefPtr<CefBrowser> browser() const { return browser_; }

  // Called once the browser exists; the shell may already have requested a URL
  // before CreateBrowser finished, so the view flushes it here.
  std::function<void()> onCreated;

 private:
  std::shared_ptr<Bridge> bridge_;
  ViewOptions options_;
  CefRefPtr<CefBrowser> browser_;

  IMPLEMENT_REFCOUNTING(KernelClient);
};

class CefView final : public IKernelView {
 public:
  CefView(const ViewOptions& options, ViewEvents events) {
    bridge_ = std::make_shared<Bridge>();
    bridge_->events = std::move(events);
    client_ = new KernelClient(bridge_, options);
    client_->onCreated = [this]() {
      flushPendingState();
      flushPendingUrl();
    };

    CefWindowInfo info;
    NSView* parent = (__bridge NSView*)options.parent;
    info.SetAsChild((__bridge void*)parent, CefRect(0, 0, 800, 600));
    CefBrowserSettings settings;
    // Isolated profiles use a separate CefRequestContext; wired up with the
    // tab model rather than guessed here.
    CefBrowserHost::CreateBrowser(info, client_, CefString(options.url.c_str()), settings, nullptr, nullptr);
  }

  ~CefView() override {
    if (client_) client_->onCreated = nullptr;
    if (bridge_) bridge_->alive = false;
    if (auto browser = client_ ? client_->browser() : nullptr) {
      browser->GetHost()->CloseBrowser(true);
    }
  }

  void load(const std::string& url) override {
    pendingUrl_ = url;
    flushPendingUrl();
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
    boundsX_ = x;
    boundsY_ = y;
    boundsWidth_ = width;
    boundsHeight_ = height;
    hasBounds_ = true;
    applyBounds();
  }

  void setVisible(bool visible) override {
    visible_ = visible;
    hasVisible_ = true;
    applyVisible();
  }

  void find(const std::string& text, bool forward, bool matchCase) override {
    auto b = this->browser();
    if (!b) return;
    if (text.empty()) {
      b->GetHost()->StopFinding(true);
      return;
    }
    const bool findNext = text == lastFindText_ && matchCase == lastMatchCase_;
    lastFindText_ = text;
    lastMatchCase_ = matchCase;
    b->GetHost()->Find(CefString(text.c_str()), forward, matchCase, findNext);
  }

  void stopFinding(bool clearSelection) override {
    if (auto b = this->browser()) b->GetHost()->StopFinding(clearSelection);
    lastFindText_.clear();
  }

  void openDevTools() override {
    if (auto b = this->browser()) {
      CefWindowInfo info;
      CefBrowserSettings settings;
      b->GetHost()->ShowDevTools(info, nullptr, settings, CefPoint());
    }
  }

  void setZoom(double percent) override {
    pendingZoom_ = percent;
    hasZoom_ = true;
    applyZoom();
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

  void applyBounds() {
    if (!hasBounds_) return;
    if (auto b = this->browser()) {
      if (auto handle = b->GetHost()->GetWindowHandle()) {
        NSView* view = (__bridge NSView*)handle;
        [view setFrame:NSMakeRect(boundsX_, boundsY_, boundsWidth_, boundsHeight_)];
      }
    }
  }

  void applyVisible() {
    if (!hasVisible_) return;
    if (auto b = this->browser()) {
      if (auto handle = b->GetHost()->GetWindowHandle()) {
        NSView* view = (__bridge NSView*)handle;
        view.hidden = !visible_;
      }
    }
  }

  void applyZoom() {
    if (!hasZoom_) return;
    if (auto b = this->browser()) {
      // Chromium zoom levels are logarithmic with a 1.2 step.
      const double level = std::log(pendingZoom_ / 100.0) / std::log(1.2);
      b->GetHost()->SetZoomLevel(level);
    }
  }

  void flushPendingState() {
    applyBounds();
    applyVisible();
    applyZoom();
  }

  void flushPendingUrl() {
    if (pendingUrl_.empty()) return;
    if (auto frame = mainFrame()) {
      frame->LoadURL(CefString(pendingUrl_.c_str()));
      pendingUrl_.clear();
    }
  }

  std::string pendingUrl_;
  double pendingZoom_ = 100.0;
  bool hasZoom_ = false;
  std::string lastFindText_;
  bool lastMatchCase_ = false;
  bool hasBounds_ = false;
  int boundsX_ = 0;
  int boundsY_ = 0;
  int boundsWidth_ = 800;
  int boundsHeight_ = 600;
  bool hasVisible_ = false;
  bool visible_ = true;
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

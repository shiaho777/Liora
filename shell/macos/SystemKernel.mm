#import <Cocoa/Cocoa.h>
#import <WebKit/WebKit.h>

#include <algorithm>
#include <memory>
#include <string>

#include "SystemKernel.h"

using liora::IKernelView;
using liora::KernelInfo;
using liora::KernelNetSetup;
using liora::ViewEvents;
using liora::ViewOptions;

namespace {

NSString* toNSString(const std::string& value) {
  return [NSString stringWithUTF8String:value.c_str()] ?: @"";
}

std::string toStdString(NSString* value) { return value ? std::string([value UTF8String]) : std::string(); }

}  // namespace

// Receives WebKit callbacks and forwards them to the kernel owner.
@interface LioraWebViewDelegate : NSObject <WKNavigationDelegate, WKUIDelegate>
@property(nonatomic, assign) ViewEvents* events;
@end

@implementation LioraWebViewDelegate

- (void)webView:(WKWebView*)webView didFinishNavigation:(WKNavigation*)navigation {
  if (!self.events) return;
  if (self.events->onUrl) self.events->onUrl(toStdString(webView.URL.absoluteString));
  if (self.events->onTitle) self.events->onTitle(toStdString(webView.title));
  if (self.events->onLoading) self.events->onLoading(false, 1.0);
  if (self.events->onHistory) self.events->onHistory(webView.canGoBack, webView.canGoForward);
}

- (void)webView:(WKWebView*)webView didStartProvisionalNavigation:(WKNavigation*)navigation {
  if (!self.events) return;
  if (self.events->onLoading) self.events->onLoading(true, 0.0);
  if (self.events->onUrl) self.events->onUrl(toStdString(webView.URL.absoluteString));
}

- (void)webView:(WKWebView*)webView
    didFailProvisionalNavigation:(WKNavigation*)navigation
                       withError:(NSError*)error {
  if (!self.events || !self.events->onLoading) return;
  self.events->onLoading(false, 0);
}

- (void)webView:(WKWebView*)webView didCommitNavigation:(WKNavigation*)navigation {
  if (!self.events || !self.events->onHistory) return;
  self.events->onHistory(webView.canGoBack, webView.canGoForward);
}

// target="_blank" and window.open stay in the same view unless the shell
// decides to open a real tab.
- (WKWebView*)webView:(WKWebView*)webView
    createWebViewWithConfiguration:(WKWebViewConfiguration*)configuration
               forNavigationAction:(WKNavigationAction*)action
                    windowFeatures:(WKWindowFeatures*)features {
  if (!action.targetFrame && self.events && self.events->onOpenWindow) {
    bool handled = self.events->onOpenWindow(toStdString(action.request.URL.absoluteString), true);
    if (handled) return nil;
  }
  if (action.targetFrame == nil) [webView loadRequest:action.request];
  return nil;
}

@end

namespace liora::shell {
namespace {

class WebViewKernelView final : public IKernelView {
 public:
  WebViewKernelView(ViewOptions options, ViewEvents events) : events_(std::move(events)) {
    WKWebViewConfiguration* configuration = [[WKWebViewConfiguration alloc] init];
    configuration.websiteDataStore = options.incognito
                                         ? [WKWebsiteDataStore nonPersistentDataStore]
                                         : [WKWebsiteDataStore defaultDataStore];
    configuration.defaultWebpagePreferences.allowsContentJavaScript = YES;

    delegate_ = [[LioraWebViewDelegate alloc] init];
    delegate_.events = &events_;

    view_ = [[WKWebView alloc] initWithFrame:NSMakeRect(0, 0, 800, 600) configuration:configuration];
    view_.navigationDelegate = delegate_;
    view_.UIDelegate = delegate_;
    view_.allowsBackForwardNavigationGestures = YES;
    view_.allowsMagnification = NO;
    if (@available(macOS 13.3, *)) {
      view_.inspectable = YES;
    }
    if (!options.url.empty()) load(options.url);
  }

  ~WebViewKernelView() override { delegate_.events = nullptr; }

  void load(const std::string& url) override {
    NSString* text = toNSString(url);
    NSURL* parsed = [NSURL URLWithString:text];
    if (!parsed) return;
    [view_ loadRequest:[NSURLRequest requestWithURL:parsed]];
  }

  void back() override {
    if (view_.canGoBack) [view_ goBack];
  }
  void forward() override {
    if (view_.canGoForward) [view_ goForward];
  }
  void reload(bool bypassCache) override {
    if (bypassCache) {
      [view_ reloadFromOrigin];
    } else {
      [view_ reload];
    }
  }
  void stop() override { [view_ stopLoading]; }

  void setBounds(int x, int y, int width, int height) override {
    [view_ setFrame:NSMakeRect(x, y, std::max(width, 0), std::max(height, 0))];
  }
  void setVisible(bool visible) override { view_.hidden = !visible; }
  void setZoom(double percent) override { view_.pageZoom = percent / 100.0; }

  void postMessage(const std::string& json) override {
    NSString* script = [NSString
        stringWithFormat:@"window.__lioraKernelDeliver && window.__lioraKernelDeliver(%@);",
                         toNSString(json)];
    [view_ evaluateJavaScript:script completionHandler:nil];
  }

  void* nativeHandle() const override { return (__bridge void*)view_; }

  NSView* nsView() const { return view_; }

 private:
  WKWebView* view_ = nil;
  LioraWebViewDelegate* delegate_ = nil;
  ViewEvents events_;
};

}  // namespace

void registerSystemKernel(KernelManager& kernels) {
  auto kernel = std::make_shared<SystemKernel>();
  kernels.attach(kernel);
}

SystemKernel::SystemKernel() = default;
SystemKernel::~SystemKernel() = default;

void SystemKernel::setProxy(const std::string& proxyUrl) { proxy_ = proxyUrl; }

KernelInfo SystemKernel::info() const {
  KernelInfo info;
  info.id = KernelManager::kSystem;
  info.name = "System WebView (WebKit)";
  NSOperatingSystemVersion version = NSProcessInfo.processInfo.operatingSystemVersion;
  info.version = std::to_string(version.majorVersion) + "." + std::to_string(version.minorVersion);

  // WebKit can be pointed at a local HTTP proxy only from macOS 14, so encrypted
  // DNS via liora-net is advertised only there. ECH and forced QUIC are never
  // available: the page's TLS is owned by WebKit.
  uint32_t caps = 0;
  if (@available(macOS 14.0, *)) caps |= kCapProxy;
  info.caps = caps;
  info.available = true;
  info.netNeedsRestart = false;
  return info;
}

std::unique_ptr<IKernelView> SystemKernel::createView(const ViewOptions& options, ViewEvents events) {
  return std::make_unique<WebViewKernelView>(options, std::move(events));
}

void SystemKernel::applyNet(const KernelNetSetup& setup) { proxy_ = setup.proxy; }

void SystemKernel::shutdown() {}

}  // namespace liora::shell

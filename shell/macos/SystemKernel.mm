#import <Cocoa/Cocoa.h>
#import <Security/Security.h>
#import <WebKit/WebKit.h>

#include <algorithm>
#include <memory>
#include <string>

#include "SystemKernel.h"

using liora::DownloadEvent;
using liora::DownloadState;
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

void emitDownload(const std::shared_ptr<ViewEvents>& events, DownloadEvent event) {
  if (events && events->onDownload) events->onDownload(event);
}

// Downloads outlive the delegate that started them, so keep them alive until
// they finish (WKDownload.delegate is weak).
NSMutableSet* gActiveDownloads = nil;

void trackDownload(id delegate) {
  if (!gActiveDownloads) gActiveDownloads = [NSMutableSet set];
  [gActiveDownloads addObject:delegate];
}

void untrackDownload(id delegate) {
  [gActiveDownloads removeObject:delegate];
}

NSURL* uniqueURL(NSURL* url) {
  NSFileManager* files = [NSFileManager defaultManager];
  if (![files fileExistsAtPath:url.path]) return url;
  NSString* directory = url.URLByDeletingLastPathComponent.path;
  NSString* name = url.lastPathComponent.stringByDeletingPathExtension;
  NSString* ext = url.pathExtension;
  for (int i = 1; i < 1000; ++i) {
    NSString* candidate =
        [NSString stringWithFormat:@"%@ (%d)%@", name, i, ext.length ? [@"." stringByAppendingString:ext] : @""];
    NSURL* next = [NSURL fileURLWithPath:[directory stringByAppendingPathComponent:candidate]];
    if (![files fileExistsAtPath:next.path]) return next;
  }
  return url;
}

int64_t fileSize(NSString* path) {
  NSDictionary* attributes = [[NSFileManager defaultManager] attributesOfItemAtPath:path error:nil];
  if (!attributes) return 0;
  return [attributes[NSFileSize] longLongValue];
}

}  // namespace

// ---- Download -------------------------------------------------------------

@interface LioraDownloadDelegate : NSObject <WKDownloadDelegate>
- (instancetype)initWithEvents:(std::shared_ptr<ViewEvents>)events options:(const ViewOptions&)options;
@end

@implementation LioraDownloadDelegate {
  std::shared_ptr<ViewEvents> _events;
  ViewOptions _options;
  NSString* _identifier;
  NSURL* _destination;
  int64_t _expected;
  int64_t _lastReported;
  NSTimer* _timer;
}

- (instancetype)initWithEvents:(std::shared_ptr<ViewEvents>)events options:(const ViewOptions&)options {
  if ((self = [super init])) {
    _events = std::move(events);
    _options = options;
    _identifier = [[NSUUID UUID] UUIDString];
  }
  return self;
}

- (void)download:(WKDownload*)download
    decideDestinationUsingResponse:(NSURLResponse*)response
                 suggestedFilename:(NSString*)suggestedFilename
               completionHandler:(void (^)(NSURL* _Nullable))completionHandler {
  NSString* directory = _options.downloadDir.empty() ? @"" : toNSString(_options.downloadDir);
  if (!directory.length) {
    directory = [NSSearchPathForDirectoriesInDomains(NSDownloadsDirectory, NSUserDomainMask, YES) firstObject];
  }
  if (!directory.length) directory = NSTemporaryDirectory();
  [[NSFileManager defaultManager] createDirectoryAtPath:directory
                            withIntermediateDirectories:YES
                                             attributes:nil
                                                  error:nil];

  NSString* name = suggestedFilename.length ? suggestedFilename : @"download";
  NSURL* target = [NSURL fileURLWithPath:[directory stringByAppendingPathComponent:name]];

  if (_options.askWhereToSave) {
    NSSavePanel* panel = [NSSavePanel savePanel];
    panel.nameFieldStringValue = name;
    panel.directoryURL = [NSURL fileURLWithPath:directory];
    if ([panel runModal] == NSModalResponseOK && panel.URL) target = panel.URL;
  } else {
    target = uniqueURL(target);
  }

  _destination = target;
  _expected = response.expectedContentLength;

  DownloadEvent event;
  event.id = toStdString(_identifier);
  event.filename = toStdString(target.lastPathComponent);
  event.path = toStdString(target.path);
  event.total = _expected;
  event.tab = _options.tab;
  event.state = DownloadState::kStarted;
  emitDownload(_events, event);
  NSLog(@"Liora: download started %@", target.path);

  _timer = [NSTimer scheduledTimerWithTimeInterval:0.4
                                           repeats:YES
                                             block:^(NSTimer* timer) {
                                               [self tick];
                                             }];
  completionHandler(target);
}

- (void)tick {
  if (!_destination) return;
  int64_t size = fileSize(_destination.path);
  if (size == _lastReported) return;
  _lastReported = size;

  DownloadEvent event;
  event.id = toStdString(_identifier);
  event.filename = toStdString(_destination.lastPathComponent);
  event.path = toStdString(_destination.path);
  event.received = size;
  event.total = _expected;
  event.tab = _options.tab;
  event.state = DownloadState::kProgress;
  emitDownload(_events, event);
}

- (void)downloadDidFinish:(WKDownload*)download {
  [_timer invalidate];
  _timer = nil;

  DownloadEvent event;
  event.id = toStdString(_identifier);
  event.path = toStdString(_destination.path);
  event.filename = toStdString(_destination.lastPathComponent);
  event.received = fileSize(_destination.path);
  event.total = event.received;
  event.tab = _options.tab;
  event.state = DownloadState::kFinished;
  emitDownload(_events, event);
  NSLog(@"Liora: download finished %@", _destination.path);
  untrackDownload(self);
}

- (void)download:(WKDownload*)download didFailWithError:(NSError*)error resumeData:(NSData*)resumeData {
  [_timer invalidate];
  _timer = nil;

  DownloadEvent event;
  event.id = toStdString(_identifier);
  event.path = _destination ? toStdString(_destination.path) : std::string();
  event.filename = _destination ? toStdString(_destination.lastPathComponent) : std::string();
  event.tab = _options.tab;
  event.state = DownloadState::kFailed;
  event.error = toStdString(error.localizedDescription);
  emitDownload(_events, event);
  NSLog(@"Liora: download failed: %@", error.localizedDescription);
  untrackDownload(self);
}

@end

// ---- Navigation and permissions -------------------------------------------

@interface LioraWebViewDelegate : NSObject <WKNavigationDelegate, WKUIDelegate>
- (instancetype)initWithEvents:(std::shared_ptr<ViewEvents>)events options:(const ViewOptions&)options;
@end

@implementation LioraWebViewDelegate {
  std::shared_ptr<ViewEvents> _events;
  ViewOptions _options;
}

- (instancetype)initWithEvents:(std::shared_ptr<ViewEvents>)events options:(const ViewOptions&)options {
  if ((self = [super init])) {
    _events = std::move(events);
    _options = options;
  }
  return self;
}

- (void)webView:(WKWebView*)webView
    didReceiveAuthenticationChallenge:(NSURLAuthenticationChallenge*)challenge
                    completionHandler:(void (^)(NSURLSessionAuthChallengeDisposition,
                                                NSURLCredential*))completionHandler {
  NSURLProtectionSpace* space = challenge.protectionSpace;
  if (_events && _events->onSecurity &&
      [space.authenticationMethod isEqualToString:NSURLAuthenticationMethodServerTrust] &&
      space.serverTrust != NULL) {
    liora::SecurityInfo info;
    info.host = toStdString(space.host);
    info.https = true;

    if (@available(macOS 12.0, *)) {
      CFArrayRef chain = SecTrustCopyCertificateChain(space.serverTrust);
      if (chain != NULL) {
        if (CFArrayGetCount(chain) > 0) {
          SecCertificateRef leaf =
              static_cast<SecCertificateRef>(const_cast<void*>(CFArrayGetValueAtIndex(chain, 0)));
          if (leaf) {
            CFStringRef summary = SecCertificateCopySubjectSummary(leaf);
            if (summary) {
              info.subject = toStdString((__bridge NSString*)summary);
              CFRelease(summary);
            }
            CFStringRef keys[] = {kSecOIDX509V1ValidityNotAfter};
            CFArrayRef keyList = CFArrayCreate(nullptr, (const void**)keys, 1, &kCFTypeArrayCallBacks);
            CFDictionaryRef values = SecCertificateCopyValues(leaf, keyList, nullptr);
            if (values) {
              NSDictionary* dict = CFBridgingRelease(values);
              NSDictionary* field = dict[(__bridge NSString*)kSecOIDX509V1ValidityNotAfter];
              NSNumber* value = field[(__bridge NSString*)kSecPropertyKeyValue];
              if (value) {
                // CFAbsoluteTime counts seconds from 2001-01-01, not from 1970.
                const double kCFAbsoluteEpoch = 978307200.0;
                info.notAfter = static_cast<int64_t>((value.doubleValue + kCFAbsoluteEpoch) * 1000.0);
              }
            }
            if (keyList) CFRelease(keyList);
          }
        }
        CFRelease(chain);
      }
    }

    CFErrorRef trustError = nullptr;
    info.valid = SecTrustEvaluateWithError(space.serverTrust, &trustError);
    if (trustError) CFRelease(trustError);

    NSLog(@"Liora: TLS %s valid=%d subject=%@", info.host.c_str(), (int)info.valid,
          toNSString(info.subject));
    _events->onSecurity(info);
  }
  completionHandler(NSURLSessionAuthChallengePerformDefaultHandling, nil);
}

- (void)webView:(WKWebView*)webView didFinishNavigation:(WKNavigation*)navigation {
  if (!_events) return;
  if (_events->onUrl) _events->onUrl(toStdString(webView.URL.absoluteString));
  if (_events->onTitle) _events->onTitle(toStdString(webView.title));
  if (_events->onLoading) _events->onLoading(false, 1.0);
  if (_events->onHistory) _events->onHistory(webView.canGoBack, webView.canGoForward);
}

- (void)webView:(WKWebView*)webView didStartProvisionalNavigation:(WKNavigation*)navigation {
  if (!_events) return;
  if (_events->onLoading) _events->onLoading(true, 0.0);
  if (_events->onUrl) _events->onUrl(toStdString(webView.URL.absoluteString));
}

- (void)webView:(WKWebView*)webView
    didFailProvisionalNavigation:(WKNavigation*)navigation
                       withError:(NSError*)error {
  if (!_events) return;
  if (_events->onLoading) _events->onLoading(false, 0);

  // 102 is WebKitErrorFrameLoadInterruptedByPolicyChange: the navigation became
  // a download. Cancellation means a newer navigation replaced this one.
  // Neither is a failure the user should see.
  if ([error.domain isEqualToString:@"WebKitErrorDomain"] && error.code == 102) return;
  if ([error.domain isEqualToString:NSURLErrorDomain] && error.code == NSURLErrorCancelled) return;

  if (_events->onError) {
    // webView.URL is nil when a provisional load fails, so take the failing
    // URL from the error itself.
    NSURL* failingURL = error.userInfo[NSURLErrorFailingURLErrorKey];
    NSString* failing = failingURL.absoluteString;
    if (!failing.length) failing = webView.URL.absoluteString;
    _events->onError(toStdString(failing), static_cast<int>(error.code),
                     toStdString(error.localizedDescription));
  }
}

- (void)webView:(WKWebView*)webView
    didFailNavigation:(WKNavigation*)navigation
            withError:(NSError*)error {
  if (!_events || !_events->onError) return;
  _events->onError(toStdString(webView.URL.absoluteString), static_cast<int>(error.code),
                   toStdString(error.localizedDescription));
}

- (void)webView:(WKWebView*)webView didCommitNavigation:(WKNavigation*)navigation {
  if (!_events || !_events->onHistory) return;
  _events->onHistory(webView.canGoBack, webView.canGoForward);
}

// target="_blank" and window.open stay in the same view unless the shell
// decides to open a real tab.
- (WKWebView*)webView:(WKWebView*)webView
    createWebViewWithConfiguration:(WKWebViewConfiguration*)configuration
               forNavigationAction:(WKNavigationAction*)action
                    windowFeatures:(WKWindowFeatures*)features {
  if (!action.targetFrame && _events && _events->onOpenWindow) {
    bool handled = _events->onOpenWindow(toStdString(action.request.URL.absoluteString), true);
    if (handled) return nil;
  }
  if (action.targetFrame == nil) [webView loadRequest:action.request];
  return nil;
}

- (void)webView:(WKWebView*)webView
    decidePolicyForNavigationAction:(WKNavigationAction*)navigationAction
                    decisionHandler:(void (^)(WKNavigationActionPolicy))decisionHandler {
  if (@available(macOS 11.3, *)) {
    if (navigationAction.shouldPerformDownload) {
      decisionHandler(WKNavigationActionPolicyDownload);
      return;
    }
  }
  decisionHandler(WKNavigationActionPolicyAllow);
}

- (void)webView:(WKWebView*)webView
    decidePolicyForNavigationResponse:(WKNavigationResponse*)navigationResponse
                      decisionHandler:(void (^)(WKNavigationResponsePolicy))decisionHandler {
  // A response the view cannot render (a binary, an attachment) is a download.
  if (!navigationResponse.canShowMIMEType) {
    decisionHandler(WKNavigationResponsePolicyDownload);
    return;
  }
  decisionHandler(WKNavigationResponsePolicyAllow);
}

- (void)webView:(WKWebView*)webView
    navigationAction:(WKNavigationAction*)navigationAction
    didBecomeDownload:(WKDownload*)download API_AVAILABLE(macos(11.3)) {
  [self attachDownload:download];
}

- (void)webView:(WKWebView*)webView
    navigationResponse:(WKNavigationResponse*)navigationResponse
     didBecomeDownload:(WKDownload*)download API_AVAILABLE(macos(11.3)) {
  [self attachDownload:download];
}

- (void)attachDownload:(WKDownload*)download {
  LioraDownloadDelegate* delegate = [[LioraDownloadDelegate alloc] initWithEvents:_events options:_options];
  download.delegate = delegate;
  trackDownload(delegate);
}

- (void)webView:(WKWebView*)webView
    requestMediaCapturePermissionForOrigin:(WKSecurityOrigin*)origin
                           initiatedByFrame:(WKFrameInfo*)frame
                                       type:(WKMediaCaptureType)type
                            decisionHandler:(void (^)(WKPermissionDecision))decisionHandler
    API_AVAILABLE(macos(12.0)) {
  if (@available(macOS 12.0, *)) {
    NSString* originText = [NSString stringWithFormat:@"%@://%@", origin.protocol, origin.host];
    std::string kind = "camera";
    if (type == WKMediaCaptureTypeMicrophone) {
      kind = "microphone";
    } else if (type == WKMediaCaptureTypeCameraAndMicrophone) {
      kind = "camera+microphone";
    }

    bool allow = false;
    if (_options.askPermission) allow = _options.askPermission(toStdString(originText), kind);
    NSLog(@"Liora: %s for %@ -> %@", kind.c_str(), originText, allow ? @"granted" : @"denied");
    decisionHandler(allow ? WKPermissionDecisionGrant : WKPermissionDecisionDeny);
    return;
  }
  decisionHandler(WKPermissionDecisionDeny);
}

@end

namespace liora::shell {
namespace {

class WebViewKernelView final : public IKernelView {
 public:
  WebViewKernelView(const ViewOptions& options, ViewEvents events)
      : events_(std::make_shared<ViewEvents>(std::move(events))) {
    WKWebViewConfiguration* configuration = [[WKWebViewConfiguration alloc] init];
    configuration.websiteDataStore = options.incognito
                                         ? [WKWebsiteDataStore nonPersistentDataStore]
                                         : [WKWebsiteDataStore defaultDataStore];
    configuration.defaultWebpagePreferences.allowsContentJavaScript = YES;

    delegate_ = [[LioraWebViewDelegate alloc] initWithEvents:events_ options:options];

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

  ~WebViewKernelView() override { events_.reset(); }

  void load(const std::string& url) override {
    NSURL* parsed = [NSURL URLWithString:toNSString(url)];
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

 private:
  WKWebView* view_ = nil;
  LioraWebViewDelegate* delegate_ = nil;
  std::shared_ptr<ViewEvents> events_;
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

#import <Cocoa/Cocoa.h>
#import <Security/Security.h>
#import <WebKit/WebKit.h>

#include <algorithm>
#include <memory>
#include <string>

#include "SystemKernel.h"

#include "liora/reader_script.hpp"

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

// Runs in the page after load: pick the declared icon (largest first), fall
// back to /favicon.ico, rasterise to a small PNG and hand it to the shell as a
// data URL. Cross-origin icons fail the canvas read and are simply skipped.
NSString* const kFaviconScript = @R"JS(
(function () {
  try {
    var links = Array.prototype.slice.call(
      document.querySelectorAll('link[rel~="icon" i], link[rel="shortcut icon" i], link[rel="apple-touch-icon" i], link[rel="apple-touch-icon-precomposed" i]'));
    function sizeOf(link) {
      var sizes = (link.getAttribute('sizes') || '').toLowerCase();
      if (!sizes || sizes === 'any') return 64;
      var best = 0;
      sizes.split(/\s+/).forEach(function (token) {
        var parts = token.split('x');
        var value = parseInt(parts[0], 10);
        if (value > best) best = value;
      });
      return best || 64;
    }
    links.sort(function (a, b) { return sizeOf(b) - sizeOf(a); });
    var candidates = [];
    links.forEach(function (link) {
      var href = link.getAttribute('href');
      if (!href) return;
      try { candidates.push(new URL(href, document.baseURI).href); } catch (e) {}
    });
    candidates.push(new URL('/favicon.ico', location.href).href);
    var seen = {};
    candidates = candidates.filter(function (url) {
      if (seen[url]) return false;
      seen[url] = true;
      return true;
    }).slice(0, 4);
    function finish(dataUrl) {
      if (window.webkit && window.webkit.messageHandlers && window.webkit.messageHandlers.lioraFavicon) {
        window.webkit.messageHandlers.lioraFavicon.postMessage(dataUrl || '');
      }
    }
    function tryIndex(index) {
      if (index >= candidates.length) { finish(''); return; }
      var image = new Image();
      image.onload = function () {
        try {
          var size = 64;
          var canvas = document.createElement('canvas');
          canvas.width = size;
          canvas.height = size;
          var context = canvas.getContext('2d');
          var width = image.naturalWidth || size;
          var height = image.naturalHeight || size;
          var scale = Math.min(size / width, size / height);
          var drawWidth = Math.max(1, Math.round(width * scale));
          var drawHeight = Math.max(1, Math.round(height * scale));
          context.drawImage(image, (size - drawWidth) / 2, (size - drawHeight) / 2, drawWidth, drawHeight);
          finish(canvas.toDataURL('image/png'));
        } catch (e) { tryIndex(index + 1); }
      };
      image.onerror = function () { tryIndex(index + 1); };
      image.src = candidates[index];
    }
    tryIndex(0);
  } catch (e) {}
})();
)JS";

}  // namespace

// ---- Download -------------------------------------------------------------

@interface LioraDownloadDelegate : NSObject <WKDownloadDelegate>
- (instancetype)initWithEvents:(std::shared_ptr<ViewEvents>)events options:(const ViewOptions&)options;
@property(nonatomic, weak) WKDownload* download;
@property(nonatomic, strong, nullable) NSData* resumeData;
@property(nonatomic, copy, nullable) NSString* sourceURL;
@property(nonatomic, strong, nullable) NSURL* lastDestination;
@property(nonatomic, assign) BOOL cancelled;
- (NSString*)identifier;
- (void)cancel;
- (void)restartProgressTimer;
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

- (NSString*)identifier {
  return _identifier;
}

- (void)cancel {
  self.cancelled = YES;
  [_timer invalidate];
  _timer = nil;
  if (self.download) {
    [self.download cancel:^(NSData* resumeData) {
      // Keep the resume data so a retry can continue where this left off.
      if (resumeData) self.resumeData = resumeData;
      // WebKit does not call didFailWithError after a user cancel, so report
      // the cancelled state here instead of leaving the row stuck at its
      // last progress value.
      [self reportCancelled];
    }];
  } else {
    [self reportCancelled];
  }
}

- (void)restartProgressTimer {
  [_timer invalidate];
  _timer = [NSTimer scheduledTimerWithTimeInterval:0.4
                                           repeats:YES
                                             block:^(NSTimer* timer) {
                                               [self tick];
                                             }];
}

- (void)reportCancelled {
  DownloadEvent event;
  event.id = toStdString(_identifier);
  event.path = _destination ? toStdString(_destination.path) : std::string();
  event.filename = _destination ? toStdString(_destination.lastPathComponent) : std::string();
  event.received = _destination ? fileSize(_destination.path) : 0;
  event.total = _expected;
  event.tab = _options.tab;
  event.state = DownloadState::kFailed;
  event.error = "cancelled";
  event.resumable = self.resumeData != nil;
  emitDownload(_events, event);
  NSLog(@"Liora: download cancelled %@", _destination.path);
  untrackDownload(self);
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

  if (self.lastDestination) {
    // A retry continues into the same file instead of asking again or
    // creating a "(1)" copy next to the partial one.
    target = self.lastDestination;
  } else if (_options.askWhereToSave) {
    NSSavePanel* panel = [NSSavePanel savePanel];
    panel.nameFieldStringValue = name;
    panel.directoryURL = [NSURL fileURLWithPath:directory];
    if ([panel runModal] == NSModalResponseOK && panel.URL) target = panel.URL;
  } else {
    target = uniqueURL(target);
  }
  self.lastDestination = target;

  _destination = target;
  _expected = response.expectedContentLength;
  if ([response.URL isKindOfClass:[NSURL class]]) self.sourceURL = response.URL.absoluteString;

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

  if (resumeData) self.resumeData = resumeData;

  DownloadEvent event;
  event.id = toStdString(_identifier);
  event.path = _destination ? toStdString(_destination.path) : std::string();
  event.filename = _destination ? toStdString(_destination.lastPathComponent) : std::string();
  event.received = _destination ? fileSize(_destination.path) : 0;
  event.total = _expected;
  event.tab = _options.tab;
  event.state = DownloadState::kFailed;
  event.error = self.cancelled ? "cancelled" : toStdString(error.localizedDescription);
  event.resumable = self.resumeData != nil;
  emitDownload(_events, event);
  NSLog(@"Liora: download %@: %@", self.cancelled ? @"cancelled" : @"failed", error.localizedDescription);
  untrackDownload(self);
}

@end

// ---- Favicon bridge ---------------------------------------------------------

// Receives the data URL produced by kFaviconScript. A separate object so the
// web view configuration does not retain the kernel view.
@interface LioraFaviconHandler : NSObject <WKScriptMessageHandler>
- (instancetype)initWithEvents:(std::shared_ptr<ViewEvents>)events;
@end

@implementation LioraFaviconHandler {
  std::weak_ptr<ViewEvents> _events;
}

- (instancetype)initWithEvents:(std::shared_ptr<ViewEvents>)events {
  if ((self = [super init])) _events = events;
  return self;
}

- (void)userContentController:(WKUserContentController*)controller
      didReceiveScriptMessage:(WKScriptMessage*)message {
  auto events = _events.lock();
  if (!events || !events->onFavicon) return;
  if (![message.body isKindOfClass:[NSString class]]) return;
  NSString* text = (NSString*)message.body;
  // Only small inline images; anything else is ignored rather than trusted.
  if (text.length > 200000 || ![text hasPrefix:@"data:image/"]) return;
  events->onFavicon(toStdString(text));
}

@end

// ---- Title observation ------------------------------------------------------

// WKWebView.title is KVO-compliant. Forward changes so a title set by page
// script (or a user script) after load reaches the tab.
@interface LioraTitleObserver : NSObject
- (instancetype)initWithEvents:(std::shared_ptr<ViewEvents>)events;
@end

@implementation LioraTitleObserver {
  std::weak_ptr<ViewEvents> _events;
}

- (instancetype)initWithEvents:(std::shared_ptr<ViewEvents>)events {
  if ((self = [super init])) _events = events;
  return self;
}

- (void)observeValueForKeyPath:(NSString*)keyPath
                      ofObject:(id)object
                        change:(NSDictionary<NSKeyValueChangeKey, id>*)change
                       context:(void*)context {
  if (![keyPath isEqualToString:@"title"]) return;
  auto events = _events.lock();
  if (!events || !events->onTitle) return;
  NSString* title = change[NSKeyValueChangeNewKey];
  if (![title isKindOfClass:[NSString class]] || !title.length) return;
  events->onTitle(toStdString(title));
}

@end

// ---- Navigation and permissions -------------------------------------------

@interface LioraWebViewDelegate : NSObject <WKNavigationDelegate, WKUIDelegate>
- (instancetype)initWithEvents:(std::shared_ptr<ViewEvents>)events options:(const ViewOptions&)options;
// Downloads started by this view, keyed by the id reported to the interface.
// Delegates stay here after failure so a retry can reuse their resume data.
@property(nonatomic, readonly) NSMutableDictionary<NSString*, LioraDownloadDelegate*>* downloads;
@end

@implementation LioraWebViewDelegate {
  std::shared_ptr<ViewEvents> _events;
  ViewOptions _options;
}

- (instancetype)initWithEvents:(std::shared_ptr<ViewEvents>)events options:(const ViewOptions&)options {
  if ((self = [super init])) {
    _events = std::move(events);
    _options = options;
    _downloads = [NSMutableDictionary dictionary];
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
  if (_events->onFavicon) {
    [webView evaluateJavaScript:kFaviconScript completionHandler:nil];
  }
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
  delegate.download = download;
  download.delegate = delegate;
  self.downloads[[delegate identifier]] = delegate;
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
    if (options.incognito) {
      configuration.websiteDataStore = [WKWebsiteDataStore nonPersistentDataStore];
    } else if (!options.containerId.empty()) {
      // macOS 14+ supports named persistent data stores. Older systems still
      // get container isolation, but without persistence.
      WKWebsiteDataStore* store = nil;
      if (@available(macOS 14.0, *)) {
        NSUUID* identifier = [[NSUUID alloc] initWithUUIDString:toNSString(options.containerId)];
        if (identifier) store = [WKWebsiteDataStore dataStoreForIdentifier:identifier];
      }
      configuration.websiteDataStore = store ?: [WKWebsiteDataStore nonPersistentDataStore];
    } else {
      configuration.websiteDataStore = [WKWebsiteDataStore defaultDataStore];
    }
    configuration.defaultWebpagePreferences.allowsContentJavaScript = YES;

    faviconHandler_ = [[LioraFaviconHandler alloc] initWithEvents:events_];
    [configuration.userContentController addScriptMessageHandler:faviconHandler_
                                                            name:@"lioraFavicon"];

    delegate_ = [[LioraWebViewDelegate alloc] initWithEvents:events_ options:options];

    view_ = [[WKWebView alloc] initWithFrame:NSMakeRect(0, 0, 800, 600) configuration:configuration];
    view_.navigationDelegate = delegate_;
    view_.UIDelegate = delegate_;
    view_.allowsBackForwardNavigationGestures = YES;
    view_.allowsMagnification = NO;
    if (@available(macOS 13.3, *)) {
      view_.inspectable = YES;
    }
    // Pages and user scripts can change document.title after load; observe it
    // so the tab title stays current instead of freezing at navigation time.
    titleObserver_ = [[LioraTitleObserver alloc] initWithEvents:events_];
    [view_ addObserver:titleObserver_ forKeyPath:@"title" options:NSKeyValueObservingOptionNew context:nil];
    if (!options.url.empty()) load(options.url);
  }

  ~WebViewKernelView() override {
    [view_ removeObserver:titleObserver_ forKeyPath:@"title"];
    [view_.configuration.userContentController removeScriptMessageHandlerForName:@"lioraFavicon"];
    events_.reset();
  }

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

  void setContentControls(const liora::ContentControls& controls) override {
    controls_ = controls;
    // JavaScript is a real WebKit preference; it applies to the next load.
    view_.configuration.defaultWebpagePreferences.allowsContentJavaScript =
        controls.javascript ? YES : NO;
    rebuildInjectedScripts();
  }

  void setUserScripts(const std::vector<liora::InjectedScript>& scripts) override {
    userScripts_ = scripts;
    rebuildInjectedScripts();
  }

  // Content-policy scripts and user scripts share one WKUserContentController.
  // Rebuild both together so neither wipes the other.
  void rebuildInjectedScripts() {
    WKUserContentController* controller = view_.configuration.userContentController;
    [controller removeAllUserScripts];

    // Images and autoplay have no native per-view switch, so a document-start
    // script enforces them.
    NSMutableString* policy = [NSMutableString string];
    if (!controls_.images) {
      [policy appendString:@"img, picture, source[type^='image'], svg image "
                           "{ visibility: hidden !important; }"];
    }
    if (controls_.autoplay != "allow") {
      NSString* condition = controls_.autoplay == "block"
                                ? @"true"
                                : @"!media.muted && media.volume > 0";
      [policy appendFormat:
          @"(function(){var stop=function(media){if(%@){try{media.pause();}catch(e){}}};"
           "document.addEventListener('play',function(e){stop(e.target);},true);"
           "var mo=new MutationObserver(function(){document.querySelectorAll('video,audio')"
           ".forEach(function(m){if(m.autoplay){stop(m);}});});"
           "mo.observe(document.documentElement,{childList:true,subtree:true,attributes:true,"
           "attributeFilter:['autoplay']});})();",
          condition];
    }
    if (policy.length) {
      [controller addUserScript:[[WKUserScript alloc]
                                    initWithSource:policy
                                     injectionTime:WKUserScriptInjectionTimeAtDocumentStart
                                  forMainFrameOnly:NO]];
    }

    for (const auto& script : userScripts_) {
      if (script.code.empty()) continue;
      // "start" runs before page scripts; "end"/"idle" run after the document
      // is parsed. WebKit has no idle time, so idle maps to document end.
      WKUserScriptInjectionTime time = script.runAt == "start"
                                           ? WKUserScriptInjectionTimeAtDocumentStart
                                           : WKUserScriptInjectionTimeAtDocumentEnd;
      [controller addUserScript:[[WKUserScript alloc]
                                    initWithSource:toNSString(script.code)
                                     injectionTime:time
                                  forMainFrameOnly:NO]];
    }
  }

  void find(const std::string& text, bool forward, bool matchCase, bool newQuery) override {
    if (text.empty()) return;
    WKFindConfiguration* configuration = [[WKFindConfiguration alloc] init];
    configuration.backwards = !forward;
    configuration.caseSensitive = matchCase;
    configuration.wraps = YES;

    if (newQuery || text != findText_ || matchCase != findCase_) {
      findText_ = text;
      findCase_ = matchCase;
      findTotal_ = 0;
      findActive_ = 0;
      auto events = events_;
      bool backwards = !forward;
      recountMatches(text, matchCase, [this, text, configuration, events, backwards]() {
        if (backwards) {
          // Start a backwards search at the last match: select the first,
          // then step back once — wraps=YES lands on the last occurrence.
          [view_ findString:toNSString(text)
              withConfiguration:configuration
            completionHandler:^(WKFindResult* result) {
              if (!result.matchFound) {
                if (events && events->onFindResult) events->onFindResult(0, 0);
                return;
              }
              if (findTotal_ <= 0) {
                findTotal_ = 1;
                findActive_ = 1;
                if (events && events->onFindResult) events->onFindResult(1, 1);
                return;
              }
              findActive_ = 1;
              WKFindConfiguration* back = [[WKFindConfiguration alloc] init];
              back.backwards = YES;
              back.caseSensitive = findCase_;
              back.wraps = YES;
              stepFind(text, false, back);
            }];
          return;
        }
        // Report the total before a match is selected, then let WebKit pick
        // the first match and locate its real index.
        if (findTotal_ > 0 && events && events->onFindResult) events->onFindResult(findTotal_, 0);
        [view_ findString:toNSString(text)
            withConfiguration:configuration
          completionHandler:^(WKFindResult* result) {
            if (!result.matchFound) {
              if (events && events->onFindResult) events->onFindResult(0, 0);
              return;
            }
            if (findTotal_ <= 0) {
              // WebKit found a match the DOM walk missed (shadow root etc.);
              // report it honestly instead of claiming "no results".
              findTotal_ = 1;
              findActive_ = 1;
              if (events && events->onFindResult) events->onFindResult(1, 1);
              return;
            }
            locateActive(text, [this, events](int active) {
              findActive_ = active > 0 ? active : 1;
              if (events && events->onFindResult) events->onFindResult(findTotal_, findActive_);
            });
          }];
      });
      return;
    }
    stepFind(text, forward, configuration);
  }

  void stopFinding(bool clearSelection) override {
    findText_.clear();
    findTotal_ = 0;
    findActive_ = 0;
    if (!clearSelection) return;
    [view_ evaluateJavaScript:@"window.getSelection().removeAllRanges()"
            completionHandler:nil];
  }

 private:
  // WebKit's findString reports only whether a match exists, so the total is
  // counted from the DOM text (TreeWalker over visible text nodes) and the
  // active index is derived from the selection offset within the same walk.
  // Shadow DOM and iframes are outside this count; the selection itself is
  // still owned by WebKit.
  void recountMatches(const std::string& text, bool matchCase, void (^done)(void)) {
    NSString* script = [NSString
        stringWithFormat:@"(function(){var q=%@;var cs=%@;if(!q)return 0;"
                          "var walker=document.createTreeWalker(document.documentElement,NodeFilter.SHOW_TEXT,{"
                          "acceptNode:function(n){var p=n.parentElement;if(!p)return NodeFilter.FILTER_REJECT;"
                          "var tag=p.tagName;if(tag==='SCRIPT'||tag==='STYLE'||tag==='NOSCRIPT'||tag==='TEMPLATE')return NodeFilter.FILTER_REJECT;"
                          "for(var a=p;a&&a!==document.documentElement;a=a.parentElement){"
                          "var s=window.getComputedStyle(a);if(s.display==='none'||s.visibility==='hidden')return NodeFilter.FILTER_REJECT;}"
                          "return NodeFilter.FILTER_ACCEPT;}});"
                          "var total=0,node;var needle=cs?q:q.toLowerCase();"
                          "while((node=walker.nextNode())){"
                          "var hay=node.nodeValue;if(!cs)hay=hay.toLowerCase();"
                          "var i=hay.indexOf(needle);while(i!==-1){total++;i=hay.indexOf(needle,i+needle.length);}}"
                          "return total;})();",
                         jsonLiteral(toNSString(text)), matchCase ? @"true" : @"false"];
    auto events = events_;
    [view_ evaluateJavaScript:script
            completionHandler:^(id result, NSError* error) {
              findTotal_ = (!error && [result isKindOfClass:[NSNumber class]]) ? [result intValue] : 0;
              if (findTotal_ == 0 && events && events->onFindResult) {
                events->onFindResult(0, 0);
              }
              done();
            }];
  }

  void stepFind(const std::string& text, bool forward, WKFindConfiguration* configuration) {
    auto events = events_;
    bool backwards = !forward;
    [view_ findString:toNSString(text)
        withConfiguration:configuration
      completionHandler:^(WKFindResult* result) {
        if (!result.matchFound) {
          findActive_ = 0;
          if (events && events->onFindResult) events->onFindResult(0, 0);
          return;
        }
        if (findTotal_ <= 0) {
          // The DOM count missed (e.g. the match lives in a shadow root);
          // report that a match is selected without inventing a total.
          if (events && events->onFindResult) events->onFindResult(1, 1);
          return;
        }
        // Advance the cached index by one step, wrapping like WebKit does.
        if (findActive_ == 0) {
          findActive_ = backwards ? findTotal_ : 1;
        } else {
          findActive_ = backwards ? (findActive_ == 1 ? findTotal_ : findActive_ - 1)
                                  : (findActive_ == findTotal_ ? 1 : findActive_ + 1);
        }
        locateActive(text, [this, events](int active) {
          const int reported = active > 0 ? active : findActive_;
          if (events && events->onFindResult) events->onFindResult(findTotal_, reported);
        });
      }];
  }

  // Recomputes which occurrence the current selection is, so the reported
  // index stays honest after wrap-around or a mid-page start.
  void locateActive(const std::string& text, std::function<void(int)> done) {
    NSString* script = [NSString
        stringWithFormat:@"(function(){var q=%@;var cs=%@;var sel=window.getSelection();"
                          "if(!sel||sel.rangeCount===0||!q)return 0;"
                          "var range=sel.getRangeAt(0);"
                          "var sc=range.startContainer,so=range.startOffset;"
                          "if(sc.nodeType!==3){var txt=sel.toString();if(!txt)return 0;"
                          "var w2=document.createTreeWalker(document.documentElement,NodeFilter.SHOW_TEXT,null);"
                          "var n2;while((n2=w2.nextNode())){var v=n2.nodeValue,probe=txt;"
                          "if(!cs){v=v.toLowerCase();probe=probe.toLowerCase();}"
                          "var at=v.indexOf(probe);if(at!==-1){sc=n2;so=at;break;}}}"
                          "var walker=document.createTreeWalker(document.documentElement,NodeFilter.SHOW_TEXT,{"
                          "acceptNode:function(n){var p=n.parentElement;if(!p)return NodeFilter.FILTER_REJECT;"
                          "var tag=p.tagName;if(tag==='SCRIPT'||tag==='STYLE'||tag==='NOSCRIPT'||tag==='TEMPLATE')return NodeFilter.FILTER_REJECT;"
                          "for(var a=p;a&&a!==document.documentElement;a=a.parentElement){"
                          "var s=window.getComputedStyle(a);if(s.display==='none'||s.visibility==='hidden')return NodeFilter.FILTER_REJECT;}"
                          "return NodeFilter.FILTER_ACCEPT;}});"
                          "var index=0,node,fallback=0;while((node=walker.nextNode())){"
                          "var hay=node.nodeValue,needle=q;if(!cs){hay=hay.toLowerCase();needle=needle.toLowerCase();}"
                          "var i=hay.indexOf(needle);while(i!==-1){"
                          "index++;"
                          "if(node===sc){"
                          "if(i===so)return index;"
                          "if(!fallback&&so>=i&&so<=i+needle.length)fallback=index;}"
                          "i=hay.indexOf(needle,i+needle.length);}}"
                          "return fallback;})();",
                         jsonLiteral(toNSString(text)), findCase_ ? @"true" : @"false"];
    [view_ evaluateJavaScript:script
            completionHandler:^(id result, NSError* error) {
              int active = 0;
              if (!error && [result isKindOfClass:[NSNumber class]]) active = [result intValue];
              if (active > 0) findActive_ = active;
              done(active);
            }];
  }

  static NSString* jsonLiteral(NSString* value) {
    NSData* data = [NSJSONSerialization dataWithJSONObject:@[ value ] options:0 error:nil];
    NSString* array = [[NSString alloc] initWithData:data encoding:NSUTF8StringEncoding];
    // Strip the surrounding [ ] to get a safely escaped JSON string literal.
    return [array substringWithRange:NSMakeRange(1, array.length - 2)];
  }

 public:
  void extractReader(std::function<void(const std::string&)> done) override {
    auto callback = std::make_shared<std::function<void(const std::string&)>>(std::move(done));
    [view_ evaluateJavaScript:toNSString(liora::kReaderScript)
            completionHandler:^(id result, NSError* error) {
              std::string json;
              if (!error && [result isKindOfClass:[NSString class]]) {
                json = toStdString((NSString*)result);
              }
              if (callback && *callback) (*callback)(json);
            }];
  }

  void postMessage(const std::string& json) override {
    NSString* script = [NSString
        stringWithFormat:@"window.__lioraKernelDeliver && window.__lioraKernelDeliver(%@);",
                         toNSString(json)];
    [view_ evaluateJavaScript:script completionHandler:nil];
  }

  void* nativeHandle() const override { return (__bridge void*)view_; }

  bool hasDownload(const std::string& downloadId) override {
    return delegate_.downloads[toNSString(downloadId)] != nil;
  }

  void cancelDownload(const std::string& downloadId) override {
    LioraDownloadDelegate* delegate = delegate_.downloads[toNSString(downloadId)];
    if (delegate) [delegate cancel];
  }

  bool retryDownload(const std::string& downloadId) override {
    LioraDownloadDelegate* delegate = delegate_.downloads[toNSString(downloadId)];
    if (!delegate) return false;

    // Reuse the delegate: same id, so the interface updates the failed row in
    // place, and the saved destination/ask options carry over.
    delegate.cancelled = NO;
    NSData* resumeData = delegate.resumeData;
    delegate.resumeData = nil;
    NSString* sourceURL = delegate.sourceURL;
    WKWebView* view = view_;

    void (^attach)(WKDownload*) = ^(WKDownload* replacement) {
      if (!replacement) return;
      replacement.delegate = delegate;
      delegate.download = replacement;
      // A resumed download does not call decideDestination again, so restart
      // the progress timer here instead of waiting for a callback that never
      // comes.
      [delegate restartProgressTimer];
      trackDownload(delegate);
    };

    if (resumeData) {
      [view resumeDownloadFromResumeData:resumeData completionHandler:attach];
      return true;
    }
    if (sourceURL.length) {
      // No resume data (e.g. the server does not support ranges): start the
      // same request again rather than pretending to continue.
      NSURL* url = [NSURL URLWithString:sourceURL];
      if (url) {
        [view startDownloadUsingRequest:[NSURLRequest requestWithURL:url] completionHandler:attach];
        return true;
      }
    }
    return false;
  }

 private:
  WKWebView* view_ = nil;
  LioraWebViewDelegate* delegate_ = nil;
  LioraFaviconHandler* faviconHandler_ = nil;
  LioraTitleObserver* titleObserver_ = nil;
  std::shared_ptr<ViewEvents> events_;
  liora::ContentControls controls_;
  std::vector<liora::InjectedScript> userScripts_;
  std::string findText_;
  bool findCase_ = false;
  int findTotal_ = 0;
  int findActive_ = 0;
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
  uint32_t caps = kCapContentControls;
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

// Liora macOS shell: a Cocoa window that hosts the WebUI and one kernel view.
// All state lives in Core; this file only moves pixels and messages.

#import <Cocoa/Cocoa.h>
#import <Network/Network.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
#import <WebKit/WebKit.h>

#include <fcntl.h>
#include <sys/file.h>

#include <cstdlib>
#include <map>
#include <memory>
#include <string>

#import "LioraNet.h"
#include "SystemKernel.h"
#include "liora/core.hpp"
#include "liora/paths.hpp"

#ifdef LIORA_WITH_CEF
#include "liora/cef_kernel.hpp"
#endif

namespace {

NSString* ns(const std::string& value) {
  return [NSString stringWithUTF8String:value.c_str()] ?: @"";
}

std::string stdString(NSString* value) { return value ? std::string([value UTF8String]) : std::string(); }

NSString* jsonEscape(id object) {
  NSData* data = [NSJSONSerialization dataWithJSONObject:@[ object ] options:0 error:nil];
  if (!data) return @"null";
  NSString* array = [[NSString alloc] initWithData:data encoding:NSUTF8StringEncoding];
  // Strip the surrounding brackets to get a standalone JSON value.
  return [array substringWithRange:NSMakeRange(1, array.length - 2)];
}

}  // namespace

@class LioraController;

// ---- Scheme handler -------------------------------------------------------

@interface LioraSchemeHandler : NSObject <WKURLSchemeHandler>
@property(nonatomic, copy) NSString* root;
@end

@implementation LioraSchemeHandler

- (void)webView:(WKWebView*)webView startURLSchemeTask:(id<WKURLSchemeTask>)task {
  NSURL* url = task.request.URL;
  NSString* path = url.path.length ? url.path : @"/index.html";
  if ([path isEqualToString:@"/"]) path = @"/index.html";

  // Keep every request inside the asset directory.
  NSString* relative = [path stringByTrimmingCharactersInSet:[NSCharacterSet characterSetWithCharactersInString:@"/"]];
  NSString* full = [self.root stringByAppendingPathComponent:relative];
  NSString* resolved = [full stringByStandardizingPath];
  NSString* rootResolved = [self.root stringByStandardizingPath];
  if (![resolved hasPrefix:rootResolved]) {
    [task didFailWithError:[NSError errorWithDomain:@"io.liora" code:403 userInfo:nil]];
    return;
  }

  NSData* data = [NSData dataWithContentsOfFile:resolved];
  if (!data) {
    [task didFailWithError:[NSError errorWithDomain:@"io.liora" code:404 userInfo:nil]];
    return;
  }

  NSString* ext = resolved.pathExtension.lowercaseString;
  NSDictionary* types = @{
    @"html" : @"text/html",
    @"js" : @"text/javascript",
    @"mjs" : @"text/javascript",
    @"css" : @"text/css",
    @"json" : @"application/json",
    @"svg" : @"image/svg+xml",
    @"png" : @"image/png",
    @"woff2" : @"font/woff2",
  };
  NSString* mime = types[ext] ?: @"application/octet-stream";
  NSHTTPURLResponse* response =
      [[NSHTTPURLResponse alloc] initWithURL:url
                                 statusCode:200
                                HTTPVersion:@"HTTP/1.1"
                               headerFields:@{@"Content-Type" : mime, @"Cache-Control" : @"no-store"}];
  [task didReceiveResponse:response];
  [task didReceiveData:data];
  [task didFinish];
}

- (void)webView:(WKWebView*)webView stopURLSchemeTask:(id<WKURLSchemeTask>)task {}

@end

// ---- Bridge ---------------------------------------------------------------

@interface LioraBridge : NSObject <WKScriptMessageHandler>
@property(nonatomic, weak) LioraController* controller;
@end

// ---- Window controller ----------------------------------------------------

@interface LioraController : NSObject <NSApplicationDelegate, WKNavigationDelegate>
@property(nonatomic, strong) NSWindow* window;
@property(nonatomic, strong) WKWebView* ui;
@property(nonatomic, strong) NSView* container;
@property(nonatomic, strong) LioraNetService* netService;
@property(nonatomic, assign) CGRect pendingContentRect;
@property(nonatomic, assign) BOOL contentVisible;
@property(nonatomic, assign) int64_t activeTab;
@property(nonatomic, assign) BOOL refreshPending;
@property(nonatomic, assign) BOOL openedLaunchUrl;

- (void)refreshNetworkNow;
- (void)scheduleNetworkRefresh;
@end

// The message handler is a separate object so the web view configuration does
// not retain the controller (which owns the web view).
@interface LioraController (Messages)
- (void)userContentController:(WKUserContentController*)controller
      didReceiveScriptMessage:(WKScriptMessage*)message;
@end

@implementation LioraController {
  std::unique_ptr<liora::Core> _core;
  // One kernel view per tab. The shell only positions and activates them.
  std::map<int64_t, std::unique_ptr<liora::IKernelView>> _views;
}

- (void)applicationDidFinishLaunching:(NSNotification*)notification {
  [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
  [self buildMenu];

  NSRect frame = NSMakeRect(0, 0, 1280, 840);
  NSWindowStyleMask style = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                            NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable |
                            NSWindowStyleMaskFullSizeContentView;
  self.window = [[NSWindow alloc] initWithContentRect:frame styleMask:style backing:NSBackingStoreBuffered defer:NO];
  self.window.title = @"Liora";
  self.window.titlebarAppearsTransparent = YES;
  self.window.titleVisibility = NSWindowTitleHidden;
  self.window.minSize = NSMakeSize(720, 480);
  self.window.backgroundColor = [NSColor windowBackgroundColor];
  [self.window center];

  self.container = [[NSView alloc] initWithFrame:frame];
  self.container.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
  self.window.contentView = self.container;

  [self startCore];
  [self buildInterface];

  [self.window makeKeyAndOrderFront:nil];
  [NSApp activateIgnoringOtherApps:YES];
}

// Deep link for automation: once the interface is ready, ask it to navigate.
// Going through the interface keeps one code path for real navigation.
- (void)webView:(WKWebView*)webView didFinishNavigation:(WKNavigation*)navigation {
  if (webView != self.ui || self.openedLaunchUrl) return;
  self.openedLaunchUrl = YES;

  const char* tabs = getenv("LIORA_OPEN_TABS");
  if (tabs && *tabs) {
    NSString* list = ns(tabs);
    // Let the interface finish mounting before calling into it.
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 300 * NSEC_PER_MSEC), dispatch_get_main_queue(), ^{
      for (NSString* entry in [list componentsSeparatedByString:@","]) {
        NSString* url = [entry stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceCharacterSet]];
        if (!url.length) continue;
        NSString* script = [NSString stringWithFormat:@"window.__lioraNewTab && window.__lioraNewTab(%@);",
                                                      jsonEscape(url)];
        [self.ui evaluateJavaScript:script
                  completionHandler:^(id result, NSError* error) {
                    if (error) NSLog(@"Liora: new tab script failed: %@", error.localizedDescription);
                  }];
        NSLog(@"Liora: opening tab %@", url);
      }
    });
    return;
  }

  const char* target = getenv("LIORA_OPEN_URL");
  if (!target || !*target) return;
  dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 300 * NSEC_PER_MSEC), dispatch_get_main_queue(), ^{
    NSString* script = [NSString stringWithFormat:@"window.__lioraOpenUrl && window.__lioraOpenUrl(%@);",
                                                  jsonEscape(ns(target))];
    [self.ui evaluateJavaScript:script
              completionHandler:^(id result, NSError* error) {
                if (error) NSLog(@"Liora: open url script failed: %@", error.localizedDescription);
              }];
    NSLog(@"Liora: opening %s", target);
  });
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication*)sender { return YES; }

- (void)applicationWillTerminate:(NSNotification*)notification {
  [self.netService stop];
  if (_core) _core->stop();
}

- (void)buildMenu {
  NSMenu* main = [[NSMenu alloc] init];

  NSMenuItem* appItem = [[NSMenuItem alloc] init];
  NSMenu* appMenu = [[NSMenu alloc] init];
  [appMenu addItemWithTitle:@"关于 Liora" action:@selector(orderFrontStandardAboutPanel:) keyEquivalent:@""];
  [appMenu addItem:[NSMenuItem separatorItem]];
  [appMenu addItemWithTitle:@"隐藏 Liora" action:@selector(hide:) keyEquivalent:@"h"];
  [appMenu addItemWithTitle:@"退出 Liora" action:@selector(terminate:) keyEquivalent:@"q"];
  appItem.submenu = appMenu;
  [main addItem:appItem];

  NSMenuItem* editItem = [[NSMenuItem alloc] init];
  NSMenu* edit = [[NSMenu alloc] initWithTitle:@"编辑"];
  [edit addItemWithTitle:@"撤销" action:@selector(undo:) keyEquivalent:@"z"];
  [edit addItemWithTitle:@"重做" action:@selector(redo:) keyEquivalent:@"Z"];
  [edit addItem:[NSMenuItem separatorItem]];
  [edit addItemWithTitle:@"剪切" action:@selector(cut:) keyEquivalent:@"x"];
  [edit addItemWithTitle:@"复制" action:@selector(copy:) keyEquivalent:@"c"];
  [edit addItemWithTitle:@"粘贴" action:@selector(paste:) keyEquivalent:@"v"];
  [edit addItemWithTitle:@"全选" action:@selector(selectAll:) keyEquivalent:@"a"];
  editItem.submenu = edit;
  [main addItem:editItem];

  NSMenuItem* viewItem = [[NSMenuItem alloc] init];
  NSMenu* view = [[NSMenu alloc] initWithTitle:@"显示"];
  [view addItemWithTitle:@"重新加载" action:@selector(reloadInterface:) keyEquivalent:@"r"];
  viewItem.submenu = view;
  [main addItem:viewItem];

  NSApp.mainMenu = main;
}

- (void)startCore {
  using namespace liora;
  Core::Options options;
  options.paths = CorePaths::forProfile(paths::dataRoot() / "Profiles" / "Default");
  _core = std::make_unique<Core>(options);

  if (auto error = _core->start()) {
    NSAlert* alert = [[NSAlert alloc] init];
    alert.messageText = @"Liora 无法启动";
    alert.informativeText = ns(*error);
    [alert runModal];
    [NSApp terminate:nil];
    return;
  }

  shell::registerSystemKernel(_core->kernels());
  // Start liora-net and point WebKit at it before any page loads.
  [self refreshNetworkNow];
#ifdef LIORA_WITH_CEF
  [self startCefIfSelected];
#endif

  __weak LioraController* weakSelf = self;
  _core->setNotificationSink([weakSelf](const liora::Json& message) {
    NSString* text = ns(message.dump());
    std::string method = message.value("method", std::string());
    dispatch_async(dispatch_get_main_queue(), ^{
      [weakSelf deliver:text];
      // Network settings changed: rebuild the local resolver and proxy.
      if (method == "net.rulesChanged" || method == "settings.changed") {
        [weakSelf scheduleNetworkRefresh];
      }
    });
  });
}

- (LioraNetService*)netService {
  if (!_netService) _netService = [[LioraNetService alloc] init];
  return _netService;
}

#ifdef LIORA_WITH_CEF
// Chromium is heavy and its network switches are start-up arguments, so it is
// only initialized when the user actually selected it.
- (void)startCefIfSelected {
  if (!_core) return;
  // Read the setting directly: resolve() only returns kernels that are already
  // available, which is exactly what initialisation establishes.
  liora::Json chosen = _core->settings().get("kernel.default");
  if (!chosen.is_string() || chosen.get<std::string>() != liora::KernelManager::kCef) return;

  liora::cef::Startup startup;
  startup.cachePath = liora::pathToUtf8(liora::paths::dataRoot() / "CEF");
  startup.switches = _core->applyNetwork(liora::KernelManager::kCef).switches;

  std::string error;
  if (!liora::cef::initialize(startup, &error)) {
    NSLog(@"Liora: CEF failed to start: %s", error.c_str());
    return;
  }
  _core->attachKernel(liora::cef::createKernel());
  NSLog(@"Liora: CEF kernel ready (%zu switches)", startup.switches.size());
}
#endif

- (void)scheduleNetworkRefresh {
  if (self.refreshPending) return;
  self.refreshPending = YES;
  __weak LioraController* weakSelf = self;
  dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 250 * NSEC_PER_MSEC), dispatch_get_main_queue(), ^{
    [weakSelf refreshNetworkNow];
  });
}

- (void)refreshNetworkNow {
  self.refreshPending = NO;
  if (!_core) return;

  liora::Json request = {
      {"jsonrpc", "2.0"}, {"id", 1}, {"method", "net.config"}, {"params", liora::Json::object()}};
  liora::Json response = _core->handleJson(request);
  auto result = response.find("result");
  if (result == response.end() || !result->is_object()) {
    NSLog(@"Liora: net.config failed: %s", response.dump().c_str());
    return;
  }

  bool needsNet = result->value("needsNet", false);
  std::string config = result->contains("config") ? (*result)["config"].dump() : "{}";

  // The kernel decides whether it needs the local service: a kernel without
  // native DoH needs the proxy even for a plain DoH provider.
  liora::KernelNetSetup setup = _core->applyNetwork(liora::KernelManager::kSystem);
  needsNet = needsNet || setup.usesNet || setup.netMissing;
  if (!setup.unsupported.empty()) {
    NSLog(@"Liora: kernel cannot apply: %s", liora::join(setup.unsupported, ", ").c_str());
  }

  if (!needsNet) {
    [self.netService stop];
    _core->setNetRuntime(liora::NetRuntime{});
    [self applyProxyPort:0];
    return;
  }

  NSString* configText = ns(config);
  if (self.netService.dohPort > 0 && [self.netService.runningConfig isEqualToString:configText]) {
    [self publishRuntime];
    [self applyProxyPort:self.netService.proxyPort];
    return;
  }

  NSString* message = nil;
  if (![self.netService startWithConfig:configText error:&message]) {
    NSLog(@"Liora: liora-net failed: %@", message);
    _core->setNetRuntime(liora::NetRuntime{});
    [self applyProxyPort:0];
    return;
  }
  [self publishRuntime];
  [self applyProxyPort:self.netService.proxyPort];
}

- (void)publishRuntime {
  liora::NetRuntime runtime;
  runtime.running = self.netService.dohPort > 0;
  runtime.dohPort = static_cast<uint16_t>(self.netService.dohPort);
  runtime.proxyPort = static_cast<uint16_t>(self.netService.proxyPort);
  runtime.spki = stdString(self.netService.spki);
  if (_core) _core->setNetRuntime(runtime);
  NSLog(@"Liora: resolver runtime running=%d doh=%u proxy=%u", (int)runtime.running,
        (unsigned)runtime.dohPort, (unsigned)runtime.proxyPort);
}

- (void)applyProxyPort:(int)port {
  if (@available(macOS 14.0, *)) {
    WKWebsiteDataStore* store = [WKWebsiteDataStore defaultDataStore];
    if (port > 0) {
      NSString* portText = [NSString stringWithFormat:@"%d", port];
      nw_endpoint_t endpoint = nw_endpoint_create_host("127.0.0.1", portText.UTF8String);
      nw_proxy_config_t config = nw_proxy_config_create_http_connect(endpoint, NULL);
      store.proxyConfigurations = config ? @[ config ] : @[];
      NSLog(@"Liora: kernel traffic goes through the local resolver on port %d", port);
    } else {
      store.proxyConfigurations = @[];
    }
  } else if (port > 0) {
    NSLog(@"Liora: the system WebView needs macOS 14 or later to use a proxy");
  }
}

- (NSString*)assetRoot {
  if (const char* override = getenv("LIORA_UI_DIR")) return ns(override);
  NSString* bundled = [[NSBundle mainBundle] pathForResource:@"ui" ofType:nil];
  if (bundled) return bundled;
  return ns(LIORA_UI_DIR);
}

- (void)buildInterface {
  WKWebViewConfiguration* configuration = [[WKWebViewConfiguration alloc] init];
  configuration.websiteDataStore = [WKWebsiteDataStore nonPersistentDataStore];

  LioraSchemeHandler* handler = [[LioraSchemeHandler alloc] init];
  handler.root = [self assetRoot];
  [configuration setURLSchemeHandler:handler forURLScheme:@"liora-ui"];

  WKUserContentController* controller = [[WKUserContentController alloc] init];
  LioraBridge* bridge = [[LioraBridge alloc] init];
  bridge.controller = self;
  [controller addScriptMessageHandler:bridge name:@"liora"];
  configuration.userContentController = controller;

  self.ui = [[WKWebView alloc] initWithFrame:self.container.bounds configuration:configuration];
  self.ui.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
  self.ui.navigationDelegate = self;
  self.ui.allowsBackForwardNavigationGestures = NO;
  self.ui.allowsMagnification = NO;
  if (@available(macOS 13.3, *)) self.ui.inspectable = YES;
  [self.container addSubview:self.ui];

  NSURL* url = [NSURL URLWithString:@"liora-ui://app/index.html"];
  [self.ui loadRequest:[NSURLRequest requestWithURL:url]];
}

- (liora::IKernelView*)viewForTab:(int64_t)tab {
  auto existing = _views.find(tab);
  if (existing != _views.end()) return existing->second.get();
  if (!_core) return nullptr;

  liora::Scope scope{"", tab};
  std::string kernelId = _core->kernels().resolve(_core->settings(), scope);
  auto kernel = _core->kernels().instance(kernelId);
  if (!kernel) {
    NSLog(@"Liora: kernel '%s' is not ready, using the system kernel", kernelId.c_str());
    kernel = _core->kernels().instance(liora::KernelManager::kSystem);
  }
  if (!kernel) {
    NSLog(@"Liora: no kernel is available");
    return nullptr;
  }

  liora::ViewOptions options;
  options.tab = tab;
  options.parent = (__bridge void*)self.container;

  liora::ViewEvents events;
  __weak LioraController* weakSelf = self;
  events.onUrl = [weakSelf](const std::string& url) {
    NSString* text = ns(url);
    dispatch_async(dispatch_get_main_queue(), ^{
      [weakSelf notifyInterface:@"browser.urlChanged" params:@{@"url" : text}];
    });
  };
  events.onTitle = [weakSelf](const std::string& title) {
    NSString* text = ns(title);
    dispatch_async(dispatch_get_main_queue(), ^{
      [weakSelf notifyInterface:@"browser.titleChanged" params:@{@"title" : text}];
    });
  };
  events.onError = [weakSelf, tab](const std::string& url, int code, const std::string& message) {
    NSString* failedUrl = ns(url);
    NSString* reason = ns(message);
    dispatch_async(dispatch_get_main_queue(), ^{
      [weakSelf handleLoadFailure:failedUrl code:code message:reason tab:tab];
    });
  };
  // target="_blank" opens in the same view for now; real tabs arrive with the
  // tab model in the interface.
  events.onOpenWindow = [](const std::string&, bool) { return false; };

  auto view = kernel->createView(options, std::move(events));
  if (!view) return nullptr;

  NSView* native = (__bridge NSView*)view->nativeHandle();
  if (native) [self.container addSubview:native positioned:NSWindowAbove relativeTo:self.ui];
  _views[tab] = std::move(view);
  return _views[tab].get();
}

// A failed load is shown by the interface, not as a blank page: the browser
// hides the kernel view and reports the failure so the user can retry or turn
// on a direct rule for that host.
- (void)handleLoadFailure:(NSString*)url code:(int)code message:(NSString*)message tab:(int64_t)tab {
  self.contentVisible = NO;
  [self layoutContent];
  NSLog(@"Liora: load failed for %@ (%d)", url, code);
  [self notifyInterface:@"browser.loadFailed"
                 params:@{@"url" : url ?: @"", @"code" : @(code), @"message" : message ?: @"", @"tab" : @(tab)}];
}

- (void)notifyInterface:(NSString*)method params:(NSDictionary*)params {  NSDictionary* message = @{@"jsonrpc" : @"2.0", @"method" : method, @"params" : params ?: @{}};
  NSData* data = [NSJSONSerialization dataWithJSONObject:message options:0 error:nil];
  if (!data) return;
  [self deliver:[[NSString alloc] initWithData:data encoding:NSUTF8StringEncoding]];
}

- (void)reloadInterface:(id)sender {
  [self.ui reload];
}

// ---- Messages -------------------------------------------------------------

- (void)userContentController:(WKUserContentController*)controller
      didReceiveScriptMessage:(WKScriptMessage*)message {
  if (![message.body isKindOfClass:[NSString class]]) return;
  NSString* body = (NSString*)message.body;

  NSData* data = [body dataUsingEncoding:NSUTF8StringEncoding];
  NSDictionary* request = [NSJSONSerialization JSONObjectWithData:data options:0 error:nil];
  if (![request isKindOfClass:[NSDictionary class]]) return;

  NSString* method = request[@"method"];
  if ([method hasPrefix:@"ui."]) {
    [self handleShellMessage:method params:request[@"params"]];
    return;
  }

  std::string response = _core ? _core->handle(stdString(body)) : std::string();
  if (!response.empty()) [self deliver:ns(response)];
}

- (void)deliver:(NSString*)text {
  NSString* script =
      [NSString stringWithFormat:@"window.__lioraDeliver && window.__lioraDeliver(%@);", jsonEscape(text)];
  [self.ui evaluateJavaScript:script completionHandler:nil];
}

- (void)handleShellMessage:(NSString*)method params:(id)params {
  if ([method isEqualToString:@"ui.contentRect"]) {
    NSDictionary* rect = params;
    self.pendingContentRect =
        CGRectMake([rect[@"x"] doubleValue], [rect[@"y"] doubleValue], [rect[@"width"] doubleValue],
                   [rect[@"height"] doubleValue]);
    self.contentVisible = [rect[@"visible"] boolValue];
    if (rect[@"tab"]) self.activeTab = [rect[@"tab"] longLongValue];
    [self layoutContent];
    return;
  }

  if ([method isEqualToString:@"ui.setPage"]) {
    self.contentVisible = NO;
    [self layoutContent];
    return;
  }

  if ([method isEqualToString:@"ui.showTab"]) {
    if (params[@"tab"]) self.activeTab = [params[@"tab"] longLongValue];
    if (params[@"visible"]) self.contentVisible = [params[@"visible"] boolValue];
    [self layoutContent];
    return;
  }

  if ([method isEqualToString:@"ui.closeTab"]) {
    int64_t tab = [params[@"tab"] longLongValue];
    auto entry = _views.find(tab);
    if (entry != _views.end()) {
      NSView* native = (__bridge NSView*)entry->second->nativeHandle();
      [native removeFromSuperview];
      _views.erase(entry);
    }
    return;
  }

  if ([method isEqualToString:@"ui.navigate"]) {    NSString* url = params[@"url"];
    if (!url.length) return;
    int64_t tab = params[@"tab"] ? [params[@"tab"] longLongValue] : self.activeTab;
    self.activeTab = tab;
    self.contentVisible = YES;
    liora::IKernelView* view = [self viewForTab:tab];
    if (!view) return;
    [self layoutContent];
    view->load(stdString(url));
    return;
  }

  if ([method isEqualToString:@"ui.setZoom"]) {
    int64_t tab = params[@"tab"] ? [params[@"tab"] longLongValue] : self.activeTab;
    double percent = [params[@"percent"] doubleValue];
    liora::IKernelView* view = [self viewForTab:tab];
    if (view) view->setZoom(percent > 0 ? percent : 100);
    return;
  }

  if ([method isEqualToString:@"ui.openPath"]) {
    NSString* path = params[@"path"];
    if (path.length) [[NSWorkspace sharedWorkspace] openURL:[NSURL fileURLWithPath:path]];
    return;
  }

  if ([method isEqualToString:@"ui.openSystemSettings"]) {
    [[NSWorkspace sharedWorkspace]
        openURL:[NSURL URLWithString:@"x-apple.systempreferences:com.apple.preference.security?Privacy_AllFiles"]];
    return;
  }

  if ([method isEqualToString:@"ui.pickFile"]) {
    NSOpenPanel* panel = [NSOpenPanel openPanel];
    panel.canChooseFiles = YES;
    panel.canChooseDirectories = NO;
    panel.allowsMultipleSelection = NO;
    [panel beginSheetModalForWindow:self.window
                  completionHandler:^(NSModalResponse response) {
                    if (response != NSModalResponseOK) return;
                    NSString* path = panel.URL.path;
                    [self deliver:[NSString stringWithFormat:@"{\"jsonrpc\":\"2.0\",\"method\":\"ui.filePicked\","
                                                              @"\"params\":{\"purpose\":%@,\"path\":%@}}",
                                                     jsonEscape(params[@"purpose"] ?: @""), jsonEscape(path)]];
                  }];
    return;
  }

  if ([method isEqualToString:@"ui.pickSaveFile"]) {
    NSSavePanel* panel = [NSSavePanel savePanel];
    panel.nameFieldStringValue = params[@"suggested"] ?: @"backup.liora";
    [panel beginSheetModalForWindow:self.window
                  completionHandler:^(NSModalResponse response) {
                    if (response != NSModalResponseOK) return;
                    [self handleSave:[panel URL].path params:params];
                  }];
    return;
  }
}

- (void)handleSave:(NSString*)path params:(NSDictionary*)params {
  if (!_core) return;
  NSString* purpose = params[@"purpose"];
  if ([purpose isEqualToString:@"bookmarksHtml"]) {
    liora::Json request = {{"jsonrpc", "2.0"},
                           {"id", 1},
                           {"method", "migrate.exportHtml"},
                           {"params", {{"path", stdString(path)}}}};
    _core->handleJson(request);
  } else {
    liora::Json request = {{"jsonrpc", "2.0"},
                           {"id", 1},
                           {"method", "backup.create"},
                           {"params", {{"path", stdString(path)}}}};
    _core->handleJson(request);
  }
}

// ---- Layout ---------------------------------------------------------------

- (void)layoutContent {
  CGFloat height = self.container.bounds.size.height;
  CGRect rect = self.pendingContentRect;
  if (getenv("LIORA_DEBUG_LAYOUT")) {
    NSLog(@"Liora: layout container=%.0fx%.0f rect=(%.0f,%.0f %.0fx%.0f) tab=%lld visible=%d",
          self.container.bounds.size.width, height, rect.origin.x, rect.origin.y, rect.size.width,
          rect.size.height, (long long)self.activeTab, (int)self.contentVisible);
  }
  for (auto& entry : _views) {
    liora::IKernelView* view = entry.second.get();
    bool active = entry.first == self.activeTab && self.contentVisible;
    if (!active) {
      view->setVisible(false);
      continue;
    }
    // The interface reports CSS pixels with a top-left origin; the kernel view
    // lives in this container, which uses a bottom-left origin.
    view->setBounds(static_cast<int>(rect.origin.x),
                    static_cast<int>(height - rect.origin.y - rect.size.height),
                    static_cast<int>(rect.size.width), static_cast<int>(rect.size.height));
    view->setVisible(true);
  }
}

- (void)windowDidResize:(NSNotification*)notification { [self layoutContent]; }

@end

@implementation LioraBridge
- (void)userContentController:(WKUserContentController*)controller
      didReceiveScriptMessage:(WKScriptMessage*)message {
  [self.controller userContentController:controller didReceiveScriptMessage:message];
}
@end

// ---- Entry point ----------------------------------------------------------

namespace {
LioraController* g_controller = nil;
}

int main(int argc, const char* argv[]) {
  @autoreleasepool {
#ifdef LIORA_WITH_CEF
    // Chromium re-launches this executable for renderer and utility processes.
    // This must run before NSApplication is created.
    int code = liora::cef::executeProcess(argc, const_cast<char**>(argv));
    if (code >= 0) return code;
#endif
    // One browser process per profile. A lock file works even when the app is
    // started from a terminal, where LaunchServices has no registration.
    std::string lockPath =
        liora::pathToUtf8(liora::paths::dataRoot() / "instance.lock");
    std::error_code lockError;
    std::filesystem::create_directories(liora::paths::dataRoot(), lockError);
    static int instanceLock = open(lockPath.c_str(), O_CREAT | O_RDWR, 0600);
    if (instanceLock >= 0 && flock(instanceLock, LOCK_EX | LOCK_NB) != 0) {
      NSString* bundleId = [[NSBundle mainBundle] bundleIdentifier];
      for (NSRunningApplication* other in
               [NSRunningApplication runningApplicationsWithBundleIdentifier:bundleId]) {
        [other activateWithOptions:0];
      }
      NSLog(@"Liora: another instance is already running");
      return 0;
    }

    NSApplication* app = [NSApplication sharedApplication];
    g_controller = [[LioraController alloc] init];
    app.delegate = g_controller;
    // Connect resize notifications to the content layout.
    [[NSNotificationCenter defaultCenter] addObserver:g_controller
                                             selector:@selector(windowDidResize:)
                                                 name:NSWindowDidResizeNotification
                                               object:nil];
    [app run];
  }
  return 0;
}

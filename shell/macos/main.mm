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
@property(nonatomic, assign) BOOL kernelFullscreen;
@property(nonatomic, assign) int64_t fullscreenTab;
@property(nonatomic, assign) BOOL sentRuntimeInfo;
@property(nonatomic, copy) NSString* cefStatus;
@property(nonatomic, copy) NSString* appliedConfig;

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

  // Tell the interface how the app itself is signed. A diagnostic report that
  // omits this cannot explain why Chromium's helper processes are rejected.
  if (!self.sentRuntimeInfo) {
    self.sentRuntimeInfo = YES;
    [self sendRuntimeInfo];
  }

  // Verification hooks for the command bridge. Several commands allow a test
  // to open a screen, act, then dump the resulting state.
  const char* commandEnvs[] = {"LIORA_TEST_COMMAND",  "LIORA_TEST_COMMAND2", "LIORA_TEST_COMMAND3",
                               "LIORA_TEST_COMMAND4", "LIORA_TEST_COMMAND5", "LIORA_TEST_COMMAND6",
                               "LIORA_TEST_COMMAND7"};
  const char* delayEnvs[] = {"LIORA_TEST_COMMAND_DELAY_MS",  "LIORA_TEST_COMMAND2_DELAY_MS",
                             "LIORA_TEST_COMMAND3_DELAY_MS", "LIORA_TEST_COMMAND4_DELAY_MS",
                             "LIORA_TEST_COMMAND5_DELAY_MS", "LIORA_TEST_COMMAND6_DELAY_MS",
                             "LIORA_TEST_COMMAND7_DELAY_MS"};
  for (int index = 0; index < 7; ++index) {
    const char* command = getenv(commandEnvs[index]);
    if (!command || !*command) continue;
    NSString* name = ns(command);
    double delay = 0.3;
    if (const char* ms = getenv(delayEnvs[index])) {
      delay = atof(ms) / 1000.0;
    }
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(delay * NSEC_PER_SEC)),
                   dispatch_get_main_queue(), ^{
                     [self runTestCommand:name];
                   });
  }

  // Verification hook: deliver a saved diagnostic report after the interface
  // has mounted, without opening a file panel.
  const char* reportPath = getenv("LIORA_TEST_REPORT");
  if (reportPath && *reportPath) {
    NSString* path = ns(reportPath);
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 2500 * NSEC_PER_MSEC), dispatch_get_main_queue(), ^{
      NSError* readError = nil;
      NSString* content = [NSString stringWithContentsOfFile:path
                                                    encoding:NSUTF8StringEncoding
                                                       error:&readError];
      NSDictionary* payload = content
                                  ? @{@"purpose" : @"diagnosticReport", @"path" : path, @"content" : content}
                                  : @{@"purpose" : @"diagnosticReport",
                                      @"path" : path,
                                      @"error" : readError.localizedDescription ?: @"cannot read the file"};
      [self notifyInterface:@"ui.filePicked" params:payload];
      NSLog(@"Liora: test report delivered from %@", path);
    });
  }

  const char* tabs = getenv("LIORA_OPEN_TABS");  if (tabs && *tabs) {
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
  [view addItemWithTitle:@"新建标签页" action:@selector(newTabCommand:) keyEquivalent:@"t"];
  [view addItemWithTitle:@"关闭标签页" action:@selector(closeTabCommand:) keyEquivalent:@"w"];
  [view addItemWithTitle:@"重新加载" action:@selector(reloadCommand:) keyEquivalent:@"r"];
  [view addItem:[NSMenuItem separatorItem]];
  [view addItemWithTitle:@"查找…" action:@selector(findCommand:) keyEquivalent:@"f"];
  [view addItemWithTitle:@"聚焦地址栏" action:@selector(focusAddressCommand:) keyEquivalent:@"l"];
  [view addItem:[NSMenuItem separatorItem]];
  [view addItemWithTitle:@"放大" action:@selector(zoomInCommand:) keyEquivalent:@"="];
  [view addItemWithTitle:@"缩小" action:@selector(zoomOutCommand:) keyEquivalent:@"-"];
  [view addItemWithTitle:@"实际大小" action:@selector(zoomResetCommand:) keyEquivalent:@"0"];
  NSMenuItem* devTools = [view addItemWithTitle:@"开发者工具"
                                         action:@selector(devToolsCommand:)
                                  keyEquivalent:@"i"];
  devTools.keyEquivalentModifierMask = NSEventModifierFlagCommand | NSEventModifierFlagOption;
  [view addItemWithTitle:@"下载" action:@selector(showDownloadsCommand:) keyEquivalent:@"J"];
  [view addItemWithTitle:@"设置…" action:@selector(openSettingsCommand:) keyEquivalent:@","];
  viewItem.submenu = view;
  [main addItem:viewItem];

  // Menu key equivalents fire even when the page view has focus, which page
  // level listeners cannot do.
  for (NSMenuItem* item in view.itemArray) {
    item.target = self;
  }

  NSApp.mainMenu = main;
}

- (void)sendUICommand:(NSString*)name {
  NSString* script = [NSString stringWithFormat:@"window.__lioraCommand && window.__lioraCommand(%@);",
                                                jsonEscape(name)];
  [self.ui evaluateJavaScript:script completionHandler:nil];
}

- (void)runTestCommand:(NSString*)name {
  [self sendUICommand:name];
  NSLog(@"Liora: command %@", name);

  // Verification hook: print the visible text of the current interface.
  if ([name isEqualToString:@"pagedump"]) {
    [self.ui evaluateJavaScript:@"window.__lioraPageText || ''"
              completionHandler:^(id result, NSError* error) {
                if (error) {
                  NSLog(@"Liora: page dump unavailable: %@", error.localizedDescription);
                } else {
                  NSLog(@"Liora: page\n%@", result);
                }
              }];
  }
  // Verification hook: print the timeline text the copy action produces.
  if ([name isEqualToString:@"timelinetext"]) {
    [self.ui evaluateJavaScript:@"window.__lioraTimelineText || ''"
              completionHandler:^(id result, NSError* error) {
                if (error) {
                  NSLog(@"Liora: timeline unavailable: %@", error.localizedDescription);
                } else {
                  NSLog(@"Liora: timeline\n%@", result);
                }
              }];
  }
  // Verification hook: print the failure report the interface generated.
  if ([name isEqualToString:@"reporttext"]) {
    [self.ui evaluateJavaScript:@"window.__lioraReportText || ''"
              completionHandler:^(id result, NSError* error) {
                if (error) {
                  NSLog(@"Liora: report unavailable: %@", error.localizedDescription);
                } else {
                  NSLog(@"Liora: report\n%@", result);
                }
              }];
  }
}

- (void)newTabCommand:(id)sender { [self sendUICommand:@"newTab"]; }
- (void)closeTabCommand:(id)sender { [self sendUICommand:@"closeTab"]; }
- (void)reloadCommand:(id)sender { [self sendUICommand:@"reload"]; }
- (void)focusAddressCommand:(id)sender { [self sendUICommand:@"focusAddress"]; }
- (void)findCommand:(id)sender { [self sendUICommand:@"find"]; }
- (void)openSettingsCommand:(id)sender { [self sendUICommand:@"settings"]; }
- (void)zoomInCommand:(id)sender { [self sendUICommand:@"zoomIn"]; }
- (void)zoomOutCommand:(id)sender { [self sendUICommand:@"zoomOut"]; }
- (void)zoomResetCommand:(id)sender { [self sendUICommand:@"zoomReset"]; }
- (void)devToolsCommand:(id)sender { [self sendUICommand:@"devTools"]; }
- (void)showDownloadsCommand:(id)sender { [self sendUICommand:@"downloads"]; }

- (void)startCore {
  using namespace liora;
  Core::Options options;
  options.paths = CorePaths::forProfile(paths::dataRoot() / "Profiles" / "Default");
  _core = std::make_unique<Core>(options);

  if (auto error = _core->start()) {
    NSLog(@"Liora: core start failed: %s", error->c_str());
    NSAlert* alert = [[NSAlert alloc] init];
    alert.messageText = @"Liora 无法启动";
    alert.informativeText = ns(*error);
    [alert runModal];
    [NSApp terminate:nil];
    return;
  }

  // Distinguishes an unselected Chromium from one that failed to start, which
  // is the difference between a preference and a bug report.
  self.cefStatus = @"not selected";

  shell::registerSystemKernel(_core->kernels());
  // Test hook: make CEF selectable without a CEF build so the confirmation
  // path can be exercised. It is never set in normal launches.
  if (getenv("LIORA_TEST_CEF_AVAILABLE")) {
    _core->kernels().setAvailable(liora::KernelManager::kCef, true, "test");
  }
  // Start liora-net and point WebKit at it before any page loads.
  [self refreshNetworkNowMarkingApplied:YES];
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
  liora::KernelNetSetup setup = _core->applyNetwork(liora::KernelManager::kCef);
  startup.switches = setup.switches;

  // Chromium validates helper processes against an Apple-issued signing
  // identity. Without one, single-process mode keeps rendering local and
  // usable for development, at the cost of isolation and GPU acceleration.
  bool singleProcess = getenv("LIORA_CEF_SINGLE_PROCESS") != nullptr;
  liora::Json compat = _core->settings().get("kernel.cef.singleProcess");
  if (compat.is_boolean() && compat.get<bool>()) singleProcess = true;

  // KernelNetSetup carries DoH/proxy outside the switch list; Chromium only
  // reads them as start-up arguments. Single-process mode cannot use the V8
  // proxy resolver, so it falls back to system DNS instead of hanging.
  if (!singleProcess && !setup.dohTemplates.empty()) {
    startup.switches.push_back("--dns-over-https-mode=" +
                               (setup.dnsMode.empty() ? std::string("secure") : setup.dnsMode));
    startup.switches.push_back("--dns-over-https-templates=" + setup.dohTemplates);
  }
  if (!singleProcess && !setup.proxy.empty()) {
    startup.switches.push_back("--proxy-server=" + setup.proxy);
  }

  if (singleProcess) {
    startup.switches.push_back("--single-process");
    startup.switches.push_back("--in-process-gpu");
    startup.switches.push_back("--disable-gpu");
    startup.switches.push_back("--enable-features=NetworkServiceInProcess");
    // Avoid the login-keychain prompt for Chromium's Safe Storage in this
    // unsigned development mode.
    startup.switches.push_back("--use-mock-keychain");
  }
  if (getenv("LIORA_CEF_NO_SANDBOX")) startup.switches.push_back("--no-sandbox");
  // Test hook: window.open without a gesture is normally popup-blocked.
  if (getenv("LIORA_CEF_DISABLE_POPUP_BLOCKING")) {
    startup.switches.push_back("--disable-popup-blocking");
  }

  std::string error;
  if (!liora::cef::initialize(startup, &error)) {
    self.cefStatus = [NSString stringWithFormat:@"failed: %@", ns(error)];
    NSLog(@"Liora: CEF failed to start: %s", error.c_str());
    return;
  }
  _core->attachKernel(liora::cef::createKernel());
  self.cefStatus = @"ready";
  NSMutableArray* switchList = [NSMutableArray array];
  for (const auto& item : startup.switches) [switchList addObject:ns(item)];
  NSLog(@"Liora: CEF kernel ready (%zu switches: %@)", startup.switches.size(),
        [switchList componentsJoinedByString:@" "]);
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
  [self refreshNetworkNowMarkingApplied:NO];
}

// `markApplied` is set on launch and on an explicit restart. A scheduled
// refresh only restarts the resolver, so the kernel views keep the previous
// snapshot and the interface can report unapplied changes.
- (void)refreshNetworkNowMarkingApplied:(BOOL)markApplied {
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
  NSString* configText = ns(config);

  // The kernel decides whether it needs the local service: a kernel without
  // native DoH needs the proxy even for a plain DoH provider.
  liora::KernelNetSetup setup = _core->applyNetwork(liora::KernelManager::kSystem);
  needsNet = needsNet || setup.usesNet || setup.netMissing;
  if (!setup.unsupported.empty()) {
    NSLog(@"Liora: kernel cannot apply: %s", liora::join(setup.unsupported, ", ").c_str());
  }

  if (!needsNet) {
    [self.netService stop];
    if (markApplied) [self markApplied:configText proxy:@""];
    [self publishRuntime];
    [self applyProxyPort:0];
    return;
  }

  if (self.netService.dohPort > 0 && [self.netService.runningConfig isEqualToString:configText]) {
    if (markApplied) [self markApplied:configText proxy:[self localProxyURL]];
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
  [self recordResolverRestart];
  if (markApplied) [self markApplied:configText proxy:[self localProxyURL]];
  [self publishRuntime];
  [self applyProxyPort:self.netService.proxyPort];
}

// Counts every successful liora-net start, including automatic resolver
// refreshes. The DNS panel uses it to distinguish a process restart from a
// configuration change.
- (void)recordResolverRestart {
  if (!_core) return;
  liora::Json request = {
      {"jsonrpc", "2.0"},
      {"id", 1},
      {"method", "net.markResolverRestart"},
      {"params", {{"at", liora::nowMs()}}}};
  liora::Json response = _core->handleJson(request);
  auto result = response.find("result");
  if (result != response.end() && result->is_object()) {
    NSLog(@"Liora: resolver restart recorded count=%lld",
          static_cast<long long>(result->value("count", int64_t{0})));
  }
}

- (NSString*)localProxyURL {
  return self.netService.proxyPort > 0
             ? [NSString stringWithFormat:@"http://127.0.0.1:%d", self.netService.proxyPort]
             : @"";
}

- (void)markApplied:(NSString*)config proxy:(NSString*)proxy {
  self.appliedConfig = config;
  [self recordNetApplied:proxy];
}

// Writes the applied snapshot to Core so the DNS panel can show it after a
// navigation or app restart.
- (void)recordNetApplied:(NSString*)proxy {
  if (!_core) return;

  liora::Json planRequest = {
      {"jsonrpc", "2.0"}, {"id", 1}, {"method", "dns.plan"}, {"params", liora::Json::object()}};
  liora::Json planResponse = _core->handleJson(planRequest);

  std::string provider;
  std::string protocol;
  std::string mode;
  std::string summary;
  auto result = planResponse.find("result");
  if (result != planResponse.end() && result->is_object()) {
    mode = result->value("mode", std::string());
    summary = result->value("summary", std::string());
    auto endpoint = result->find("endpoint");
    if (endpoint != result->end() && endpoint->is_object()) {
      provider = endpoint->value("name", std::string());
      protocol = endpoint->value("protocol", std::string());
    }
  }

  liora::Json request = {
      {"jsonrpc", "2.0"},
      {"id", 2},
      {"method", "net.markApplied"},
      {"params",
       {{"proxy", stdString(proxy)},
        {"provider", provider},
        {"protocol", protocol},
        {"mode", mode},
        {"summary", summary},
        {"at", liora::nowMs()}}}};
  _core->handleJson(request);
  NSLog(@"Liora: applied snapshot provider=%s proxy=%s", provider.c_str(), stdString(proxy).c_str());
}

- (void)publishRuntime {
  liora::NetRuntime runtime;
  runtime.running = self.netService.dohPort > 0;
  runtime.dohPort = static_cast<uint16_t>(self.netService.dohPort);
  runtime.proxyPort = static_cast<uint16_t>(self.netService.proxyPort);
  runtime.diagPort = static_cast<uint16_t>(self.netService.diagPort);
  runtime.spki = stdString(self.netService.spki);
  runtime.appliedConfig = stdString(self.appliedConfig);
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
  options.downloadDir = _core->settings().get("downloads.dir").get<std::string>();
  options.askWhereToSave = _core->settings().get("downloads.ask").get<bool>();
  options.askPermission = [core = _core.get()](const std::string& origin, const std::string& kind) -> bool {
    if (!core) return false;
    if (auto stored = core->storage().permission(origin, kind)) return *stored == "allow";

    NSString* kindLabel = @"摄像头";
    if (kind == "microphone") kindLabel = @"麦克风";
    else if (kind == "camera+microphone") kindLabel = @"摄像头与麦克风";

    __block bool allow = false;
    void (^ask)(void) = ^{
      NSAlert* alert = [[NSAlert alloc] init];
      alert.messageText = [NSString stringWithFormat:@"允许 %@ 使用%@？", ns(origin), kindLabel];
      alert.informativeText = @"只有你允许后，这个站点才能使用该设备。";
      [alert addButtonWithTitle:@"允许"];
      [alert addButtonWithTitle:@"拒绝"];

      NSButton* remember = [[NSButton alloc] initWithFrame:NSMakeRect(0, 0, 240, 22)];
      [remember setButtonType:NSButtonTypeSwitch];
      remember.title = @"记住这个决定";
      alert.accessoryView = remember;

      allow = [alert runModal] == NSAlertFirstButtonReturn;
      if (remember.state == NSControlStateValueOn) {
        core->storage().setPermission(origin, kind, allow ? "allow" : "block");
      }
    };

    if ([NSThread isMainThread]) {
      ask();
    } else {
      dispatch_sync(dispatch_get_main_queue(), ask);
    }
    return allow;
  };

  liora::ViewEvents events;
  __weak LioraController* weakSelf = self;
  events.onUrl = [weakSelf, tab](const std::string& url) {
    NSString* text = ns(url);
    dispatch_async(dispatch_get_main_queue(), ^{
      [weakSelf notifyInterface:@"browser.urlChanged" params:@{@"url" : text, @"tab" : @(tab)}];
    });
  };
  events.onFullscreen = [weakSelf, tab](bool fullscreen) {
    dispatch_async(dispatch_get_main_queue(), ^{
      [weakSelf setKernelFullscreen:fullscreen tab:tab];
    });
  };
  events.onFindResult = [weakSelf, tab](int matches, int active) {
    dispatch_async(dispatch_get_main_queue(), ^{
      [weakSelf notifyInterface:@"browser.findResult"
                         params:@{@"tab" : @(tab), @"matches" : @(matches), @"active" : @(active)}];
    });
  };
  events.onHistory = [weakSelf, tab](bool canBack, bool canForward) {
    dispatch_async(dispatch_get_main_queue(), ^{
      [weakSelf notifyInterface:@"browser.history"
                         params:@{@"tab" : @(tab), @"canBack" : @(canBack), @"canForward" : @(canForward)}];
    });
  };
  events.onTitle = [weakSelf, tab](const std::string& title) {
    NSString* text = ns(title);
    dispatch_async(dispatch_get_main_queue(), ^{
      [weakSelf notifyInterface:@"browser.titleChanged" params:@{@"title" : text, @"tab" : @(tab)}];
    });
  };
  events.onError = [weakSelf, tab](const std::string& url, int code, const std::string& message) {
    NSString* failedUrl = ns(url);
    NSString* reason = ns(message);
    dispatch_async(dispatch_get_main_queue(), ^{
      [weakSelf handleLoadFailure:failedUrl code:code message:reason tab:tab];
    });
  };
  events.onDownload = [weakSelf, core = _core.get()](const liora::DownloadEvent& event) {    NSString* state = @"started";
    switch (event.state) {
      case liora::DownloadState::kStarted: state = @"started"; break;
      case liora::DownloadState::kProgress: state = @"progress"; break;
      case liora::DownloadState::kFinished: state = @"finished"; break;
      case liora::DownloadState::kFailed: state = @"failed"; break;
    }

    // Persist only terminal states; progress is too noisy for disk history.
    if (core && (event.state == liora::DownloadState::kFinished || event.state == liora::DownloadState::kFailed)) {
      liora::DownloadRecord record;
      record.id = event.id;
      record.filename = event.filename;
      record.path = event.path;
      record.state = event.state == liora::DownloadState::kFinished ? "finished" : "failed";
      record.error = event.error;
      record.bytes = event.received;
      record.total = event.total;
      record.created = liora::nowMs();
      core->storage().saveDownload(record);
    }
    NSDictionary* params = @{
      @"id" : ns(event.id),
      @"filename" : ns(event.filename),
      @"path" : ns(event.path),
      @"received" : @(event.received),
      @"total" : @(event.total),
      @"tab" : @(event.tab),
      @"state" : state,
      @"error" : ns(event.error),
    };
    dispatch_async(dispatch_get_main_queue(), ^{
      // A download replaces the page WebKit was about to render, so hide the
      // blank kernel view and let the interface show the download state.
      if (event.state == liora::DownloadState::kStarted && weakSelf) {
        weakSelf.contentVisible = NO;
        [weakSelf layoutContent];
      }
      [weakSelf notifyInterface:@"browser.download" params:params];
    });
  };
  events.onSecurity = [weakSelf, tab](const liora::SecurityInfo& info) {
    NSDictionary* params = @{
      @"tab" : @(tab),
      @"host" : ns(info.host),
      @"subject" : ns(info.subject),
      @"valid" : @(info.valid),
      @"notAfter" : @(info.notAfter),
      @"https" : @(info.https),
    };
    dispatch_async(dispatch_get_main_queue(), ^{
      [weakSelf notifyInterface:@"browser.security" params:params];
    });
  };
  // A page-created window becomes a real interface tab. Returning true tells
  // the kernel that the shell owns the window and it must not create one.
  events.onOpenWindow = [weakSelf](const std::string& url, bool) {
    if (url.empty() || !weakSelf) return false;
    NSString* text = ns(url);
    dispatch_async(dispatch_get_main_queue(), ^{
      NSLog(@"Liora: opening popup tab %@", text);
      NSString* script = [NSString stringWithFormat:@"window.__lioraNewTab && window.__lioraNewTab(%@);",
                                                    jsonEscape(text)];
      [weakSelf.ui evaluateJavaScript:script completionHandler:nil];
    });
    return true;
  };
  // A kernel may create its native view asynchronously (CEF). Lay out again
  // once it exists so a newly opened tab cannot cover the toolbar.
  events.onReady = [weakSelf]() {
    dispatch_async(dispatch_get_main_queue(), ^{
      [weakSelf layoutContent];
      // Adding an async native view above the WebView can leave the toolbar
      // unpainted until the next tab change; ask AppKit to redraw it once.
      weakSelf.ui.needsDisplay = YES;
    });
  };

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

// Describes the running app's code signature. Ad-hoc builds have no Team ID,
// which is exactly what macOS and Chromium's process checks object to.
- (NSString*)signingSummary {
  SecCodeRef code = NULL;
  if (SecCodeCopySelf(kSecCSDefaultFlags, &code) != errSecSuccess || !code) return @"unknown";
  CFDictionaryRef raw = NULL;
  OSStatus status = SecCodeCopySigningInformation((SecStaticCodeRef)code, kSecCSSigningInformation, &raw);
  CFRelease(code);
  if (status != errSecSuccess || !raw) return @"unknown";

  NSDictionary* info = CFBridgingRelease(raw);
  NSString* team = info[(__bridge NSString*)kSecCodeInfoTeamIdentifier];
  NSString* identifier = info[(__bridge NSString*)kSecCodeInfoIdentifier];
  if (team.length) {
    return [NSString stringWithFormat:@"Developer ID (%@ / %@)", team, identifier ?: @""];
  }
  if (identifier.length) {
    return [NSString stringWithFormat:@"ad-hoc (no Team ID, %@)", identifier];
  }
  return @"ad-hoc (no Team ID)";
}

// Sent on first paint and on request. The push can race the interface's
// __lioraDeliver registration, so the interface also asks for it explicitly.
- (void)sendRuntimeInfo {
  NSString* signature = [self signingSummary];
  NSLog(@"Liora: runtime info signing=%@ cef=%@", signature, self.cefStatus ?: @"not selected");
  [self notifyInterface:@"browser.runtime"
                 params:@{
                   @"codeSigning" : signature,
                   @"cefStatus" : self.cefStatus ?: @"not selected",
                 }];
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

// Restarts the local resolver and reloads kernel views so new network switches
// and the new ephemeral proxy port take effect. Chromium reads these at start-up.
- (void)restartKernel:(NSString*)kernelId {
  if (!_core) return;
  // No explicit kernel (the toolbar action) means the resolved default.
  std::string chosen = kernelId.length
                           ? stdString(kernelId)
                           : _core->kernels().resolve(_core->settings(), liora::Scope{});
  NSString* kernel = ns(chosen);

  [self.netService stop];
  _core->setNetRuntime(liora::NetRuntime{});
  [self refreshNetworkNowMarkingApplied:YES];

  liora::KernelNetSetup setup = _core->applyNetwork(stdString(kernel));

  for (auto& entry : _views) {
    entry.second->reload(true);
  }
  NSLog(@"Liora: reloaded %lu kernel views", (unsigned long)_views.size());

  NSMutableArray* switches = [NSMutableArray array];
  for (const auto& item : setup.switches) [switches addObject:ns(item)];

  [self notifyInterface:@"browser.kernelRestarted"
                 params:@{
                   @"kernel" : ns(setup.kernel),
                   @"switches" : switches,
                   @"dnsMode" : ns(setup.dnsMode),
                   @"dohTemplates" : ns(setup.dohTemplates),
                   @"proxy" : ns(setup.proxy),
                 }];
  NSLog(@"Liora: kernel restarted kernel=%@ proxy=%@ switches=%lu", ns(setup.kernel),
        ns(setup.proxy), (unsigned long)setup.switches.size());
}

// Validates a diagnostic path with Core, then moves it to the macOS trash.
- (BOOL)trashDiagnosticPath:(NSString*)path name:(NSString**)outName error:(NSString**)error {
  if (!_core) return NO;
  liora::Json response = _core->handleJson(
      liora::Json{{"jsonrpc", "2.0"},
                  {"id", 1},
                  {"method", "diagnostics.validateDelete"},
                  {"params", {{"path", stdString(path)}}}});
  auto result = response.find("result");
  if (result == response.end() || !result->is_object()) {
    if (error) {
      NSString* message = @"cannot move the file to the trash";
      if (response.contains("error") && response["error"].is_object()) {
        message = ns(response["error"].value("message", std::string()));
      }
      *error = message;
    }
    return NO;
  }

  std::string safePath = result->value("path", std::string());
  NSString* name = ns(result->value("name", std::string()));
  NSError* trashError = nil;
  NSURL* resultingURL = nil;
  BOOL moved = [[NSFileManager defaultManager]
      trashItemAtURL:[NSURL fileURLWithPath:ns(safePath)]
    resultingItemURL:&resultingURL
               error:&trashError];
  if (!moved) {
    if (error) *error = trashError.localizedDescription ?: @"cannot move the file to the trash";
    return NO;
  }
  NSLog(@"Liora: moved diagnostic to trash %@", resultingURL.path ?: ns(safePath));
  if (outName) *outName = name;
  return YES;
}

// Reads a diagnostic report and hands its contents to the interface, which
// cannot fetch file:// URLs itself.
- (void)deliverDiagnosticFile:(NSString*)path purpose:(NSString*)purpose {
  if (!path.length) return;
  NSMutableDictionary* payload = [NSMutableDictionary dictionary];
  payload[@"purpose"] = purpose.length ? purpose : @"diagnosticReport";
  payload[@"path"] = path;
  NSError* readError = nil;
  NSString* content = [NSString stringWithContentsOfFile:path
                                                encoding:NSUTF8StringEncoding
                                                   error:&readError];
  if (content) {
    payload[@"content"] = content;
  } else {
    payload[@"error"] = readError.localizedDescription ?: @"cannot read the file";
  }
  [self notifyInterface:@"ui.filePicked" params:payload];
}

// Writes text into the configured downloads folder with a timestamped name.
- (BOOL)saveTextFile:(NSString*)content
              prefix:(NSString*)prefix
                path:(NSString**)outPath
               error:(NSString**)error {
  NSString* directory = nil;
  if (_core) {
    std::string configured = _core->settings().get("downloads.dir").get<std::string>();
    if (!configured.empty()) directory = ns(configured);
  }
  if (!directory.length) {
    directory =
        [NSSearchPathForDirectoriesInDomains(NSDownloadsDirectory, NSUserDomainMask, YES) firstObject];
  }
  if (!directory.length) directory = NSTemporaryDirectory();

  NSDateFormatter* formatter = [[NSDateFormatter alloc] init];
  formatter.dateFormat = @"yyyyMMdd-HHmmss";
  NSString* stamp = [formatter stringFromDate:[NSDate date]];
  NSString* path =
      [directory stringByAppendingPathComponent:[NSString stringWithFormat:@"%@-%@.txt", prefix, stamp]];

  NSError* writeError = nil;
  if ([content writeToFile:path atomically:YES encoding:NSUTF8StringEncoding error:&writeError]) {
    if (outPath) *outPath = path;
    return YES;
  }
  if (error) *error = writeError.localizedDescription ?: @"";
  return NO;
}

// ---- Component install -----------------------------------------------------

// Components are fetched by the shell because Core deliberately has no network
// code. Core still verifies every staged file against the catalog before the
// component becomes active.
- (void)notifyComponent:(NSString*)componentId state:(NSString*)state message:(NSString*)message {
  NSMutableDictionary* payload = [NSMutableDictionary dictionary];
  payload[@"id"] = componentId ?: @"";
  payload[@"state"] = state ?: @"";
  if (message.length) payload[@"message"] = message;
  [self notifyInterface:@"browser.componentProgress" params:payload];
}

- (void)failComponent:(NSString*)componentId message:(NSString*)message {
  NSLog(@"Liora: component %@ failed: %@", componentId, message);
  [self notifyComponent:componentId state:@"failed" message:message];
  [self notifyInterface:@"browser.componentFailed"
                 params:@{@"id" : componentId ?: @"", @"message" : message ?: @""}];
}

- (void)installComponent:(NSString*)componentId version:(NSString*)version {
  if (!_core || !componentId.length) return;

  liora::Json response = _core->handleJson(liora::Json{
      {"jsonrpc", "2.0"},
      {"id", 1},
      {"method", "components.plan"},
      {"params", {{"id", stdString(componentId)}, {"version", stdString(version ?: @"")}}}});
  auto result = response.find("result");
  if (result == response.end() || !result->is_object()) {
    std::string message = "cannot plan the component";
    auto failure = response.find("error");
    if (failure != response.end() && failure->is_object()) {
      message = failure->value("message", message);
    }
    [self failComponent:componentId message:ns(message)];
    return;
  }

  liora::Json plan = *result;
  NSString* planJSON = ns(plan.dump());
  NSString* urlText = ns(plan.value("url", std::string()));
  NSURL* url = [NSURL URLWithString:urlText];
  if (!url || !url.scheme.length) {
    [self failComponent:componentId message:@"this component has no download URL"];
    return;
  }

  NSLog(@"Liora: component %@ downloading %@", componentId, urlText);
  [self notifyComponent:componentId state:@"downloading" message:nil];
  __weak LioraController* weakSelf = self;
  NSURLSessionDownloadTask* task = [[NSURLSession sharedSession]
      downloadTaskWithURL:url
        completionHandler:^(NSURL* location, NSURLResponse* response, NSError* error) {
          LioraController* strongSelf = weakSelf;
          if (!strongSelf) return;
          if (error || !location) {
            NSString* message = error.localizedDescription ?: @"the download failed";
            dispatch_async(dispatch_get_main_queue(), ^{
              [strongSelf failComponent:componentId message:message];
            });
            return;
          }
          if ([response isKindOfClass:[NSHTTPURLResponse class]] &&
              ((NSHTTPURLResponse*)response).statusCode != 200) {
            NSInteger status = ((NSHTTPURLResponse*)response).statusCode;
            dispatch_async(dispatch_get_main_queue(), ^{
              [strongSelf failComponent:componentId
                                message:[NSString stringWithFormat:@"the server returned %ld", (long)status]];
            });
            return;
          }

          NSString* extension = url.pathExtension.length ? url.pathExtension : @"pkg";
          NSString* archive = [NSTemporaryDirectory()
              stringByAppendingPathComponent:[NSString stringWithFormat:@"liora-component-%@.%@",
                                                                         NSUUID.UUID.UUIDString, extension]];
          NSFileManager* manager = [NSFileManager defaultManager];
          [manager removeItemAtPath:archive error:nil];
          NSError* moveError = nil;
          if (![manager moveItemAtURL:location
                                toURL:[NSURL fileURLWithPath:archive]
                                error:&moveError]) {
            NSString* message = moveError.localizedDescription ?: @"cannot keep the download";
            dispatch_async(dispatch_get_main_queue(), ^{
              [strongSelf failComponent:componentId message:message];
            });
            return;
          }

          liora::Json downloaded = liora::Json::parse(stdString(planJSON), nullptr, false);
          int64_t expected = downloaded.value("size", int64_t{0});
          NSDictionary* attributes = [manager attributesOfItemAtPath:archive error:nil];
          int64_t actual = attributes ? [attributes[NSFileSize] longLongValue] : 0;
          if (expected > 0 && actual != expected) {
            [manager removeItemAtPath:archive error:nil];
            NSString* message = [NSString
                stringWithFormat:@"size mismatch: expected %lld bytes, got %lld",
                                 (long long)expected, (long long)actual];
            dispatch_async(dispatch_get_main_queue(), ^{
              [strongSelf failComponent:componentId message:message];
            });
            return;
          }

          dispatch_async(dispatch_get_main_queue(), ^{
            [strongSelf notifyComponent:componentId state:@"unpacking" message:nil];
          });

          NSString* staging = ns(downloaded.value("stagingDir", std::string()));
          NSString* unpackError = nil;
          BOOL unpacked = [strongSelf unpackArchive:archive into:staging error:&unpackError];
          [manager removeItemAtPath:archive error:nil];
          if (!unpacked) {
            [manager removeItemAtPath:staging error:nil];
            dispatch_async(dispatch_get_main_queue(), ^{
              [strongSelf failComponent:componentId message:unpackError ?: @"cannot unpack the component"];
            });
            return;
          }

          dispatch_async(dispatch_get_main_queue(), ^{
            [strongSelf commitComponent:componentId plan:downloaded];
          });
        }];
  [task resume];
}

- (void)commitComponent:(NSString*)componentId plan:(const liora::Json&)plan {
  if (!_core) return;
  [self notifyComponent:componentId state:@"verifying" message:nil];
  liora::Json response = _core->handleJson(liora::Json{
      {"jsonrpc", "2.0"}, {"id", 1}, {"method", "components.commit"}, {"params", plan}});
  auto result = response.find("result");
  if (result == response.end() || !result->is_object()) {
    std::string message = "cannot install the component";
    auto failure = response.find("error");
    if (failure != response.end() && failure->is_object()) {
      message = failure->value("message", message);
    }
    // Never leave a half-verified package behind for the next install.
    std::string staging = plan.value("stagingDir", std::string());
    if (!staging.empty()) {
      [[NSFileManager defaultManager] removeItemAtPath:ns(staging) error:nil];
    }
    [self failComponent:componentId message:ns(message)];
    return;
  }

  NSString* version = ns(result->value("version", std::string()));
  NSLog(@"Liora: component %@ installed version=%@", componentId, version);
  [self notifyComponent:componentId state:@"installed" message:nil];
  [self notifyInterface:@"browser.componentInstalled"
                 params:@{@"id" : componentId, @"version" : version}];
}

// bsdtar handles zip and tar archives, which covers the package formats the
// catalog currently uses. Extraction happens into a fresh staging directory;
// Core then verifies every catalogued file's size and SHA-256 before commit.
- (BOOL)unpackArchive:(NSString*)archive into:(NSString*)directory error:(NSString**)error {
  if (!directory.length) {
    if (error) *error = @"the component has no staging directory";
    return NO;
  }
  NSFileManager* manager = [NSFileManager defaultManager];
  [manager removeItemAtPath:directory error:nil];
  NSError* createError = nil;
  if (![manager createDirectoryAtPath:directory
          withIntermediateDirectories:YES
                           attributes:nil
                                error:&createError]) {
    if (error) *error = createError.localizedDescription ?: @"cannot create the staging directory";
    return NO;
  }

  NSTask* task = [[NSTask alloc] init];
  task.executableURL = [NSURL fileURLWithPath:@"/usr/bin/tar"];
  task.arguments = @[ @"-xf", archive, @"-C", directory ];
  NSPipe* stderrPipe = [NSPipe pipe];
  task.standardError = stderrPipe;
  task.standardOutput = [NSPipe pipe];
  NSError* launchError = nil;
  if (![task launchAndReturnError:&launchError]) {
    if (error) *error = launchError.localizedDescription ?: @"cannot run the unpacker";
    return NO;
  }
  [task waitUntilExit];
  if (task.terminationStatus == 0) return YES;

  NSData* data = [stderrPipe.fileHandleForReading readDataToEndOfFile];
  NSString* text = [[NSString alloc] initWithData:data encoding:NSUTF8StringEncoding];
  if (error) *error = text.length ? text : @"the component archive is invalid";
  return NO;
}

- (void)handleShellMessage:(NSString*)method params:(id)params {
  if ([method isEqualToString:@"ui.trashDiagnostic"]) {
    NSString* path = params[@"path"];
    if (!path.length) return;
    NSString* name = nil;
    NSString* message = nil;
    if ([self trashDiagnosticPath:path name:&name error:&message]) {
      [self notifyInterface:@"browser.diagnosticDeleted"
                     params:@{@"path" : path, @"name" : name ?: @""}];
    } else {
      [self notifyInterface:@"browser.diagnosticDeleteFailed"
                     params:@{@"message" : message ?: @""}];
    }
    return;
  }

  if ([method isEqualToString:@"ui.trashDiagnostics"]) {
    NSArray* paths = params[@"paths"];
    int deleted = 0;
    int failed = 0;
    if (!_core || ![paths isKindOfClass:[NSArray class]]) {
      [self notifyInterface:@"browser.diagnosticsTrashed" params:@{@"deleted" : @0, @"failed" : @0}];
      return;
    }
    for (NSString* path in paths) {
      if (![path isKindOfClass:[NSString class]] || !path.length) {
        ++failed;
        continue;
      }
      NSString* name = nil;
      NSString* message = nil;
      if ([self trashDiagnosticPath:path name:&name error:&message]) {
        ++deleted;
      } else {
        ++failed;
      }
    }
    NSLog(@"Liora: trashed %d diagnostics (%d failed)", deleted, failed);
    [self notifyInterface:@"browser.diagnosticsTrashed"
                   params:@{@"deleted" : @(deleted), @"failed" : @(failed)}];
    return;
  }

  if ([method isEqualToString:@"ui.installComponent"]) {
    [self installComponent:params[@"id"] version:params[@"version"]];
    return;
  }

  if ([method isEqualToString:@"ui.runtime"]) {
    [self sendRuntimeInfo];
    return;
  }

  if ([method isEqualToString:@"ui.openDiagnostic"]) {
    NSString* path = params[@"path"];
    if (path.length) [self deliverDiagnosticFile:path purpose:params[@"purpose"]];
    return;
  }

  if ([method isEqualToString:@"ui.restartKernel"]) {
    [self restartKernel:params[@"kernel"]];
    return;
  }

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

  if ([method isEqualToString:@"ui.testFullscreen"]) {
    [self setKernelFullscreen:[params[@"fullscreen"] boolValue] tab:self.activeTab];
    return;
  }

  if ([method isEqualToString:@"ui.find"] || [method isEqualToString:@"ui.stopFind"]) {
    int64_t tab = params[@"tab"] ? [params[@"tab"] longLongValue] : self.activeTab;
    liora::IKernelView* view = [self viewForTab:tab];
    if (!view) return;
    if ([method isEqualToString:@"ui.stopFind"]) {
      view->stopFinding(true);
    } else {
      NSString* text = params[@"text"] ?: @"";
      bool forward = [params[@"forward"] boolValue];
      bool matchCase = [params[@"matchCase"] boolValue];
      view->find(stdString(text), forward, matchCase);
    }
    return;
  }

  if ([method isEqualToString:@"ui.devTools"]) {
    int64_t tab = params[@"tab"] ? [params[@"tab"] longLongValue] : self.activeTab;
    liora::IKernelView* view = [self viewForTab:tab];
    if (view) view->openDevTools();
    return;
  }

  if ([method isEqualToString:@"ui.back"] || [method isEqualToString:@"ui.forward"]) {
    int64_t tab = params[@"tab"] ? [params[@"tab"] longLongValue] : self.activeTab;
    self.activeTab = tab;
    liora::IKernelView* view = [self viewForTab:tab];
    if (!view) return;
    if ([method isEqualToString:@"ui.back"]) {
      view->back();
    } else {
      view->forward();
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

  if ([method isEqualToString:@"ui.saveDiagnostic"]) {
    NSString* content = params[@"content"];
    if (!content.length) return;
    NSString* path = nil;
    NSString* message = nil;
    if ([self saveTextFile:content prefix:@"liora-diagnostic" path:&path error:&message]) {
      NSLog(@"Liora: diagnostic saved %@", path);
      [self notifyInterface:@"browser.diagnosticSaved" params:@{@"path" : path}];
    } else {
      [self notifyInterface:@"browser.diagnosticFailed" params:@{@"message" : message ?: @""}];
    }
    return;
  }

  if ([method isEqualToString:@"ui.saveTimeline"]) {
    NSString* content = params[@"content"];
    if (!content.length) return;
    NSString* path = nil;
    NSString* message = nil;
    if ([self saveTextFile:content prefix:@"liora-timeline" path:&path error:&message]) {
      NSLog(@"Liora: timeline saved %@", path);
      [self notifyInterface:@"browser.timelineSaved" params:@{@"path" : path}];
    } else {
      [self notifyInterface:@"browser.timelineFailed" params:@{@"message" : message ?: @""}];
    }
    return;
  }

  if ([method isEqualToString:@"ui.openPath"]) {
    NSString* path = params[@"path"];
    NSLog(@"Liora: opening path %@", path);
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
                    NSString* purpose = params[@"purpose"] ?: @"";

                    if ([purpose isEqualToString:@"diagnosticReport"]) {
                      [self deliverDiagnosticFile:path purpose:@"diagnosticReport"];
                      return;
                    }

                    NSDictionary* message = @{
                      @"jsonrpc" : @"2.0",
                      @"method" : @"ui.filePicked",
                      @"params" : @{@"purpose" : purpose, @"path" : path},
                    };
                    NSData* data = [NSJSONSerialization dataWithJSONObject:message options:0 error:nil];
                    if (!data) return;
                    [self deliver:[[NSString alloc] initWithData:data encoding:NSUTF8StringEncoding]];
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

- (void)setKernelFullscreen:(BOOL)fullscreen tab:(int64_t)tab {
  if (fullscreen) {
    self.kernelFullscreen = YES;
    self.fullscreenTab = tab;
    self.activeTab = tab;
    self.contentVisible = YES;
    [self layoutContent];
    if (!(self.window.styleMask & NSWindowStyleMaskFullScreen)) {
      [self.window toggleFullScreen:nil];
    }
  } else {
    self.kernelFullscreen = NO;
    [self layoutContent];
    if (self.window.styleMask & NSWindowStyleMaskFullScreen) {
      [self.window toggleFullScreen:nil];
    }
  }
}

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
    if (self.kernelFullscreen && entry.first == self.fullscreenTab) {
      view->setBounds(0, 0, static_cast<int>(self.container.bounds.size.width),
                      static_cast<int>(height));
      view->setVisible(true);
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

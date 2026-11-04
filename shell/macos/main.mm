// Liora macOS shell: a Cocoa window that hosts the WebUI and one kernel view.
// All state lives in Core; this file only moves pixels and messages.

#import <Cocoa/Cocoa.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
#import <WebKit/WebKit.h>

#include <cstdlib>
#include <memory>
#include <string>

#include "SystemKernel.h"
#include "liora/core.hpp"
#include "liora/paths.hpp"

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
@property(nonatomic, strong) WKWebView* content;
@property(nonatomic, strong) NSView* container;
@property(nonatomic, assign) CGRect pendingContentRect;
@property(nonatomic, assign) BOOL contentVisible;
@end

// The message handler is a separate object so the web view configuration does
// not retain the controller (which owns the web view).
@interface LioraController (Messages)
- (void)userContentController:(WKUserContentController*)controller
      didReceiveScriptMessage:(WKScriptMessage*)message;
@end

@implementation LioraController {
  std::unique_ptr<liora::Core> _core;
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
  [self buildContent];

  [self.window makeKeyAndOrderFront:nil];
  [NSApp activateIgnoringOtherApps:YES];
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication*)sender { return YES; }

- (void)applicationWillTerminate:(NSNotification*)notification {
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
  [self applyNetworkToKernel];

  __weak LioraController* weakSelf = self;
  _core->setNotificationSink([weakSelf](const liora::Json& message) {
    NSString* text = ns(message.dump());
    dispatch_async(dispatch_get_main_queue(), ^{
      [weakSelf deliver:text];
    });
  });
}

- (void)applyNetworkToKernel {
  if (!_core) return;
  liora::KernelNetSetup setup = _core->applyNetwork(liora::KernelManager::kSystem);
  if (!setup.unsupported.empty()) {
    // Visible in the interface; the shell does not pretend to apply them.
    NSLog(@"Liora: kernel cannot apply: %s", liora::join(setup.unsupported, ", ").c_str());
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

- (void)buildContent {
  WKWebViewConfiguration* configuration = [[WKWebViewConfiguration alloc] init];
  self.content = [[WKWebView alloc] initWithFrame:NSMakeRect(0, 0, 0, 0) configuration:configuration];
  self.content.hidden = YES;
  self.content.allowsBackForwardNavigationGestures = YES;
  if (@available(macOS 13.3, *)) self.content.inspectable = YES;
  [self.container addSubview:self.content];
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
    [self layoutContent];
    return;
  }

  if ([method isEqualToString:@"ui.setPage"]) {
    self.content.hidden = YES;
    self.contentVisible = NO;
    return;
  }

  if ([method isEqualToString:@"ui.navigate"]) {
    NSString* url = params[@"url"];
    if (!url.length) return;
    self.contentVisible = YES;
    self.content.hidden = NO;
    [self.content loadRequest:[NSURLRequest requestWithURL:[NSURL URLWithString:url]]];
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
  if (!self.content) return;
  CGFloat height = self.container.bounds.size.height;
  CGRect rect = self.pendingContentRect;
  // The interface reports CSS pixels with a top-left origin.
  self.content.frame = NSMakeRect(rect.origin.x, height - rect.origin.y - rect.size.height, rect.size.width,
                                  rect.size.height);
  self.content.hidden = !self.contentVisible;
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

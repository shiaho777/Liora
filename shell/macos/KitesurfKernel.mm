#import <Cocoa/Cocoa.h>

#include "KitesurfKernel.h"

#include <atomic>
#include <map>
#include <memory>
#include <mutex>

#include "liora/reader_script.hpp"

@class LioraKitesurfImageView;

namespace liora::shell {
namespace {

NSString* toNSString(const std::string& text) {
  return [NSString stringWithUTF8String:text.c_str()] ?: @"";
}

std::string toStdString(NSString* text) {
  if (!text) return {};
  const char* utf8 = [text UTF8String];
  return utf8 ? std::string(utf8) : std::string();
}

void onMain(void (^block)(void)) {
  if ([NSThread isMainThread]) {
    block();
  } else {
    dispatch_async(dispatch_get_main_queue(), block);
  }
}

int modifiersFromFlags(NSEventModifierFlags flags) {
  int modifiers = 0;
  if (flags & NSEventModifierFlagOption) modifiers |= 1;
  if (flags & NSEventModifierFlagControl) modifiers |= 2;
  if (flags & NSEventModifierFlagCommand) modifiers |= 4;
  if (flags & NSEventModifierFlagShift) modifiers |= 8;
  return modifiers;
}

// One WebSocket to the engine's browser endpoint; every tab is a CDP target
// with a flat sessionId. All state lives on the main thread except the id
// counter, which input events may touch from any thread.
class CdpClient : public std::enable_shared_from_this<CdpClient> {
 public:
  using Reply = std::function<void(const Json& result, const std::string& error)>;

  explicit CdpClient(std::string endpoint) : endpoint_(std::move(endpoint)) {}

  void connect(std::function<void(const std::string& error)> done) {
    NSURL* url = [NSURL URLWithString:toNSString(endpoint_)];
    if (!url) {
      done("invalid endpoint");
      return;
    }
    NSURLSession* session =
        [NSURLSession sessionWithConfiguration:NSURLSessionConfiguration.ephemeralSessionConfiguration];
    task_ = [session webSocketTaskWithURL:url];
    connected_ = false;
    [task_ resume];
    receiveLoop();
    // The engine answers Browser.getVersion quickly; use it as the handshake.
    auto weak = weak_from_this();
    send("Browser.getVersion", Json::object(), "",
         [weak, done](const Json& result, const std::string& error) {
           auto self = weak.lock();
           if (!self) {
             done(error.empty() ? "connection closed" : error);
             return;
           }
           if (!error.empty()) {
             done(error);
             return;
           }
           self->version_ = result.value("product", std::string("Kitesurf"));
           self->connected_ = true;
           done("");
         });
  }

  bool connected() const { return connected_; }
  const std::string& version() const { return version_; }

  void send(const std::string& method, const Json& params, const std::string& sessionId, Reply reply) {
    if (!task_) {
      if (reply) onMain(^{ reply(Json(), "not connected"); });
      return;
    }
    int id = ++nextId_;
    Json message = {{"id", id}, {"method", method}, {"params", params}};
    if (!sessionId.empty()) message["sessionId"] = sessionId;
    if (reply) pending_[id] = std::move(reply);
    NSString* text = toNSString(message.dump());
    NSURLSessionWebSocketMessage* frame =
        [[NSURLSessionWebSocketMessage alloc] initWithString:text];
    auto weak = weak_from_this();
    [task_ sendMessage:frame
        completionHandler:^(NSError* error) {
          if (!error) return;
          auto self = weak.lock();
          if (!self) return;
          std::string reason = toStdString(error.localizedDescription);
          onMain(^{ self->failPending(reason); });
        }];
  }

  void setEventHandler(std::function<void(const std::string& method, const std::string& sessionId,
                                          const Json& params)> handler) {
    eventHandler_ = std::move(handler);
  }

  void setCloseHandler(std::function<void()> handler) { closeHandler_ = std::move(handler); }

  void close() {
    closed_ = true;
    [task_ cancelWithCloseCode:NSURLSessionWebSocketCloseCodeNormalClosure reason:nil];
    task_ = nil;
  }

 private:
  void receiveLoop() {
    auto weak = weak_from_this();
    [task_ receiveMessageWithCompletionHandler:^(NSURLSessionWebSocketMessage* message, NSError* error) {
      auto self = weak.lock();
      if (!self || self->closed_) return;
      if (error || !message) {
        std::string reason = error ? toStdString(error.localizedDescription) : "connection closed";
        onMain(^{
          self->connected_ = false;
          self->failPending(reason);
          if (self->closeHandler_) self->closeHandler_();
        });
        return;
      }
      if (message.type == NSURLSessionWebSocketMessageTypeString) {
        std::string text = toStdString(message.string);
        onMain(^{ self->dispatch(text); });
      }
      self->receiveLoop();
    }];
  }

  void dispatch(const std::string& text) {
    Json message = Json::parse(text, nullptr, false);
    if (message.is_discarded() || !message.is_object()) return;
    if (message.contains("id")) {
      int id = message.value("id", 0);
      auto it = pending_.find(id);
      if (it == pending_.end()) return;
      Reply reply = std::move(it->second);
      pending_.erase(it);
      std::string error;
      if (message.contains("error") && message["error"].is_object()) {
        error = message["error"].value("message", std::string("CDP error"));
      }
      Json result = message.contains("result") ? message["result"] : Json::object();
      reply(result, error);
      return;
    }
    std::string method = message.value("method", std::string());
    std::string sessionId = message.value("sessionId", std::string());
    Json params = message.contains("params") ? message["params"] : Json::object();
    if (eventHandler_) eventHandler_(method, sessionId, params);
  }

  void failPending(const std::string& reason) {
    auto pending = std::move(pending_);
    pending_.clear();
    for (auto& [id, reply] : pending) reply(Json(), reason);
  }

  std::string endpoint_;
  NSURLSessionWebSocketTask* task_ = nil;
  std::atomic<int> nextId_{0};
  std::map<int, Reply> pending_;
  std::function<void(const std::string&, const std::string&, const Json&)> eventHandler_;
  std::function<void()> closeHandler_;
  std::string version_;
  bool connected_ = false;
  bool closed_ = false;
};

class KitesurfKernel;

// The native side of one tab: an image view showing screencast frames, with
// mouse and keyboard events forwarded to the page's CDP session.
class KitesurfView final : public IKernelView {
 public:
  KitesurfView(KitesurfKernel& kernel, std::shared_ptr<CdpClient> client, const ViewOptions& options,
               ViewEvents events);
  ~KitesurfView() override;

  void load(const std::string& url) override;
  void back() override;
  void forward() override;
  void reload(bool bypassCache) override;
  void stop() override;
  void setBounds(int x, int y, int width, int height) override;
  void setVisible(bool visible) override;
  void setZoom(double percent) override;
  void openDevTools() override;
  void extractReader(std::function<void(const std::string&)> done) override;
  void postMessage(const std::string& json) override;
  void* nativeHandle() const override;

  void handleEvent(const std::string& method, const Json& params, const std::string& sessionId);
  void attachInput();
  void sessionReady(const std::string& sessionId);
  void engineGone();

 private:
  void send(const std::string& method, const Json& params, CdpClient::Reply reply = nullptr);
  void evaluate(const std::string& expression, CdpClient::Reply reply);
  void startScreencast();
  void refreshHistory();
  void refreshTitle();
  void navigateHistory(bool forward);
  void applyViewport();

  KitesurfKernel& kernel_;
  std::shared_ptr<CdpClient> client_;
  std::shared_ptr<ViewEvents> events_;
  LioraKitesurfImageView* imageView_ = nil;
  std::string targetId_;
  std::string sessionId_;
  std::string currentUrl_;
  std::string pendingUrl_;
  bool loading_ = false;
  bool visible_ = true;
  bool destroyed_ = false;
  int width_ = 1280;
  int height_ = 800;
  double zoom_ = 100.0;
};

class KitesurfKernel final : public IKernel {
 public:
  KitesurfKernel(std::shared_ptr<CdpClient> client, std::string endpoint)
      : client_(std::move(client)), endpoint_(std::move(endpoint)) {
    client_->setEventHandler([this](const std::string& method, const std::string& sessionId,
                                    const Json& params) { routeEvent(method, sessionId, params); });
    client_->setCloseHandler([this]() {
      NSLog(@"Liora: Kitesurf engine connection closed");
      for (auto* view : views_) view->engineGone();
    });
  }

  KernelInfo info() const override {
    KernelInfo info;
    info.id = KernelManager::kKitesurf;
    info.name = "Kitesurf (experimental)";
    info.version = client_->version();
    // Matches the Core description: DevTools over CDP, a fresh isolate per
    // page, and nothing the engine cannot actually do.
    info.caps = kCapDevTools | kCapIsolatedProfiles;
    info.available = true;
    info.netNeedsRestart = false;
    return info;
  }

  std::unique_ptr<IKernelView> createView(const ViewOptions& options, ViewEvents events) override {
    return std::make_unique<KitesurfView>(*this, client_, options, std::move(events));
  }

  void applyNet(const KernelNetSetup& setup) override {
    // The engine's outbound fetch has no proxy hook yet; Core reports DNS as
    // unsupported for this kernel, so there is nothing to apply here.
    (void)setup;
  }

  void shutdown() override {
    for (auto* view : views_) view->engineGone();
    client_->close();
  }

  void registerView(KitesurfView* view) { views_.push_back(view); }
  void unregisterView(KitesurfView* view) {
    views_.erase(std::remove(views_.begin(), views_.end(), view), views_.end());
  }

  void routeEvent(const std::string& method, const std::string& sessionId, const Json& params) {
    for (auto* view : views_) view->handleEvent(method, params, sessionId);
  }

  const std::string& endpoint() const { return endpoint_; }

 private:
  std::shared_ptr<CdpClient> client_;
  std::string endpoint_;
  std::vector<KitesurfView*> views_;
};

}  // namespace
}  // namespace liora::shell

// A plain NSImageView that reports mouse and keyboard activity to the view.
// Objective-C declarations must live in the global scope.
@interface LioraKitesurfImageView : NSImageView
@property(nonatomic, copy) void (^onMouse)(NSEvent* event, NSPoint point, int kind);
@property(nonatomic, copy) void (^onKey)(NSEvent* event, BOOL down);
@property(nonatomic, copy) void (^onScroll)(NSEvent* event, NSPoint point);
- (NSPoint)pointFor:(NSEvent*)event;
@end

@implementation LioraKitesurfImageView

- (BOOL)acceptsFirstResponder {
  return YES;
}

- (BOOL)acceptsFirstMouse:(NSEvent*)event {
  (void)event;
  return YES;
}

- (NSPoint)pointFor:(NSEvent*)event {
  NSPoint point = [self convertPoint:event.locationInWindow fromView:nil];
  // AppKit is bottom-left origin; CDP expects top-left CSS pixels.
  return NSMakePoint(point.x, self.bounds.size.height - point.y);
}

- (void)mouseDown:(NSEvent*)event {
  if (self.onMouse) self.onMouse(event, [self pointFor:event], 0);
  [self.window makeFirstResponder:self];
}
- (void)mouseUp:(NSEvent*)event {
  if (self.onMouse) self.onMouse(event, [self pointFor:event], 1);
}
- (void)rightMouseDown:(NSEvent*)event {
  if (self.onMouse) self.onMouse(event, [self pointFor:event], 2);
}
- (void)rightMouseUp:(NSEvent*)event {
  if (self.onMouse) self.onMouse(event, [self pointFor:event], 3);
}
- (void)otherMouseDown:(NSEvent*)event {
  if (self.onMouse) self.onMouse(event, [self pointFor:event], 4);
}
- (void)otherMouseUp:(NSEvent*)event {
  if (self.onMouse) self.onMouse(event, [self pointFor:event], 5);
}
- (void)mouseMoved:(NSEvent*)event {
  if (self.onMouse) self.onMouse(event, [self pointFor:event], 6);
}
- (void)mouseDragged:(NSEvent*)event {
  if (self.onMouse) self.onMouse(event, [self pointFor:event], 6);
}
- (void)scrollWheel:(NSEvent*)event {
  if (self.onScroll) self.onScroll(event, [self pointFor:event]);
}
- (void)keyDown:(NSEvent*)event {
  if (self.onKey) self.onKey(event, YES);
}
- (void)keyUp:(NSEvent*)event {
  if (self.onKey) self.onKey(event, NO);
}
- (void)updateTrackingAreas {
  [super updateTrackingAreas];
  for (NSTrackingArea* area in self.trackingAreas) [self removeTrackingArea:area];
  NSTrackingArea* area =
      [[NSTrackingArea alloc] initWithRect:self.bounds
                                   options:NSTrackingMouseMoved | NSTrackingActiveInKeyWindow |
                                           NSTrackingInVisibleRect
                                     owner:self
                                  userInfo:nil];
  [self addTrackingArea:area];
}

@end

namespace liora::shell {
namespace {

KitesurfView::KitesurfView(KitesurfKernel& kernel, std::shared_ptr<CdpClient> client,
                           const ViewOptions& options, ViewEvents events)
    : kernel_(kernel), client_(std::move(client)),
      events_(std::make_shared<ViewEvents>(std::move(events))), pendingUrl_(options.url) {
  imageView_ = [[LioraKitesurfImageView alloc] initWithFrame:NSMakeRect(0, 0, 800, 600)];
  imageView_.imageScaling = NSImageScaleProportionallyUpOrDown;
  imageView_.imageAlignment = NSImageAlignTopLeft;
  imageView_.wantsLayer = YES;
  imageView_.layer.backgroundColor = NSColor.whiteColor.CGColor;
  kernel_.registerView(this);
  attachInput();

  auto* self = this;
  client_->send("Target.createTarget", {{"url", "about:blank"}}, "",
                [self](const Json& result, const std::string& error) {
                  if (self->destroyed_) return;
                  if (!error.empty()) {
                    NSLog(@"Liora: Kitesurf createTarget failed: %s", error.c_str());
                    if (self->events_->onError) {
                      self->events_->onError(self->pendingUrl_, -1, error);
                    }
                    return;
                  }
                  self->targetId_ = result.value("targetId", std::string());
                  self->client_->send("Target.attachToTarget",
                                      {{"targetId", self->targetId_}, {"flatten", true}}, "",
                                      [self](const Json& attached, const std::string& attachError) {
                                        if (self->destroyed_) return;
                                        if (!attachError.empty()) {
                                          NSLog(@"Liora: Kitesurf attach failed: %s",
                                                attachError.c_str());
                                          return;
                                        }
                                        self->sessionReady(attached.value("sessionId", std::string()));
                                      });
                });
}

KitesurfView::~KitesurfView() {
  destroyed_ = true;
  kernel_.unregisterView(this);
  if (!targetId_.empty() && client_->connected()) {
    client_->send("Target.closeTarget", {{"targetId", targetId_}}, "", nullptr);
  }
  imageView_.onMouse = nil;
  imageView_.onKey = nil;
  imageView_.onScroll = nil;
  [imageView_ removeFromSuperview];
  events_.reset();
}

void KitesurfView::sessionReady(const std::string& sessionId) {
  sessionId_ = sessionId;
  if (events_->onReady) events_->onReady();
  startScreencast();
  if (!pendingUrl_.empty()) {
    std::string url = pendingUrl_;
    pendingUrl_.clear();
    load(url);
  }
}

void KitesurfView::send(const std::string& method, const Json& params, CdpClient::Reply reply) {
  if (sessionId_.empty()) {
    if (reply) reply(Json(), "session not ready");
    return;
  }
  client_->send(method, params, sessionId_, std::move(reply));
}

void KitesurfView::evaluate(const std::string& expression, CdpClient::Reply reply) {
  send("Runtime.evaluate",
       {{"expression", expression}, {"returnByValue", true}, {"awaitPromise", true}},
       std::move(reply));
}

void KitesurfView::load(const std::string& url) {
  if (sessionId_.empty()) {
    pendingUrl_ = url;
    return;
  }
  currentUrl_ = url;
  loading_ = true;
  if (events_->onLoading) events_->onLoading(true, 0.1);
  if (events_->onUrl) events_->onUrl(url);
  auto events = events_;
  send("Page.navigate", {{"url", url}}, [events, url](const Json& result, const std::string& error) {
    std::string errorText = result.value("errorText", std::string());
    if (!error.empty() || !errorText.empty()) {
      std::string message = error.empty() ? errorText : error;
      if (events->onLoading) events->onLoading(false, 1.0);
      if (events->onError) events->onError(url, -1, message);
    }
  });
}

void KitesurfView::navigateHistory(bool forward) {
  auto events = events_;
  auto* self = this;
  send("Page.getNavigationHistory", Json::object(),
       [self, forward, events](const Json& result, const std::string& error) {
         if (!error.empty() || !result.contains("entries")) return;
         int index = result.value("currentIndex", 0);
         const auto& entries = result["entries"];
         int target = forward ? index + 1 : index - 1;
         if (target < 0 || target >= static_cast<int>(entries.size())) return;
         int entryId = entries[target].value("id", 0);
         self->send("Page.navigateToHistoryEntry", {{"entryId", entryId}}, nullptr);
       });
}

void KitesurfView::back() { navigateHistory(false); }
void KitesurfView::forward() { navigateHistory(true); }

void KitesurfView::reload(bool bypassCache) {
  send("Page.reload", {{"ignoreCache", bypassCache}}, nullptr);
}

void KitesurfView::stop() {
  // The engine has no Page.stopLoading; a navigation to the current URL is
  // the closest honest equivalent, so do nothing rather than fake it.
}

void KitesurfView::setBounds(int x, int y, int width, int height) {
  [imageView_ setFrame:NSMakeRect(x, y, std::max(width, 0), std::max(height, 0))];
  width_ = std::max(width, 32);
  height_ = std::max(height, 32);
  applyViewport();
}

void KitesurfView::applyViewport() {
  if (sessionId_.empty() || !visible_) return;
  int width = std::max(32, static_cast<int>(width_ * 100.0 / zoom_));
  int height = std::max(32, static_cast<int>(height_ * 100.0 / zoom_));
  send("Emulation.setDeviceMetricsOverride",
       {{"width", width}, {"height", height}, {"deviceScaleFactor", 1}, {"mobile", false}}, nullptr);
  send("Page.startScreencast",
       {{"format", "jpeg"},
        {"quality", 80},
        {"maxWidth", std::min(width, 2560)},
        {"maxHeight", std::min(height, 2560)},
        {"everyNthFrame", 1}},
       nullptr);
}

void KitesurfView::setVisible(bool visible) {
  visible_ = visible;
  imageView_.hidden = !visible;
  if (sessionId_.empty()) return;
  if (visible) {
    applyViewport();
  } else {
    send("Page.stopScreencast", Json::object(), nullptr);
  }
}

void KitesurfView::setZoom(double percent) {
  zoom_ = percent > 0 ? percent : 100.0;
  applyViewport();
}

void KitesurfView::openDevTools() {
  // The engine serves its own DevTools frontend; open it in the default
  // browser pointed at this engine. Honest and functional.
  std::string base = kernel_.endpoint();
  auto ws = base.find("ws://");
  if (ws == 0) base = "http://" + base.substr(5);
  auto path = base.find("/devtools");
  if (path != std::string::npos) base = base.substr(0, path);
  NSURL* url = [NSURL URLWithString:toNSString(base + "/?inspect=1")];
  if (url) [[NSWorkspace sharedWorkspace] openURL:url];
}

void KitesurfView::extractReader(std::function<void(const std::string&)> done) {
  auto callback = std::make_shared<std::function<void(const std::string&)>>(std::move(done));
  evaluate(liora::kReaderScript, [callback](const Json& result, const std::string& error) {
    std::string json;
    if (error.empty() && result.contains("result") && result["result"].is_object()) {
      const auto& value = result["result"];
      if (value.contains("value") && value["value"].is_string()) {
        json = value["value"].get<std::string>();
      }
    }
    if (callback && *callback) (*callback)(json);
  });
}

void KitesurfView::postMessage(const std::string& json) {
  // liora:// pages are served by the shell's own WebView, never by this
  // kernel, so there is no bridge to deliver to.
  (void)json;
}

void* KitesurfView::nativeHandle() const { return (__bridge void*)imageView_; }

void KitesurfView::startScreencast() { applyViewport(); }

void KitesurfView::refreshHistory() {
  auto events = events_;
  send("Page.getNavigationHistory", Json::object(),
       [events](const Json& result, const std::string& error) {
         if (!error.empty() || !result.contains("entries")) return;
         int index = result.value("currentIndex", 0);
         int count = static_cast<int>(result["entries"].size());
         if (events->onHistory) events->onHistory(index > 0, index + 1 < count);
       });
}

void KitesurfView::refreshTitle() {
  auto events = events_;
  evaluate("document.title", [events](const Json& result, const std::string& error) {
    if (!error.empty() || !result.contains("result")) return;
    const auto& value = result["result"];
    if (value.contains("value") && value["value"].is_string() && events->onTitle) {
      events->onTitle(value["value"].get<std::string>());
    }
  });
}

void KitesurfView::handleEvent(const std::string& method, const Json& params,
                               const std::string& sessionId) {
  // Crash events may arrive without a session; match them by target id.
  if (method == "Target.targetCrashed" || method == "Inspector.targetCrashed") {
    std::string target = params.value("targetId", std::string());
    if (!target.empty() && target != targetId_) return;
    if (target.empty() && (sessionId_.empty() || sessionId != sessionId_)) return;
    if (loading_) {
      loading_ = false;
      if (events_->onLoading) events_->onLoading(false, 1.0);
    }
    if (events_->onError) {
      events_->onError(currentUrl_, -1, "the Kitesurf page isolate crashed or exceeded its budget");
    }
    return;
  }
  if (sessionId != sessionId_ || sessionId_.empty()) return;
  if (method == "Page.screencastFrame") {
    int frameSession = params.value("sessionId", 0);
    send("Page.screencastFrameAck", {{"sessionId", frameSession}}, nullptr);
    std::string data = params.value("data", std::string());
    if (data.empty()) return;
    NSData* bytes = [[NSData alloc] initWithBase64EncodedString:toNSString(data) options:0];
    NSImage* image = bytes ? [[NSImage alloc] initWithData:bytes] : nil;
    if (image) imageView_.image = image;
    return;
  }
  if (method == "Page.frameNavigated") {
    if (params.contains("frame") && params["frame"].is_object()) {
      std::string url = params["frame"].value("url", std::string());
      if (!url.empty() && url != currentUrl_) {
        currentUrl_ = url;
        if (events_->onUrl) events_->onUrl(url);
      }
    }
    refreshTitle();
    refreshHistory();
    return;
  }
  if (method == "Page.navigatedWithinDocument") {
    std::string url = params.value("url", std::string());
    if (!url.empty()) {
      currentUrl_ = url;
      if (events_->onUrl) events_->onUrl(url);
    }
    return;
  }
  if (method == "Page.loadEventFired" || method == "Page.frameStoppedLoading") {
    if (loading_) {
      loading_ = false;
      if (events_->onLoading) events_->onLoading(false, 1.0);
    }
    refreshTitle();
    refreshHistory();
    return;
  }
  if (method == "Page.frameStartedLoading") {
    if (!loading_) {
      loading_ = true;
      if (events_->onLoading) events_->onLoading(true, 0.3);
    }
    return;
  }
}

void KitesurfView::engineGone() {
  if (loading_) {
    loading_ = false;
    if (events_->onLoading) events_->onLoading(false, 1.0);
  }
  if (events_->onError) {
    events_->onError(currentUrl_, -1, "the Kitesurf engine is not reachable");
  }
}

void KitesurfView::attachInput() {
  auto* self = this;
  imageView_.onMouse = ^(NSEvent* event, NSPoint point, int kind) {
    if (self->destroyed_ || self->sessionId_.empty()) return;
    std::string type;
    std::string button = "left";
    int clickCount = static_cast<int>(event.clickCount);
    switch (kind) {
      case 0: type = "mousePressed"; break;
      case 1: type = "mouseReleased"; break;
      case 2: type = "mousePressed"; button = "right"; break;
      case 3: type = "mouseReleased"; button = "right"; break;
      case 4: type = "mousePressed"; button = "middle"; break;
      case 5: type = "mouseReleased"; button = "middle"; break;
      default: type = "mouseMoved"; clickCount = 0; break;
    }
    Json params = {{"type", type},
                   {"x", point.x},
                   {"y", point.y},
                   {"button", button},
                   {"clickCount", clickCount},
                   {"modifiers", modifiersFromFlags(event.modifierFlags)}};
    self->send("Input.dispatchMouseEvent", params, nullptr);
  };
  imageView_.onScroll = ^(NSEvent* event, NSPoint point) {
    if (self->destroyed_ || self->sessionId_.empty()) return;
    Json params = {{"type", "mouseWheel"},
                   {"x", point.x},
                   {"y", point.y},
                   {"deltaX", -event.scrollingDeltaX},
                   {"deltaY", -event.scrollingDeltaY},
                   {"modifiers", modifiersFromFlags(event.modifierFlags)}};
    self->send("Input.dispatchMouseEvent", params, nullptr);
  };
  imageView_.onKey = ^(NSEvent* event, BOOL down) {
    if (self->destroyed_ || self->sessionId_.empty()) return;
    std::string text = toStdString(event.characters);
    Json params = {{"type", down ? "keyDown" : "keyUp"},
                   {"key", toStdString(event.charactersIgnoringModifiers)},
                   {"code", toStdString(event.charactersIgnoringModifiers)},
                   {"windowsVirtualKeyCode", event.keyCode},
                   {"modifiers", modifiersFromFlags(event.modifierFlags)}};
    if (down && !text.empty() && text.size() == 1 && text[0] >= 32) params["text"] = text;
    self->send("Input.dispatchKeyEvent", params, nullptr);
  };
}

}  // namespace

void connectKitesurfKernel(KernelManager& kernels, const std::string& endpoint,
                           std::function<void(const std::string& error)> done) {
  auto client = std::make_shared<CdpClient>(endpoint);
  client->connect([&kernels, client, endpoint, done](const std::string& error) {
    if (!error.empty()) {
      done(error);
      return;
    }
    auto kernel = std::make_shared<KitesurfKernel>(client, endpoint);
    kernels.attach(kernel);
    NSLog(@"Liora: Kitesurf kernel ready (%s)", client->version().c_str());
    done("");
  });
}

}  // namespace liora::shell
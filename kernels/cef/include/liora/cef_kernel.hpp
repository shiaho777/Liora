#pragma once

#include <memory>
#include <string>
#include <vector>

#include "liora/kernel.hpp"

namespace liora::cef {

struct Startup {
  std::string cachePath;
  std::string userAgent;
  // Chromium switches computed by Core, e.g.
  //   --enable-quic
  //   --origin-to-force-quic-on=x.com:443
  //   --ignore-certificate-errors-spki-list=...
  std::vector<std::string> switches;
  bool noSandbox = true;
};

// Starts the CEF runtime on the main thread. The caller must have processed
// CefExecuteProcess() for helper processes before this.
bool initialize(const Startup& startup, std::string* error);

// Drives the message loop when external_message_pump is enabled. Called from a
// timer installed by initialize(); exposed for tests.
void pump();

void shutdown();

// Registers a kernel whose capabilities reflect what Chromium can do natively.
std::shared_ptr<IKernel> createKernel();

}  // namespace liora::cef

#pragma once

#include <memory>
#include <string>

#include "liora/kernel.hpp"

namespace liora::shell {

// The system WebView kernel: a WKWebView driven by Liora's view interface.
// Capabilities reflect what WebKit can actually do on this platform, so the
// interface never offers a switch that would silently do nothing.
class SystemKernel final : public IKernel {
 public:
  SystemKernel();
  ~SystemKernel() override;

  KernelInfo info() const override;
  std::unique_ptr<IKernelView> createView(const ViewOptions& options, ViewEvents events) override;
  void applyNet(const KernelNetSetup& setup) override;
  void shutdown() override;

  // Proxy settings for macOS 14 and later, applied to new views.
  void setProxy(const std::string& proxyUrl);

 private:
  std::string proxy_;
};

// Registers the kernel with the manager if the platform can host it.
void registerSystemKernel(KernelManager& kernels);

}  // namespace liora::shell

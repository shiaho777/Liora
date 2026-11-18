#pragma once

#include <functional>
#include <string>

#include "liora/kernel.hpp"

namespace liora::shell {

// Kitesurf is Cloudflare's experimental engine: it renders pages in workerd
// isolates and speaks CDP, so Liora hosts it as a remote kernel — screencast
// frames in, input events out. It is not a hardened sandbox; the interface
// says so before anyone selects it.
//
// Connects to a locally running engine (scripts/kitesurf.sh). The callback
// runs on the main thread with an empty error on success.
void connectKitesurfKernel(KernelManager& kernels, const std::string& endpoint,
                           std::function<void(const std::string& error)> done);

}  // namespace liora::shell
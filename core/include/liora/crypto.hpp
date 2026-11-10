#pragma once

#include <string_view>

namespace liora {

// Verifies a detached Ed25519 signature over `message`.
//
// `publicKey` (32 bytes) and `signature` (64 bytes) may be hex or base64; both
// encodings are accepted so catalogs can choose either. Returns false for
// malformed input rather than throwing.
bool ed25519Verify(std::string_view publicKey, std::string_view message, std::string_view signature);

}  // namespace liora

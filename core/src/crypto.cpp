#include "liora/crypto.hpp"

#include <cstring>
#include <optional>
#include <random>
#include <string>
#include <vector>

#include "liora/util.hpp"

extern "C" {
#include "tweetnacl.h"
}

#if defined(__APPLE__)
#include <stdlib.h>
#endif

namespace liora {

namespace {

int hexValue(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

// Accepts fixed-length hex (exactly bytes*2) or base64 (standard or url-safe).
std::optional<std::string> decodeMaterial(std::string_view text, size_t bytes) {
  std::string trimmed = trim(text);
  if (trimmed.size() == bytes * 2) {
    bool hex = true;
    for (char c : trimmed) {
      if (hexValue(c) < 0) {
        hex = false;
        break;
      }
    }
    if (hex) {
      std::string out;
      out.reserve(bytes);
      for (size_t i = 0; i < trimmed.size(); i += 2) {
        out.push_back(static_cast<char>((hexValue(trimmed[i]) << 4) | hexValue(trimmed[i + 1])));
      }
      return out;
    }
  }

  auto decoded = base64Decode(trimmed);
  if (decoded && decoded->size() == bytes) return decoded;
  return std::nullopt;
}

}  // namespace

bool ed25519Verify(std::string_view publicKey, std::string_view message, std::string_view signature) {
  auto key = decodeMaterial(publicKey, 32);
  auto sig = decodeMaterial(signature, 64);
  if (!key || !sig) return false;

  // TweetNaCl verifies attached signatures: signature || message.
  std::vector<unsigned char> signedMessage(64 + message.size());
  std::memcpy(signedMessage.data(), sig->data(), 64);
  if (!message.empty()) {
    std::memcpy(signedMessage.data() + 64, message.data(), message.size());
  }
  std::vector<unsigned char> plain(signedMessage.size());
  unsigned long long plainLength = 0;
  return crypto_sign_open(plain.data(), &plainLength, signedMessage.data(), signedMessage.size(),
                          reinterpret_cast<const unsigned char*>(key->data())) == 0;
}

}  // namespace liora

// TweetNaCl references randombytes from its (unused by Liora) key-generation
// functions. Providing it keeps the object file self-contained.
extern "C" void randombytes(unsigned char* data, unsigned long long size) {
#if defined(__APPLE__)
  arc4random_buf(data, size);
#else
  std::random_device device;
  for (unsigned long long i = 0; i < size; ++i) {
    data[i] = static_cast<unsigned char>(device());
  }
#endif
}

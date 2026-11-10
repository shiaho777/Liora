#include "harness.hpp"
#include "liora/crypto.hpp"
#include "liora/util.hpp"

using namespace liora;

namespace {

std::string repeat(std::string_view s, int n) {
  std::string out;
  for (int i = 0; i < n; ++i) out += s;
  return out;
}

}  // namespace

LIORA_TEST(sha256_known_vectors) {
  CHECK_EQ(sha256Hex(""), std::string("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
  CHECK_EQ(sha256Hex("abc"), std::string("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
  // Multi-block input exercises the padding path.
  CHECK_EQ(sha256Hex(repeat("a", 1000)),
           std::string("41edece42d63e8d9bf515a9ba6932e1c20cbc9f5a5d134645adb5db1b9737ea3"));
}

LIORA_TEST(base64_roundtrip) {
  for (const std::string& input : {std::string(), std::string("f"), std::string("fo"), std::string("foo"),
                                   std::string("hello world"), repeat("\xff\x00", 40)}) {
    CHECK_EQ(*base64Decode(base64Encode(input)), input);
    CHECK_EQ(*base64Decode(base64Encode(input, true, false)), input);
  }
  CHECK_EQ(base64Encode("foo"), std::string("Zm9v"));
  CHECK_EQ(base64Encode("fo"), std::string("Zm8="));
}

LIORA_TEST(base64_rejects_garbage) {
  CHECK(!base64Decode("not base64!").has_value());
}

LIORA_TEST(host_ascii_passes_through_valid_names) {
  CHECK_EQ(*hostToAscii("Example.COM"), std::string("example.com"));
  CHECK_EQ(*hostToAscii("a-b.c_d.example"), std::string("a-b.c_d.example"));
}

LIORA_TEST(host_ascii_rejects_invalid) {
  CHECK(!hostToAscii("-bad.com").has_value());
  CHECK(!hostToAscii("bad-.com").has_value());
  CHECK(!hostToAscii("has space.com").has_value());
  CHECK(!hostToAscii("").has_value());
}

LIORA_TEST(host_ascii_punycode) {
  // Known IDNA conversion for 中国 (xn--fiqs8s) and 例子 (xn--fsqu00a).
  CHECK_EQ(*hostToAscii("中国"), std::string("xn--fiqs8s"));
  CHECK_EQ(*hostToAscii("例子.测试"), std::string("xn--fsqu00a.xn--0zwm56d"));
  CHECK_EQ(*hostToAscii("Bücher.example"), std::string("xn--bcher-kva.example"));
}

LIORA_TEST(text_helpers) {
  CHECK_EQ(trim("  hi \t\r\n"), std::string("hi"));
  CHECK_EQ(toLower("AbC"), std::string("abc"));
  CHECK(startsWith("https://x", "https://"));
  CHECK(endsWith("a.com", ".com"));
  CHECK_EQ(join(split("a,b,c", ','), "|"), std::string("a|b|c"));
  CHECK_EQ(split("a,,b", ',').size(), size_t{2});
}

LIORA_TEST(ids_are_unique) {
  std::string a = newId("rule");
  std::string b = newId("rule");
  CHECK_NE(a, b);
  CHECK(startsWith(a, "rule-"));
}

namespace {

std::string hexBytes(std::string_view hex) {
  auto value = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 0;
  };
  std::string out;
  for (size_t i = 0; i + 1 < hex.size(); i += 2) {
    out.push_back(static_cast<char>((value(hex[i]) << 4) | value(hex[i + 1])));
  }
  return out;
}

}  // namespace

LIORA_TEST(ed25519_verifies_rfc8032_vector) {
  // RFC 8032, Ed25519 test vector 1: empty message.
  const std::string publicKey =
      "d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a";
  const std::string signature =
      "e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e06522490155"
      "5fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b";

  CHECK(ed25519Verify(publicKey, "", signature));
  CHECK(ed25519Verify(base64Encode(hexBytes(publicKey)), "", base64Encode(hexBytes(signature))));

  // A different message must not verify, and malformed material fails closed.
  CHECK(!ed25519Verify(publicKey, "x", signature));
  CHECK(!ed25519Verify("not-a-key", "", signature));
  CHECK(!ed25519Verify(publicKey, "", ""));
  CHECK(!ed25519Verify(publicKey, "", std::string(128, '0')));
}

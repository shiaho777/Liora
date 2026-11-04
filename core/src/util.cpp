#include "liora/util.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <fstream>
#include <random>
#include <sstream>

namespace liora {

std::optional<std::string> readFile(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return std::nullopt;
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

bool writeFileAtomic(const fs::path& path, std::string_view data, std::string* error) {
  std::error_code ec;
  if (path.has_parent_path()) fs::create_directories(path.parent_path(), ec);
  fs::path tmp = path;
  tmp += ".tmp";
  {
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    if (!out) {
      if (error) *error = "cannot open " + pathToUtf8(tmp);
      return false;
    }
    out.write(data.data(), static_cast<std::streamsize>(data.size()));
    if (!out.good()) {
      if (error) *error = "write failed: " + pathToUtf8(tmp);
      return false;
    }
  }
  fs::rename(tmp, path, ec);
  if (ec) {
    if (error) *error = ec.message();
    fs::remove(tmp, ec);
    return false;
  }
  return true;
}

std::string pathToUtf8(const fs::path& path) {
  auto u8 = path.u8string();
  return std::string(u8.begin(), u8.end());
}

fs::path utf8ToPath(std::string_view utf8) {
  return fs::path(std::u8string(utf8.begin(), utf8.end()));
}

std::string toLower(std::string_view s) {
  std::string out(s);
  for (auto& c : out) {
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
  }
  return out;
}

std::string trim(std::string_view s) {
  size_t b = 0, e = s.size();
  while (b < e && static_cast<unsigned char>(s[b]) <= ' ') ++b;
  while (e > b && static_cast<unsigned char>(s[e - 1]) <= ' ') --e;
  return std::string(s.substr(b, e - b));
}

bool startsWith(std::string_view s, std::string_view prefix) {
  return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

bool endsWith(std::string_view s, std::string_view suffix) {
  return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::vector<std::string> split(std::string_view s, char sep, bool skipEmpty) {
  std::vector<std::string> out;
  size_t start = 0;
  while (start <= s.size()) {
    size_t pos = s.find(sep, start);
    if (pos == std::string_view::npos) pos = s.size();
    auto part = s.substr(start, pos - start);
    if (!part.empty() || !skipEmpty) out.emplace_back(part);
    start = pos + 1;
  }
  return out;
}

std::string join(const std::vector<std::string>& parts, std::string_view sep) {
  std::string out;
  for (size_t i = 0; i < parts.size(); ++i) {
    if (i) out += sep;
    out += parts[i];
  }
  return out;
}

int64_t nowMs() {
  using namespace std::chrono;
  return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

std::string newId(std::string_view prefix) {
  static thread_local std::mt19937_64 rng{std::random_device{}()};
  uint64_t v = rng();
  uint8_t bytes[8];
  std::memcpy(bytes, &v, sizeof(v));
  std::string out(prefix);
  if (!out.empty()) out += '-';
  out += toHex(bytes, 8);
  return out;
}

// ---- SHA-256 ----------------------------------------------------------------

namespace {

constexpr uint32_t kSha256K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

inline uint32_t rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

void sha256Block(uint32_t h[8], const uint8_t* p) {
  uint32_t w[64];
  for (int i = 0; i < 16; ++i) {
    w[i] = (uint32_t(p[i * 4]) << 24) | (uint32_t(p[i * 4 + 1]) << 16) | (uint32_t(p[i * 4 + 2]) << 8) |
           uint32_t(p[i * 4 + 3]);
  }
  for (int i = 16; i < 64; ++i) {
    uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
    uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
    w[i] = w[i - 16] + s0 + w[i - 7] + s1;
  }
  uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
  for (int i = 0; i < 64; ++i) {
    uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
    uint32_t ch = (e & f) ^ (~e & g);
    uint32_t t1 = hh + S1 + ch + kSha256K[i] + w[i];
    uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
    uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
    uint32_t t2 = S0 + maj;
    hh = g;
    g = f;
    f = e;
    e = d + t1;
    d = c;
    c = b;
    b = a;
    a = t1 + t2;
  }
  h[0] += a; h[1] += b; h[2] += c; h[3] += d;
  h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
}

}  // namespace

std::array<uint8_t, 32> sha256(std::string_view data) {
  uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                   0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
  const auto* p = reinterpret_cast<const uint8_t*>(data.data());
  size_t len = data.size();
  size_t full = len / 64;
  for (size_t i = 0; i < full; ++i) sha256Block(h, p + i * 64);

  uint8_t tail[128] = {};
  size_t rem = len % 64;
  std::memcpy(tail, p + full * 64, rem);
  tail[rem] = 0x80;
  size_t tailLen = rem < 56 ? 64 : 128;
  uint64_t bits = static_cast<uint64_t>(len) * 8;
  for (int i = 0; i < 8; ++i) tail[tailLen - 1 - i] = static_cast<uint8_t>(bits >> (i * 8));
  sha256Block(h, tail);
  if (tailLen == 128) sha256Block(h, tail + 64);

  std::array<uint8_t, 32> out{};
  for (int i = 0; i < 8; ++i) {
    out[i * 4] = static_cast<uint8_t>(h[i] >> 24);
    out[i * 4 + 1] = static_cast<uint8_t>(h[i] >> 16);
    out[i * 4 + 2] = static_cast<uint8_t>(h[i] >> 8);
    out[i * 4 + 3] = static_cast<uint8_t>(h[i]);
  }
  return out;
}

std::string sha256Hex(std::string_view data) {
  auto d = sha256(data);
  return toHex(d.data(), d.size());
}

std::string toHex(const uint8_t* data, size_t size) {
  static const char* digits = "0123456789abcdef";
  std::string out;
  out.reserve(size * 2);
  for (size_t i = 0; i < size; ++i) {
    out += digits[data[i] >> 4];
    out += digits[data[i] & 0xf];
  }
  return out;
}

// ---- Base64 -----------------------------------------------------------------

std::string base64Encode(std::string_view data, bool url, bool pad) {
  const char* table = url ? "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_"
                          : "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  out.reserve((data.size() + 2) / 3 * 4);
  size_t i = 0;
  const auto* p = reinterpret_cast<const uint8_t*>(data.data());
  for (; i + 2 < data.size(); i += 3) {
    uint32_t v = (p[i] << 16) | (p[i + 1] << 8) | p[i + 2];
    out += table[(v >> 18) & 63];
    out += table[(v >> 12) & 63];
    out += table[(v >> 6) & 63];
    out += table[v & 63];
  }
  size_t rem = data.size() - i;
  if (rem == 1) {
    uint32_t v = p[i] << 16;
    out += table[(v >> 18) & 63];
    out += table[(v >> 12) & 63];
    if (pad) out += "==";
  } else if (rem == 2) {
    uint32_t v = (p[i] << 16) | (p[i + 1] << 8);
    out += table[(v >> 18) & 63];
    out += table[(v >> 12) & 63];
    out += table[(v >> 6) & 63];
    if (pad) out += '=';
  }
  return out;
}

std::optional<std::string> base64Decode(std::string_view data) {
  auto value = [](char c) -> int {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+' || c == '-') return 62;
    if (c == '/' || c == '_') return 63;
    return -1;
  };
  std::string out;
  uint32_t buf = 0;
  int bits = 0;
  for (char c : data) {
    if (c == '=' || c == '\n' || c == '\r' || c == ' ') continue;
    int v = value(c);
    if (v < 0) return std::nullopt;
    buf = (buf << 6) | static_cast<uint32_t>(v);
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      out += static_cast<char>((buf >> bits) & 0xff);
    }
  }
  return out;
}

// ---- IDNA (punycode, RFC 3492) ------------------------------------------------

namespace {

bool decodeUtf8(std::string_view s, std::vector<uint32_t>& out) {
  for (size_t i = 0; i < s.size();) {
    auto c = static_cast<uint8_t>(s[i]);
    uint32_t cp;
    int extra;
    if (c < 0x80) { cp = c; extra = 0; }
    else if ((c & 0xe0) == 0xc0) { cp = c & 0x1f; extra = 1; }
    else if ((c & 0xf0) == 0xe0) { cp = c & 0x0f; extra = 2; }
    else if ((c & 0xf8) == 0xf0) { cp = c & 0x07; extra = 3; }
    else return false;
    for (int k = 1; k <= extra; ++k) {
      if (i + k >= s.size()) return false;
      auto cc = static_cast<uint8_t>(s[i + k]);
      if ((cc & 0xc0) != 0x80) return false;
      cp = (cp << 6) | (cc & 0x3f);
    }
    out.push_back(cp);
    i += extra + 1;
  }
  return true;
}

uint32_t adapt(uint32_t delta, uint32_t numPoints, bool first) {
  delta = first ? delta / 700 : delta / 2;
  delta += delta / numPoints;
  uint32_t k = 0;
  while (delta > ((36 - 1) * 26) / 2) {
    delta /= 36 - 1;
    k += 36;
  }
  return k + (36 * delta) / (delta + 38);
}

std::optional<std::string> punycodeLabel(const std::vector<uint32_t>& input) {
  std::string out;
  for (uint32_t cp : input)
    if (cp < 0x80) out += static_cast<char>(cp);
  uint32_t b = static_cast<uint32_t>(out.size());
  uint32_t h = b;
  if (b > 0) out += '-';
  uint32_t n = 128, delta = 0, bias = 72;
  auto digit = [](uint32_t d) { return static_cast<char>(d < 26 ? 'a' + d : '0' + (d - 26)); };
  while (h < input.size()) {
    uint32_t m = UINT32_MAX;
    for (uint32_t cp : input)
      if (cp >= n && cp < m) m = cp;
    delta += (m - n) * (h + 1);
    n = m;
    for (uint32_t cp : input) {
      if (cp < n) ++delta;
      if (cp == n) {
        uint32_t q = delta;
        for (uint32_t k = 36;; k += 36) {
          uint32_t t = k <= bias ? 1 : (k >= bias + 26 ? 26 : k - bias);
          if (q < t) break;
          out += digit(t + (q - t) % (36 - t));
          q = (q - t) / (36 - t);
        }
        out += digit(q);
        bias = adapt(delta, h + 1, h == b);
        delta = 0;
        ++h;
      }
    }
    ++delta;
    ++n;
  }
  return "xn--" + out;
}

}  // namespace

std::optional<std::string> hostToAscii(std::string_view host) {
  std::vector<std::string> labelsOut;
  for (auto& label : split(host, '.', false)) {
    if (label.empty()) return std::nullopt;
    std::vector<uint32_t> cps;
    if (!decodeUtf8(label, cps)) return std::nullopt;
    bool ascii = std::all_of(cps.begin(), cps.end(), [](uint32_t c) { return c < 0x80; });
    if (ascii) {
      std::string lower = toLower(label);
      for (char c : lower) {
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_'))
          return std::nullopt;
      }
      if (lower.front() == '-' || lower.back() == '-') return std::nullopt;
      labelsOut.push_back(lower);
    } else {
      for (auto& cp : cps)
        if (cp >= 'A' && cp <= 'Z') cp += 32;
      auto encoded = punycodeLabel(cps);
      if (!encoded) return std::nullopt;
      labelsOut.push_back(*encoded);
    }
    if (labelsOut.back().size() > 63) return std::nullopt;
  }
  if (labelsOut.empty()) return std::nullopt;
  return join(labelsOut, ".");
}

}  // namespace liora
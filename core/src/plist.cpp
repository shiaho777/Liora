#include "liora/plist.hpp"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

#include "liora/util.hpp"

namespace liora::plist {

namespace {

constexpr int kMaxDepth = 64;
constexpr double kAppleEpochOffset = 978307200.0;

class Reader {
 public:
  explicit Reader(std::string_view b) : b_(b) {}

  std::optional<Json> run(std::string* error) {
    if (b_.size() < 8 + 32 || b_.substr(0, 6) != "bplist") return fail(error, "not a binary plist");
    size_t t = b_.size() - 32;
    offsetSize_ = byte(t + 6);
    refSize_ = byte(t + 7);
    uint64_t count = be(t + 8, 8);
    uint64_t top = be(t + 16, 8);
    uint64_t tableOffset = be(t + 24, 8);
    if (offsetSize_ < 1 || offsetSize_ > 8 || refSize_ < 1 || refSize_ > 8) return fail(error, "bad trailer");
    if (count == 0 || count > b_.size() || top >= count) return fail(error, "bad object count");
    if (tableOffset >= t || (t - tableOffset) / offsetSize_ < count) return fail(error, "bad offset table");
    offsets_.resize(count);
    for (uint64_t i = 0; i < count; ++i) {
      offsets_[i] = be(tableOffset + i * offsetSize_, offsetSize_);
      if (offsets_[i] >= tableOffset) return fail(error, "object offset out of range");
    }
    end_ = tableOffset;
    visiting_.assign(count, false);
    auto v = object(top, 0);
    if (!v) return fail(error, error_.empty() ? "malformed plist" : error_);
    return v;
  }

 private:
  static std::optional<Json> fail(std::string* error, std::string msg) {
    if (error) *error = std::move(msg);
    return std::nullopt;
  }

  uint8_t byte(size_t at) const { return static_cast<uint8_t>(b_[at]); }

  uint64_t be(size_t at, size_t n) const {
    uint64_t v = 0;
    for (size_t i = 0; i < n; ++i) v = (v << 8) | byte(at + i);
    return v;
  }

  bool has(size_t at, size_t n) const { return at <= end_ && n <= end_ - at; }

  // Reads the element count for markers whose low nibble may be 0xF.
  bool length(size_t& at, uint8_t low, uint64_t& out) {
    if (low != 0x0f) {
      out = low;
      return true;
    }
    if (!has(at, 1)) return false;
    uint8_t m = byte(at);
    if ((m & 0xf0) != 0x10) return false;
    size_t n = size_t{1} << (m & 0x0f);
    if (n > 8 || !has(at + 1, n)) return false;
    out = be(at + 1, n);
    at += 1 + n;
    return out <= end_;
  }

  std::optional<Json> object(uint64_t ref, int depth) {
    if (ref >= offsets_.size() || depth > kMaxDepth) return std::nullopt;
    if (visiting_[ref]) {
      error_ = "plist contains a cycle";
      return std::nullopt;
    }
    visiting_[ref] = true;
    auto v = decode(static_cast<size_t>(offsets_[ref]), depth);
    visiting_[ref] = false;
    return v;
  }

  std::optional<Json> decode(size_t at, int depth) {
    if (!has(at, 1)) return std::nullopt;
    uint8_t marker = byte(at++);
    uint8_t hi = marker >> 4, low = marker & 0x0f;
    switch (hi) {
      case 0x0:
        if (marker == 0x08) return Json(false);
        if (marker == 0x09) return Json(true);
        return Json(nullptr);
      case 0x1: {
        size_t n = size_t{1} << low;
        if (n > 16 || !has(at, n)) return std::nullopt;
        // 16-byte ints are signed 128-bit; keep the low 64 bits.
        uint64_t v = be(at + (n > 8 ? n - 8 : 0), n > 8 ? 8 : n);
        if (n == 8 || n == 16) return Json(static_cast<int64_t>(v));
        return Json(v);
      }
      case 0x2: {
        size_t n = size_t{1} << low;
        if (!has(at, n)) return std::nullopt;
        if (n == 4) {
          uint32_t u = static_cast<uint32_t>(be(at, 4));
          float f;
          std::memcpy(&f, &u, 4);
          return Json(static_cast<double>(f));
        }
        if (n == 8) return Json(readDouble(at));
        return std::nullopt;
      }
      case 0x3: {
        if (marker != 0x33 || !has(at, 8)) return std::nullopt;
        double s = readDouble(at);
        if (!std::isfinite(s)) return Json(nullptr);
        return Json{{"$date", static_cast<int64_t>((s + kAppleEpochOffset) * 1000.0)}};
      }
      case 0x4: {
        uint64_t n;
        if (!length(at, low, n) || !has(at, n)) return std::nullopt;
        return Json{{"$data", base64Encode(b_.substr(at, n))}};
      }
      case 0x5: {
        uint64_t n;
        if (!length(at, low, n) || !has(at, n)) return std::nullopt;
        return Json(latin1ToUtf8(b_.substr(at, n)));
      }
      case 0x6: {
        uint64_t n;
        if (!length(at, low, n) || n > end_ / 2 || !has(at, n * 2)) return std::nullopt;
        return Json(utf16beToUtf8(at, n));
      }
      case 0x8: {
        size_t n = size_t{low} + 1;
        if (n > 8 || !has(at, n)) return std::nullopt;
        return Json{{"$uid", be(at, n)}};
      }
      case 0xa: {
        uint64_t n;
        if (!length(at, low, n) || n > end_ / refSize_ || !has(at, n * refSize_)) return std::nullopt;
        Json arr = Json::array();
        for (uint64_t i = 0; i < n; ++i) {
          auto v = object(be(at + i * refSize_, refSize_), depth + 1);
          if (!v) return std::nullopt;
          arr.push_back(std::move(*v));
        }
        return arr;
      }
      case 0xd: {
        uint64_t n;
        if (!length(at, low, n) || n > end_ / (2 * refSize_) || !has(at, 2 * n * refSize_)) return std::nullopt;
        Json obj = Json::object();
        for (uint64_t i = 0; i < n; ++i) {
          auto k = object(be(at + i * refSize_, refSize_), depth + 1);
          if (!k || !k->is_string()) return std::nullopt;
          auto v = object(be(at + (n + i) * refSize_, refSize_), depth + 1);
          if (!v) return std::nullopt;
          obj[k->get<std::string>()] = std::move(*v);
        }
        return obj;
      }
      default:
        return std::nullopt;
    }
  }

  double readDouble(size_t at) const {
    uint64_t u = be(at, 8);
    double d;
    std::memcpy(&d, &u, 8);
    return d;
  }

  static void appendUtf8(std::string& out, uint32_t cp) {
    if (cp < 0x80) {
      out += static_cast<char>(cp);
    } else if (cp < 0x800) {
      out += static_cast<char>(0xc0 | (cp >> 6));
      out += static_cast<char>(0x80 | (cp & 0x3f));
    } else if (cp < 0x10000) {
      out += static_cast<char>(0xe0 | (cp >> 12));
      out += static_cast<char>(0x80 | ((cp >> 6) & 0x3f));
      out += static_cast<char>(0x80 | (cp & 0x3f));
    } else {
      out += static_cast<char>(0xf0 | (cp >> 18));
      out += static_cast<char>(0x80 | ((cp >> 12) & 0x3f));
      out += static_cast<char>(0x80 | ((cp >> 6) & 0x3f));
      out += static_cast<char>(0x80 | (cp & 0x3f));
    }
  }

  static std::string latin1ToUtf8(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) appendUtf8(out, static_cast<uint8_t>(c));
    return out;
  }

  std::string utf16beToUtf8(size_t at, uint64_t units) const {
    std::string out;
    out.reserve(units);
    for (uint64_t i = 0; i < units; ++i) {
      uint32_t u = static_cast<uint32_t>(be(at + i * 2, 2));
      if (u >= 0xd800 && u < 0xdc00 && i + 1 < units) {
        uint32_t lo = static_cast<uint32_t>(be(at + (i + 1) * 2, 2));
        if (lo >= 0xdc00 && lo < 0xe000) {
          appendUtf8(out, 0x10000 + ((u - 0xd800) << 10) + (lo - 0xdc00));
          ++i;
          continue;
        }
      }
      if (u >= 0xd800 && u < 0xe000) u = 0xfffd;
      appendUtf8(out, u);
    }
    return out;
  }

  std::string_view b_;
  size_t offsetSize_ = 0;
  size_t refSize_ = 0;
  size_t end_ = 0;
  std::vector<uint64_t> offsets_;
  std::vector<bool> visiting_;
  std::string error_;
};

}  // namespace

std::optional<Json> parseBinary(std::string_view bytes, std::string* error) {
  return Reader(bytes).run(error);
}

}  // namespace liora::plist

#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "liora/json.hpp"

namespace liora::plist {

// Parses a binary property list (bplist00) into JSON.
//   data  -> {"$data": base64}
//   date  -> {"$date": unix milliseconds}
//   uid   -> {"$uid": n}
// Used to read Safari bookmarks and other macOS app data.
std::optional<Json> parseBinary(std::string_view bytes, std::string* error = nullptr);

}  // namespace liora::plist

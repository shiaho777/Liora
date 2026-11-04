#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace liora {

namespace fs = std::filesystem;

std::optional<std::string> readFile(const fs::path& path);
bool writeFileAtomic(const fs::path& path, std::string_view data, std::string* error = nullptr);

std::string pathToUtf8(const fs::path& path);
fs::path utf8ToPath(std::string_view utf8);

std::string toLower(std::string_view s);
std::string trim(std::string_view s);
bool startsWith(std::string_view s, std::string_view prefix);
bool endsWith(std::string_view s, std::string_view suffix);
std::vector<std::string> split(std::string_view s, char sep, bool skipEmpty = true);
std::string join(const std::vector<std::string>& parts, std::string_view sep);

int64_t nowMs();
std::string newId(std::string_view prefix = {});

std::array<uint8_t, 32> sha256(std::string_view data);
std::string sha256Hex(std::string_view data);
std::string toHex(const uint8_t* data, size_t size);

std::string base64Encode(std::string_view data, bool url = false, bool pad = true);
std::optional<std::string> base64Decode(std::string_view data);

// Converts a host name with non-ASCII labels to its ASCII (punycode) form.
// Returns nullopt when the input contains characters that are not valid in a host.
std::optional<std::string> hostToAscii(std::string_view host);

}  // namespace liora
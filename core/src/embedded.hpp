#pragma once

#include <cstddef>
#include <string_view>

namespace liora::embedded {

extern const unsigned char dns_providers[];
extern const std::size_t dns_providers_size;
extern const unsigned char rule_presets[];
extern const std::size_t rule_presets_size;
extern const unsigned char search_engines[];
extern const std::size_t search_engines_size;
extern const unsigned char update_key[];
extern const std::size_t update_key_size;

inline std::string_view view(const unsigned char* data, std::size_t size) {
  return {reinterpret_cast<const char*>(data), size};
}

inline std::string_view dnsProviders() { return view(dns_providers, dns_providers_size); }
inline std::string_view rulePresets() { return view(rule_presets, rule_presets_size); }
inline std::string_view searchEngines() { return view(search_engines, search_engines_size); }
inline std::string_view updateKey() { return view(update_key, update_key_size); }

}  // namespace liora::embedded

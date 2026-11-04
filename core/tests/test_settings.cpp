#include "harness.hpp"

#include <filesystem>

#include "liora/settings.hpp"

using namespace liora;

namespace {

struct TempDir {
  fs::path path;

  TempDir() {
    path = fs::temp_directory_path() / ("liora-test-" + newId());
    fs::create_directories(path);
  }
  ~TempDir() {
    std::error_code ec;
    fs::remove_all(path, ec);
  }
  fs::path operator/(std::string_view name) const { return path / utf8ToPath(name); }
};

}  // namespace

LIORA_TEST(settings_defaults_resolve_without_a_file) {
  TempDir dir;
  Settings settings(dir / "settings.json");
  CHECK(settings.load());
  CHECK_EQ(settings.get("dns.provider").get<std::string>(), std::string("cloudflare"));
  CHECK_EQ(settings.get("kernel.default").get<std::string>(), std::string("system"));
  CHECK_EQ(settings.get("content.zoom").get<int>(), 100);
}

LIORA_TEST(settings_site_override_beats_global_and_subdomains_inherit) {
  TempDir dir;
  Settings settings(dir / "settings.json");
  CHECK(!settings.set("content.zoom", 150, Scope{"example.com", 0}));

  CHECK_EQ(settings.get("content.zoom", Scope{"example.com", 0}).get<int>(), 150);
  CHECK_EQ(settings.get("content.zoom", Scope{"news.example.com", 0}).get<int>(), 150);
  CHECK_EQ(settings.get("content.zoom", Scope{"other.com", 0}).get<int>(), 100);

  // A global change does not disturb the site override.
  CHECK(!settings.set("content.zoom", 120));
  CHECK_EQ(settings.get("content.zoom", Scope{"example.com", 0}).get<int>(), 150);
  CHECK_EQ(settings.get("content.zoom", Scope{"other.com", 0}).get<int>(), 120);
}

LIORA_TEST(settings_tab_override_beats_site) {
  TempDir dir;
  Settings settings(dir / "settings.json");
  // content.javascript can be overridden at every level.
  CHECK(!settings.set("content.javascript", false, Scope{"example.com", 7}));
  CHECK_EQ(settings.get("content.javascript", Scope{"example.com", 7}).get<bool>(), false);
  CHECK_EQ(settings.get("content.javascript", Scope{"example.com", 8}).get<bool>(), true);

  settings.clearTab(7);
  CHECK_EQ(settings.get("content.javascript", Scope{"example.com", 7}).get<bool>(), true);
}

LIORA_TEST(settings_reject_unknown_keys_values_and_scopes) {
  TempDir dir;
  Settings settings(dir / "settings.json");
  CHECK(settings.set("nope.key", "x").has_value());
  CHECK(settings.set("ui.theme", "neon").has_value());
  CHECK(settings.set("content.zoom", "big").has_value());
  CHECK(settings.set("content.zoom", 9000).has_value());
  // ui.theme is global-only.
  CHECK(settings.set("ui.theme", "dark", Scope{"example.com", 0}).has_value());
  // kernel.default is allowed per site.
  CHECK(!settings.set("kernel.default", "cef", Scope{"example.com", 0}).has_value());
}

LIORA_TEST(settings_roundtrip_and_merge) {
  TempDir dir;
  fs::path file = dir / "settings.json";
  Settings a(file);
  CHECK(!a.set("ui.theme", "dark"));
  CHECK(!a.set("content.zoom", 130, Scope{"example.com", 0}));
  CHECK(a.save());

  Settings b(file);
  CHECK(b.load());
  CHECK_EQ(b.get("ui.theme").get<std::string>(), std::string("dark"));
  CHECK_EQ(b.get("content.zoom", Scope{"example.com", 0}).get<int>(), 130);

  // Merging keeps existing values and adds new ones.
  CHECK(!b.importData({{"global", {{"search.engine", "google"}}}}, true));
  CHECK_EQ(b.get("search.engine").get<std::string>(), std::string("google"));
  CHECK_EQ(b.get("ui.theme").get<std::string>(), std::string("dark"));

  // Unknown keys in a newer file are ignored instead of failing the load.
  CHECK(!b.importData({{"global", {{"future.key", 1}}}}, true));
}

LIORA_TEST(settings_describe_lists_schema_and_overrides) {
  TempDir dir;
  Settings settings(dir / "settings.json");
  CHECK(!settings.set("content.zoom", 140, Scope{"example.com", 0}));
  Json described = settings.describe();
  CHECK(described["items"].is_array());
  CHECK(described["items"].size() > 20);
  CHECK(described["sites"].is_array());
  CHECK_EQ(described["sites"].size(), size_t{1});
  CHECK_EQ(settings.siteOverrides("example.com")["content.zoom"].get<int>(), 140);
}

LIORA_TEST(settings_normalize_site) {
  CHECK_EQ(Settings::normalizeSite("https://Example.COM:8443/path?q=1"), std::string("example.com"));
  CHECK_EQ(Settings::normalizeSite("sub.example.com."), std::string("sub.example.com"));
  CHECK_EQ(Settings::normalizeSite("user@example.com/x"), std::string("example.com"));
  CHECK_EQ(Settings::normalizeSite("例子.测试"), std::string("xn--fsqu00a.xn--0zwm56d"));
  CHECK_EQ(Settings::normalizeSite(""), std::string(""));
}

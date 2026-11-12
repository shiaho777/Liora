#include "harness.hpp"

#include <filesystem>

#include "liora/core.hpp"
#include "liora/util.hpp"

using namespace liora;

namespace {

struct TempProfile {
  fs::path path;
  TempProfile() {
    path = fs::temp_directory_path() / ("liora-watch-test-" + newId());
    fs::create_directories(path);
  }
  ~TempProfile() {
    std::error_code ec;
    fs::remove_all(path, ec);
  }
};

Json rpc(Core& core, int id, const std::string& method, Json params = Json::object()) {
  return core.handleJson(
      Json{{"jsonrpc", "2.0"}, {"id", id}, {"method", method}, {"params", std::move(params)}});
}

}  // namespace

LIORA_TEST(watches_validate_and_persist) {
  TempProfile profile;
  Core::Options options;
  options.paths = CorePaths::forProfile(profile.path);
  options.skipAutoBackup = true;
  Core core(options);
  CHECK(!core.start());

  Json invalid = rpc(core, 1, "watch.add",
                     {{"url", "file:///tmp/secret"}, {"title", "bad"}, {"intervalMinutes", 60}});
  CHECK(invalid.contains("error"));

  Json added = rpc(core, 2, "watch.add",
                   {{"url", "https://example.com/pricing"},
                    {"title", "Pricing"},
                    {"keyword", "price"},
                    {"intervalMinutes", 15}});
  CHECK_EQ(added["result"]["status"], std::string("watching"));
  std::string id = added["result"]["id"];

  Json duplicate = rpc(core, 3, "watch.add",
                       {{"url", "https://example.com/pricing"},
                        {"title", "Pricing"},
                        {"keyword", "price"},
                        {"intervalMinutes", 60}});
  CHECK_EQ(duplicate["result"]["id"], id);

  // Invalid UTF-8 from an external fetcher must never poison the JSON store.
  Json touched = rpc(core, 4, "watch.touch",
                     {{"id", id},
                      {"checkedAt", 1000},
                      {"hash", "abc"},
                      {"changed", true},
                      {"summary", std::string("\xBF")}});
  CHECK_EQ(touched["result"]["status"], std::string("changed"));
  try {
    (void)touched.dump();
    (void)rpc(core, 5, "watch.list").dump();
  } catch (const std::exception& error) {
    test::fail(__FILE__, __LINE__, error.what());
  }

  Json seen = rpc(core, 6, "watch.seen", {{"id", id}});
  CHECK_EQ(seen["result"]["status"], std::string("watching"));
  core.stop();

  Core reopened(options);
  CHECK(!reopened.start());
  Json list = rpc(reopened, 7, "watch.list");
  CHECK_EQ(list["result"].size(), size_t{1});
  CHECK_EQ(list["result"][0]["id"], id);

  Json removed = rpc(reopened, 8, "watch.remove", {{"id", id}});
  CHECK_EQ(removed["result"]["ok"], true);
  Json empty = rpc(reopened, 9, "watch.list");
  CHECK_EQ(empty["result"].size(), size_t{0});
  reopened.stop();
}

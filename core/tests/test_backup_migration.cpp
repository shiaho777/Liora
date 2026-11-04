#include "harness.hpp"

#include <filesystem>

#include "liora/backup.hpp"
#include "liora/migration.hpp"
#include "liora/storage.hpp"
#include "migration/internal.hpp"

using namespace liora;
using namespace liora::migration;

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

const char* kSampleHtml = R"HTML(<!DOCTYPE NETSCAPE-Bookmark-file-1>
<META HTTP-EQUIV="Content-Type" CONTENT="text/html; charset=UTF-8">
<TITLE>Bookmarks</TITLE>
<H1>Bookmarks</H1>
<DL><p>
    <DT><H3 ADD_DATE="1600000000" PERSONAL_TOOLBAR_FOLDER="true">Bookmarks bar</H3>
    <DL><p>
        <DT><A HREF="https://example.com/?a=1&amp;b=2" ADD_DATE="1600000001">Example &amp; Co</A>
        <DT><H3>Nested</H3>
        <DL><p>
            <DT><A HREF="https://nested.example">Nested link</A>
        </DL><p>
    </DL><p>
    <DT><A HREF="https://other.example/path">Other link</A>
    <DT><A HREF="javascript:void(0)">Ignored</A>
    <DT><H3>Empty folder</H3>
</DL><p>
)HTML";

std::unique_ptr<Storage> openStorage(const TempDir& dir) {
  auto storage = std::make_unique<Storage>(dir / "liora.db");
  std::string error;
  CHECK(storage->open(&error));
  return storage;
}

int64_t countTree(const Json& node) {
  if (!node.is_object()) return 0;
  if (node.value("type", "") == "url") return 1;
  int64_t n = 0;
  if (auto kids = node.find("children"); kids != node.end() && kids->is_array()) {
    for (const auto& kid : *kids) n += countTree(kid);
  }
  return n;
}

}  // namespace

LIORA_TEST(backup_roundtrip_and_verify) {
  Backup backup;
  backup.registerSection({"greeting", [] { return Json{{"hello", "world"}}; },
                          [](const Json& data, bool) -> std::optional<std::string> {
                            if (data.value("hello", "") != "world") return "unexpected data";
                            return std::nullopt;
                          },
                          [](const Json&) { return Json{{"count", 1}}; }});

  Json envelope = backup.create();
  CHECK(!Backup::verify(envelope).has_value());
  CHECK_EQ(envelope["format"], std::string("liora-backup"));
  CHECK_EQ(envelope["sections"]["greeting"]["hello"], std::string("world"));

  Json inspection = backup.inspect(envelope);
  CHECK_EQ(inspection["valid"], true);
  CHECK_EQ(inspection["sections"][0]["name"], std::string("greeting"));
  CHECK_EQ(inspection["sections"][0]["supported"], true);

  // Tampering with the payload is detected.
  Json tampered = envelope;
  tampered["sections"]["greeting"]["hello"] = "evil";
  CHECK(Backup::verify(tampered).has_value());
}

LIORA_TEST(backup_rejects_unknown_and_newer_formats) {
  CHECK(Backup::verify(Json{{"format", "other"}, {"version", 1}}).has_value());
  CHECK(Backup::verify(Json{{"format", "liora-backup"}, {"version", 99}, {"sections", Json::object()}}).has_value());
  CHECK(Backup::verify(Json{{"format", "liora-backup"}, {"version", 1}}).has_value());
}

LIORA_TEST(backup_restore_selected_sections_only) {
  TempDir dir;
  auto storage = openStorage(dir);
  CHECK(storage->addBookmark(Storage::kBookmarkBar, "One", "https://one.example").has_value());

  Backup backup;
  int restored = 0;
  backup.registerSection({"first", [] { return Json(1); },
                          [&](const Json&, bool) -> std::optional<std::string> {
                            ++restored;
                            return std::nullopt;
                          },
                          {}});
  backup.registerSection({"second", [] { return Json(2); },
                          [&](const Json&, bool) -> std::optional<std::string> {
                            ++restored;
                            return std::nullopt;
                          },
                          {}});

  Json envelope = backup.create();
  CHECK(!backup.restore(envelope, {"second"}, true).has_value());
  CHECK_EQ(restored, 1);
  CHECK(backup.restore(envelope, {"missing"}, true).has_value());
}

LIORA_TEST(backup_auto_rotation_keeps_newest) {
  TempDir dir;
  Backup backup;
  backup.registerSection({"data", [] { return Json::object(); }, [](const Json&, bool) { return std::nullopt; }, {}});

  for (int i = 0; i < 5; ++i) {
    std::string error;
    CHECK(backup.autoBackup(dir / "backups", 2, "auto", &error).has_value());
  }
  auto files = Backup::list(dir / "backups");
  CHECK_EQ(files.size(), size_t{2});
}

LIORA_TEST(migration_parse_html_bookmarks) {
  auto tree = Migrator::parseBookmarksHtml(kSampleHtml);
  CHECK(tree.has_value());
  const Json& t = *tree;

  // The toolbar folder is mapped to "bar", not nested as a folder.
  CHECK_EQ(countTree(t["bar"]), int64_t{2});
  CHECK_EQ(countTree(t["other"]), int64_t{1});

  const Json& bar = t["bar"]["children"];
  CHECK_EQ(bar[0]["url"], std::string("https://example.com/?a=1&b=2"));
  CHECK_EQ(bar[0]["title"], std::string("Example & Co"));
  CHECK_EQ(bar[0]["created"], int64_t{1600000001000});
  CHECK_EQ(bar[1]["type"], std::string("folder"));
  CHECK_EQ(bar[1]["title"], std::string("Nested"));
  CHECK_EQ(bar[1]["children"][0]["url"], std::string("https://nested.example"));

  // The empty folder survives, and javascript: URLs are dropped.
  CHECK_EQ(t["other"]["children"].size(), size_t{2});
  CHECK_EQ(t["other"]["children"][1]["type"], std::string("folder"));
  CHECK_EQ(t["other"]["children"][1]["title"], std::string("Empty folder"));
}

LIORA_TEST(migration_html_rejects_non_bookmark_files) {
  CHECK(!Migrator::parseBookmarksHtml("<html><body>hi</body></html>").has_value());
}

LIORA_TEST(migration_html_export_roundtrip) {
  TempDir dir;
  auto storage = openStorage(dir);
  auto folder = storage->addFolder(Storage::kBookmarkBar, "Folder");
  CHECK(folder.has_value());
  CHECK(storage->addBookmark(*folder, "A & B", "https://example.com/?x=1&y=2").has_value());
  CHECK(storage->addBookmark(Storage::kOtherBookmarks, "Other", "https://other.example").has_value());

  std::string html = Migrator::bookmarksToHtml(storage->bookmarkTree());
  CHECK(html.find("NETSCAPE-Bookmark-file") != std::string::npos);
  CHECK(html.find("A &amp; B") != std::string::npos);
  CHECK(html.find("x=1&amp;y=2") != std::string::npos);
  CHECK(html.find("PERSONAL_TOOLBAR_FOLDER=\"true\"") != std::string::npos);

  auto reparsed = Migrator::parseBookmarksHtml(html);
  CHECK(reparsed.has_value());
  CHECK_EQ(countTree((*reparsed)["bar"]), int64_t{1});
  CHECK_EQ(countTree((*reparsed)["other"]), int64_t{1});
}

LIORA_TEST(migration_chromium_bookmarks_json) {
  const char* json = R"({
    "roots": {
      "bookmark_bar": {"type": "folder", "name": "Bookmarks bar", "date_added": "13300000000000000",
        "children": [
          {"type": "url", "name": "Example", "url": "https://example.com", "date_added": "13300000000000000"},
          {"type": "folder", "name": "Sub", "date_added": "13300000000000000",
            "children": [{"type": "url", "name": "Sub link", "url": "https://sub.example"}]}
        ]},
      "other": {"type": "folder", "name": "Other", "children": []},
      "synced": {"type": "folder", "name": "Mobile", "children": [
          {"type": "url", "name": "Mobile link", "url": "https://mobile.example"}
      ]}
    }
  })";

  Failure failure;
  auto tree = parseChromiumBookmarks(json, failure);
  CHECK(tree.has_value());
  CHECK_EQ(countTree((*tree)["bar"]), int64_t{2});
  CHECK_EQ(countTree((*tree)["mobile"]), int64_t{1});
  CHECK_EQ((*tree)["bar"]["children"][0]["url"], std::string("https://example.com"));
}

LIORA_TEST(migration_migrate_file_is_idempotent) {
  TempDir dir;
  auto storage = openStorage(dir);
  fs::path file = dir / "bookmarks.html";
  CHECK(writeFileAtomic(file, kSampleHtml));

  MigrationOptions options;
  MigrationResult first = Migrator::migrateFile(*storage, file, options);
  CHECK(first.ok);
  CHECK(first.error.empty());
  CHECK_EQ(first.bookmarks, int64_t{3});

  MigrationResult second = Migrator::migrateFile(*storage, file, options);
  CHECK(second.ok);
  // Re-running the same import adds nothing.
  int64_t total = countTree(storage->bookmarkTree()["bar"]) + countTree(storage->bookmarkTree()["other"]);
  CHECK_EQ(total, int64_t{3});
}

LIORA_TEST(migration_reports_missing_file) {
  TempDir dir;
  auto storage = openStorage(dir);
  MigrationOptions options;
  MigrationResult result = Migrator::migrateFile(*storage, dir / "nope.html", options);
  CHECK(!result.ok);
  CHECK_EQ(result.error, std::string("not_found"));
}

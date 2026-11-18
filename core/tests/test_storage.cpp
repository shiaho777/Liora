#include "harness.hpp"

#include <filesystem>

#include "liora/storage.hpp"

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

struct Fixture {
  TempDir dir;
  Storage storage;

  Fixture() : storage(dir / "liora.db") {
    std::string error;
    CHECK(storage.open(&error));
  }
};

}  // namespace

LIORA_TEST(storage_creates_root_folders) {
  Fixture f;
  auto bar = f.storage.bookmark(Storage::kBookmarkBar);
  CHECK(bar.has_value());
  CHECK(bar->folder);
  CHECK_EQ(bar->title, std::string("Bookmarks bar"));
  CHECK(f.storage.bookmark(Storage::kOtherBookmarks).has_value());
  CHECK(f.storage.bookmark(Storage::kMobileBookmarks).has_value());
}

LIORA_TEST(storage_bookmarks_keep_insertion_order) {
  Fixture f;
  auto a = f.storage.addBookmark(Storage::kBookmarkBar, "A", "https://a.example");
  auto b = f.storage.addBookmark(Storage::kBookmarkBar, "B", "https://b.example");
  CHECK(a.has_value());
  CHECK(b.has_value());

  auto children = f.storage.children(Storage::kBookmarkBar);
  CHECK_EQ(children.size(), size_t{2});
  CHECK_EQ(children[0].title, std::string("A"));
  CHECK_EQ(children[1].title, std::string("B"));

  // Inserting at position 0 shifts the others down.
  auto first = f.storage.addBookmark(Storage::kBookmarkBar, "First", "https://first.example", int64_t{0});
  CHECK(first.has_value());
  children = f.storage.children(Storage::kBookmarkBar);
  CHECK_EQ(children.size(), size_t{3});
  CHECK_EQ(children[0].title, std::string("First"));
  CHECK_EQ(children[1].position, int64_t{1});
}

LIORA_TEST(storage_bookmark_tree_and_search) {
  Fixture f;
  auto folder = f.storage.addFolder(Storage::kOtherBookmarks, "Work");
  CHECK(folder.has_value());
  CHECK(f.storage.addBookmark(*folder, "Docs", "https://docs.example").has_value());
  CHECK(f.storage.addBookmark(Storage::kOtherBookmarks, "News", "https://news.example").has_value());

  Json tree = f.storage.bookmarkTree();
  CHECK(tree.contains("other"));
  CHECK_EQ(tree["other"]["children"].size(), size_t{2});
  CHECK_EQ(tree["other"]["children"][0]["type"], std::string("folder"));
  CHECK_EQ(tree["other"]["children"][0]["children"].size(), size_t{1});

  auto found = f.storage.searchBookmarks("docs");
  CHECK_EQ(found.size(), size_t{1});
  CHECK_EQ(found[0].url, std::string("https://docs.example"));

  // LIKE wildcards in user input are treated literally.
  CHECK_EQ(f.storage.searchBookmarks("%").size(), size_t{0});
}

LIORA_TEST(storage_move_bookmark_rejects_cycles) {
  Fixture f;
  auto outer = f.storage.addFolder(Storage::kOtherBookmarks, "Outer");
  auto inner = f.storage.addFolder(*outer, "Inner");
  CHECK(outer.has_value());
  CHECK(inner.has_value());

  CHECK(!f.storage.moveBookmark(*inner, *inner));
  CHECK(!f.storage.moveBookmark(*outer, *inner));
  CHECK(f.storage.moveBookmark(*inner, Storage::kBookmarkBar));
  CHECK_EQ(f.storage.children(*outer).size(), size_t{0});
}

LIORA_TEST(storage_remove_keeps_positions_dense) {
  Fixture f;
  for (int i = 0; i < 3; ++i) {
    CHECK(f.storage.addBookmark(Storage::kBookmarkBar, "b" + std::to_string(i), "https://b" + std::to_string(i) + ".example").has_value());
  }
  auto children = f.storage.children(Storage::kBookmarkBar);
  CHECK(f.storage.removeBookmark(children[1].id));

  auto remaining = f.storage.children(Storage::kBookmarkBar);
  CHECK_EQ(remaining.size(), size_t{2});
  CHECK_EQ(remaining[0].position, int64_t{0});
  CHECK_EQ(remaining[1].position, int64_t{1});

  // Root folders cannot be removed.
  CHECK(!f.storage.removeBookmark(Storage::kBookmarkBar));
}

LIORA_TEST(storage_rejects_bookmarks_in_non_folders) {
  Fixture f;
  auto leaf = f.storage.addBookmark(Storage::kBookmarkBar, "Leaf", "https://leaf.example");
  CHECK(leaf.has_value());
  CHECK(!f.storage.addBookmark(*leaf, "Child", "https://child.example").has_value());
  CHECK(!f.storage.addBookmark(Storage::kBookmarkBar, "Empty", "").has_value());
}

LIORA_TEST(storage_history_counts_and_suggest) {
  Fixture f;
  int64_t now = nowMs();
  f.storage.addVisit("https://example.com/page", "Example page", false, now - 5000);
  f.storage.addVisit("https://example.com/page", "Example page", true, now - 1000);
  f.storage.addVisit("https://other.example/x", "Other", false, now - 2000);

  auto suggest = f.storage.suggest("example.com");
  CHECK(!suggest.empty());
  CHECK_EQ(suggest[0].url, std::string("https://example.com/page"));
  CHECK_EQ(suggest[0].visitCount, int64_t{2});
  CHECK_EQ(suggest[0].typedCount, int64_t{1});

  // Title matches also complete.
  CHECK_EQ(f.storage.suggest("Other")[0].url, std::string("https://other.example/x"));
  CHECK(f.storage.suggest("").empty());

  auto top = f.storage.topSites(5);
  CHECK_EQ(top.size(), size_t{2});
  CHECK_EQ(top[0].url, std::string("https://example.com/page"));
}

LIORA_TEST(storage_history_merge_is_idempotent) {
  Fixture f;
  HistoryEntry e;
  e.url = "https://imported.example";
  e.title = "Imported";
  e.visitCount = 5;
  e.lastVisit = nowMs() - 1000;
  f.storage.mergeHistory(e);
  f.storage.mergeHistory(e);
  f.storage.mergeHistory(e);

  auto found = f.storage.searchHistory("imported");
  CHECK_EQ(found.size(), size_t{1});
  // Counts are not summed for the same import; the larger value wins.
  CHECK_EQ(found[0].visitCount, int64_t{5});
}

LIORA_TEST(storage_history_clear_range) {
  Fixture f;
  int64_t now = nowMs();
  f.storage.addVisit("https://old.example", "Old", false, now - 10 * 24 * 3600 * 1000);
  f.storage.addVisit("https://new.example", "New", false, now);

  f.storage.clearHistory(0, now - 24 * 3600 * 1000);
  auto found = f.storage.searchHistory("");
  CHECK_EQ(found.size(), size_t{1});
  CHECK_EQ(found[0].url, std::string("https://new.example"));

  f.storage.clearHistory();
  CHECK(f.storage.searchHistory("").empty());
}

LIORA_TEST(storage_permissions) {
  Fixture f;
  f.storage.setPermission("https://a.example", "camera", "allow");
  CHECK_EQ(*f.storage.permission("https://a.example", "camera"), std::string("allow"));
  CHECK(!f.storage.permission("https://a.example", "microphone").has_value());

  f.storage.setPermission("https://a.example", "camera", "block");
  CHECK_EQ(*f.storage.permission("https://a.example", "camera"), std::string("block"));

  // An empty value removes the entry.
  f.storage.setPermission("https://a.example", "camera", "");
  CHECK(!f.storage.permission("https://a.example", "camera").has_value());
}

LIORA_TEST(storage_download_history) {
  Fixture f;

  DownloadRecord first;
  first.id = "d1";
  first.filename = "first.bin";
  first.path = "/tmp/first.bin";
  first.state = "finished";
  first.bytes = 100;
  first.created = 1000;
  f.storage.saveDownload(first);

  DownloadRecord second;
  second.id = "d2";
  second.filename = "second.bin";
  second.path = "/tmp/second.bin";
  second.state = "failed";
  second.error = "network lost";
  second.created = 2000;
  f.storage.saveDownload(second);

  // Re-saving the same id updates it rather than duplicating.
  first.bytes = 250;
  first.state = "finished";
  f.storage.saveDownload(first);

  auto list = f.storage.downloads(10);
  CHECK_EQ(list.size(), size_t{2});
  CHECK_EQ(list[0].id, std::string("d2"));  // newest first
  CHECK_EQ(list[0].error, std::string("network lost"));
  CHECK_EQ(list[1].id, std::string("d1"));
  CHECK_EQ(list[1].bytes, int64_t{250});

  CHECK(f.storage.removeDownload("d2"));
  CHECK_EQ(f.storage.downloads(10).size(), size_t{1});

  f.storage.clearDownloads();
  CHECK(f.storage.downloads(10).empty());
}

LIORA_TEST(storage_export_import_roundtrip_and_dedupe) {
  Fixture f;
  CHECK(f.storage.addBookmark(Storage::kBookmarkBar, "One", "https://one.example").has_value());
  f.storage.addVisit("https://one.example", "One", true);
  f.storage.setPermission("https://one.example", "camera", "allow");

  Json exported = f.storage.exportData();
  CHECK_EQ(exported["bookmarks"]["bar"]["children"].size(), size_t{1});
  CHECK_EQ(exported["history"].size(), size_t{1});
  CHECK_EQ(exported["permissions"].size(), size_t{1});

  // Importing the same data twice must not duplicate anything.
  CHECK(!f.storage.importData(exported, true));
  CHECK(!f.storage.importData(exported, true));
  CHECK_EQ(f.storage.children(Storage::kBookmarkBar).size(), size_t{1});
  CHECK_EQ(f.storage.searchHistory("one").size(), size_t{1});

  // A replacing import clears first.
  CHECK(!f.storage.importData(exported, false));
  CHECK_EQ(f.storage.children(Storage::kBookmarkBar).size(), size_t{1});
}

LIORA_TEST(storage_favicon_cache_roundtrip) {
  Fixture f;
  CHECK(!f.storage.favicon("x.example").has_value());

  f.storage.setFavicon("x.example", "data:image/png;base64,AAAA");
  auto icon = f.storage.favicon("x.example");
  CHECK(icon.has_value());
  CHECK_EQ(*icon, std::string("data:image/png;base64,AAAA"));

  // Overwriting replaces, never duplicates.
  f.storage.setFavicon("x.example", "data:image/png;base64,BBBB");
  CHECK_EQ(*f.storage.favicon("x.example"), std::string("data:image/png;base64,BBBB"));
  CHECK_EQ(f.storage.favicons(10).size(), size_t{1});
  CHECK_EQ(f.storage.favicons(10)[0].host, std::string("x.example"));

  // An empty value removes the entry.
  f.storage.setFavicon("x.example", "");
  CHECK(!f.storage.favicon("x.example").has_value());
  CHECK(f.storage.favicons(10).empty());

  // Empty hosts are ignored rather than stored.
  f.storage.setFavicon("", "data:image/png;base64,CCCC");
  CHECK(f.storage.favicons(10).empty());
}

LIORA_TEST(storage_favicon_cache_stays_bounded) {
  Fixture f;
  for (int i = 0; i < 520; ++i) {
    f.storage.setFavicon("host" + std::to_string(i) + ".example", "data:image/png;base64,AAAA");
  }
  auto all = f.storage.favicons(1000);
  CHECK_EQ(all.size(), size_t{512});
  // The oldest entries were evicted, the newest survive.
  CHECK(!f.storage.favicon("host0.example").has_value());
  CHECK(f.storage.favicon("host519.example").has_value());
}

LIORA_TEST(storage_favicon_survives_reopen) {
  TempDir dir;
  {
    Storage storage(dir / "liora.db");
    CHECK(storage.open());
    storage.setFavicon("persist.example", "data:image/png;base64,AAAA");
  }
  Storage reopened(dir / "liora.db");
  CHECK(reopened.open());
  auto icon = reopened.favicon("persist.example");
  CHECK(icon.has_value());
  CHECK_EQ(*icon, std::string("data:image/png;base64,AAAA"));
}

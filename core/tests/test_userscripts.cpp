#include "harness.hpp"

#include "liora/userscripts.hpp"

using namespace liora;

namespace {

UserScript makeScript(const std::string& name, std::vector<std::string> matches,
                      const std::string& code = "console.log(1);") {
  UserScript s;
  s.name = name;
  s.matches = std::move(matches);
  s.code = code;
  return s;
}

}  // namespace

LIORA_TEST(userscript_pattern_matching) {
  // Bare domain matches itself and subdomains.
  CHECK(UserScriptSet::patternMatches("x.com", "x.com"));
  CHECK(UserScriptSet::patternMatches("x.com", "www.x.com"));
  CHECK(UserScriptSet::patternMatches("x.com", "a.b.x.com"));
  CHECK(!UserScriptSet::patternMatches("x.com", "notx.com"));
  CHECK(!UserScriptSet::patternMatches("x.com", "x.com.evil.com"));

  // Subdomains-only pattern excludes the apex.
  CHECK(UserScriptSet::patternMatches("*.x.com", "www.x.com"));
  CHECK(!UserScriptSet::patternMatches("*.x.com", "x.com"));

  // Exact pattern matches only the apex.
  CHECK(UserScriptSet::patternMatches("=x.com", "x.com"));
  CHECK(!UserScriptSet::patternMatches("=x.com", "www.x.com"));

  // Wildcard matches everything.
  CHECK(UserScriptSet::patternMatches("*", "anything.example"));
}

LIORA_TEST(userscript_pattern_normalization) {
  std::string error;
  CHECK_EQ(*UserScriptSet::normalizePattern("X.com", &error), std::string("x.com"));
  CHECK_EQ(*UserScriptSet::normalizePattern("*.X.com", &error), std::string("*.x.com"));
  CHECK_EQ(*UserScriptSet::normalizePattern("=X.com", &error), std::string("=x.com"));
  CHECK_EQ(*UserScriptSet::normalizePattern("*", &error), std::string("*"));
  CHECK(!UserScriptSet::normalizePattern("not a domain", &error));
  CHECK(!UserScriptSet::normalizePattern("a*b.com", &error));
}

LIORA_TEST(userscript_validation_rejects_bad_input) {
  std::string error;
  // No name.
  auto noName = UserScript::fromJson(Json{{"matches", Json::array({"x.com"})}, {"code", "x"}}, &error);
  CHECK(!noName);
  // No code.
  auto noCode = UserScript::fromJson(Json{{"name", "n"}, {"matches", Json::array({"x.com"})}}, &error);
  CHECK(!noCode);
  // No matches.
  auto noMatch = UserScript::fromJson(Json{{"name", "n"}, {"code", "x"}}, &error);
  CHECK(!noMatch);
  // Bad runAt.
  auto badRun = UserScript::fromJson(
      Json{{"name", "n"}, {"code", "x"}, {"matches", Json::array({"x.com"})}, {"runAt", "never"}},
      &error);
  CHECK(!badRun);
  // Oversized code.
  auto bigCode = UserScript::fromJson(
      Json{{"name", "n"}, {"code", std::string(300 * 1024, 'a')}, {"matches", Json::array({"x.com"})}},
      &error);
  CHECK(!bigCode);
}

LIORA_TEST(userscript_match_returns_only_enabled) {
  UserScriptSet set;
  auto a = makeScript("a", {"x.com"});
  a.id = "a";
  auto b = makeScript("b", {"x.com"});
  b.id = "b";
  b.enabled = false;
  CHECK(!set.add(a));
  CHECK(!set.add(b));

  auto matched = set.match("www.x.com");
  CHECK_EQ(matched.size(), size_t{1});
  CHECK_EQ(matched[0]->id, std::string("a"));

  // Disabling removes it from matches.
  CHECK(!set.setEnabled("a", false));
  CHECK(set.match("www.x.com").empty());
}

LIORA_TEST(userscript_add_update_remove) {
  UserScriptSet set;
  auto s = makeScript("demo", {"x.com"});
  CHECK(!set.add(s));
  CHECK_EQ(set.scripts().size(), size_t{1});
  std::string id = set.scripts()[0].id;
  CHECK(!id.empty());

  // Update changes code and preserves id/created.
  auto updated = *set.find(id);
  updated.code = "console.log(2);";
  CHECK(!set.update(updated));
  CHECK_EQ(set.find(id)->code, std::string("console.log(2);"));

  // Duplicate id rejected.
  auto dup = makeScript("dup", {"y.com"});
  dup.id = id;
  CHECK(set.add(dup).has_value());

  // Remove.
  CHECK(set.remove(id));
  CHECK(set.scripts().empty());
  CHECK(!set.remove(id));
}

LIORA_TEST(userscript_roundtrip_json) {
  UserScriptSet set;
  auto s = makeScript("roundtrip", {"x.com", "*.y.com"});
  s.runAt = ScriptRunAt::kStart;
  CHECK(!set.add(s));

  Json exported = set.toJson();
  UserScriptSet reopened;
  CHECK(!reopened.load(exported));
  CHECK_EQ(reopened.scripts().size(), size_t{1});
  CHECK_EQ(reopened.scripts()[0].runAt, ScriptRunAt::kStart);
  CHECK_EQ(reopened.scripts()[0].matches.size(), size_t{2});
}
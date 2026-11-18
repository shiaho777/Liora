#include "liora/core.hpp"

namespace liora {

void Core::loadUserScripts() {
  std::lock_guard lock(userScriptsMutex_);
  auto text = readFile(paths_.root / "userscripts.json");
  if (!text) return;
  Json doc = Json::parse(*text, nullptr, false);
  if (doc.is_discarded() || !doc.is_object()) return;
  auto it = doc.find("scripts");
  if (it == doc.end() || !it->is_array()) return;
  // A corrupt file must not stop the browser; keep whatever parses.
  if (auto error = userScripts_.load(*it)) {
    rpc_.notify("core.warning", {{"source", "userscripts"}, {"message", *error}});
  }
}

void Core::saveUserScripts() {
  std::lock_guard lock(userScriptsMutex_);
  Json doc = {{"version", 1}, {"scripts", userScripts_.toJson()}};
  std::string error;
  if (!writeFileAtomic(paths_.root / "userscripts.json",
                       doc.dump(2, ' ', false, Json::error_handler_t::replace), &error)) {
    rpc_.notify("core.warning", {{"source", "userscripts"}, {"message", error}});
  }
}

void Core::registerUserScriptMethods() {
  rpc_.setNamespaceDescription("userscript", "User scripts injected into matching pages");

  rpc_.add("userscript.list", [this](const Json&) {
    std::lock_guard lock(userScriptsMutex_);
    return userScripts_.toJson();
  });

  rpc_.add("userscript.add", [this](const Json& params) {
    std::string error;
    auto script = UserScript::fromJson(params, &error);
    if (!script) rpcFail(kRpcInvalidParams, error);
    {
      std::lock_guard lock(userScriptsMutex_);
      if (auto addError = userScripts_.add(*script)) rpcFail(kRpcInvalidParams, *addError);
    }
    saveUserScripts();
    rpc_.notify("userscripts.changed", nullptr);
    std::lock_guard lock(userScriptsMutex_);
    return userScripts_.scripts().back().toJson();
  });

  rpc_.add("userscript.update", [this](const Json& params) {
    std::string error;
    auto script = UserScript::fromJson(params, &error);
    if (!script) rpcFail(kRpcInvalidParams, error);
    if (script->id.empty()) rpcFail(kRpcInvalidParams, "missing script id");
    {
      std::lock_guard lock(userScriptsMutex_);
      if (auto updateError = userScripts_.update(*script)) rpcFail(kRpcNotFound, *updateError);
    }
    saveUserScripts();
    rpc_.notify("userscripts.changed", nullptr);
    std::lock_guard lock(userScriptsMutex_);
    return userScripts_.find(script->id)->toJson();
  });

  rpc_.add("userscript.remove", [this](const Json& params) {
    std::string id = params::string(params, "id");
    {
      std::lock_guard lock(userScriptsMutex_);
      if (!userScripts_.remove(id)) rpcFail(kRpcNotFound, "unknown script: " + id);
    }
    saveUserScripts();
    rpc_.notify("userscripts.changed", nullptr);
    return Json{{"ok", true}};
  });

  rpc_.add("userscript.setEnabled", [this](const Json& params) {
    std::string id = params::string(params, "id");
    bool enabled = params.value("enabled", true);
    {
      std::lock_guard lock(userScriptsMutex_);
      if (auto error = userScripts_.setEnabled(id, enabled)) rpcFail(kRpcNotFound, *error);
    }
    saveUserScripts();
    rpc_.notify("userscripts.changed", nullptr);
    return Json{{"ok", true}};
  });

  // The shell asks which scripts apply to a host before loading a page.
  rpc_.add("userscript.forHost", [this](const Json& params) {
    std::string host = params::string(params, "host");
    std::lock_guard lock(userScriptsMutex_);
    Json out = Json::array();
    for (const auto* script : userScripts_.match(host)) out.push_back(script->toJson());
    return out;
  });
}

}  // namespace liora
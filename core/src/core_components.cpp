#include "liora/core.hpp"

namespace liora {

namespace {

ComponentFile fileFromJson(const Json& j) {
  ComponentFile file;
  file.path = params::string(j, "path");
  file.sha256 = params::string(j, "sha256");
  file.size = params::optionalInteger(j, "size").value_or(0);
  return file;
}

InstallPlan planFromJson(const Json& j) {
  InstallPlan plan;
  plan.id = params::string(j, "id");
  plan.version = params::string(j, "version");
  plan.url = params::optionalString(j, "url").value_or("");
  plan.size = params::optionalInteger(j, "size").value_or(0);
  plan.signature = params::optionalString(j, "signature").value_or("");
  plan.stagingDir = utf8ToPath(params::string(j, "stagingDir"));
  if (auto files = j.find("files"); files != j.end() && files->is_array()) {
    for (const auto& f : *files) plan.files.push_back(fileFromJson(f));
  }
  return plan;
}

}  // namespace

void Core::registerComponentMethods() {
  // ---- Kernels ----

  rpc_.add("kernels.list", [this](const Json&) { return kernels_.toJson(); });

  rpc_.add("kernels.current", [this](const Json& params) {
    Scope scope;
    if (auto site = params::optionalString(params, "site"); site && !site->empty()) {
      scope.site = Settings::normalizeSite(*site);
    }
    if (auto tab = params::optionalInteger(params, "tab"); tab) scope.tab = *tab;
    return Json{{"id", kernels_.resolve(settings_, scope)}};
  });

  rpc_.add("kernels.select", [this](const Json& params) {
    std::string id = params::string(params, "id");
    auto info = kernels_.find(id);
    if (!info) rpcFail(kRpcNotFound, "unknown kernel: " + id);
    if (!info->available) rpcFail(kRpcUnavailable, info->name + " is not installed");

    Scope scope;
    if (auto site = params::optionalString(params, "site"); site && !site->empty()) {
      scope.site = Settings::normalizeSite(*site);
    }
    if (auto tab = params::optionalInteger(params, "tab"); tab) scope.tab = *tab;

    if (auto error = settings_.set("kernel.default", id, scope)) rpcFail(kRpcInvalidParams, *error);
    persistSettings();
    rpc_.notify("kernels.changed", {{"id", id}, {"scope", scope.site}, {"tab", scope.tab}});
    // The shell applies the network setup when it creates the next view.
    return Json{{"id", id},
                {"setup", applyNetwork(id).toJson()},
                {"restartRequired", info->netNeedsRestart}};
  });

  rpc_.add("kernels.setup", [this](const Json& params) {
    std::string id = params::optionalString(params, "id").value_or(kernels_.resolve(settings_, Scope{}));
    if (!kernels_.find(id)) rpcFail(kRpcNotFound, "unknown kernel: " + id);
    return applyNetwork(id).toJson();
  });

  // ---- Components ----

  rpc_.add("components.catalog", [this](const Json&) { return components_.describe(); });

  rpc_.add("components.updates", [this](const Json&) {
    return components_.updates(ComponentManager::platformOs(), ComponentManager::platformArch());
  });

  rpc_.add("components.installed", [this](const Json&) {
    Json out = Json::array();
    for (const auto& item : components_.installed()) out.push_back(item.toJson());
    return out;
  });

  rpc_.add("components.plan", [this](const Json& params) {
    std::string id = params::string(params, "id");
    std::string version = params::optionalString(params, "version").value_or("");
    std::string os = params::optionalString(params, "os").value_or(ComponentManager::platformOs());
    std::string arch = params::optionalString(params, "arch").value_or(ComponentManager::platformArch());
    std::string error;
    auto plan = components_.plan(id, version, os, arch, &error);
    if (!plan) rpcFail(kRpcNotFound, error);
    return plan->toJson();
  });

  rpc_.add("components.commit", [this](const Json& params) {
    InstallPlan plan = planFromJson(params);
    if (auto error = components_.commit(plan)) rpcFail(kRpcInvalidParams, *error);
    rpc_.notify("components.changed", {{"id", plan.id}, {"version", plan.version}});
    return Json{{"id", plan.id}, {"version", plan.version}, {"installed", true}};
  });

  rpc_.add("components.activate", [this](const Json& params) {
    std::string id = params::string(params, "id");
    std::string version = params::string(params, "version");
    if (!components_.activate(id, version)) rpcFail(kRpcNotFound, "that version is not installed");
    rpc_.notify("components.changed", {{"id", id}, {"version", version}});
    return Json{{"ok", true}};
  });

  rpc_.add("components.remove", [this](const Json& params) {
    std::string id = params::string(params, "id");
    std::string version = params::string(params, "version");
    if (!components_.remove(id, version)) rpcFail(kRpcNotFound, "that version is not installed");
    rpc_.notify("components.changed", {{"id", id}, {"version", version}});
    return Json{{"ok", true}};
  });

  rpc_.add("components.rollback", [this](const Json& params) {
    std::string id = params::string(params, "id");
    if (!components_.rollback(id)) rpcFail(kRpcNotFound, "no installed version of " + id);
    rpc_.notify("components.changed", {{"id", id}});
    return Json{{"ok", true}};
  });

  rpc_.add("components.reloadCatalog", [this](const Json& params) {
    // The shell normally sends the downloaded document itself; a path is kept
    // for local tooling. Both forms are verified before anything is replaced,
    // and a valid catalog is persisted for the next launch.
    std::string text;
    if (params.contains("catalog") && params["catalog"].is_string()) {
      text = params["catalog"].get<std::string>();
      if (text.empty()) rpcFail(kRpcInvalidParams, "the catalog is empty");
      if (auto error = components_.loadCatalog(text)) rpcFail(kRpcInvalidParams, *error);
    } else {
      std::string path = params::string(params, "path");
      auto data = readFile(utf8ToPath(path));
      if (!data) rpcFail(kRpcInvalidParams, "cannot read " + path);
      if (auto error = components_.loadCatalog(*data)) rpcFail(kRpcInvalidParams, *error);
      text = *data;
    }

    std::string error;
    if (!writeFileAtomic(paths().root / "components.json", text, &error)) {
      rpcFail(kRpcInternalError,
              error.empty() ? "cannot save the component catalog" : error);
    }
    rpc_.notify("components.changed", {{"id", "*"}});
    return components_.describe();
  });
}

}  // namespace liora

#include "liora/components.hpp"

#include <algorithm>
#include <cstdlib>
#include <system_error>

#if defined(_WIN32)
#include <windows.h>
#elif defined(__APPLE__)
#include <sys/utsname.h>
#include <unistd.h>
#else
#include <sys/utsname.h>
#endif

namespace liora {

namespace {

constexpr const char* kIndexFile = "index.json";

std::string getStr(const Json& j, const char* key) {
  return j.is_object() && j.contains(key) && j[key].is_string() ? j[key].get<std::string>() : std::string();
}

int64_t getInt(const Json& j, const char* key) {
  return j.is_object() && j.contains(key) && j[key].is_number_integer() ? j[key].get<int64_t>() : 0;
}

bool isSafeRelativePath(std::string_view path) {
  if (path.empty() || path.size() > 512 || path.front() == '/' || path.find('\\') != std::string_view::npos) {
    return false;
  }
  if (path.size() > 1 && path[1] == ':') return false;
  for (const auto& part : split(path, '/', false)) {
    if (part.empty() || part == "." || part == "..") return false;
  }
  return true;
}

// Compares dotted version strings numerically: "1.10" > "1.9".
int compareVersions(std::string_view a, std::string_view b) {
  auto parts = [](std::string_view v) {
    std::vector<std::string> out = split(v, '.');
    if (!out.empty() && out.back().empty()) out.pop_back();
    return out;
  };
  auto pa = parts(a), pb = parts(b);
  for (size_t i = 0; i < std::max(pa.size(), pb.size()); ++i) {
    auto num = [](const std::string& s) {
      size_t end = 0;
      while (end < s.size() && s[end] >= '0' && s[end] <= '9') ++end;
      return end ? std::strtoll(s.substr(0, end).c_str(), nullptr, 10) : 0LL;
    };
    long long na = i < pa.size() ? num(pa[i]) : 0;
    long long nb = i < pb.size() ? num(pb[i]) : 0;
    if (na != nb) return na < nb ? -1 : 1;
  }
  return 0;
}

std::string versionKey(std::string_view v) { return "v" + std::string(v); }

}  // namespace

Json ComponentFile::toJson() const { return {{"path", path}, {"sha256", sha256}, {"size", size}}; }

bool ComponentBuild::matches(std::string_view platformOs, std::string_view platformArch) const {
  if (os != platformOs) return false;
  return arch == platformArch || arch == "universal" || platformArch == "universal";
}

Json ComponentBuild::toJson() const {
  Json filesJson = Json::array();
  for (const auto& f : files) filesJson.push_back(f.toJson());
  return {{"os", os}, {"arch", arch}, {"url", url}, {"size", size}, {"files", filesJson}};
}

Json ComponentVersion::toJson() const {
  Json buildsJson = Json::array();
  for (const auto& b : builds) buildsJson.push_back(b.toJson());
  return {{"version", version}, {"minApp", minApp}, {"builds", buildsJson}};
}

const ComponentVersion* ComponentEntry::version(std::string_view v) const {
  for (const auto& item : versions) {
    if (item.version == v) return &item;
  }
  return nullptr;
}

const ComponentVersion* ComponentEntry::latest() const {
  const ComponentVersion* best = nullptr;
  for (const auto& item : versions) {
    if (!best || compareVersions(best->version, item.version) < 0) best = &item;
  }
  return best;
}

Json ComponentEntry::toJson() const {
  Json versionsJson = Json::array();
  for (const auto& v : versions) versionsJson.push_back(v.toJson());
  return {{"id", id},     {"name", name},       {"kind", kind},
          {"description", description}, {"required", required}, {"versions", versionsJson}};
}

Json InstalledComponent::toJson() const {
  return {{"id", id}, {"version", version}, {"path", path}, {"installedAt", installedAt}, {"current", current}};
}

Json InstallPlan::toJson() const {
  Json filesJson = Json::array();
  for (const auto& f : files) filesJson.push_back(f.toJson());
  return {{"id", id},
          {"version", version},
          {"url", url},
          {"size", size},
          {"signature", signature},
          {"files", filesJson},
          {"stagingDir", pathToUtf8(stagingDir)}};
}

ComponentManager::ComponentManager(fs::path root) : root_(std::move(root)) { loadIndex(); }

void ComponentManager::setVerifier(SignatureVerifier verifier) {
  std::lock_guard lock(mutex_);
  verifier_ = std::move(verifier);
}

void ComponentManager::setPublicKey(std::string key) {
  std::lock_guard lock(mutex_);
  publicKey_ = std::move(key);
}

void ComponentManager::requireSignature(bool require) {
  std::lock_guard lock(mutex_);
  requireSignature_ = require;
}

bool ComponentManager::signatureRequired() const {
  std::lock_guard lock(mutex_);
  return requireSignature_ || (verifier_ && !publicKey_.empty());
}

std::string ComponentManager::platformOs() {
#if defined(_WIN32)
  return "windows";
#elif defined(__APPLE__)
  return "macos";
#else
  return "linux";
#endif
}

std::string ComponentManager::platformArch() {
#if defined(_WIN32)
  SYSTEM_INFO info{};
  GetNativeSystemInfo(&info);
  switch (info.wProcessorArchitecture) {
    case PROCESSOR_ARCHITECTURE_ARM64: return "arm64";
    case PROCESSOR_ARCHITECTURE_AMD64: return "x64";
    default: return "x86";
  }
#else
  struct utsname u {};
  if (uname(&u) == 0) {
    std::string machine = toLower(u.machine);
    if (machine == "arm64" || machine == "aarch64") return "arm64";
    if (machine == "x86_64" || machine == "amd64") return "x64";
    return machine;
  }
  return "unknown";
#endif
}

std::optional<std::string> ComponentManager::checkCatalogSignature(const Json& doc) const {
  std::lock_guard lock(mutex_);
  if (!requireSignature_) return std::nullopt;
  if (!verifier_) return "signature verification is not available";
  if (publicKey_.empty()) return "no update key is configured";
  if (catalogSignature_.empty()) return "the catalog is not signed";
  // The signature covers the canonical form of the document without its signature.
  Json body = doc;
  body.erase("signature");
  if (!verifier_(publicKey_, body.dump(), catalogSignature_)) return "the catalog signature is invalid";
  return std::nullopt;
}

std::optional<std::string> ComponentManager::loadCatalog(std::string_view json) {
  Json doc = Json::parse(json, nullptr, false);
  if (doc.is_discarded() || !doc.is_object()) return "the component catalog is not valid JSON";
  if (auto error = checkCatalogSignature(doc)) return error;

  auto list = doc.find("components");
  if (list == doc.end() || !list->is_array()) return "the component catalog has no components";

  std::vector<ComponentEntry> entries;
  for (const auto& item : *list) {
    if (!item.is_object()) continue;
    ComponentEntry entry;
    entry.id = getStr(item, "id");
    if (entry.id.empty()) continue;
    entry.name = getStr(item, "name");
    entry.kind = getStr(item, "kind");
    entry.description = getStr(item, "description");
    entry.required = item.value("required", false);
    auto versions = item.find("versions");
    if (versions == item.end() || !versions->is_array()) continue;
    for (const auto& v : *versions) {
      if (!v.is_object()) continue;
      ComponentVersion version;
      version.version = getStr(v, "version");
      version.minApp = getStr(v, "minApp");
      if (version.version.empty()) continue;
      auto builds = v.find("builds");
      if (builds == v.end() || !builds->is_array()) continue;
      for (const auto& b : *builds) {
        if (!b.is_object()) continue;
        ComponentBuild build;
        build.os = getStr(b, "os");
        build.arch = getStr(b, "arch");
        build.url = getStr(b, "url");
        build.size = getInt(b, "size");
        if (build.os.empty() || build.url.empty()) continue;
        auto files = b.find("files");
        if (files != b.end() && files->is_array()) {
          for (const auto& f : *files) {
            if (!f.is_object()) continue;
            ComponentFile file{getStr(f, "path"), getStr(f, "sha256"), getInt(f, "size")};
            if (!isSafeRelativePath(file.path) || file.sha256.size() != 64) continue;
            build.files.push_back(std::move(file));
          }
        }
        version.builds.push_back(std::move(build));
      }
      entry.versions.push_back(std::move(version));
    }
    entries.push_back(std::move(entry));
  }

  {
    std::lock_guard lock(mutex_);
    entries_ = std::move(entries);
  }
  return std::nullopt;
}

std::optional<std::string> ComponentManager::loadCatalogFile(const fs::path& file) {
  auto text = readFile(file);
  if (!text) return "cannot read " + pathToUtf8(file);
  Json doc = Json::parse(*text, nullptr, false);
  if (!doc.is_discarded() && doc.is_object() && doc.contains("signature") && doc["signature"].is_string()) {
    std::lock_guard lock(mutex_);
    catalogSignature_ = doc["signature"].get<std::string>();
  }
  return loadCatalog(*text);
}

const ComponentEntry* ComponentManager::find(std::string_view id) const {
  std::lock_guard lock(mutex_);
  for (const auto& e : entries_) {
    if (e.id == id) return &e;
  }
  return nullptr;
}

Json ComponentManager::describe() const {
  Json out = Json::array();
  std::lock_guard lock(mutex_);
  for (const auto& e : entries_) {
    Json item = e.toJson();
    for (const auto& inst : installed_) {
      if (inst.id != e.id) continue;
      item["installedVersion"] = inst.version;
      break;
    }
    out.push_back(std::move(item));
  }
  return out;
}

std::vector<std::string> ComponentManager::updates(std::string_view os, std::string_view arch) const {
  std::vector<std::string> out;
  std::lock_guard lock(mutex_);
  for (const auto& e : entries_) {
    const ComponentVersion* newest = nullptr;
    for (const auto& v : e.versions) {
      bool any = std::any_of(v.builds.begin(), v.builds.end(), [&](const ComponentBuild& b) { return b.matches(os, arch); });
      if (any && (!newest || compareVersions(newest->version, v.version) < 0)) newest = &v;
    }
    if (!newest) continue;
    const InstalledComponent* installedVersion = nullptr;
    for (const auto& inst : installed_) {
      if (inst.id == e.id && (!installedVersion || compareVersions(installedVersion->version, inst.version) < 0)) {
        installedVersion = &inst;
      }
    }
    if (!installedVersion || compareVersions(installedVersion->version, newest->version) < 0) out.push_back(e.id);
  }
  return out;
}

std::optional<InstallPlan> ComponentManager::plan(std::string_view id, std::string_view version,
                                                  std::string_view os, std::string_view arch,
                                                  std::string* error) const {
  auto fail = [&](std::string message) -> std::optional<InstallPlan> {
    if (error) *error = std::move(message);
    return std::nullopt;
  };
  std::lock_guard lock(mutex_);
  const ComponentEntry* entry = nullptr;
  for (const auto& e : entries_) {
    if (e.id == id) entry = &e;
  }
  if (!entry) return fail("unknown component '" + std::string(id) + "'");

  const ComponentVersion* chosen = version.empty() ? entry->latest() : entry->version(version);
  if (!chosen) return fail("no version '" + std::string(version) + "' of " + std::string(id));

  const ComponentBuild* build = nullptr;
  for (const auto& b : chosen->builds) {
    if (b.matches(os, arch)) {
      build = &b;
      break;
    }
  }
  if (!build) return fail(std::string(id) + " " + chosen->version + " has no build for " + std::string(os) + "/" +
                          std::string(arch));

  InstallPlan result;
  result.id = entry->id;
  result.version = chosen->version;
  result.url = build->url;
  result.size = build->size;
  result.files = build->files;
  result.stagingDir = root_ / "staging" / (entry->id + "-" + chosen->version);
  return result;
}

std::optional<std::string> ComponentManager::verifyTree(const fs::path& dir,
                                                        const std::vector<ComponentFile>& files) {
  if (files.empty()) return "the package lists no files";
  for (const auto& file : files) {
    if (!isSafeRelativePath(file.path)) return "unsafe path in the package: " + file.path;
    fs::path full = dir / utf8ToPath(file.path);
    // resolve() collapses ".." so a symlink cannot escape the staging directory.
    std::error_code ec;
    fs::path resolved = fs::weakly_canonical(full, ec);
    fs::path base = fs::weakly_canonical(dir, ec);
    if (ec) return "cannot inspect " + file.path;
    auto rel = resolved.lexically_relative(base);
    if (rel.empty() || *rel.begin() == "..") return "the package writes outside its directory: " + file.path;

    auto data = readFile(full);
    if (!data) return "missing file: " + file.path;
    if (file.size > 0 && static_cast<int64_t>(data->size()) != file.size) return "wrong size: " + file.path;
    if (sha256Hex(*data) != toLower(file.sha256)) return "checksum mismatch: " + file.path;
  }
  return std::nullopt;
}

std::optional<std::string> ComponentManager::commit(const InstallPlan& plan) {
  if (auto error = verifyTree(plan.stagingDir, plan.files)) return error;

  std::lock_guard lock(mutex_);
  bool known = false;
  for (const auto& e : entries_) known = known || e.id == plan.id;
  if (!known) return "unknown component '" + plan.id + "'";

  fs::path target = componentPath(plan.id, plan.version);
  std::error_code ec;
  fs::remove_all(target, ec);
  if (target.has_parent_path()) fs::create_directories(target.parent_path(), ec);
  fs::rename(plan.stagingDir, target, ec);
  if (ec) return "cannot move the package into place: " + ec.message();

  for (auto& inst : installed_) {
    if (inst.id == plan.id) inst.current = false;
  }
  installed_.erase(std::remove_if(installed_.begin(), installed_.end(),
                                  [&](const InstalledComponent& i) {
                                    return i.id == plan.id && i.version == plan.version;
                                  }),
                   installed_.end());
  installed_.push_back({plan.id, plan.version, pathToUtf8(target), nowMs(), true});
  std::string error;
  if (!saveIndex(&error)) return error.empty() ? "cannot save the component index" : error;
  return std::nullopt;
}

bool ComponentManager::activate(std::string_view id, std::string_view version) {
  std::lock_guard lock(mutex_);
  bool found = false;
  for (auto& inst : installed_) {
    if (inst.id != id) continue;
    if (inst.version == version) found = true;
  }
  if (!found) return false;
  for (auto& inst : installed_) {
    if (inst.id == id) inst.current = inst.version == version;
  }
  saveIndex();
  return true;
}

bool ComponentManager::remove(std::string_view id, std::string_view version) {
  std::lock_guard lock(mutex_);
  auto it = std::find_if(installed_.begin(), installed_.end(),
                         [&](const InstalledComponent& i) { return i.id == id && i.version == version; });
  if (it == installed_.end()) return false;
  std::error_code ec;
  fs::remove_all(utf8ToPath(it->path), ec);
  installed_.erase(it);
  saveIndex();
  return true;
}

bool ComponentManager::rollback(std::string_view id) {
  std::lock_guard lock(mutex_);
  const InstalledComponent* best = nullptr;
  for (const auto& inst : installed_) {
    if (inst.id == id && (!best || compareVersions(best->version, inst.version) < 0)) best = &inst;
  }
  if (!best) return false;
  std::string version = best->version;
  for (auto& inst : installed_) {
    if (inst.id == id) inst.current = inst.version == version;
  }
  saveIndex();
  return true;
}

std::optional<InstalledComponent> ComponentManager::current(std::string_view id) const {
  std::lock_guard lock(mutex_);
  for (const auto& inst : installed_) {
    if (inst.id == id && inst.current) return inst;
  }
  return std::nullopt;
}

std::vector<InstalledComponent> ComponentManager::installed() const {
  std::lock_guard lock(mutex_);
  return installed_;
}

fs::path ComponentManager::componentPath(std::string_view id, std::string_view version) const {
  return root_ / utf8ToPath(std::string(id)) / versionKey(version);
}

bool ComponentManager::available(std::string_view id) const {
  auto inst = current(id);
  if (!inst) return false;
  std::error_code ec;
  return fs::is_directory(utf8ToPath(inst->path), ec);
}

bool ComponentManager::loadIndex() {
  auto text = readFile(root_ / kIndexFile);
  if (!text) return true;
  Json doc = Json::parse(*text, nullptr, false);
  if (doc.is_discarded() || !doc.is_object()) return false;
  std::vector<InstalledComponent> installed;
  if (auto list = doc.find("installed"); list != doc.end() && list->is_array()) {
    for (const auto& item : *list) {
      InstalledComponent inst;
      inst.id = getStr(item, "id");
      inst.version = getStr(item, "version");
      inst.path = getStr(item, "path");
      inst.installedAt = getInt(item, "installedAt");
      inst.current = item.value("current", false);
      if (!inst.id.empty() && !inst.version.empty()) installed.push_back(std::move(inst));
    }
  }
  std::lock_guard lock(mutex_);
  installed_ = std::move(installed);
  return true;
}

bool ComponentManager::saveIndex(std::string* error) const {
  Json list = Json::array();
  for (const auto& inst : installed_) list.push_back(inst.toJson());
  Json doc = {{"version", 1}, {"installed", list}};
  std::error_code ec;
  fs::create_directories(root_, ec);
  return writeFileAtomic(root_ / kIndexFile, doc.dump(2), error);
}

}  // namespace liora

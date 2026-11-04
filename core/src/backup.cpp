#include "liora/backup.hpp"

#include <algorithm>
#include <ctime>
#include <system_error>

namespace liora {

namespace {

std::string timestampName(int64_t ms) {
  std::time_t t = static_cast<std::time_t>(ms / 1000);
  std::tm tm{};
#if defined(_WIN32)
  gmtime_s(&tm, &t);
#else
  gmtime_r(&t, &tm);
#endif
  char buf[32];
  std::strftime(buf, sizeof(buf), "%Y%m%d-%H%M%S", &tm);
  return buf;
}

bool safeTag(std::string_view tag) {
  if (tag.empty() || tag.size() > 32) return false;
  return std::all_of(tag.begin(), tag.end(), [](char c) {
    return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-';
  });
}

}  // namespace

void Backup::registerSection(Section section) {
  std::lock_guard lock(mutex_);
  if (!sections_.count(section.name)) order_.push_back(section.name);
  sections_[section.name] = std::move(section);
}

std::vector<std::string> Backup::sectionNames() const {
  std::lock_guard lock(mutex_);
  return order_;
}

Json Backup::create(const std::vector<std::string>& sections) const {
  std::vector<Section> selected;
  {
    std::lock_guard lock(mutex_);
    const auto& names = sections.empty() ? order_ : sections;
    for (const auto& name : names) {
      auto it = sections_.find(name);
      if (it != sections_.end() && it->second.exporter) selected.push_back(it->second);
    }
  }
  Json data = Json::object();
  for (const auto& s : selected) data[s.name] = s.exporter();

  Json env = Json::object();
  env["format"] = kFormat;
  env["version"] = kVersion;
  env["created"] = nowMs();
#ifdef LIORA_VERSION
  env["app"] = LIORA_VERSION;
#endif
  env["checksum"] = sha256Hex(data.dump());
  env["sections"] = std::move(data);
  return env;
}

bool Backup::write(const fs::path& file, const std::vector<std::string>& sections,
                   std::string* error) const {
  return writeFileAtomic(file, create(sections).dump(), error);
}

std::optional<std::string> Backup::verify(const Json& env) {
  if (!env.is_object() || env.value("format", "") != kFormat) return "not a Liora backup";
  if (!env.contains("version") || !env["version"].is_number_integer()) return "backup has no version";
  if (env["version"].get<int>() > kVersion) return "backup was made by a newer version of Liora";
  if (!env.contains("sections") || !env["sections"].is_object()) return "backup has no data";
  auto checksum = env.value("checksum", "");
  if (checksum.empty() || checksum != sha256Hex(env["sections"].dump())) {
    return "backup is damaged (checksum mismatch)";
  }
  return std::nullopt;
}

std::optional<Json> Backup::read(const fs::path& file, std::string* error) {
  auto text = readFile(file);
  if (!text) {
    if (error) *error = "cannot read backup file";
    return std::nullopt;
  }
  Json env = Json::parse(*text, nullptr, false);
  if (env.is_discarded()) {
    if (error) *error = "backup file is not valid JSON";
    return std::nullopt;
  }
  if (auto err = verify(env)) {
    if (error) *error = *err;
    return std::nullopt;
  }
  return env;
}

Json Backup::inspect(const Json& env) const {
  Json out = Json::object();
  if (auto err = verify(env)) {
    out["valid"] = false;
    out["error"] = *err;
    return out;
  }
  out["valid"] = true;
  out["created"] = env.value("created", int64_t{0});
  out["app"] = env.value("app", "");
  Json list = Json::array();
  std::lock_guard lock(mutex_);
  for (const auto& [name, data] : env["sections"].items()) {
    Json s = {{"name", name}};
    auto it = sections_.find(name);
    s["supported"] = it != sections_.end() && it->second.importer != nullptr;
    if (it != sections_.end() && it->second.summarizer) {
      s["summary"] = it->second.summarizer(data);
    } else if (data.is_array()) {
      s["summary"] = {{"count", data.size()}};
    }
    list.push_back(std::move(s));
  }
  out["sections"] = std::move(list);
  return out;
}

std::optional<std::string> Backup::restore(const Json& env, const std::vector<std::string>& sections,
                                           bool merge) const {
  if (auto err = verify(env)) return err;
  const Json& data = env["sections"];

  std::vector<std::pair<Section, const Json*>> plan;
  {
    std::lock_guard lock(mutex_);
    std::vector<std::string> names = sections;
    if (names.empty()) {
      for (const auto& name : order_) {
        if (data.contains(name)) names.push_back(name);
      }
    }
    for (const auto& name : names) {
      if (!data.contains(name)) return "backup does not contain \"" + name + "\"";
      auto it = sections_.find(name);
      if (it == sections_.end() || !it->second.importer) {
        return "this version of Liora cannot restore \"" + name + "\"";
      }
      plan.emplace_back(it->second, &data[name]);
    }
  }
  for (const auto& [section, value] : plan) {
    if (auto err = section.importer(*value, merge)) return section.name + ": " + *err;
  }
  return std::nullopt;
}

std::optional<fs::path> Backup::autoBackup(const fs::path& dir, size_t keep, std::string_view tag,
                                           std::string* error) const {
  if (!safeTag(tag)) {
    if (error) *error = "invalid backup tag";
    return std::nullopt;
  }
  std::error_code ec;
  fs::create_directories(dir, ec);
  auto name = std::string(tag) + "-" + timestampName(nowMs());
  auto file = dir / (name + kExtension);
  for (int i = 2; fs::exists(file, ec); ++i) file = dir / (name + "-" + std::to_string(i) + kExtension);
  if (!write(file, {}, error)) return std::nullopt;

  std::vector<fs::path> same;
  for (const auto& p : list(dir)) {
    if (startsWith(pathToUtf8(p.filename()), std::string(tag) + "-")) same.push_back(p);
  }
  // list() is newest first.
  for (size_t i = keep; i < same.size(); ++i) fs::remove(same[i], ec);
  return file;
}

std::vector<fs::path> Backup::list(const fs::path& dir) {
  std::vector<std::pair<fs::file_time_type, fs::path>> files;
  std::error_code ec;
  for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
    if (!it->is_regular_file(ec) || it->path().extension() != kExtension) continue;
    files.emplace_back(it->last_write_time(ec), it->path());
  }
  std::sort(files.begin(), files.end(), [](const auto& a, const auto& b) {
    if (a.first != b.first) return a.first > b.first;
    return a.second.filename() > b.second.filename();
  });
  std::vector<fs::path> out;
  for (auto& f : files) out.push_back(std::move(f.second));
  return out;
}

}  // namespace liora

#pragma once

#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "liora/json.hpp"
#include "liora/util.hpp"

namespace liora {

// A backup is a JSON envelope:
//   {"format": "liora-backup", "version": 1, "created": ms, "app": "0.1.0",
//    "sections": {name: data, ...}, "checksum": sha256(sections.dump())}
// Each kind of user data registers a section with an exporter and importer,
// so new data types do not change the format.
class Backup {
 public:
  using Exporter = std::function<Json()>;
  // Returns an error message on failure.
  using Importer = std::function<std::optional<std::string>(const Json& data, bool merge)>;
  // Summarizes section data for previews, e.g. {"count": 120}.
  using Summarizer = std::function<Json(const Json& data)>;

  struct Section {
    std::string name;
    Exporter exporter;
    Importer importer;
    Summarizer summarizer;
  };

  static constexpr int kVersion = 1;
  static constexpr const char* kFormat = "liora-backup";
  static constexpr const char* kExtension = ".liora";

  void registerSection(Section section);
  std::vector<std::string> sectionNames() const;

  // Builds an envelope with the given sections, or all sections when empty.
  Json create(const std::vector<std::string>& sections = {}) const;
  bool write(const fs::path& file, const std::vector<std::string>& sections = {},
             std::string* error = nullptr) const;

  // Reads and verifies an envelope (format, version, checksum).
  static std::optional<Json> read(const fs::path& file, std::string* error = nullptr);
  static std::optional<std::string> verify(const Json& envelope);

  // Sections present in the envelope with their summaries and whether this
  // build knows how to restore them.
  Json inspect(const Json& envelope) const;

  // Restores the selected sections (all known ones when empty). Every
  // selected section is validated before any is written.
  std::optional<std::string> restore(const Json& envelope, const std::vector<std::string>& sections,
                                     bool merge) const;

  // Writes an automatic backup into dir and keeps only the newest `keep`.
  std::optional<fs::path> autoBackup(const fs::path& dir, size_t keep, std::string_view tag = "auto",
                                     std::string* error = nullptr) const;
  static std::vector<fs::path> list(const fs::path& dir);

 private:
  mutable std::mutex mutex_;
  std::map<std::string, Section> sections_;
  std::vector<std::string> order_;
};

}  // namespace liora

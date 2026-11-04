#include "liora/paths.hpp"

#include <cstdlib>
#include <mutex>
#include <optional>
#include <system_error>

#if defined(_WIN32)
#include <windows.h>
#include <shlobj.h>
#else
#include <pwd.h>
#include <unistd.h>
#endif

#include "liora/util.hpp"

namespace liora::paths {

namespace {

std::mutex g_mutex;
std::optional<std::filesystem::path> g_dataRoot;

std::optional<std::filesystem::path> envPath(const char* name) {
#if defined(_WIN32)
  wchar_t* value = nullptr;
  size_t len = 0;
  std::wstring wname(name, name + std::char_traits<char>::length(name));
  if (_wdupenv_s(&value, &len, wname.c_str()) != 0 || value == nullptr) return std::nullopt;
  std::filesystem::path p(value);
  free(value);
  if (p.empty()) return std::nullopt;
  return p;
#else
  const char* value = std::getenv(name);
  if (value == nullptr || *value == '\0') return std::nullopt;
  return std::filesystem::path(value);
#endif
}

#if defined(_WIN32)
std::optional<std::filesystem::path> knownFolder(REFKNOWNFOLDERID id) {
  PWSTR raw = nullptr;
  if (FAILED(SHGetKnownFolderPath(id, KF_FLAG_DEFAULT, nullptr, &raw))) return std::nullopt;
  std::filesystem::path p(raw);
  CoTaskMemFree(raw);
  return p;
}
#endif

std::filesystem::path computeDataRoot() {
  if (auto overridden = envPath("LIORA_DATA_DIR")) return *overridden;
#if defined(_WIN32)
  return localAppData() / "Liora";
#elif defined(__APPLE__)
  return homeDir() / "Library" / "Application Support" / "Liora";
#else
  if (auto xdg = envPath("XDG_DATA_HOME")) return *xdg / "liora";
  return homeDir() / ".local" / "share" / "liora";
#endif
}

}  // namespace

std::filesystem::path dataRoot() {
  std::lock_guard lock(g_mutex);
  if (!g_dataRoot) g_dataRoot = computeDataRoot();
  return *g_dataRoot;
}

void setDataRoot(const std::filesystem::path& root) {
  std::lock_guard lock(g_mutex);
  g_dataRoot = root;
}

std::filesystem::path homeDir() {
#if defined(_WIN32)
  if (auto p = envPath("USERPROFILE")) return *p;
  if (auto p = knownFolder(FOLDERID_Profile)) return *p;
  return std::filesystem::path(L"C:\\");
#else
  if (auto p = envPath("HOME")) return *p;
  if (const passwd* pw = getpwuid(getuid()); pw != nullptr && pw->pw_dir != nullptr) {
    return std::filesystem::path(pw->pw_dir);
  }
  return std::filesystem::path("/");
#endif
}

std::filesystem::path localAppData() {
#if defined(_WIN32)
  if (auto p = knownFolder(FOLDERID_LocalAppData)) return *p;
  if (auto p = envPath("LOCALAPPDATA")) return *p;
  return homeDir() / "AppData" / "Local";
#elif defined(__APPLE__)
  return homeDir() / "Library" / "Application Support";
#else
  if (auto xdg = envPath("XDG_CONFIG_HOME")) return *xdg;
  return homeDir() / ".config";
#endif
}

std::filesystem::path roamingAppData() {
#if defined(_WIN32)
  if (auto p = knownFolder(FOLDERID_RoamingAppData)) return *p;
  if (auto p = envPath("APPDATA")) return *p;
  return homeDir() / "AppData" / "Roaming";
#else
  return localAppData();
#endif
}

std::filesystem::path tempDir() {
  std::error_code ec;
  auto p = std::filesystem::temp_directory_path(ec);
  if (ec) return dataRoot() / "tmp";
  return p;
}

}  // namespace liora::paths

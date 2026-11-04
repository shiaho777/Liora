#pragma once

#include <filesystem>

namespace liora::paths {

// Platform data directory for Liora:
//   macOS:   ~/Library/Application Support/Liora
//   Windows: %LOCALAPPDATA%\Liora
//   Linux:   $XDG_DATA_HOME/liora (default ~/.local/share/liora)
// Mobile shells must call setDataRoot() before creating the Core.
std::filesystem::path dataRoot();
void setDataRoot(const std::filesystem::path& root);

std::filesystem::path homeDir();

// Per-user application data roots other browsers use.
//   macOS:   ~/Library/Application Support
//   Windows: %LOCALAPPDATA% (Chromium family) and %APPDATA% (Firefox)
//   Linux:   ~/.config
std::filesystem::path localAppData();
std::filesystem::path roamingAppData();

std::filesystem::path tempDir();

}  // namespace liora::paths
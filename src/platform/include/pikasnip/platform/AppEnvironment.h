#pragma once

#include <filesystem>

namespace pikasnip::platform {

/// Returns true if the current process is running inside an MSIX / AppX package identity.
[[nodiscard]] bool IsPackaged() noexcept;

/// Returns the full path to the current executable.
[[nodiscard]] std::filesystem::path GetExecutablePath();

/// Returns the directory containing the current executable.
[[nodiscard]] std::filesystem::path GetExecutableDirectory();

/// Returns the configuration directory:
/// - Packaged: %LOCALAPPDATA%\OZLab\PikaSnip
/// - Unpackaged (portable): executable directory if writable, else %LOCALAPPDATA%\OZLab\PikaSnip
[[nodiscard]] std::filesystem::path GetConfigDirectory();

/// Returns the path to settings.json inside GetConfigDirectory().
[[nodiscard]] std::filesystem::path GetSettingsFilePath();

/// Returns the default screenshot save directory (Pictures\PikaSnip).
[[nodiscard]] std::filesystem::path GetDefaultSaveDirectory();

} // namespace pikasnip::platform

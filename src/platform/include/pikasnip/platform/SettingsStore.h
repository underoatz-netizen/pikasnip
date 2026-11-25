#pragma once

#include <filesystem>
#include <string>

namespace pikasnip::platform {

/// User-facing settings persisted as UTF-8 JSON.
///
/// Hotkeys use the "Mod+Mod+Key" notation parsed by HotkeyManager
/// (e.g. "PrintScreen", "Alt+A", "Ctrl+Shift+S").
struct AppSettings {
    std::string defaultHotkey = "PrintScreen";
    std::string altHotkey = "Alt+A";
    std::string ocrHotkey = "Alt+T";
    std::string pinHotkey = "Alt+P";
    int delaySeconds = 0;
    bool runAtStartup = false;
    float strokeThickness = 3.0f;
    std::string accentHex = "#38BDF8";
};

/// Loads settings.json from GetSettingsFilePath().
/// Returns defaults when the file is missing, unreadable, or malformed.
[[nodiscard]] AppSettings LoadSettings();

/// Loads settings from an explicit path (used by tests and portable overrides).
[[nodiscard]] AppSettings LoadSettings(const std::filesystem::path& path);

/// Writes settings.json to GetSettingsFilePath() with an atomic temp-file
/// replace. Returns false on I/O failure.
[[nodiscard]] bool SaveSettings(const AppSettings& settings);

/// Writes settings to an explicit path with an atomic temp-file replace.
[[nodiscard]] bool SaveSettings(const std::filesystem::path& path, const AppSettings& settings);

} // namespace pikasnip::platform

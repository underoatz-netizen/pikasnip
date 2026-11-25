#pragma once

#include "pikasnip/platform/SettingsStore.h"

#include <windows.h>

#include <string>
#include <vector>

namespace pikasnip::app {

enum HotkeyId : int {
    kHotkeyDefaultCapture = 1,
    kHotkeyAltCapture = 2,
    kHotkeyOcr = 3,
    kHotkeyPin = 4
};

struct HotkeyStatus {
    int id = 0;
    std::string label;
    bool registered = false;
    DWORD lastError = 0;
};

/// Registers global capture/tool hotkeys gracefully without failing startup on conflicts.
/// Uses a lightweight WH_KEYBOARD_LL hook for bare PrintScreen so PikaSnip reliably overrides
/// Windows 11 Snipping Tool interception.
class HotkeyManager {
public:
    ~HotkeyManager();

    void RegisterConfiguredHotkeys(HWND hwnd, const pikasnip::platform::AppSettings& settings);
    void UnregisterAll() noexcept;

    [[nodiscard]] const std::vector<HotkeyStatus>& statuses() const noexcept { return statuses_; }

private:
    static LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam);
    bool RegisterSingle(HWND hwnd, int id, const std::string& hotkeyText);
    bool InstallPrintScreenHook(int id) noexcept;

    HWND boundHwnd_ = nullptr;
    HHOOK keyboardHook_ = nullptr;
    int snapshotHookId_ = 0;
    bool snapshotSuppressed_ = false;
    std::vector<HotkeyStatus> statuses_;

    static HotkeyManager* s_hookOwner_;
};

} // namespace pikasnip::app

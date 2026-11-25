#include "HotkeyManager.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <sstream>

namespace pikasnip::app {

HotkeyManager* HotkeyManager::s_hookOwner_ = nullptr;

namespace {

std::string ToUpperAscii(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::toupper(c));
    });
    return s;
}

bool ParseHotkeyCombo(const std::string& text, UINT& outModifiers, UINT& outVk) {
    outModifiers = MOD_NOREPEAT;
    outVk = 0;

    std::stringstream ss(text);
    std::string token;
    while (std::getline(ss, token, '+')) {
        const std::string upper = ToUpperAscii(token);
        if (upper == "ALT") {
            outModifiers |= MOD_ALT;
        } else if (upper == "CTRL" || upper == "CONTROL") {
            outModifiers |= MOD_CONTROL;
        } else if (upper == "SHIFT") {
            outModifiers |= MOD_SHIFT;
        } else if (upper == "WIN" || upper == "WINDOWS") {
            outModifiers |= MOD_WIN;
        } else if (upper == "PRINTSCREEN" || upper == "PRTSC" || upper == "SNAPSHOT") {
            outVk = VK_SNAPSHOT;
        } else if (upper.size() == 1 && upper[0] >= 'A' && upper[0] <= 'Z') {
            outVk = static_cast<UINT>(upper[0]);
        } else if (upper.size() == 1 && upper[0] >= '0' && upper[0] <= '9') {
            outVk = static_cast<UINT>(upper[0]);
        } else if (upper.size() >= 2 && upper[0] == 'F') {
            const int fn = std::atoi(upper.c_str() + 1);
            if (fn >= 1 && fn <= 24) {
                outVk = static_cast<UINT>(VK_F1 + (fn - 1));
            }
        }
    }

    return outVk != 0;
}

bool AnyModifierKeyPressed() noexcept {
    return (::GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0 ||
           (::GetAsyncKeyState(VK_MENU) & 0x8000) != 0 ||
           (::GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0 ||
           (::GetAsyncKeyState(VK_LWIN) & 0x8000) != 0 ||
           (::GetAsyncKeyState(VK_RWIN) & 0x8000) != 0;
}

} // namespace

HotkeyManager::~HotkeyManager() {
    UnregisterAll();
}

LRESULT CALLBACK HotkeyManager::LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    HotkeyManager* self = s_hookOwner_;
    if (nCode == HC_ACTION && lParam != 0 && self != nullptr && self->boundHwnd_ != nullptr) {
        const auto* pkb = reinterpret_cast<const KBDLLHOOKSTRUCT*>(lParam);
        if (pkb->vkCode == VK_SNAPSHOT && (pkb->flags & LLKHF_INJECTED) == 0) {
            if (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN) {
                if (!AnyModifierKeyPressed()) {
                    if (!self->snapshotSuppressed_) {
                        self->snapshotSuppressed_ = true;
                        ::PostMessageW(
                            self->boundHwnd_,
                            WM_HOTKEY,
                            static_cast<WPARAM>(self->snapshotHookId_),
                            0);
                    }
                    return 1;
                }
            } else if (wParam == WM_KEYUP || wParam == WM_SYSKEYUP) {
                if (self->snapshotSuppressed_) {
                    self->snapshotSuppressed_ = false;
                    return 1;
                }
            }
        }
    }
    return ::CallNextHookEx(nullptr, nCode, wParam, lParam);
}

bool HotkeyManager::InstallPrintScreenHook(int id) noexcept {
    if (keyboardHook_ != nullptr) {
        return true;
    }
    snapshotHookId_ = id;
    snapshotSuppressed_ = false;
    s_hookOwner_ = this;
    keyboardHook_ = ::SetWindowsHookExW(
        WH_KEYBOARD_LL,
        &HotkeyManager::LowLevelKeyboardProc,
        ::GetModuleHandleW(nullptr),
        0);
    if (keyboardHook_ == nullptr) {
        s_hookOwner_ = nullptr;
        snapshotHookId_ = 0;
        return false;
    }
    return true;
}

void HotkeyManager::RegisterConfiguredHotkeys(
    HWND hwnd,
    const pikasnip::platform::AppSettings& settings) {
    UnregisterAll();
    if (hwnd == nullptr) {
        return;
    }
    boundHwnd_ = hwnd;

    (void)RegisterSingle(hwnd, kHotkeyDefaultCapture, settings.defaultHotkey);
    (void)RegisterSingle(hwnd, kHotkeyAltCapture, settings.altHotkey);
    (void)RegisterSingle(hwnd, kHotkeyOcr, settings.ocrHotkey);
    (void)RegisterSingle(hwnd, kHotkeyPin, settings.pinHotkey);
}

bool HotkeyManager::RegisterSingle(HWND hwnd, int id, const std::string& hotkeyText) {
    HotkeyStatus status;
    status.id = id;
    status.label = hotkeyText;

    UINT modifiers = MOD_NOREPEAT;
    UINT vk = 0;
    if (!ParseHotkeyCombo(hotkeyText, modifiers, vk)) {
        status.registered = false;
        status.lastError = ERROR_INVALID_PARAMETER;
        statuses_.push_back(std::move(status));
        return false;
    }

    const UINT coreMods = modifiers & ~static_cast<UINT>(MOD_NOREPEAT);
    if (vk == VK_SNAPSHOT && coreMods == 0 && InstallPrintScreenHook(id)) {
        status.registered = true;
        status.lastError = 0;
        statuses_.push_back(std::move(status));
        return true;
    }

    if (::RegisterHotKey(hwnd, id, modifiers, vk)) {
        status.registered = true;
        status.lastError = 0;
    } else {
        status.registered = false;
        status.lastError = ::GetLastError();
    }
    statuses_.push_back(std::move(status));
    return status.registered;
}

void HotkeyManager::UnregisterAll() noexcept {
    if (keyboardHook_ != nullptr) {
        ::UnhookWindowsHookEx(keyboardHook_);
        keyboardHook_ = nullptr;
    }
    if (s_hookOwner_ == this) {
        s_hookOwner_ = nullptr;
    }
    snapshotHookId_ = 0;
    snapshotSuppressed_ = false;

    if (boundHwnd_ != nullptr) {
        for (const auto& st : statuses_) {
            if (st.registered) {
                ::UnregisterHotKey(boundHwnd_, st.id);
            }
        }
        boundHwnd_ = nullptr;
    }
    statuses_.clear();
}

} // namespace pikasnip::app

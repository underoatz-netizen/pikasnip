#include "pikasnip/platform/StartupManager.h"
#include "pikasnip/platform/AppEnvironment.h"

#include <windows.h>
#include <unknwn.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.ApplicationModel.h>

#include <string>
#include <thread>

namespace pikasnip::platform {

namespace {

constexpr wchar_t kStartupTaskId[] = L"PikaSnipStartupTask";
constexpr wchar_t kRunKeyPath[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t kRunValueName[] = L"PikaSnip";

struct ScopedMtaApartment {
    ScopedMtaApartment() {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
    }
    ~ScopedMtaApartment() {
        winrt::uninit_apartment();
    }
    ScopedMtaApartment(const ScopedMtaApartment&) = delete;
    ScopedMtaApartment& operator=(const ScopedMtaApartment&) = delete;
};

template <typename Fn>
bool RunOnWinRtMtaThread(Fn&& fn) noexcept {
    bool result = false;
    try {
        std::thread worker([&result, fn = std::forward<Fn>(fn)]() noexcept {
            try {
                const ScopedMtaApartment mta;
                result = fn();
            } catch (...) {
                result = false;
            }
        });
        worker.join();
    } catch (...) {
        return false;
    }
    return result;
}

bool IsStartupEnabledPackaged() {
    return RunOnWinRtMtaThread([]() -> bool {
        using winrt::Windows::ApplicationModel::StartupTask;
        using winrt::Windows::ApplicationModel::StartupTaskState;
        const auto task = StartupTask::GetAsync(kStartupTaskId).get();
        const auto state = task.State();
        return state == StartupTaskState::Enabled || state == StartupTaskState::EnabledByPolicy;
    });
}

bool SetStartupEnabledPackaged(bool enable) {
    return RunOnWinRtMtaThread([enable]() -> bool {
        using winrt::Windows::ApplicationModel::StartupTask;
        using winrt::Windows::ApplicationModel::StartupTaskState;
        const auto task = StartupTask::GetAsync(kStartupTaskId).get();
        if (enable) {
            const auto newState = task.RequestEnableAsync().get();
            return newState == StartupTaskState::Enabled || newState == StartupTaskState::EnabledByPolicy;
        }
        task.Disable();
        return true;
    });
}

bool IsStartupEnabledUnpackaged() {
    DWORD type = 0;
    DWORD dataBytes = 0;
    const LSTATUS st = ::RegGetValueW(
        HKEY_CURRENT_USER,
        kRunKeyPath,
        kRunValueName,
        RRF_RT_REG_SZ,
        &type,
        nullptr,
        &dataBytes);
    return st == ERROR_SUCCESS && dataBytes > sizeof(wchar_t);
}

bool SetStartupEnabledUnpackaged(bool enable) {
    if (!enable) {
        HKEY hKey = nullptr;
        const LSTATUS openRc = ::RegOpenKeyExW(
            HKEY_CURRENT_USER,
            kRunKeyPath,
            0,
            KEY_SET_VALUE,
            &hKey);
        if (openRc == ERROR_FILE_NOT_FOUND) {
            return true;
        }
        if (openRc != ERROR_SUCCESS) {
            return false;
        }
        const LSTATUS delRc = ::RegDeleteValueW(hKey, kRunValueName);
        ::RegCloseKey(hKey);
        return delRc == ERROR_SUCCESS || delRc == ERROR_FILE_NOT_FOUND;
    }

    const std::filesystem::path exePath = GetExecutablePath();
    if (exePath.empty()) {
        return false;
    }
    const std::wstring quotedCmd = L"\"" + exePath.wstring() + L"\"";

    HKEY hKey = nullptr;
    const LSTATUS createRc = ::RegCreateKeyExW(
        HKEY_CURRENT_USER,
        kRunKeyPath,
        0,
        nullptr,
        REG_OPTION_NON_VOLATILE,
        KEY_SET_VALUE,
        nullptr,
        &hKey,
        nullptr);
    if (createRc != ERROR_SUCCESS) {
        return false;
    }

    const DWORD bytes = static_cast<DWORD>((quotedCmd.size() + 1) * sizeof(wchar_t));
    const LSTATUS setRc = ::RegSetValueExW(
        hKey,
        kRunValueName,
        0,
        REG_SZ,
        reinterpret_cast<const BYTE*>(quotedCmd.c_str()),
        bytes);
    ::RegCloseKey(hKey);
    return setRc == ERROR_SUCCESS;
}

} // namespace

bool IsStartupEnabled() {
    if (IsPackaged()) {
        return IsStartupEnabledPackaged();
    }
    return IsStartupEnabledUnpackaged();
}

bool SetStartupEnabled(bool enable) {
    if (IsPackaged()) {
        return SetStartupEnabledPackaged(enable);
    }
    return SetStartupEnabledUnpackaged(enable);
}

} // namespace pikasnip::platform

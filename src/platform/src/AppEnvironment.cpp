#include "pikasnip/platform/AppEnvironment.h"

#include <windows.h>
#include <appmodel.h>
#include <shlobj.h>

#include <string>
#include <vector>

namespace pikasnip::platform {

namespace {

std::filesystem::path GetKnownFolderPath(REFKNOWNFOLDERID folderId) {
    PWSTR rawPath = nullptr;
    const HRESULT hr = ::SHGetKnownFolderPath(folderId, KF_FLAG_CREATE, nullptr, &rawPath);
    if (SUCCEEDED(hr) && rawPath != nullptr) {
        std::filesystem::path result(rawPath);
        ::CoTaskMemFree(rawPath);
        return result;
    }
    if (rawPath != nullptr) {
        ::CoTaskMemFree(rawPath);
    }
    return {};
}

std::filesystem::path GetFallbackLocalAppData() {
    std::filesystem::path base = GetKnownFolderPath(FOLDERID_LocalAppData);
    if (base.empty()) {
        wchar_t buffer[MAX_PATH]{};
        const DWORD len = ::GetEnvironmentVariableW(L"LOCALAPPDATA", buffer, MAX_PATH);
        if (len > 0 && len < MAX_PATH) {
            base = std::filesystem::path(buffer);
        }
    }
    if (base.empty()) {
        base = GetExecutableDirectory();
    }
    return base / L"OZLab" / L"PikaSnip";
}

bool ProbeDirectoryWritable(const std::filesystem::path& dir) noexcept {
    if (dir.empty()) {
        return false;
    }
    const DWORD pid = ::GetCurrentProcessId();
    const ULONGLONG tick = ::GetTickCount64();
    const std::filesystem::path probe =
        dir / (L".pikasnip_probe_" + std::to_wstring(pid) + L"_" + std::to_wstring(tick) + L".tmp");

    HANDLE hFile = ::CreateFileW(
        probe.c_str(),
        GENERIC_WRITE,
        0,
        nullptr,
        CREATE_NEW,
        FILE_ATTRIBUTE_TEMPORARY | FILE_ATTRIBUTE_HIDDEN | FILE_FLAG_DELETE_ON_CLOSE,
        nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        return false;
    }
    ::CloseHandle(hFile);
    return true;
}

} // namespace

bool IsPackaged() noexcept {
    UINT32 length = 0;
    const LONG rc = ::GetCurrentPackageFullName(&length, nullptr);
    return rc == ERROR_INSUFFICIENT_BUFFER || rc == ERROR_SUCCESS;
}

std::filesystem::path GetExecutablePath() {
    std::vector<wchar_t> buffer(MAX_PATH, L'\0');
    for (;;) {
        const DWORD len = ::GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (len == 0) {
            return {};
        }
        if (len < buffer.size()) {
            return std::filesystem::path(buffer.data(), buffer.data() + len);
        }
        if (buffer.size() >= 32768) {
            return {};
        }
        buffer.resize(buffer.size() * 2, L'\0');
    }
}

std::filesystem::path GetExecutableDirectory() {
    const std::filesystem::path exePath = GetExecutablePath();
    if (exePath.empty()) {
        return std::filesystem::current_path();
    }
    return exePath.parent_path();
}

std::filesystem::path GetConfigDirectory() {
    if (!IsPackaged()) {
        const std::filesystem::path exeDir = GetExecutableDirectory();
        if (ProbeDirectoryWritable(exeDir)) {
            return exeDir;
        }
    }
    return GetFallbackLocalAppData();
}

std::filesystem::path GetSettingsFilePath() {
    return GetConfigDirectory() / L"settings.json";
}

std::filesystem::path GetDefaultSaveDirectory() {
    std::filesystem::path pictures = GetKnownFolderPath(FOLDERID_Pictures);
    if (pictures.empty()) {
        wchar_t buffer[MAX_PATH]{};
        const DWORD len = ::GetEnvironmentVariableW(L"USERPROFILE", buffer, MAX_PATH);
        if (len > 0 && len < MAX_PATH) {
            pictures = std::filesystem::path(buffer) / L"Pictures";
        }
    }
    if (pictures.empty()) {
        return GetConfigDirectory() / L"Captures";
    }
    return pictures / L"PikaSnip";
}

} // namespace pikasnip::platform

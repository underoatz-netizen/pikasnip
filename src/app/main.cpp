#include "TrayHostWindow.h"
#include "pikasnip/capture/CaptureEngine.h"
#include "pikasnip/capture/WindowDetector.h"
#include "pikasnip/platform/AppEnvironment.h"
#include "pikasnip/platform/ComInitializer.h"
#include "pikasnip/platform/D2dContext.h"
#include "pikasnip/platform/SettingsStore.h"

#include <windows.h>
#include <psapi.h>
#include <shellapi.h>

#include <cstdio>
#include <string>

namespace {

constexpr wchar_t kSingleInstanceMutexName[] = L"Local\\OZLab_PikaSnip_SingleInstance";

void WriteStdoutLine(const std::string& line) {
    HANDLE hOut = ::GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut == nullptr || hOut == INVALID_HANDLE_VALUE || ::GetFileType(hOut) == FILE_TYPE_UNKNOWN) {
        if (::AttachConsole(ATTACH_PARENT_PROCESS)) {
            hOut = ::GetStdHandle(STD_OUTPUT_HANDLE);
        }
    }
    if (hOut == nullptr || hOut == INVALID_HANDLE_VALUE) {
        return;
    }
    const std::string withNewline = line + "\n";
    DWORD written = 0;
    ::WriteFile(hOut, withNewline.data(), static_cast<DWORD>(withNewline.size()), &written, nullptr);
}

bool HasSmokeTestFlag() {
    int argc = 0;
    LPWSTR* argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
    if (argv == nullptr) {
        return false;
    }
    bool found = false;
    for (int i = 1; i < argc; ++i) {
        if (::lstrcmpiW(argv[i], L"--smoke-test") == 0 ||
            ::lstrcmpiW(argv[i], L"--benchmark-capture") == 0) {
            found = true;
            break;
        }
    }
    ::LocalFree(argv);
    return found;
}

int RunSmokeTest() {
    const pikasnip::platform::ComInitializer com;
    const bool packaged = pikasnip::platform::IsPackaged();
    const auto settings = pikasnip::platform::LoadSettings();
    (void)settings;

    pikasnip::capture::WindowDetector detector;
    detector.Refresh();

    const pikasnip::platform::RectI vb = pikasnip::platform::GetVirtualScreenRect();
    pikasnip::capture::CaptureEngine engine;
    (void)engine.WarmUp();
    const auto monitors = engine.EnumerateMonitors();
    pikasnip::capture::CapturedFrame frame;
    const bool ok = engine.CaptureAll(frame);

    const int32_t frameW = frame.width;
    const int32_t frameH = frame.height;
    const double captureMs = frame.captureMs;
    const bool usedDxgi = frame.usedDxgi;
    const size_t windowCount = detector.windows().size();

    // Release capture frame and DXGI staging/device state before measuring standby footprint.
    frame = {};
    engine.TrimStandbyResources();
    ::SetProcessWorkingSetSize(::GetCurrentProcess(), static_cast<SIZE_T>(-1), static_cast<SIZE_T>(-1));

    PROCESS_MEMORY_COUNTERS_EX pmc{};
    pmc.cb = sizeof(pmc);
    double ramMb = 0.0;
    if (::GetProcessMemoryInfo(::GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof(pmc))) {
        ramMb = static_cast<double>(pmc.WorkingSetSize) / (1024.0 * 1024.0);
    }

    char buf[192]{};
    std::snprintf(buf, sizeof(buf), "PACKAGED=%d", packaged ? 1 : 0);
    WriteStdoutLine(buf);
    std::snprintf(buf, sizeof(buf), "CAPTURE_MS=%.2f", captureMs);
    WriteStdoutLine(buf);
    std::snprintf(buf, sizeof(buf), "FRAME=%dx%d", frameW, frameH);
    WriteStdoutLine(buf);
    std::snprintf(buf, sizeof(buf), "VIRTUAL=%d,%d,%d,%d MONITORS=%zu", vb.left, vb.top, vb.width(), vb.height(), monitors.size());
    WriteStdoutLine(buf);
    std::snprintf(buf, sizeof(buf), "USED_DXGI=%d", usedDxgi ? 1 : 0);
    WriteStdoutLine(buf);
    std::snprintf(buf, sizeof(buf), "WINDOWS=%zu", windowCount);
    WriteStdoutLine(buf);
    std::snprintf(buf, sizeof(buf), "STANDBY_RAM_MB=%.2f", ramMb);
    WriteStdoutLine(buf);

    return ok ? 0 : 1;
}

} // namespace

int WINAPI wWinMain(
    HINSTANCE hInstance,
    HINSTANCE /*hPrevInstance*/,
    PWSTR /*pCmdLine*/,
    int /*nCmdShow*/) {
    if (HasSmokeTestFlag()) {
        return RunSmokeTest();
    }

    HANDLE singleInstance = ::CreateMutexW(nullptr, FALSE, kSingleInstanceMutexName);
    if (singleInstance != nullptr && ::GetLastError() == ERROR_ALREADY_EXISTS) {
        ::CloseHandle(singleInstance);
        return 0;
    }

    const pikasnip::platform::ComInitializer com;
    pikasnip::platform::D2dContext d2d;

    const auto settings = pikasnip::platform::LoadSettings();
    pikasnip::app::TrayHostWindow tray(hInstance, d2d, settings);
    if (!tray.Create()) {
        if (singleInstance != nullptr) {
            ::CloseHandle(singleInstance);
        }
        return 1;
    }

    MSG msg{};
    while (::GetMessageW(&msg, nullptr, 0, 0) > 0) {
        ::TranslateMessage(&msg);
        ::DispatchMessageW(&msg);
    }

    tray.Destroy();
    if (singleInstance != nullptr) {
        ::CloseHandle(singleInstance);
    }
    return static_cast<int>(msg.wParam);
}

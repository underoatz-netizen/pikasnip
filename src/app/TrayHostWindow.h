#pragma once

#include "HotkeyManager.h"
#include "pikasnip/capture/CaptureEngine.h"
#include "pikasnip/capture/WindowDetector.h"
#include "pikasnip/ocr/OcrEngine.h"
#include "pikasnip/overlay/OverlayWindow.h"
#include "pikasnip/pin/PinManager.h"
#include "pikasnip/platform/D2dContext.h"
#include "pikasnip/platform/SettingsStore.h"
#include "pikasnip/platform/ToastWindow.h"

#include <windows.h>

#include <optional>

namespace pikasnip::app {

class TrayHostWindow {
public:
    TrayHostWindow(
        HINSTANCE hInstance,
        pikasnip::platform::D2dContext& d2d,
        pikasnip::platform::AppSettings settings);
    ~TrayHostWindow();

    TrayHostWindow(const TrayHostWindow&) = delete;
    TrayHostWindow& operator=(const TrayHostWindow&) = delete;

    [[nodiscard]] bool Create();
    void Destroy() noexcept;

    [[nodiscard]] HWND hwnd() const noexcept { return hwnd_; }

private:
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    bool AddTrayIcon() noexcept;
    void RemoveTrayIcon() noexcept;
    void ShowContextMenu();
    void ScheduleCapture(int delaySeconds);
    void StartCaptureNow(pikasnip::overlay::OverlayMode mode = pikasnip::overlay::OverlayMode::Standard);
    void PinFromClipboard();
    void PinFrame(pikasnip::capture::CapturedFrame&& frame, std::optional<POINT> initialPos);
    void RunOcrOnFrame(pikasnip::capture::CapturedFrame&& frame);

    HINSTANCE hInstance_ = nullptr;
    pikasnip::platform::D2dContext& d2d_;
    pikasnip::platform::AppSettings settings_;
    HWND hwnd_ = nullptr;
    HICON trayIcon_ = nullptr;
    bool ownsTrayIcon_ = false;
    UINT taskbarCreatedMsg_ = 0;
    bool trayIconAdded_ = false;
    bool trayVersion4_ = false;

    HotkeyManager hotkeys_;
    pikasnip::capture::CaptureEngine captureEngine_;
    pikasnip::overlay::OverlayWindow overlay_;
    pikasnip::pin::PinManager pinManager_;
    pikasnip::ocr::OcrEngine ocrEngine_;
    pikasnip::platform::ToastWindow toast_;
};

} // namespace pikasnip::app

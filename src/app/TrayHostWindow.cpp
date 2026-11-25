#include "TrayHostWindow.h"
#include "pikasnip/platform/Clipboard.h"
#include "pikasnip/platform/StartupManager.h"

#include <shellapi.h>

#include <utility>

namespace pikasnip::app {

namespace {

constexpr wchar_t kTrayHostClassName[] = L"OZLab_PikaSnip_TrayHost";
constexpr UINT kTrayCallbackMsg = WM_APP + 101;
constexpr UINT kPinCleanupMsg = WM_APP + 105;
constexpr UINT kTrayIconId = 1;
constexpr UINT_PTR kDelayCaptureTimerId = 10;
constexpr int kAppIconResourceId = 101;

enum MenuCommandId : UINT {
    IDM_CAPTURE_NOW = 1001,
    IDM_DELAY_3S = 1002,
    IDM_DELAY_5S = 1003,
    IDM_RUN_AT_STARTUP = 1004,
    IDM_EXIT = 1005,
    IDM_QUICK_OCR = 1006,
    IDM_PIN_CLIPBOARD = 1007,
    IDM_CLOSE_ALL_PINS = 1008
};

bool RegisterTrayHostClass(HINSTANCE hInstance, WNDPROC wndProc) noexcept {
    WNDCLASSEXW wc{};
    if (::GetClassInfoExW(hInstance, kTrayHostClassName, &wc)) {
        return true;
    }

    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = wndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = kTrayHostClassName;
    return ::RegisterClassExW(&wc) != 0;
}

} // namespace

LRESULT CALLBACK TrayHostWindow::WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_NCCREATE) {
        const auto* cs = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        auto* self = static_cast<TrayHostWindow*>(cs->lpCreateParams);
        if (self != nullptr) {
            self->hwnd_ = hwnd;
            ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
    }
    auto* self = reinterpret_cast<TrayHostWindow*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (self != nullptr) {
        return self->HandleMessage(hwnd, msg, wParam, lParam);
    }
    return ::DefWindowProcW(hwnd, msg, wParam, lParam);
}

TrayHostWindow::TrayHostWindow(
    HINSTANCE hInstance,
    pikasnip::platform::D2dContext& d2d,
    pikasnip::platform::AppSettings settings)
    : hInstance_(hInstance),
      d2d_(d2d),
      settings_(std::move(settings)),
      overlay_(d2d),
      pinManager_(d2d),
      toast_(d2d) {
    overlay_.SetPinCallback(
        [this](pikasnip::capture::CapturedFrame&& frame, pikasnip::platform::RectI region) {
            PinFrame(std::move(frame), POINT{region.left, region.top});
        });
    overlay_.SetOcrCallback(
        [this](pikasnip::capture::CapturedFrame&& frame) {
            RunOcrOnFrame(std::move(frame));
        });
}

TrayHostWindow::~TrayHostWindow() {
    Destroy();
}

bool TrayHostWindow::Create() {
    if (hwnd_ != nullptr) {
        return true;
    }
    if (!RegisterTrayHostClass(hInstance_, &TrayHostWindow::WindowProc)) {
        return false;
    }

    taskbarCreatedMsg_ = ::RegisterWindowMessageW(L"TaskbarCreated");

    trayIcon_ = static_cast<HICON>(::LoadImageW(
        hInstance_,
        MAKEINTRESOURCEW(kAppIconResourceId),
        IMAGE_ICON,
        ::GetSystemMetrics(SM_CXSMICON),
        ::GetSystemMetrics(SM_CYSMICON),
        LR_DEFAULTCOLOR));
    ownsTrayIcon_ = (trayIcon_ != nullptr);
    if (trayIcon_ == nullptr) {
        trayIcon_ = ::LoadIconW(nullptr, IDI_APPLICATION);
    }

    hwnd_ = ::CreateWindowExW(
        WS_EX_TOOLWINDOW,
        kTrayHostClassName,
        L"PikaSnip",
        WS_POPUP,
        0,
        0,
        0,
        0,
        nullptr,
        nullptr,
        hInstance_,
        this);
    if (hwnd_ == nullptr) {
        return false;
    }

    (void)AddTrayIcon();
    hotkeys_.RegisterConfiguredHotkeys(hwnd_, settings_);
    return true;
}

void TrayHostWindow::Destroy() noexcept {
    overlay_.Close(false);
    toast_.Close();
    pinManager_.CloseAll();
    hotkeys_.UnregisterAll();
    RemoveTrayIcon();

    if (hwnd_ != nullptr) {
        ::KillTimer(hwnd_, kDelayCaptureTimerId);
        ::DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }

    if (trayIcon_ != nullptr) {
        if (ownsTrayIcon_) {
            ::DestroyIcon(trayIcon_);
        }
        trayIcon_ = nullptr;
        ownsTrayIcon_ = false;
    }
}

bool TrayHostWindow::AddTrayIcon() noexcept {
    if (hwnd_ == nullptr) {
        return false;
    }
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = hwnd_;
    nid.uID = kTrayIconId;
    nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    nid.uCallbackMessage = kTrayCallbackMsg;
    nid.hIcon = trayIcon_;
    ::lstrcpynW(nid.szTip, L"PikaSnip (PrtSc / Alt+A / Alt+T / Alt+P)", static_cast<int>(std::size(nid.szTip)));

    trayIconAdded_ = ::Shell_NotifyIconW(NIM_ADD, &nid) != FALSE;
    trayVersion4_ = false;
    if (trayIconAdded_) {
        nid.uVersion = NOTIFYICON_VERSION_4;
        trayVersion4_ = ::Shell_NotifyIconW(NIM_SETVERSION, &nid) != FALSE;
    }
    return trayIconAdded_;
}

void TrayHostWindow::RemoveTrayIcon() noexcept {
    if (!trayIconAdded_ || hwnd_ == nullptr) {
        return;
    }
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = hwnd_;
    nid.uID = kTrayIconId;
    ::Shell_NotifyIconW(NIM_DELETE, &nid);
    trayIconAdded_ = false;
}

void TrayHostWindow::ShowContextMenu() {
    HMENU menu = ::CreatePopupMenu();
    if (menu == nullptr) {
        return;
    }

    const bool startupEnabled = pikasnip::platform::IsStartupEnabled();

    ::AppendMenuW(menu, MF_STRING, IDM_CAPTURE_NOW, L"Capture Now (PrtSc / Alt+A)");
    ::AppendMenuW(menu, MF_STRING, IDM_QUICK_OCR, L"Quick OCR (Alt+T)");
    ::AppendMenuW(menu, MF_STRING, IDM_PIN_CLIPBOARD, L"Pin Clipboard (Alt+P)");
    ::AppendMenuW(menu, MF_STRING, IDM_DELAY_3S, L"Delayed Capture (3s)");
    ::AppendMenuW(menu, MF_STRING, IDM_DELAY_5S, L"Delayed Capture (5s)");
    if (pinManager_.count() > 0U) {
        ::AppendMenuW(menu, MF_STRING, IDM_CLOSE_ALL_PINS, L"Close All Pins");
    }
    ::AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    ::AppendMenuW(
        menu,
        MF_STRING | (startupEnabled ? MF_CHECKED : MF_UNCHECKED),
        IDM_RUN_AT_STARTUP,
        L"Run at Startup");
    ::AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    ::AppendMenuW(menu, MF_STRING, IDM_EXIT, L"Exit");

    POINT pt{};
    ::GetCursorPos(&pt);
    ::SetForegroundWindow(hwnd_);
    ::TrackPopupMenu(
        menu,
        TPM_RIGHTBUTTON | TPM_BOTTOMALIGN,
        pt.x,
        pt.y,
        0,
        hwnd_,
        nullptr);
    ::PostMessageW(hwnd_, WM_NULL, 0, 0);
    ::DestroyMenu(menu);
}

void TrayHostWindow::ScheduleCapture(int delaySeconds) {
    if (hwnd_ == nullptr) {
        return;
    }
    ::KillTimer(hwnd_, kDelayCaptureTimerId);
    if (delaySeconds <= 0) {
        StartCaptureNow();
        return;
    }
    ::SetTimer(hwnd_, kDelayCaptureTimerId, static_cast<UINT>(delaySeconds * 1000), nullptr);
}

void TrayHostWindow::StartCaptureNow(pikasnip::overlay::OverlayMode mode) {
    if (overlay_.isOpen()) {
        return;
    }

    pikasnip::capture::WindowDetector detector;
    detector.Refresh(hwnd_);

    pikasnip::capture::CapturedFrame frame;
    if (!captureEngine_.CaptureAll(frame)) {
        return;
    }

    (void)overlay_.Show(
        hInstance_,
        std::move(frame),
        std::move(detector),
        [this](bool /*copied*/) {
            // Release cached DXGI/D3D11 resources and trim working set so standby RAM stays < 15 MB.
            captureEngine_.TrimStandbyResources();
            if (pinManager_.count() == 0U) {
                ::SetProcessWorkingSetSize(
                    ::GetCurrentProcess(),
                    static_cast<SIZE_T>(-1),
                    static_cast<SIZE_T>(-1));
            }
        },
        mode);
}

void TrayHostWindow::PinFromClipboard() {
    if (overlay_.isOpen()) {
        return;
    }
    if (!d2d_.isInitialized()) {
        (void)d2d_.Initialize();
    }

    pikasnip::platform::ClipboardImage clipImg;
    if (!pikasnip::platform::ReadImageFromClipboard(hwnd_, d2d_.wicFactory(), clipImg) ||
        !clipImg.isValid()) {
        (void)toast_.Show(hInstance_, L"No image in clipboard", true);
        return;
    }

    pikasnip::capture::CapturedFrame frame;
    frame.virtualBounds = pikasnip::platform::RectI{0, 0, clipImg.width, clipImg.height};
    frame.width = clipImg.width;
    frame.height = clipImg.height;
    frame.strideBytes = clipImg.strideBytes;
    frame.bgraPixels = std::move(clipImg.bgraPixels);
    PinFrame(std::move(frame), std::nullopt);
}

void TrayHostWindow::PinFrame(
    pikasnip::capture::CapturedFrame&& frame,
    std::optional<POINT> initialPos) {
    pikasnip::pin::PinOptions options;
    options.alwaysOnTop = true;
    options.initialPosition = initialPos;

    const bool ok = pinManager_.CreatePin(
        hInstance_,
        hwnd_,
        kPinCleanupMsg,
        frame,
        options,
        [this](const std::wstring& message, bool isError) {
            (void)toast_.Show(hInstance_, message, isError);
        },
        [this](pikasnip::capture::CapturedFrame&& ocrFrame) {
            RunOcrOnFrame(std::move(ocrFrame));
        });
    if (ok) {
        (void)toast_.Show(hInstance_, L"Pinned (Right-click / E: Edit, Middle-click / Esc: Close)", false);
    } else {
        (void)toast_.Show(hInstance_, L"Failed to create pin window", true);
    }
}

void TrayHostWindow::RunOcrOnFrame(pikasnip::capture::CapturedFrame&& frame) {
    const pikasnip::ocr::OcrResult res = ocrEngine_.RecognizeFrame(frame);
    if (!res.succeeded) {
        (void)toast_.Show(
            hInstance_,
            res.errorMessage.empty() ? L"OCR failed" : res.errorMessage,
            true);
    } else if (res.text.empty()) {
        (void)toast_.Show(hInstance_, L"No text detected", false);
    } else if (pikasnip::platform::CopyUnicodeTextToClipboard(hwnd_, res.text)) {
        (void)toast_.Show(
            hInstance_,
            L"Copied OCR text (" + std::to_wstring(res.text.size()) + L" chars)",
            false);
    } else {
        (void)toast_.Show(hInstance_, L"Failed to copy OCR text", true);
    }

    captureEngine_.TrimStandbyResources();
    if (pinManager_.count() == 0U) {
        ::SetProcessWorkingSetSize(
            ::GetCurrentProcess(),
            static_cast<SIZE_T>(-1),
            static_cast<SIZE_T>(-1));
    }
}

LRESULT TrayHostWindow::HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (taskbarCreatedMsg_ != 0 && msg == taskbarCreatedMsg_) {
        (void)AddTrayIcon();
        return 0;
    }

    if (msg == kPinCleanupMsg) {
        pinManager_.PruneClosed();
        if (pinManager_.count() == 0U) {
            ::SetProcessWorkingSetSize(
                ::GetCurrentProcess(),
                static_cast<SIZE_T>(-1),
                static_cast<SIZE_T>(-1));
        }
        return 0;
    }

    switch (msg) {
        case kTrayCallbackMsg: {
            const UINT eventMsg = LOWORD(lParam);
            if (trayVersion4_) {
                if (eventMsg == NIN_SELECT || eventMsg == NIN_KEYSELECT) {
                    StartCaptureNow();
                    return 0;
                }
                if (eventMsg == WM_CONTEXTMENU) {
                    ShowContextMenu();
                    return 0;
                }
            } else {
                if (eventMsg == WM_LBUTTONUP) {
                    StartCaptureNow();
                    return 0;
                }
                if (eventMsg == WM_RBUTTONUP) {
                    ShowContextMenu();
                    return 0;
                }
            }
            break;
        }

        case WM_HOTKEY: {
            const int id = static_cast<int>(wParam);
            if (id == kHotkeyDefaultCapture || id == kHotkeyAltCapture) {
                ScheduleCapture(settings_.delaySeconds);
            } else if (id == kHotkeyOcr) {
                StartCaptureNow(pikasnip::overlay::OverlayMode::QuickOcr);
            } else if (id == kHotkeyPin) {
                PinFromClipboard();
            }
            return 0;
        }

        case WM_TIMER: {
            if (wParam == kDelayCaptureTimerId) {
                ::KillTimer(hwnd, kDelayCaptureTimerId);
                StartCaptureNow();
                return 0;
            }
            break;
        }

        case WM_COMMAND: {
            switch (LOWORD(wParam)) {
                case IDM_CAPTURE_NOW:
                    StartCaptureNow();
                    return 0;
                case IDM_QUICK_OCR:
                    StartCaptureNow(pikasnip::overlay::OverlayMode::QuickOcr);
                    return 0;
                case IDM_PIN_CLIPBOARD:
                    PinFromClipboard();
                    return 0;
                case IDM_DELAY_3S:
                    ScheduleCapture(3);
                    return 0;
                case IDM_DELAY_5S:
                    ScheduleCapture(5);
                    return 0;
                case IDM_CLOSE_ALL_PINS:
                    pinManager_.CloseAll();
                    ::SetProcessWorkingSetSize(
                        ::GetCurrentProcess(),
                        static_cast<SIZE_T>(-1),
                        static_cast<SIZE_T>(-1));
                    return 0;
                case IDM_RUN_AT_STARTUP: {
                    const bool current = pikasnip::platform::IsStartupEnabled();
                    if (pikasnip::platform::SetStartupEnabled(!current)) {
                        settings_.runAtStartup = !current;
                        (void)pikasnip::platform::SaveSettings(settings_);
                    }
                    return 0;
                }
                case IDM_EXIT:
                    ::PostQuitMessage(0);
                    return 0;
                default:
                    break;
            }
            break;
        }

        case WM_DESTROY:
            hotkeys_.UnregisterAll();
            RemoveTrayIcon();
            break;

        case WM_NCDESTROY:
            ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            hwnd_ = nullptr;
            break;

        default:
            break;
    }

    return ::DefWindowProcW(hwnd, msg, wParam, lParam);
}

} // namespace pikasnip::app

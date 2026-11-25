#include "PinToolbarWindow.h"

#include <shellscalingapi.h>
#include <windowsx.h>

#include <algorithm>
#include <utility>

namespace pikasnip::pin {

namespace {

constexpr wchar_t kPinToolbarClassName[] = L"OZLab_PikaSnip_PinToolbarWindow";

bool RegisterPinToolbarClass(HINSTANCE hInstance, WNDPROC wndProc) noexcept {
    WNDCLASSEXW wc{};
    if (::GetClassInfoExW(hInstance, kPinToolbarClassName, &wc)) {
        return true;
    }

    wc.cbSize = sizeof(wc);
    wc.style = 0;
    wc.lpfnWndProc = wndProc;
    wc.hInstance = hInstance;
    wc.hCursor = ::LoadCursorW(nullptr, IDC_HAND);
    wc.lpszClassName = kPinToolbarClassName;
    return ::RegisterClassExW(&wc) != 0;
}

void QueryMonitorWorkAndDpi(
    pikasnip::platform::RectI targetRect,
    HWND fallbackHwnd,
    pikasnip::platform::RectI& outWorkArea,
    UINT& outDpi) noexcept {
    RECT winRect = targetRect.toWin32();
    HMONITOR monitor = ::MonitorFromRect(&winRect, MONITOR_DEFAULTTONEAREST);

    outWorkArea = pikasnip::platform::GetVirtualScreenRect();
    outDpi = (fallbackHwnd != nullptr) ? pikasnip::platform::GetWindowDpi(fallbackHwnd) : 96U;

    if (monitor != nullptr) {
        MONITORINFO mi{};
        mi.cbSize = sizeof(mi);
        if (::GetMonitorInfoW(monitor, &mi)) {
            outWorkArea = pikasnip::platform::RectI::fromWin32(mi.rcWork);
        }
        UINT dpiX = 96U;
        UINT dpiY = 96U;
        if (SUCCEEDED(::GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY)) && dpiX != 0U) {
            outDpi = dpiX;
        }
    }

    if (outWorkArea.isEmpty()) {
        outWorkArea = pikasnip::platform::RectI{
            0, 0, ::GetSystemMetrics(SM_CXSCREEN), ::GetSystemMetrics(SM_CYSCREEN)};
    }
    if (outDpi == 0U) {
        outDpi = 96U;
    }
}

} // namespace

LRESULT CALLBACK PinToolbarWindow::WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_NCCREATE) {
        const auto* cs = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        auto* self = static_cast<PinToolbarWindow*>(cs->lpCreateParams);
        if (self != nullptr) {
            self->hwnd_ = hwnd;
            ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
    }
    auto* self = reinterpret_cast<PinToolbarWindow*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (self != nullptr) {
        return self->HandleMessage(hwnd, msg, wParam, lParam);
    }
    return ::DefWindowProcW(hwnd, msg, wParam, lParam);
}

PinToolbarWindow::PinToolbarWindow(pikasnip::platform::D2dContext& d2d) noexcept
    : d2d_(d2d) {}

PinToolbarWindow::~PinToolbarWindow() {
    Hide();
}

bool PinToolbarWindow::ShowForPin(
    HINSTANCE hInstance,
    HWND ownerPinHwnd,
    pikasnip::platform::RectI pinRect,
    ActionCallback onAction,
    pikasnip::annotations::AnnotationTool activeTool,
    const pikasnip::annotations::AnnotationStyle& activeStyle,
    bool canUndo,
    bool canRedo) {
    if (!d2d_.isInitialized() && FAILED(d2d_.Initialize())) {
        return false;
    }
    if (!RegisterPinToolbarClass(hInstance, &PinToolbarWindow::WindowProc)) {
        return false;
    }

    ownerPinHwnd_ = ownerPinHwnd;
    onAction_ = std::move(onAction);
    activeTool_ = activeTool;
    activeStyle_ = activeStyle;
    canUndo_ = canUndo;
    canRedo_ = canRedo;

    pikasnip::platform::RectI monitorWork{};
    UINT dpi = 96U;
    QueryMonitorWorkAndDpi(pinRect, ownerPinHwnd_, monitorWork, dpi);
    toolbar_.ComputeLayout(pinRect, monitorWork, dpi, false);
    const pikasnip::platform::RectI bounds = toolbar_.bounds();
    if (bounds.isEmpty()) {
        return false;
    }

    if (hwnd_ == nullptr) {
        trackingMouseLeave_ = false;
        isPressed_ = false;
        pressedAction_ = pikasnip::overlay::ToolbarActionKind::None;
        closing_ = false;

        const HWND created = ::CreateWindowExW(
            WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
            kPinToolbarClassName,
            L"PikaSnip Pin Toolbar",
            WS_POPUP,
            bounds.left,
            bounds.top,
            bounds.width(),
            bounds.height(),
            ownerPinHwnd_,
            nullptr,
            hInstance,
            this);
        if (created == nullptr || hwnd_ == nullptr) {
            return false;
        }
    } else {
        ::SetWindowPos(
            hwnd_,
            HWND_TOPMOST,
            bounds.left,
            bounds.top,
            bounds.width(),
            bounds.height(),
            SWP_NOACTIVATE);
    }

    if (!EnsureRenderResources()) {
        Hide();
        return false;
    }

    if (renderTarget_) {
        const HRESULT hr = renderTarget_->Resize(D2D1::SizeU(
            static_cast<UINT32>(std::max(1, bounds.width())),
            static_cast<UINT32>(std::max(1, bounds.height()))));
        if (FAILED(hr)) {
            ReleaseRenderResources();
        }
    }

    ::SetWindowPos(
        hwnd_,
        HWND_TOPMOST,
        bounds.left,
        bounds.top,
        bounds.width(),
        bounds.height(),
        SWP_NOACTIVATE | SWP_SHOWWINDOW);
    ::InvalidateRect(hwnd_, nullptr, FALSE);
    ::UpdateWindow(hwnd_);
    return true;
}

void PinToolbarWindow::SyncPosition(pikasnip::platform::RectI pinRect) {
    if (hwnd_ == nullptr) {
        return;
    }

    pikasnip::platform::RectI monitorWork{};
    UINT dpi = 96U;
    QueryMonitorWorkAndDpi(pinRect, ownerPinHwnd_, monitorWork, dpi);
    toolbar_.ComputeLayout(pinRect, monitorWork, dpi, false);
    const pikasnip::platform::RectI bounds = toolbar_.bounds();
    if (bounds.isEmpty()) {
        return;
    }

    ::SetWindowPos(
        hwnd_,
        HWND_TOPMOST,
        bounds.left,
        bounds.top,
        bounds.width(),
        bounds.height(),
        SWP_NOACTIVATE);

    if (renderTarget_) {
        const HRESULT hr = renderTarget_->Resize(D2D1::SizeU(
            static_cast<UINT32>(std::max(1, bounds.width())),
            static_cast<UINT32>(std::max(1, bounds.height()))));
        if (FAILED(hr)) {
            ReleaseRenderResources();
        }
    }
    ::InvalidateRect(hwnd_, nullptr, FALSE);
}

void PinToolbarWindow::SyncState(
    pikasnip::annotations::AnnotationTool activeTool,
    const pikasnip::annotations::AnnotationStyle& activeStyle,
    bool canUndo,
    bool canRedo) noexcept {
    activeTool_ = activeTool;
    activeStyle_ = activeStyle;
    canUndo_ = canUndo;
    canRedo_ = canRedo;
    if (hwnd_ != nullptr) {
        ::InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

void PinToolbarWindow::Hide() noexcept {
    if (closing_) {
        return;
    }
    closing_ = true;

    if (hwnd_ != nullptr) {
        const HWND hwnd = hwnd_;
        if (::GetCapture() == hwnd) {
            ::ReleaseCapture();
        }
        ::DestroyWindow(hwnd);
        hwnd_ = nullptr;
    } else {
        ReleaseRenderResources();
    }

    toolbar_.Reset();
    onAction_ = nullptr;
    ownerPinHwnd_ = nullptr;
    closing_ = false;
}

bool PinToolbarWindow::EnsureRenderResources() {
    if (hwnd_ == nullptr || d2d_.d2dFactory() == nullptr) {
        return false;
    }

    if (!renderTarget_) {
        RECT client{};
        ::GetClientRect(hwnd_, &client);
        const UINT32 w = static_cast<UINT32>(std::max<LONG>(1L, client.right - client.left));
        const UINT32 h = static_cast<UINT32>(std::max<LONG>(1L, client.bottom - client.top));

        const D2D1_RENDER_TARGET_PROPERTIES rtProps = D2D1::RenderTargetProperties(
            D2D1_RENDER_TARGET_TYPE_DEFAULT,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE),
            96.0f,
            96.0f);
        const D2D1_HWND_RENDER_TARGET_PROPERTIES hwndProps = D2D1::HwndRenderTargetProperties(
            hwnd_,
            D2D1::SizeU(w, h));
        const HRESULT hr = d2d_.d2dFactory()->CreateHwndRenderTarget(
            rtProps,
            hwndProps,
            renderTarget_.ReleaseAndGetAddressOf());
        if (FAILED(hr) || !renderTarget_) {
            return false;
        }
    }
    return true;
}

void PinToolbarWindow::ReleaseRenderResources() noexcept {
    renderTarget_.Reset();
}

void PinToolbarWindow::Render() {
    if (!EnsureRenderResources()) {
        return;
    }

    const pikasnip::platform::RectI bounds = toolbar_.bounds();
    renderTarget_->BeginDraw();
    renderTarget_->Clear(D2D1::ColorF(0x1E1E1E));
    if (!bounds.isEmpty()) {
        toolbar_.Draw(
            renderTarget_.Get(),
            d2d_.dwriteFactory(),
            static_cast<float>(-bounds.left),
            static_cast<float>(-bounds.top),
            activeTool_,
            activeStyle_,
            canUndo_,
            canRedo_);
    }

    const HRESULT hr = renderTarget_->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) {
        ReleaseRenderResources();
    }
}

LRESULT PinToolbarWindow::HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_ERASEBKGND:
            return 1;

        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;

        case WM_PAINT: {
            PAINTSTRUCT ps{};
            ::BeginPaint(hwnd, &ps);
            Render();
            ::EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_MOUSEMOVE: {
            if (!trackingMouseLeave_) {
                TRACKMOUSEEVENT tme{};
                tme.cbSize = sizeof(tme);
                tme.dwFlags = TME_LEAVE;
                tme.hwndTrack = hwnd;
                if (::TrackMouseEvent(&tme)) {
                    trackingMouseLeave_ = true;
                }
            }
            const pikasnip::platform::RectI bounds = toolbar_.bounds();
            const int32_t vx = bounds.left + GET_X_LPARAM(lParam);
            const int32_t vy = bounds.top + GET_Y_LPARAM(lParam);
            toolbar_.SetHoveredPoint(vx, vy);
            ::InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }

        case WM_MOUSELEAVE: {
            trackingMouseLeave_ = false;
            toolbar_.SetHoveredPoint(-1000000, -1000000);
            ::InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }

        case WM_LBUTTONDOWN: {
            const pikasnip::platform::RectI bounds = toolbar_.bounds();
            const int32_t vx = bounds.left + GET_X_LPARAM(lParam);
            const int32_t vy = bounds.top + GET_Y_LPARAM(lParam);
            isPressed_ = true;
            pressedAction_ = toolbar_.HitTest(vx, vy).kind;
            ::SetCapture(hwnd);
            ::InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }

        case WM_LBUTTONUP: {
            if (isPressed_) {
                const pikasnip::overlay::ToolbarActionKind pressed = pressedAction_;
                isPressed_ = false;
                pressedAction_ = pikasnip::overlay::ToolbarActionKind::None;
                if (::GetCapture() == hwnd) {
                    ::ReleaseCapture();
                }

                const pikasnip::platform::RectI bounds = toolbar_.bounds();
                const int32_t vx = bounds.left + GET_X_LPARAM(lParam);
                const int32_t vy = bounds.top + GET_Y_LPARAM(lParam);
                const pikasnip::overlay::ToolbarHit hit = toolbar_.HitTest(vx, vy);
                if (hit.kind != pikasnip::overlay::ToolbarActionKind::None &&
                    hit.kind == pressed &&
                    onAction_) {
                    ActionCallback cb = onAction_;
                    cb(hit);
                }
            }
            return 0;
        }

        case WM_CAPTURECHANGED: {
            isPressed_ = false;
            pressedAction_ = pikasnip::overlay::ToolbarActionKind::None;
            return 0;
        }

        case WM_NCDESTROY: {
            hwnd_ = nullptr;
            trackingMouseLeave_ = false;
            isPressed_ = false;
            pressedAction_ = pikasnip::overlay::ToolbarActionKind::None;
            ReleaseRenderResources();
            ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            return ::DefWindowProcW(hwnd, msg, wParam, lParam);
        }

        default:
            break;
    }

    return ::DefWindowProcW(hwnd, msg, wParam, lParam);
}

} // namespace pikasnip::pin

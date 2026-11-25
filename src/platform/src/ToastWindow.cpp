#include "pikasnip/platform/ToastWindow.h"
#include "pikasnip/platform/Dpi.h"

#include <shellscalingapi.h>

#include <algorithm>
#include <cmath>

namespace pikasnip::platform {

namespace {

constexpr wchar_t kToastClassName[] = L"OZLab_PikaSnip_ToastWindow";
constexpr UINT_PTR kDismissTimerId = 1;
constexpr UINT kMinDurationMs = 300;

D2D1_COLOR_F HexColor(uint32_t rgb, float alpha = 1.0f) noexcept {
    return D2D1::ColorF(
        static_cast<float>((rgb >> 16) & 0xFFU) / 255.0f,
        static_cast<float>((rgb >> 8) & 0xFFU) / 255.0f,
        static_cast<float>(rgb & 0xFFU) / 255.0f,
        alpha);
}

bool RegisterToastClass(HINSTANCE hInstance, WNDPROC wndProc) noexcept {
    WNDCLASSEXW wc{};
    if (::GetClassInfoExW(hInstance, kToastClassName, &wc)) {
        return true;
    }

    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = wndProc;
    wc.hInstance = hInstance;
    wc.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kToastClassName;
    return ::RegisterClassExW(&wc) != 0;
}

UINT QueryMonitorDpi(HMONITOR monitor) noexcept {
    if (monitor != nullptr) {
        UINT dpiX = 96;
        UINT dpiY = 96;
        if (SUCCEEDED(::GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY)) && dpiX != 0) {
            return dpiX;
        }
    }
    const UINT sysDpi = ::GetDpiForSystem();
    return sysDpi != 0 ? sysDpi : 96U;
}

} // namespace

LRESULT CALLBACK ToastWindow::WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_NCCREATE) {
        const auto* cs = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        auto* self = static_cast<ToastWindow*>(cs->lpCreateParams);
        if (self != nullptr) {
            self->hwnd_ = hwnd;
            ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
    }
    auto* self = reinterpret_cast<ToastWindow*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (self != nullptr) {
        return self->HandleMessage(hwnd, msg, wParam, lParam);
    }
    return ::DefWindowProcW(hwnd, msg, wParam, lParam);
}

ToastWindow::ToastWindow(D2dContext& d2d) noexcept
    : d2d_(d2d) {}

ToastWindow::~ToastWindow() {
    Close();
}

bool ToastWindow::Show(
    HINSTANCE hInstance,
    std::wstring_view message,
    bool isError,
    UINT durationMs) {
    if (message.empty()) {
        return false;
    }
    if (!d2d_.isInitialized() && FAILED(d2d_.Initialize())) {
        return false;
    }
    if (!RegisterToastClass(hInstance, &ToastWindow::WindowProc)) {
        return false;
    }

    POINT cursorPt{};
    if (!::GetCursorPos(&cursorPt)) {
        cursorPt = {0, 0};
    }

    HMONITOR monitor = ::MonitorFromPoint(cursorPt, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    RectI workArea = GetVirtualScreenRect();
    if (monitor != nullptr && ::GetMonitorInfoW(monitor, &mi)) {
        workArea = RectI::fromWin32(mi.rcWork);
    }
    if (workArea.isEmpty()) {
        workArea = RectI{0, 0, ::GetSystemMetrics(SM_CXSCREEN), ::GetSystemMetrics(SM_CYSCREEN)};
    }

    const UINT monitorDpi = QueryMonitorDpi(monitor);
    const int32_t margin = ScaleForDpi(16, monitorDpi);
    const float fontSize = ScaleForDpi(13.0f, monitorDpi);
    const float padX = ScaleForDpi(16.0f, monitorDpi);
    const float padY = ScaleForDpi(10.0f, monitorDpi);

    const float maxToastWidth = static_cast<float>(
        std::max<int32_t>(ScaleForDpi(160, monitorDpi), workArea.width() - margin * 2));
    const float maxLayoutWidth = std::min(ScaleForDpi(440.0f, monitorDpi), maxToastWidth) - padX * 2.0f;
    const float maxLayoutHeight = std::max(
        ScaleForDpi(40.0f, monitorDpi),
        static_cast<float>(workArea.height() - margin * 2) - padY * 2.0f);

    if (!textFormat_ || dpi_ != monitorDpi) {
        textFormat_.Reset();
        const HRESULT hr = d2d_.dwriteFactory()->CreateTextFormat(
            L"Segoe UI",
            nullptr,
            DWRITE_FONT_WEIGHT_SEMI_BOLD,
            DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL,
            fontSize,
            L"en-US",
            textFormat_.ReleaseAndGetAddressOf());
        if (FAILED(hr) || !textFormat_) {
            return false;
        }
        (void)textFormat_->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
    }

    Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
    HRESULT hr = d2d_.dwriteFactory()->CreateTextLayout(
        message.data(),
        static_cast<UINT32>(message.size()),
        textFormat_.Get(),
        std::max(1.0f, maxLayoutWidth),
        std::max(1.0f, maxLayoutHeight),
        layout.ReleaseAndGetAddressOf());
    if (FAILED(hr) || !layout) {
        return false;
    }

    DWRITE_TEXT_METRICS metrics{};
    hr = layout->GetMetrics(&metrics);
    if (FAILED(hr)) {
        return false;
    }

    const int32_t minW = ScaleForDpi(120, monitorDpi);
    const int32_t minH = ScaleForDpi(34, monitorDpi);
    const int32_t toastH = std::max(minH, static_cast<int32_t>(std::ceil(metrics.height + padY * 2.0f)));
    const float radius = static_cast<float>(toastH) * 0.5f;
    const float effectivePadX = std::max(padX, radius * 0.85f);
    const int32_t toastW = std::max(
        minW,
        static_cast<int32_t>(std::ceil(metrics.widthIncludingTrailingWhitespace + effectivePadX * 2.0f)));

    const int32_t toastX = std::max(workArea.left + margin, workArea.right - margin - toastW);
    const int32_t toastY = std::max(workArea.top + margin, workArea.bottom - margin - toastH);

    message_.assign(message);
    isError_ = isError;
    dpi_ = monitorDpi;
    width_ = toastW;
    height_ = toastH;
    padX_ = effectivePadX;
    padY_ = padY;
    textLayout_ = std::move(layout);

    if (hwnd_ == nullptr) {
        hwnd_ = ::CreateWindowExW(
            WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT,
            kToastClassName,
            L"",
            WS_POPUP,
            toastX,
            toastY,
            toastW,
            toastH,
            nullptr,
            nullptr,
            hInstance,
            this);
        if (hwnd_ == nullptr) {
            ReleaseRenderResources();
            return false;
        }
    }

    if (!EnsureRenderResources(toastW, toastH)) {
        Close();
        return false;
    }

    ::SetWindowPos(
        hwnd_,
        HWND_TOPMOST,
        toastX,
        toastY,
        toastW,
        toastH,
        SWP_NOACTIVATE | SWP_SHOWWINDOW);

    const int diameter = std::max(2, height_);
    HRGN rgn = ::CreateRoundRectRgn(0, 0, width_ + 1, height_ + 1, diameter, diameter);
    if (rgn != nullptr && ::SetWindowRgn(hwnd_, rgn, TRUE) == 0) {
        ::DeleteObject(rgn);
    }

    const UINT clampedDuration = std::max(durationMs, kMinDurationMs);
    ::SetTimer(hwnd_, kDismissTimerId, clampedDuration, nullptr);

    ::InvalidateRect(hwnd_, nullptr, FALSE);
    ::UpdateWindow(hwnd_);
    return true;
}

void ToastWindow::Close() noexcept {
    if (closing_) {
        return;
    }
    closing_ = true;

    if (hwnd_ != nullptr) {
        ::KillTimer(hwnd_, kDismissTimerId);
        ::DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }

    ReleaseRenderResources();
    message_.clear();
    isError_ = false;
    closing_ = false;
}

bool ToastWindow::EnsureRenderResources(int32_t width, int32_t height) {
    if (hwnd_ == nullptr || d2d_.d2dFactory() == nullptr || width <= 0 || height <= 0) {
        return false;
    }

    const D2D1_SIZE_U targetSize = D2D1::SizeU(static_cast<UINT32>(width), static_cast<UINT32>(height));
    if (!renderTarget_) {
        const D2D1_RENDER_TARGET_PROPERTIES rtProps = D2D1::RenderTargetProperties(
            D2D1_RENDER_TARGET_TYPE_DEFAULT,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE),
            96.0f,
            96.0f);
        const D2D1_HWND_RENDER_TARGET_PROPERTIES hwndProps = D2D1::HwndRenderTargetProperties(
            hwnd_,
            targetSize);
        const HRESULT hr = d2d_.d2dFactory()->CreateHwndRenderTarget(
            rtProps,
            hwndProps,
            renderTarget_.ReleaseAndGetAddressOf());
        if (FAILED(hr) || !renderTarget_) {
            return false;
        }
    } else {
        const D2D1_SIZE_U currentSize = renderTarget_->GetPixelSize();
        if (currentSize.width != targetSize.width || currentSize.height != targetSize.height) {
            const HRESULT hr = renderTarget_->Resize(targetSize);
            if (FAILED(hr)) {
                renderTarget_.Reset();
                brush_.Reset();
                return EnsureRenderResources(width, height);
            }
        }
    }

    if (!brush_) {
        const HRESULT hr = renderTarget_->CreateSolidColorBrush(
            HexColor(0xF9FAFB),
            brush_.ReleaseAndGetAddressOf());
        if (FAILED(hr) || !brush_) {
            return false;
        }
    }

    return true;
}

void ToastWindow::ReleaseRenderResources() noexcept {
    textLayout_.Reset();
    textFormat_.Reset();
    brush_.Reset();
    renderTarget_.Reset();
}

void ToastWindow::Render() {
    if (!EnsureRenderResources(width_, height_) || !textLayout_) {
        return;
    }

    renderTarget_->BeginDraw();
    renderTarget_->Clear(HexColor(0x1E1E1E, 1.0f));

    const float radius = static_cast<float>(height_) * 0.5f;
    const D2D1_SIZE_F rtSize = renderTarget_->GetSize();

    brush_->SetColor(HexColor(0x1E1E1E, 1.0f));
    renderTarget_->FillRoundedRectangle(
        D2D1::RoundedRect(D2D1::RectF(0.0f, 0.0f, rtSize.width, rtSize.height), radius, radius),
        brush_.Get());

    const D2D1_RECT_F borderRect = D2D1::RectF(
        0.625f,
        0.625f,
        std::max(1.0f, rtSize.width - 0.625f),
        std::max(1.0f, rtSize.height - 0.625f));
    const float borderRadius = std::max(1.0f, radius - 0.625f);

    brush_->SetColor(isError_ ? HexColor(0xEF4444, 1.0f) : HexColor(0x38BDF8, 1.0f));
    renderTarget_->DrawRoundedRectangle(
        D2D1::RoundedRect(borderRect, borderRadius, borderRadius),
        brush_.Get(),
        1.25f);

    brush_->SetColor(isError_ ? HexColor(0xFCA5A5, 1.0f) : HexColor(0xF9FAFB, 1.0f));
    renderTarget_->DrawTextLayout(
        D2D1::Point2F(padX_, padY_),
        textLayout_.Get(),
        brush_.Get(),
        D2D1_DRAW_TEXT_OPTIONS_CLIP);

    const HRESULT hr = renderTarget_->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) {
        brush_.Reset();
        renderTarget_.Reset();
    }
}

LRESULT ToastWindow::HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
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

        case WM_TIMER: {
            if (wParam == kDismissTimerId) {
                Close();
                return 0;
            }
            break;
        }

        case WM_NCDESTROY: {
            ::KillTimer(hwnd, kDismissTimerId);
            hwnd_ = nullptr;
            ReleaseRenderResources();
            ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            return ::DefWindowProcW(hwnd, msg, wParam, lParam);
        }

        default:
            break;
    }

    return ::DefWindowProcW(hwnd, msg, wParam, lParam);
}

} // namespace pikasnip::platform

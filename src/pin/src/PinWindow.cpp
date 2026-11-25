#include "pikasnip/pin/PinWindow.h"

#include "PinToolbarWindow.h"
#include "pikasnip/annotations/AnnotationRenderer.h"
#include "pikasnip/overlay/FloatingToolbar.h"
#include "pikasnip/platform/Clipboard.h"

#include <windowsx.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <type_traits>
#include <utility>
#include <vector>

namespace pikasnip::pin {

namespace {

constexpr wchar_t kPinClassName[] = L"OZLab_PikaSnip_PinWindow";
constexpr float kMinScale = 0.25f;
constexpr float kMaxScale = 4.00f;
constexpr float kMinOpacity = 0.20f;
constexpr float kMaxOpacity = 1.00f;
constexpr int32_t kMinWindowDim = 24;
constexpr int32_t kMinVisibleVirtualPx = 32;
constexpr UINT_PTR kHudTimerId = 1;
constexpr UINT kHudDurationMs = 900;

constexpr UINT kIdmPinEdit = 2001;
constexpr UINT kIdmPinOcr = 2002;
constexpr UINT kIdmPinCopy = 2003;
constexpr UINT kIdmPinSave = 2004;
constexpr UINT kIdmPinReset = 2005;
constexpr UINT kIdmPinClose = 2006;

D2D1_COLOR_F HexColor(uint32_t rgb, float alpha = 1.0f) noexcept {
    return D2D1::ColorF(
        static_cast<float>((rgb >> 16) & 0xFFU) / 255.0f,
        static_cast<float>((rgb >> 8) & 0xFFU) / 255.0f,
        static_cast<float>(rgb & 0xFFU) / 255.0f,
        alpha);
}

bool RegisterPinClass(HINSTANCE hInstance, WNDPROC wndProc) noexcept {
    WNDCLASSEXW wc{};
    if (::GetClassInfoExW(hInstance, kPinClassName, &wc)) {
        return true;
    }

    wc.cbSize = sizeof(wc);
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = wndProc;
    wc.hInstance = hInstance;
    wc.hCursor = ::LoadCursorW(nullptr, IDC_SIZEALL);
    wc.lpszClassName = kPinClassName;
    return ::RegisterClassExW(&wc) != 0;
}

POINT ClampTopLeftToVirtualScreen(int32_t x, int32_t y, int32_t winW, int32_t winH) noexcept {
    const pikasnip::platform::RectI vb = pikasnip::platform::GetVirtualScreenRect();
    if (vb.isEmpty()) {
        return {x, y};
    }

    const int32_t visibleX = std::min(kMinVisibleVirtualPx, winW);
    const int32_t visibleY = std::min(kMinVisibleVirtualPx, winH);
    const int32_t minX = vb.left - (winW - visibleX);
    const int32_t maxX = vb.right - visibleX;
    const int32_t minY = vb.top - (winH - visibleY);
    const int32_t maxY = vb.bottom - visibleY;

    return {
        std::clamp(x, minX, std::max(minX, maxX)),
        std::clamp(y, minY, std::max(minY, maxY))
    };
}

} // namespace

LRESULT CALLBACK PinWindow::WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_NCCREATE) {
        const auto* cs = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        auto* self = static_cast<PinWindow*>(cs->lpCreateParams);
        if (self != nullptr) {
            self->hwnd_ = hwnd;
            ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
    }
    auto* self = reinterpret_cast<PinWindow*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (self != nullptr) {
        return self->HandleMessage(hwnd, msg, wParam, lParam);
    }
    return ::DefWindowProcW(hwnd, msg, wParam, lParam);
}

PinWindow::PinWindow(pikasnip::platform::D2dContext& d2d) noexcept
    : d2d_(d2d) {}

PinWindow::~PinWindow() {
    Close();
}

bool PinWindow::Create(
    HINSTANCE hInstance,
    const pikasnip::capture::CapturedFrame& image,
    const PinOptions& options,
    ClosedCallback onClosed,
    FeedbackCallback onFeedback,
    OcrCallback onOcr) {
    if (!image.isValid()) {
        return false;
    }
    if (!d2d_.isInitialized() && FAILED(d2d_.Initialize())) {
        return false;
    }
    if (!RegisterPinClass(hInstance, &PinWindow::WindowProc)) {
        return false;
    }

    if (hwnd_ != nullptr) {
        Close();
    }

    hInstance_ = hInstance;
    image_ = image;
    image_.virtualBounds = pikasnip::platform::RectI{0, 0, image_.width, image_.height};
    options_ = options;
    onClosed_ = std::move(onClosed);
    onFeedback_ = std::move(onFeedback);
    onOcr_ = std::move(onOcr);

    editMode_ = false;
    activeTool_ = pikasnip::annotations::AnnotationTool::None;
    style_ = pikasnip::annotations::AnnotationStyle{};
    annotations_.Reset();
    draftAnnotation_.reset();
    activeText_.reset();
    isAuthoring_ = false;

    scale_ = std::clamp(options_.initialScale, kMinScale, kMaxScale);
    opacity_ = std::clamp(options_.initialOpacity, kMinOpacity, kMaxOpacity);
    dragging_ = false;
    rightPressed_ = false;
    middlePressed_ = false;
    isHovered_ = false;
    trackingMouseLeave_ = false;
    closing_ = false;
    hudText_.clear();
    showHud_ = false;

    int32_t winW = kMinWindowDim;
    int32_t winH = kMinWindowDim;
    int32_t posX = 0;
    int32_t posY = 0;

    if (options_.initialPosition.has_value()) {
        winW = std::max(kMinWindowDim, static_cast<int32_t>(std::lround(static_cast<double>(image_.width) * scale_)));
        winH = std::max(kMinWindowDim, static_cast<int32_t>(std::lround(static_cast<double>(image_.height) * scale_)));
        const POINT clamped = ClampTopLeftToVirtualScreen(
            options_.initialPosition->x,
            options_.initialPosition->y,
            winW,
            winH);
        posX = clamped.x;
        posY = clamped.y;
    } else {
        POINT cursorPt{};
        if (!::GetCursorPos(&cursorPt)) {
            cursorPt = {0, 0};
        }
        HMONITOR monitor = ::MonitorFromPoint(cursorPt, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi{};
        mi.cbSize = sizeof(mi);
        pikasnip::platform::RectI workArea = pikasnip::platform::GetVirtualScreenRect();
        if (monitor != nullptr && ::GetMonitorInfoW(monitor, &mi)) {
            workArea = pikasnip::platform::RectI::fromWin32(mi.rcWork);
        }
        if (workArea.isEmpty()) {
            workArea = pikasnip::platform::RectI{
                0, 0, ::GetSystemMetrics(SM_CXSCREEN), ::GetSystemMetrics(SM_CYSCREEN)};
        }

        const double maxW = static_cast<double>(workArea.width()) * 0.85;
        const double maxH = static_cast<double>(workArea.height()) * 0.85;
        const double scaledW = static_cast<double>(image_.width) * scale_;
        const double scaledH = static_cast<double>(image_.height) * scale_;

        if (maxW > 0.0 && maxH > 0.0 && (scaledW > maxW || scaledH > maxH)) {
            const float fitScale = static_cast<float>(std::min(
                maxW / static_cast<double>(image_.width),
                maxH / static_cast<double>(image_.height)));
            scale_ = std::clamp(std::min(scale_, fitScale), kMinScale, kMaxScale);
        }

        winW = std::max(kMinWindowDim, static_cast<int32_t>(std::lround(static_cast<double>(image_.width) * scale_)));
        winH = std::max(kMinWindowDim, static_cast<int32_t>(std::lround(static_cast<double>(image_.height) * scale_)));

        const POINT centered = ClampTopLeftToVirtualScreen(
            workArea.left + (workArea.width() - winW) / 2,
            workArea.top + (workArea.height() - winH) / 2,
            winW,
            winH);
        posX = centered.x;
        posY = centered.y;
    }

    const DWORD exStyle =
        WS_EX_TOOLWINDOW |
        WS_EX_LAYERED |
        (options_.alwaysOnTop ? WS_EX_TOPMOST : 0UL);

    hwnd_ = ::CreateWindowExW(
        exStyle,
        kPinClassName,
        L"PikaSnip Pin",
        WS_POPUP,
        posX,
        posY,
        winW,
        winH,
        nullptr,
        nullptr,
        hInstance,
        this);
    if (hwnd_ == nullptr) {
        std::vector<uint8_t>{}.swap(image_.bgraPixels);
        image_ = {};
        onClosed_ = nullptr;
        onFeedback_ = nullptr;
        onOcr_ = nullptr;
        return false;
    }

    const BYTE alphaByte = static_cast<BYTE>(
        std::clamp<int>(static_cast<int>(std::lround(opacity_ * 255.0f)), 51, 255));
    (void)::SetLayeredWindowAttributes(hwnd_, 0, alphaByte, LWA_ALPHA);

    if (!EnsureRenderResources()) {
        onClosed_ = nullptr;
        onFeedback_ = nullptr;
        onOcr_ = nullptr;
        Close();
        return false;
    }

    ::SetWindowPos(
        hwnd_,
        options_.alwaysOnTop ? HWND_TOPMOST : HWND_TOP,
        posX,
        posY,
        winW,
        winH,
        SWP_SHOWWINDOW);
    ::InvalidateRect(hwnd_, nullptr, FALSE);
    ::UpdateWindow(hwnd_);
    return true;
}

void PinWindow::Close() noexcept {
    if (closing_) {
        return;
    }
    closing_ = true;

    editMode_ = false;
    activeTool_ = pikasnip::annotations::AnnotationTool::None;
    isAuthoring_ = false;
    draftAnnotation_.reset();
    activeText_.reset();
    annotations_.Reset();
    if (toolbarWindow_) {
        toolbarWindow_->Hide();
    }

    if (hwnd_ != nullptr) {
        const HWND hwnd = hwnd_;
        ::KillTimer(hwnd, kHudTimerId);
        if (::GetCapture() == hwnd) {
            ::ReleaseCapture();
        }
        ::DestroyWindow(hwnd);
        hwnd_ = nullptr;
    } else {
        ReleaseRenderResources();
        std::vector<uint8_t>{}.swap(image_.bgraPixels);
        image_ = {};
    }

    closing_ = false;
}

bool PinWindow::SetScale(float scale, std::optional<POINT> anchorScreenPt) noexcept {
    if (hwnd_ == nullptr || !image_.isValid()) {
        return false;
    }

    const float clampedScale = std::clamp(scale, kMinScale, kMaxScale);
    const int32_t newW = std::max(
        kMinWindowDim,
        static_cast<int32_t>(std::lround(static_cast<double>(image_.width) * clampedScale)));
    const int32_t newH = std::max(
        kMinWindowDim,
        static_cast<int32_t>(std::lround(static_cast<double>(image_.height) * clampedScale)));

    RECT rc{};
    if (!::GetWindowRect(hwnd_, &rc)) {
        return false;
    }

    const int32_t oldW = rc.right - rc.left;
    const int32_t oldH = rc.bottom - rc.top;

    int32_t targetX = rc.left;
    int32_t targetY = rc.top;
    if (anchorScreenPt.has_value() && oldW > 0 && oldH > 0) {
        const double relX = static_cast<double>(anchorScreenPt->x - rc.left) / static_cast<double>(oldW);
        const double relY = static_cast<double>(anchorScreenPt->y - rc.top) / static_cast<double>(oldH);
        targetX = static_cast<int32_t>(std::lround(static_cast<double>(anchorScreenPt->x) - relX * newW));
        targetY = static_cast<int32_t>(std::lround(static_cast<double>(anchorScreenPt->y) - relY * newH));
    } else {
        const int32_t centerX = rc.left + oldW / 2;
        const int32_t centerY = rc.top + oldH / 2;
        targetX = centerX - newW / 2;
        targetY = centerY - newH / 2;
    }

    const POINT clampedPos = ClampTopLeftToVirtualScreen(targetX, targetY, newW, newH);
    scale_ = clampedScale;

    ::SetWindowPos(
        hwnd_,
        nullptr,
        clampedPos.x,
        clampedPos.y,
        newW,
        newH,
        SWP_NOZORDER | SWP_NOACTIVATE);

    if (renderTarget_) {
        const HRESULT hr = renderTarget_->Resize(
            D2D1::SizeU(static_cast<UINT32>(newW), static_cast<UINT32>(newH)));
        if (FAILED(hr)) {
            ReleaseRenderResources();
        }
    }

    if (editMode_ && toolbarWindow_) {
        toolbarWindow_->SyncPosition(windowRect());
    }

    wchar_t buf[32]{};
    std::swprintf(buf, std::size(buf), L"%d%%", static_cast<int>(std::lround(scale_ * 100.0f)));
    ShowHudBadge(buf);
    ::InvalidateRect(hwnd_, nullptr, FALSE);
    return true;
}

bool PinWindow::SetOpacity(float opacity) noexcept {
    if (hwnd_ == nullptr) {
        return false;
    }

    const float clampedOpacity = std::clamp(opacity, kMinOpacity, kMaxOpacity);
    const BYTE alphaByte = static_cast<BYTE>(
        std::clamp<int>(static_cast<int>(std::lround(clampedOpacity * 255.0f)), 51, 255));
    if (!::SetLayeredWindowAttributes(hwnd_, 0, alphaByte, LWA_ALPHA)) {
        return false;
    }

    opacity_ = clampedOpacity;

    wchar_t buf[32]{};
    std::swprintf(buf, std::size(buf), L"Opacity %d%%", static_cast<int>(std::lround(opacity_ * 100.0f)));
    ShowHudBadge(buf);
    ::InvalidateRect(hwnd_, nullptr, FALSE);
    return true;
}

void PinWindow::SetEditMode(bool enabled) {
    if (hwnd_ == nullptr) {
        editMode_ = false;
        return;
    }

    if (enabled) {
        editMode_ = true;
        if (!toolbarWindow_) {
            toolbarWindow_ = std::make_unique<PinToolbarWindow>(d2d_);
        }
        (void)toolbarWindow_->ShowForPin(
            hInstance_,
            hwnd_,
            windowRect(),
            [this](const pikasnip::overlay::ToolbarHit& hit) {
                ExecuteToolbarAction(hit);
            },
            activeTool_,
            style_,
            annotations_.CanUndo(),
            annotations_.CanRedo());
        ::InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }

    CommitActiveTextIfAny();
    isAuthoring_ = false;
    draftAnnotation_.reset();
    activeTool_ = pikasnip::annotations::AnnotationTool::None;
    editMode_ = false;
    if (toolbarWindow_) {
        toolbarWindow_->Hide();
    }
    ::InvalidateRect(hwnd_, nullptr, FALSE);
}

HWND PinWindow::toolbarHwnd() const noexcept {
    return (toolbarWindow_ != nullptr) ? toolbarWindow_->hwnd() : nullptr;
}

pikasnip::platform::RectI PinWindow::windowRect() const noexcept {
    if (hwnd_ == nullptr) {
        return {};
    }
    RECT rc{};
    if (!::GetWindowRect(hwnd_, &rc)) {
        return {};
    }
    return pikasnip::platform::RectI::fromWin32(rc);
}

bool PinWindow::BuildFlattenedFrame(pikasnip::capture::CapturedFrame& outFrame) const {
    if (!image_.isValid()) {
        return false;
    }
    if (annotations_.empty()) {
        outFrame = image_;
        return true;
    }
    return pikasnip::annotations::AnnotationRenderer::FlattenRegion(
        d2d_,
        image_,
        pikasnip::platform::RectI{0, 0, image_.width, image_.height},
        annotations_,
        outFrame);
}

bool PinWindow::EnsureRenderResources() {
    if (hwnd_ == nullptr || !image_.isValid() || d2d_.d2dFactory() == nullptr) {
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

    if (!bitmap_) {
        const D2D1_BITMAP_PROPERTIES bmpProps = D2D1::BitmapProperties(
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE),
            96.0f,
            96.0f);
        const HRESULT hr = renderTarget_->CreateBitmap(
            D2D1::SizeU(static_cast<UINT32>(image_.width), static_cast<UINT32>(image_.height)),
            image_.bgraPixels.data(),
            static_cast<UINT32>(image_.strideBytes),
            &bmpProps,
            bitmap_.ReleaseAndGetAddressOf());
        if (FAILED(hr) || !bitmap_) {
            return false;
        }
    }

    if (!brush_) {
        const HRESULT hr = renderTarget_->CreateSolidColorBrush(
            HexColor(0x38BDF8),
            brush_.ReleaseAndGetAddressOf());
        if (FAILED(hr) || !brush_) {
            return false;
        }
    }

    if (!hudFormat_ && d2d_.dwriteFactory() != nullptr) {
        (void)d2d_.dwriteFactory()->CreateTextFormat(
            L"Segoe UI",
            nullptr,
            DWRITE_FONT_WEIGHT_SEMI_BOLD,
            DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL,
            12.0f,
            L"en-US",
            hudFormat_.ReleaseAndGetAddressOf());
        if (hudFormat_) {
            (void)hudFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            (void)hudFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }
    }

    return true;
}

void PinWindow::ReleaseRenderResources() noexcept {
    hudFormat_.Reset();
    brush_.Reset();
    bitmap_.Reset();
    renderTarget_.Reset();
}

void PinWindow::Render() {
    if (!EnsureRenderResources()) {
        return;
    }

    renderTarget_->BeginDraw();
    const D2D1_SIZE_F rtSize = renderTarget_->GetSize();

    renderTarget_->DrawBitmap(
        bitmap_.Get(),
        D2D1::RectF(0.0f, 0.0f, rtSize.width, rtSize.height),
        1.0f,
        D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);

    if (!annotations_.empty() || draftAnnotation_.has_value() || activeText_.has_value()) {
        const float sx = rtSize.width / static_cast<float>(std::max(1, image_.width));
        const float sy = rtSize.height / static_cast<float>(std::max(1, image_.height));
        renderTarget_->SetTransform(D2D1::Matrix3x2F::Scale(sx, sy));
        pikasnip::annotations::AnnotationRenderer::RenderVectorAnnotations(
            renderTarget_.Get(),
            d2d_.d2dFactory(),
            d2d_.dwriteFactory(),
            &image_,
            annotations_.items(),
            draftAnnotation_.has_value() ? &*draftAnnotation_ : nullptr,
            0.0f,
            0.0f,
            activeText_.has_value() ? &*activeText_ : nullptr);
        renderTarget_->SetTransform(D2D1::Matrix3x2F::Identity());
    }

    const bool activeBorder =
        isHovered_ || editMode_ || (::GetFocus() == hwnd_) || (::GetActiveWindow() == hwnd_);
    brush_->SetColor(activeBorder ? HexColor(0x38BDF8, 1.0f) : HexColor(0x3A3A3A, 1.0f));
    renderTarget_->DrawRectangle(
        D2D1::RectF(
            0.5f,
            0.5f,
            std::max(1.0f, rtSize.width - 0.5f),
            std::max(1.0f, rtSize.height - 0.5f)),
        brush_.Get(),
        1.0f);

    if (showHud_ && !hudText_.empty() && hudFormat_ &&
        rtSize.width >= 64.0f && rtSize.height >= 32.0f) {
        const float badgeW = std::min(rtSize.width - 12.0f, std::max(54.0f, static_cast<float>(hudText_.size()) * 8.5f + 16.0f));
        const float badgeH = 22.0f;
        const D2D1_RECT_F badgeRect = D2D1::RectF(6.0f, 6.0f, 6.0f + badgeW, 6.0f + badgeH);
        const D2D1_ROUNDED_RECT rounded = D2D1::RoundedRect(badgeRect, 4.0f, 4.0f);

        brush_->SetColor(HexColor(0x1E1E1E, 0.88f));
        renderTarget_->FillRoundedRectangle(rounded, brush_.Get());
        brush_->SetColor(HexColor(0x38BDF8, 0.9f));
        renderTarget_->DrawRoundedRectangle(rounded, brush_.Get(), 1.0f);
        brush_->SetColor(HexColor(0xF9FAFB, 1.0f));
        renderTarget_->DrawText(
            hudText_.c_str(),
            static_cast<UINT32>(hudText_.size()),
            hudFormat_.Get(),
            badgeRect,
            brush_.Get());
    }

    const HRESULT hr = renderTarget_->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) {
        ReleaseRenderResources();
    }
}

void PinWindow::CopyToClipboard(HWND hwnd) {
    CommitActiveTextIfAny();
    SyncToolbarState();
    pikasnip::capture::CapturedFrame flat;
    if (!BuildFlattenedFrame(flat) || !flat.isValid()) {
        return;
    }
    const bool ok = pikasnip::platform::CopyImageToClipboard(
        hwnd,
        d2d_.wicFactory(),
        flat.bgraPixels.data(),
        flat.width,
        flat.height,
        flat.strideBytes);
    if (onFeedback_) {
        onFeedback_(ok ? L"Copied pinned image" : L"Failed to copy pinned image", !ok);
    }
}

void PinWindow::SaveToDefaultDir() {
    CommitActiveTextIfAny();
    SyncToolbarState();
    pikasnip::capture::CapturedFrame flat;
    if (!BuildFlattenedFrame(flat) || !flat.isValid()) {
        return;
    }
    const std::filesystem::path savedPath = pikasnip::platform::SaveTimestampedPngToDefaultDir(
        d2d_.wicFactory(),
        flat.bgraPixels.data(),
        flat.width,
        flat.height,
        flat.strideBytes);
    if (onFeedback_) {
        if (!savedPath.empty()) {
            onFeedback_(L"Saved " + savedPath.filename().wstring(), false);
        } else {
            onFeedback_(L"Failed to save pinned image", true);
        }
    }
}

void PinWindow::RequestOcr() {
    CommitActiveTextIfAny();
    SyncToolbarState();
    pikasnip::capture::CapturedFrame flat;
    if (!BuildFlattenedFrame(flat) || !flat.isValid()) {
        return;
    }
    if (onOcr_) {
        onOcr_(std::move(flat));
    } else if (onFeedback_) {
        onFeedback_(L"OCR unavailable", true);
    }
}

void PinWindow::ShowContextMenu(POINT screenPt) {
    if (hwnd_ == nullptr) {
        return;
    }
    HMENU menu = ::CreatePopupMenu();
    if (menu == nullptr) {
        return;
    }

    const UINT editFlags = MF_STRING | (editMode_ ? MF_CHECKED : MF_UNCHECKED);
    (void)::AppendMenuW(menu, editFlags, kIdmPinEdit, L"Edit / Annotate (E / Space)");
    (void)::AppendMenuW(menu, MF_STRING, kIdmPinOcr, L"Extract Text / OCR (Ctrl+T)");
    (void)::AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    (void)::AppendMenuW(menu, MF_STRING, kIdmPinCopy, L"Copy Image (Ctrl+C)");
    (void)::AppendMenuW(menu, MF_STRING, kIdmPinSave, L"Save PNG... (Ctrl+S)");
    (void)::AppendMenuW(menu, MF_STRING, kIdmPinReset, L"Reset Zoom && Opacity (0)");
    (void)::AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    (void)::AppendMenuW(menu, MF_STRING, kIdmPinClose, L"Close Pin (Esc / Middle-Click)");

    const HWND localHwnd = hwnd_;
    ::SetForegroundWindow(localHwnd);
    const BOOL cmd = ::TrackPopupMenuEx(
        menu,
        TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY,
        screenPt.x,
        screenPt.y,
        localHwnd,
        nullptr);
    ::DestroyMenu(menu);

    if (cmd != 0 && ::IsWindow(localHwnd) != FALSE) {
        ::SendMessageW(localHwnd, WM_COMMAND, MAKEWPARAM(static_cast<WORD>(cmd), 0), 0);
    }
}

void PinWindow::ExecuteToolbarAction(const pikasnip::overlay::ToolbarHit& hit) {
    if (hit.tool != pikasnip::annotations::AnnotationTool::None) {
        CommitActiveTextIfAny();
        isAuthoring_ = false;
        draftAnnotation_.reset();
        activeTool_ = (activeTool_ == hit.tool)
            ? pikasnip::annotations::AnnotationTool::None
            : hit.tool;
        SyncToolbarState();
        if (hwnd_ != nullptr) {
            ::InvalidateRect(hwnd_, nullptr, FALSE);
        }
        return;
    }

    if (hit.isColor) {
        style_.color = hit.color;
        if (activeText_.has_value()) {
            activeText_->color = style_.color;
        }
        SyncToolbarState();
        if (hwnd_ != nullptr) {
            ::InvalidateRect(hwnd_, nullptr, FALSE);
        }
        return;
    }

    if (hit.isThickness) {
        style_.thickness = hit.thickness;
        SyncToolbarState();
        return;
    }

    switch (hit.kind) {
        case pikasnip::overlay::ToolbarActionKind::Undo:
            CommitActiveTextIfAny();
            if (annotations_.Undo()) {
                SyncToolbarState();
                if (hwnd_ != nullptr) {
                    ::InvalidateRect(hwnd_, nullptr, FALSE);
                }
            }
            break;
        case pikasnip::overlay::ToolbarActionKind::Redo:
            CommitActiveTextIfAny();
            if (annotations_.Redo()) {
                SyncToolbarState();
                if (hwnd_ != nullptr) {
                    ::InvalidateRect(hwnd_, nullptr, FALSE);
                }
            }
            break;
        case pikasnip::overlay::ToolbarActionKind::Ocr:
            RequestOcr();
            break;
        case pikasnip::overlay::ToolbarActionKind::Save:
            SaveToDefaultDir();
            break;
        case pikasnip::overlay::ToolbarActionKind::Copy:
            CopyToClipboard(hwnd_);
            break;
        case pikasnip::overlay::ToolbarActionKind::Cancel:
            SetEditMode(false);
            break;
        default:
            break;
    }
}

void PinWindow::SyncToolbarState() noexcept {
    if (toolbarWindow_ != nullptr && toolbarWindow_->isOpen()) {
        toolbarWindow_->SyncState(
            activeTool_,
            style_,
            annotations_.CanUndo(),
            annotations_.CanRedo());
    }
}

void PinWindow::CommitActiveTextIfAny() {
    if (!activeText_.has_value()) {
        return;
    }
    if (!activeText_->text.empty()) {
        annotations_.Add(*activeText_);
    }
    activeText_.reset();
}

pikasnip::annotations::PointF PinWindow::ClientPointToImage(int32_t cx, int32_t cy) const noexcept {
    if (hwnd_ == nullptr || !image_.isValid()) {
        return {};
    }
    RECT client{};
    ::GetClientRect(hwnd_, &client);
    const int32_t clientW = std::max<int32_t>(1, client.right - client.left);
    const int32_t clientH = std::max<int32_t>(1, client.bottom - client.top);

    const float ix = std::clamp(
        static_cast<float>(cx) * static_cast<float>(image_.width) / static_cast<float>(clientW),
        0.0f,
        static_cast<float>(image_.width));
    const float iy = std::clamp(
        static_cast<float>(cy) * static_cast<float>(image_.height) / static_cast<float>(clientH),
        0.0f,
        static_cast<float>(image_.height));
    return {ix, iy};
}

void PinWindow::ShowHudBadge(std::wstring text) {
    if (hwnd_ == nullptr) {
        return;
    }
    hudText_ = std::move(text);
    showHud_ = true;
    ::SetTimer(hwnd_, kHudTimerId, kHudDurationMs, nullptr);
}

LRESULT PinWindow::HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_ERASEBKGND:
            return 1;

        case WM_PAINT: {
            PAINTSTRUCT ps{};
            ::BeginPaint(hwnd, &ps);
            Render();
            ::EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_SETCURSOR: {
            if (LOWORD(lParam) == HTCLIENT) {
                LPCWSTR cursorId = IDC_SIZEALL;
                if (editMode_ && activeTool_ == pikasnip::annotations::AnnotationTool::Text) {
                    cursorId = IDC_IBEAM;
                } else if (editMode_ && activeTool_ != pikasnip::annotations::AnnotationTool::None) {
                    cursorId = IDC_CROSS;
                }
                ::SetCursor(::LoadCursorW(nullptr, cursorId));
                return TRUE;
            }
            break;
        }

        case WM_SETFOCUS:
        case WM_KILLFOCUS: {
            ::InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }

        case WM_LBUTTONDOWN: {
            ::SetFocus(hwnd);
            if (editMode_ && activeTool_ != pikasnip::annotations::AnnotationTool::None) {
                CommitActiveTextIfAny();
                const pikasnip::annotations::PointF pt =
                    ClientPointToImage(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));

                if (activeTool_ == pikasnip::annotations::AnnotationTool::StepCounter) {
                    pikasnip::annotations::StepCounterAnnotation step;
                    step.center = pt;
                    step.number = annotations_.NextStepNumber();
                    step.fill = style_.color;
                    annotations_.Add(std::move(step));
                    SyncToolbarState();
                    ::InvalidateRect(hwnd, nullptr, FALSE);
                    return 0;
                }

                if (activeTool_ == pikasnip::annotations::AnnotationTool::Text) {
                    pikasnip::annotations::TextAnnotation txt;
                    txt.origin = pt;
                    txt.color = style_.color;
                    txt.fontSize = style_.fontSize;
                    activeText_ = std::move(txt);
                    SyncToolbarState();
                    ::InvalidateRect(hwnd, nullptr, FALSE);
                    return 0;
                }

                isAuthoring_ = true;
                switch (activeTool_) {
                    case pikasnip::annotations::AnnotationTool::Rectangle: {
                        pikasnip::annotations::RectAnnotation r;
                        r.bounds = {pt.x, pt.y, pt.x, pt.y};
                        r.stroke = style_.color;
                        r.thickness = style_.thickness;
                        draftAnnotation_ = std::move(r);
                        break;
                    }
                    case pikasnip::annotations::AnnotationTool::Ellipse: {
                        pikasnip::annotations::EllipseAnnotation e;
                        e.bounds = {pt.x, pt.y, pt.x, pt.y};
                        e.stroke = style_.color;
                        e.thickness = style_.thickness;
                        draftAnnotation_ = std::move(e);
                        break;
                    }
                    case pikasnip::annotations::AnnotationTool::Arrow: {
                        pikasnip::annotations::ArrowAnnotation a;
                        a.start = pt;
                        a.end = pt;
                        a.stroke = style_.color;
                        a.thickness = style_.thickness;
                        draftAnnotation_ = std::move(a);
                        break;
                    }
                    case pikasnip::annotations::AnnotationTool::FreehandPen: {
                        pikasnip::annotations::FreehandPenAnnotation p;
                        p.points.push_back(pt);
                        p.stroke = style_.color;
                        p.thickness = style_.thickness;
                        draftAnnotation_ = std::move(p);
                        break;
                    }
                    case pikasnip::annotations::AnnotationTool::Highlighter: {
                        pikasnip::annotations::HighlighterAnnotation h;
                        h.points.push_back(pt);
                        h.color = {style_.color.r, style_.color.g, style_.color.b, 110};
                        h.thickness = std::max(12.0f, style_.thickness * 4.0f);
                        draftAnnotation_ = std::move(h);
                        break;
                    }
                    case pikasnip::annotations::AnnotationTool::Mosaic: {
                        pikasnip::annotations::MosaicAnnotation m;
                        m.bounds = {pt.x, pt.y, pt.x, pt.y};
                        m.blockSize = style_.mosaicBlockSize;
                        draftAnnotation_ = std::move(m);
                        break;
                    }
                    default:
                        isAuthoring_ = false;
                        break;
                }

                if (isAuthoring_) {
                    ::SetCapture(hwnd);
                    ::InvalidateRect(hwnd, nullptr, FALSE);
                    return 0;
                }
            }

            if (activeText_.has_value()) {
                CommitActiveTextIfAny();
                SyncToolbarState();
                ::InvalidateRect(hwnd, nullptr, FALSE);
            }

            POINT cursor{};
            RECT rc{};
            if (::GetCursorPos(&cursor) && ::GetWindowRect(hwnd, &rc)) {
                dragging_ = true;
                dragOffset_.x = cursor.x - rc.left;
                dragOffset_.y = cursor.y - rc.top;
                ::SetCapture(hwnd);
            }
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
            if (!isHovered_) {
                isHovered_ = true;
                ::InvalidateRect(hwnd, nullptr, FALSE);
            }

            if (isAuthoring_ && draftAnnotation_.has_value()) {
                const pikasnip::annotations::PointF pt =
                    ClientPointToImage(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
                std::visit(
                    [&](auto& ann) {
                        using T = std::decay_t<decltype(ann)>;
                        if constexpr (std::is_same_v<T, pikasnip::annotations::RectAnnotation> ||
                                      std::is_same_v<T, pikasnip::annotations::EllipseAnnotation> ||
                                      std::is_same_v<T, pikasnip::annotations::MosaicAnnotation>) {
                            ann.bounds.right = pt.x;
                            ann.bounds.bottom = pt.y;
                        } else if constexpr (std::is_same_v<T, pikasnip::annotations::ArrowAnnotation>) {
                            ann.end = pt;
                        } else if constexpr (std::is_same_v<T, pikasnip::annotations::FreehandPenAnnotation> ||
                                             std::is_same_v<T, pikasnip::annotations::HighlighterAnnotation>) {
                            if (ann.points.empty() ||
                                std::hypot(pt.x - ann.points.back().x, pt.y - ann.points.back().y) >= 2.0f) {
                                ann.points.push_back(pt);
                            }
                        }
                    },
                    *draftAnnotation_);
                ::InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }

            if (dragging_) {
                POINT cursor{};
                RECT rc{};
                if (::GetCursorPos(&cursor) && ::GetWindowRect(hwnd, &rc)) {
                    const int32_t winW = rc.right - rc.left;
                    const int32_t winH = rc.bottom - rc.top;
                    const POINT clamped = ClampTopLeftToVirtualScreen(
                        cursor.x - dragOffset_.x,
                        cursor.y - dragOffset_.y,
                        winW,
                        winH);
                    ::SetWindowPos(
                        hwnd,
                        nullptr,
                        clamped.x,
                        clamped.y,
                        0,
                        0,
                        SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
                    if (editMode_ && toolbarWindow_) {
                        toolbarWindow_->SyncPosition(windowRect());
                    }
                }
            }
            return 0;
        }

        case WM_MOUSELEAVE: {
            trackingMouseLeave_ = false;
            if (isHovered_) {
                isHovered_ = false;
                ::InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        }

        case WM_LBUTTONUP: {
            if (isAuthoring_) {
                isAuthoring_ = false;
                if (::GetCapture() == hwnd) {
                    ::ReleaseCapture();
                }

                if (draftAnnotation_.has_value()) {
                    bool shouldCommit = false;
                    std::visit(
                        [&shouldCommit](const auto& ann) {
                            using T = std::decay_t<decltype(ann)>;
                            if constexpr (std::is_same_v<T, pikasnip::annotations::RectAnnotation> ||
                                          std::is_same_v<T, pikasnip::annotations::EllipseAnnotation> ||
                                          std::is_same_v<T, pikasnip::annotations::MosaicAnnotation>) {
                                shouldCommit =
                                    std::abs(ann.bounds.right - ann.bounds.left) >= 3.0f &&
                                    std::abs(ann.bounds.bottom - ann.bounds.top) >= 3.0f;
                            } else if constexpr (std::is_same_v<T, pikasnip::annotations::ArrowAnnotation>) {
                                shouldCommit =
                                    std::hypot(ann.end.x - ann.start.x, ann.end.y - ann.start.y) >= 3.0f;
                            } else if constexpr (std::is_same_v<T, pikasnip::annotations::FreehandPenAnnotation> ||
                                                 std::is_same_v<T, pikasnip::annotations::HighlighterAnnotation>) {
                                shouldCommit = !ann.points.empty();
                            }
                        },
                        *draftAnnotation_);
                    if (shouldCommit) {
                        annotations_.Add(std::move(*draftAnnotation_));
                        SyncToolbarState();
                    }
                    draftAnnotation_.reset();
                }
                ::InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }

            if (dragging_) {
                dragging_ = false;
                if (::GetCapture() == hwnd) {
                    ::ReleaseCapture();
                }
            }
            return 0;
        }

        case WM_LBUTTONDBLCLK: {
            if (activeTool_ == pikasnip::annotations::AnnotationTool::None && !activeText_.has_value()) {
                CopyToClipboard(hwnd);
            }
            return 0;
        }

        case WM_MBUTTONDOWN: {
            middlePressed_ = true;
            ::SetCapture(hwnd);
            return 0;
        }

        case WM_MBUTTONUP: {
            if (middlePressed_) {
                middlePressed_ = false;
                if (::GetCapture() == hwnd) {
                    ::ReleaseCapture();
                }
                RECT client{};
                ::GetClientRect(hwnd, &client);
                const POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
                if (::PtInRect(&client, pt)) {
                    Close();
                }
            }
            return 0;
        }

        case WM_RBUTTONDOWN: {
            rightPressed_ = true;
            ::SetCapture(hwnd);
            return 0;
        }

        case WM_RBUTTONUP: {
            if (rightPressed_) {
                rightPressed_ = false;
                if (::GetCapture() == hwnd) {
                    ::ReleaseCapture();
                }
                RECT client{};
                ::GetClientRect(hwnd, &client);
                const POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
                if (::PtInRect(&client, pt)) {
                    POINT screenPt = pt;
                    ::ClientToScreen(hwnd, &screenPt);
                    ShowContextMenu(screenPt);
                }
            }
            return 0;
        }

        case WM_COMMAND: {
            switch (LOWORD(wParam)) {
                case kIdmPinEdit:
                    SetEditMode(!editMode_);
                    return 0;
                case kIdmPinOcr:
                    RequestOcr();
                    return 0;
                case kIdmPinCopy:
                    CopyToClipboard(hwnd);
                    return 0;
                case kIdmPinSave:
                    SaveToDefaultDir();
                    return 0;
                case kIdmPinReset:
                    (void)SetScale(1.0f);
                    (void)SetOpacity(1.0f);
                    return 0;
                case kIdmPinClose:
                    Close();
                    return 0;
                default:
                    break;
            }
            break;
        }

        case WM_CAPTURECHANGED: {
            dragging_ = false;
            rightPressed_ = false;
            middlePressed_ = false;
            if (isAuthoring_) {
                isAuthoring_ = false;
                draftAnnotation_.reset();
                ::InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        }

        case WM_MOUSEWHEEL: {
            const int delta = GET_WHEEL_DELTA_WPARAM(wParam);
            if (delta == 0) {
                return 0;
            }
            if ((GET_KEYSTATE_WPARAM(wParam) & MK_CONTROL) != 0) {
                (void)SetOpacity(opacity_ + (delta > 0 ? 0.10f : -0.10f));
            } else {
                POINT cursor{};
                const bool hasCursor = ::GetCursorPos(&cursor) != FALSE;
                const float factor = (delta > 0) ? 1.10f : (1.0f / 1.10f);
                (void)SetScale(
                    scale_ * factor,
                    hasCursor ? std::optional<POINT>(cursor) : std::nullopt);
            }
            return 0;
        }

        case WM_CHAR: {
            if (activeText_.has_value()) {
                const wchar_t ch = static_cast<wchar_t>(wParam);
                if (ch >= 0x20 && ch != 0x7F) {
                    activeText_->text.push_back(ch);
                    ::InvalidateRect(hwnd, nullptr, FALSE);
                }
                return 0;
            }
            break;
        }

        case WM_KEYDOWN: {
            const bool ctrlDown = (::GetKeyState(VK_CONTROL) & 0x8000) != 0;

            if (activeText_.has_value()) {
                if (wParam == VK_BACK) {
                    auto& txt = activeText_->text;
                    if (!txt.empty()) {
                        const wchar_t last = txt.back();
                        txt.pop_back();
                        if (!txt.empty() && last >= 0xDC00 && last <= 0xDFFF &&
                            txt.back() >= 0xD800 && txt.back() <= 0xDBFF) {
                            txt.pop_back();
                        }
                        ::InvalidateRect(hwnd, nullptr, FALSE);
                    }
                    return 0;
                }
                if (wParam == VK_RETURN) {
                    CommitActiveTextIfAny();
                    SyncToolbarState();
                    ::InvalidateRect(hwnd, nullptr, FALSE);
                    return 0;
                }
                if (wParam == VK_ESCAPE) {
                    activeText_.reset();
                    ::InvalidateRect(hwnd, nullptr, FALSE);
                    return 0;
                }
                if (!ctrlDown) {
                    return 0;
                }
            }

            if (!ctrlDown && (wParam == 'E' || wParam == VK_SPACE)) {
                SetEditMode(!editMode_);
                return 0;
            }
            if (ctrlDown && wParam == 'Z') {
                CommitActiveTextIfAny();
                if (annotations_.Undo()) {
                    SyncToolbarState();
                    ::InvalidateRect(hwnd, nullptr, FALSE);
                }
                return 0;
            }
            if (ctrlDown && wParam == 'Y') {
                CommitActiveTextIfAny();
                if (annotations_.Redo()) {
                    SyncToolbarState();
                    ::InvalidateRect(hwnd, nullptr, FALSE);
                }
                return 0;
            }
            if (ctrlDown && wParam == 'T') {
                RequestOcr();
                return 0;
            }
            if (ctrlDown && wParam == 'C') {
                CopyToClipboard(hwnd);
                return 0;
            }
            if (ctrlDown && wParam == 'S') {
                SaveToDefaultDir();
                return 0;
            }
            if (wParam == '0' || wParam == VK_NUMPAD0) {
                (void)SetScale(1.0f);
                (void)SetOpacity(1.0f);
                return 0;
            }
            if (wParam == VK_ESCAPE) {
                if (isAuthoring_ || draftAnnotation_.has_value()) {
                    isAuthoring_ = false;
                    draftAnnotation_.reset();
                    if (::GetCapture() == hwnd) {
                        ::ReleaseCapture();
                    }
                    ::InvalidateRect(hwnd, nullptr, FALSE);
                    return 0;
                }
                if (activeTool_ != pikasnip::annotations::AnnotationTool::None) {
                    activeTool_ = pikasnip::annotations::AnnotationTool::None;
                    SyncToolbarState();
                    ::InvalidateRect(hwnd, nullptr, FALSE);
                    return 0;
                }
                if (editMode_) {
                    SetEditMode(false);
                    return 0;
                }
                Close();
                return 0;
            }
            break;
        }

        case WM_TIMER: {
            if (wParam == kHudTimerId) {
                ::KillTimer(hwnd, kHudTimerId);
                showHud_ = false;
                hudText_.clear();
                ::InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            break;
        }

        case WM_NCDESTROY: {
            ::KillTimer(hwnd, kHudTimerId);
            hwnd_ = nullptr;
            editMode_ = false;
            activeTool_ = pikasnip::annotations::AnnotationTool::None;
            isAuthoring_ = false;
            draftAnnotation_.reset();
            activeText_.reset();
            annotations_.Reset();
            if (toolbarWindow_) {
                toolbarWindow_->Hide();
            }

            dragging_ = false;
            rightPressed_ = false;
            middlePressed_ = false;
            isHovered_ = false;
            trackingMouseLeave_ = false;
            showHud_ = false;
            hudText_.clear();

            ReleaseRenderResources();
            std::vector<uint8_t>{}.swap(image_.bgraPixels);
            image_ = {};

            ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);

            ClosedCallback cb = std::move(onClosed_);
            onClosed_ = nullptr;
            onFeedback_ = nullptr;
            onOcr_ = nullptr;
            if (cb) {
                cb(this);
            }
            return ::DefWindowProcW(hwnd, msg, wParam, lParam);
        }

        default:
            break;
    }

    return ::DefWindowProcW(hwnd, msg, wParam, lParam);
}

} // namespace pikasnip::pin

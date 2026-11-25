#include "pikasnip/overlay/OverlayWindow.h"
#include "pikasnip/annotations/AnnotationRenderer.h"
#include "pikasnip/platform/Clipboard.h"

#include <windowsx.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <utility>

namespace pikasnip::overlay {

namespace {

constexpr wchar_t kOverlayClassName[] = L"OZLab_PikaSnip_OverlayWindow";
constexpr UINT_PTR kCopiedFeedbackTimerId = 1;
constexpr UINT_PTR kStatusFeedbackTimerId = 2;
constexpr UINT kCopiedFeedbackMs = 1200;
constexpr UINT kStatusFeedbackMs = 2500;

D2D1_COLOR_F HexColor(uint32_t rgb, float alpha = 1.0f) noexcept {
    return D2D1::ColorF(
        static_cast<float>((rgb >> 16) & 0xFF) / 255.0f,
        static_cast<float>((rgb >> 8) & 0xFF) / 255.0f,
        static_cast<float>(rgb & 0xFF) / 255.0f,
        alpha);
}

bool RegisterOverlayClass(HINSTANCE hInstance, WNDPROC wndProc) noexcept {
    WNDCLASSEXW wc{};
    if (::GetClassInfoExW(hInstance, kOverlayClassName, &wc)) {
        return true;
    }

    wc.cbSize = sizeof(wc);
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = wndProc;
    wc.hInstance = hInstance;
    wc.hCursor = ::LoadCursorW(nullptr, IDC_CROSS);
    wc.lpszClassName = kOverlayClassName;

    return ::RegisterClassExW(&wc) != 0;
}

pikasnip::annotations::PointF ClampPointToRect(
    int32_t vx,
    int32_t vy,
    const pikasnip::platform::RectI& bounds) noexcept {
    if (bounds.isEmpty()) {
        return {static_cast<float>(vx), static_cast<float>(vy)};
    }
    return {
        static_cast<float>(std::clamp(vx, bounds.left, bounds.right)),
        static_cast<float>(std::clamp(vy, bounds.top, bounds.bottom))
    };
}

} // namespace

LRESULT CALLBACK OverlayWindow::WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_NCCREATE) {
        const auto* cs = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        auto* self = static_cast<OverlayWindow*>(cs->lpCreateParams);
        if (self != nullptr) {
            self->hwnd_ = hwnd;
            ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
    }
    auto* self = reinterpret_cast<OverlayWindow*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (self != nullptr) {
        return self->HandleMessage(hwnd, msg, wParam, lParam);
    }
    return ::DefWindowProcW(hwnd, msg, wParam, lParam);
}

OverlayWindow::OverlayWindow(pikasnip::platform::D2dContext& d2d) noexcept
    : d2d_(d2d) {}

OverlayWindow::~OverlayWindow() {
    Close(false);
}

bool OverlayWindow::Show(
    HINSTANCE hInstance,
    pikasnip::capture::CapturedFrame&& frame,
    pikasnip::capture::WindowDetector&& detector,
    ClosedCallback onClosed,
    OverlayMode mode) {
    if (!frame.isValid()) {
        return false;
    }
    if (hwnd_ != nullptr) {
        Close(false);
    }
    if (!d2d_.isInitialized() && FAILED(d2d_.Initialize())) {
        return false;
    }
    if (!RegisterOverlayClass(hInstance, &OverlayWindow::WindowProc)) {
        return false;
    }

    frame_ = std::move(frame);
    detector_ = std::move(detector);
    onClosed_ = std::move(onClosed);
    mode_ = mode;

    annotations_.Reset();
    toolbar_.Reset();
    activeTool_ = pikasnip::annotations::AnnotationTool::None;
    style_ = pikasnip::annotations::AnnotationStyle{};
    draftAnnotation_.reset();
    activeText_.reset();
    draftAnchor_ = {};
    copiedHexFeedback_ = false;
    statusFeedbackText_.clear();
    statusFeedbackIsError_ = false;
    handlingButtonUp_ = false;
    isAuthoring_ = false;
    pressedToolbar_ = false;
    pressedToolbarAction_ = ToolbarActionKind::None;
    closing_ = false;

    controller_.BeginSession(&frame_, &detector_);

    const pikasnip::platform::RectI vb = frame_.virtualBounds;
    hwnd_ = ::CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        kOverlayClassName,
        L"PikaSnip Overlay",
        WS_POPUP,
        vb.left,
        vb.top,
        vb.width(),
        vb.height(),
        nullptr,
        nullptr,
        hInstance,
        this);
    if (hwnd_ == nullptr) {
        frame_ = {};
        controller_.Reset();
        return false;
    }

    if (!EnsureRenderResources()) {
        Close(false);
        return false;
    }

    POINT cursorPt{};
    if (::GetCursorPos(&cursorPt)) {
        controller_.OnPointerMove(cursorPt.x, cursorPt.y);
    }

    ::SetWindowPos(
        hwnd_,
        HWND_TOPMOST,
        vb.left,
        vb.top,
        vb.width(),
        vb.height(),
        SWP_SHOWWINDOW);
    ::SetForegroundWindow(hwnd_);
    ::SetFocus(hwnd_);
    ::InvalidateRect(hwnd_, nullptr, FALSE);
    ::UpdateWindow(hwnd_);
    return true;
}

void OverlayWindow::Close(bool copied) noexcept {
    if (closing_) {
        return;
    }
    closing_ = true;

    if (hwnd_ != nullptr) {
        ::KillTimer(hwnd_, kCopiedFeedbackTimerId);
        ::KillTimer(hwnd_, kStatusFeedbackTimerId);
        if (::GetCapture() == hwnd_) {
            ::ReleaseCapture();
        }
        ::DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }

    ReleaseRenderResources();
    controller_.Reset();
    annotations_.Reset();
    toolbar_.Reset();
    activeTool_ = pikasnip::annotations::AnnotationTool::None;
    draftAnnotation_.reset();
    activeText_.reset();
    frame_ = {};
    detector_ = {};
    copiedHexFeedback_ = false;
    statusFeedbackText_.clear();
    statusFeedbackIsError_ = false;
    handlingButtonUp_ = false;
    isAuthoring_ = false;
    pressedToolbar_ = false;
    pressedToolbarAction_ = ToolbarActionKind::None;

    auto cb = std::move(onClosed_);
    onClosed_ = nullptr;
    mode_ = OverlayMode::Standard;
    closing_ = false;

    if (cb) {
        cb(copied);
    }
}

void OverlayWindow::ReleaseRenderResources() noexcept {
    magnifier_.ResetResources();
    hintFormat_.Reset();
    badgeFormat_.Reset();
    brush_.Reset();
    desktopBitmap_.Reset();
    renderTarget_.Reset();
}

void OverlayWindow::ReleaseOverlayCaptureSafely(HWND hwnd) noexcept {
    handlingButtonUp_ = true;
    if (hwnd != nullptr && ::GetCapture() == hwnd) {
        ::ReleaseCapture();
    }
    handlingButtonUp_ = false;
}

bool OverlayWindow::EnsureRenderResources() {
    if (hwnd_ == nullptr || !frame_.isValid() || d2d_.d2dFactory() == nullptr) {
        return false;
    }

    if (!renderTarget_) {
        const D2D1_RENDER_TARGET_PROPERTIES rtProps = D2D1::RenderTargetProperties(
            D2D1_RENDER_TARGET_TYPE_DEFAULT,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE),
            96.0f,
            96.0f);
        const D2D1_HWND_RENDER_TARGET_PROPERTIES hwndProps = D2D1::HwndRenderTargetProperties(
            hwnd_,
            D2D1::SizeU(static_cast<UINT32>(frame_.width), static_cast<UINT32>(frame_.height)));
        const HRESULT hr = d2d_.d2dFactory()->CreateHwndRenderTarget(
            rtProps,
            hwndProps,
            renderTarget_.ReleaseAndGetAddressOf());
        if (FAILED(hr) || !renderTarget_) {
            return false;
        }
    }

    if (!desktopBitmap_) {
        const D2D1_BITMAP_PROPERTIES bmpProps = D2D1::BitmapProperties(
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE),
            96.0f,
            96.0f);
        const HRESULT hr = renderTarget_->CreateBitmap(
            D2D1::SizeU(static_cast<UINT32>(frame_.width), static_cast<UINT32>(frame_.height)),
            frame_.bgraPixels.data(),
            static_cast<UINT32>(frame_.strideBytes),
            &bmpProps,
            desktopBitmap_.ReleaseAndGetAddressOf());
        if (FAILED(hr) || !desktopBitmap_) {
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

    if (!badgeFormat_ && d2d_.dwriteFactory() != nullptr) {
        (void)d2d_.dwriteFactory()->CreateTextFormat(
            L"Consolas",
            nullptr,
            DWRITE_FONT_WEIGHT_SEMI_BOLD,
            DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL,
            12.0f,
            L"en-US",
            badgeFormat_.ReleaseAndGetAddressOf());
    }

    if (!hintFormat_ && d2d_.dwriteFactory() != nullptr) {
        (void)d2d_.dwriteFactory()->CreateTextFormat(
            L"Segoe UI",
            nullptr,
            DWRITE_FONT_WEIGHT_SEMI_BOLD,
            DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL,
            12.0f,
            L"en-US",
            hintFormat_.ReleaseAndGetAddressOf());
        if (hintFormat_) {
            (void)hintFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            (void)hintFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }
    }

    return true;
}

pikasnip::capture::MonitorInfo OverlayWindow::FindMonitorAt(int32_t vx, int32_t vy) const noexcept {
    for (const auto& m : frame_.monitors) {
        if (m.bounds.contains(vx, vy)) {
            return m;
        }
    }
    if (!frame_.monitors.empty()) {
        return frame_.monitors.front();
    }
    pikasnip::capture::MonitorInfo fallback;
    fallback.bounds = frame_.virtualBounds;
    fallback.dpi = 96;
    fallback.isPrimary = true;
    return fallback;
}

void OverlayWindow::EnsureToolbarLayout() {
    if (controller_.mode() != SelectionMode::Selected) {
        toolbar_.Reset();
        return;
    }
    const pikasnip::platform::RectI sel = controller_.selectedRect();
    const int32_t refX = sel.left + sel.width() / 2;
    const int32_t refY = sel.top + sel.height() / 2;
    const auto mon = FindMonitorAt(refX, refY);
    toolbar_.ComputeLayout(sel, mon.bounds, mon.dpi);
}

void OverlayWindow::Render() {
    if (!EnsureRenderResources()) {
        return;
    }

    renderTarget_->BeginDraw();

    const D2D1_SIZE_F rtSize = renderTarget_->GetSize();
    renderTarget_->DrawBitmap(desktopBitmap_.Get(), D2D1::RectF(0.0f, 0.0f, rtSize.width, rtSize.height));

    const pikasnip::platform::RectI vb = frame_.virtualBounds;
    const float offX = -static_cast<float>(vb.left);
    const float offY = -static_cast<float>(vb.top);

    pikasnip::annotations::AnnotationRenderer::RenderVectorAnnotations(
        renderTarget_.Get(),
        d2d_.d2dFactory(),
        d2d_.dwriteFactory(),
        &frame_,
        annotations_.items(),
        draftAnnotation_.has_value() ? &*draftAnnotation_ : nullptr,
        offX,
        offY,
        activeText_.has_value() ? &*activeText_ : nullptr);

    const pikasnip::platform::RectI active = controller_.activeRect();

    // Dimmed exterior (alpha = 0.45f), leaving active region completely un-dimmed.
    brush_->SetColor(HexColor(0x000000, 0.45f));
    if (active.isEmpty()) {
        renderTarget_->FillRectangle(D2D1::RectF(0.0f, 0.0f, rtSize.width, rtSize.height), brush_.Get());
    } else {
        const float selL = static_cast<float>(active.left - vb.left);
        const float selT = static_cast<float>(active.top - vb.top);
        const float selR = static_cast<float>(active.right - vb.left);
        const float selB = static_cast<float>(active.bottom - vb.top);

        if (selT > 0.0f) {
            renderTarget_->FillRectangle(D2D1::RectF(0.0f, 0.0f, rtSize.width, selT), brush_.Get());
        }
        if (selB < rtSize.height) {
            renderTarget_->FillRectangle(D2D1::RectF(0.0f, selB, rtSize.width, rtSize.height), brush_.Get());
        }
        if (selL > 0.0f) {
            renderTarget_->FillRectangle(D2D1::RectF(0.0f, selT, selL, selB), brush_.Get());
        }
        if (selR < rtSize.width) {
            renderTarget_->FillRectangle(D2D1::RectF(selR, selT, rtSize.width, selB), brush_.Get());
        }

        // Selection border (#38BDF8)
        brush_->SetColor(HexColor(0x38BDF8, 1.0f));
        renderTarget_->DrawRectangle(
            D2D1::RectF(selL + 0.5f, selT + 0.5f, selR - 0.5f, selB - 0.5f),
            brush_.Get(),
            2.0f);

        // 8 resize handles in Selected mode when no annotation tool is active
        if (controller_.mode() == SelectionMode::Selected &&
            activeTool_ == pikasnip::annotations::AnnotationTool::None) {
            const auto handles = controller_.GetHandleRects(4);
            for (const auto& h : handles) {
                const D2D1_RECT_F hRect = D2D1::RectF(
                    static_cast<float>(h.left - vb.left),
                    static_cast<float>(h.top - vb.top),
                    static_cast<float>(h.right - vb.left),
                    static_cast<float>(h.bottom - vb.top));
                brush_->SetColor(HexColor(0xFFFFFF, 1.0f));
                renderTarget_->FillRectangle(hRect, brush_.Get());
                brush_->SetColor(HexColor(0x38BDF8, 1.0f));
                renderTarget_->DrawRectangle(hRect, brush_.Get(), 1.5f);
            }
        }

        // Dimension badge
        if (badgeFormat_) {
            wchar_t dimBuf[48]{};
            std::swprintf(dimBuf, std::size(dimBuf), L"%d x %d", active.width(), active.height());
            const float badgeW = 92.0f;
            const float badgeH = 22.0f;
            float badgeX = selL;
            float badgeY = selT - badgeH - 6.0f;
            if (badgeY < 4.0f) {
                badgeY = selT + 6.0f;
                badgeX = selL + 6.0f;
            }
            const D2D1_RECT_F badgeRect = D2D1::RectF(badgeX, badgeY, badgeX + badgeW, badgeY + badgeH);
            brush_->SetColor(HexColor(0x1E1E1E, 0.92f));
            renderTarget_->FillRectangle(badgeRect, brush_.Get());
            brush_->SetColor(HexColor(0x3A3A3A, 1.0f));
            renderTarget_->DrawRectangle(badgeRect, brush_.Get(), 1.0f);
            brush_->SetColor(HexColor(0xF9FAFB, 1.0f));
            renderTarget_->DrawText(
                dimBuf,
                static_cast<UINT32>(std::wcslen(dimBuf)),
                badgeFormat_.Get(),
                D2D1::RectF(badgeRect.left + 8.0f, badgeRect.top + 3.0f, badgeRect.right - 4.0f, badgeRect.bottom),
                brush_.Get());
        }
    }

    const PointI cursor = controller_.cursorPos();
    const pikasnip::capture::MonitorInfo monitor = FindMonitorAt(cursor.x, cursor.y);
    const float scale = static_cast<float>(monitor.dpi != 0 ? monitor.dpi : 96U) / 96.0f;

    if (controller_.mode() == SelectionMode::Selected) {
        EnsureToolbarLayout();
        toolbar_.Draw(
            renderTarget_.Get(),
            d2d_.dwriteFactory(),
            offX,
            offY,
            activeTool_,
            style_,
            annotations_.CanUndo(),
            annotations_.CanRedo());
    } else {
        toolbar_.Reset();
    }

    // Compact dark-charcoal bottom action hint bar when not actively typing or annotating.
    // Hidden once a region is locked, because the FloatingToolbar takes over the bottom edge.
    if (hintFormat_ && controller_.mode() != SelectionMode::Selected && !activeText_.has_value() &&
        activeTool_ == pikasnip::annotations::AnnotationTool::None) {
        const float monL = static_cast<float>(monitor.bounds.left - vb.left);
        const float monR = static_cast<float>(monitor.bounds.right - vb.left);
        const float monB = static_cast<float>(monitor.bounds.bottom - vb.top);
        const float barW = std::min(420.0f * scale, (monR - monL) - 24.0f);
        const float barH = 28.0f * scale;
        const float barX = monL + ((monR - monL) - barW) * 0.5f;
        const float barY = monB - barH - 18.0f * scale;
        const D2D1_RECT_F barRect = D2D1::RectF(barX, barY, barX + barW, barY + barH);

        brush_->SetColor(HexColor(0x1E1E1E, 0.92f));
        renderTarget_->FillRoundedRectangle(D2D1::RoundedRect(barRect, 6.0f * scale, 6.0f * scale), brush_.Get());
        brush_->SetColor(HexColor(0x3A3A3A, 1.0f));
        renderTarget_->DrawRoundedRectangle(D2D1::RoundedRect(barRect, 6.0f * scale, 6.0f * scale), brush_.Get(), 1.0f);

        const wchar_t* hintText = (mode_ == OverlayMode::QuickOcr)
            ? L"Quick OCR: Click Window / Drag Region  |  Esc: Cancel"
            : L"Click Window / Drag: Select  |  C: Copy HEX  |  Esc: Cancel";
        brush_->SetColor(HexColor(0xE5E7EB, 1.0f));
        renderTarget_->DrawText(
            hintText,
            static_cast<UINT32>(std::wcslen(hintText)),
            hintFormat_.Get(),
            barRect,
            brush_.Get());
    }

    // Live pixel magnifier HUD (hidden when annotating or hovering toolbar)
    const bool showMagnifier =
        (controller_.mode() != SelectionMode::Selected) ||
        (activeTool_ == pikasnip::annotations::AnnotationTool::None &&
         !toolbar_.Contains(cursor.x, cursor.y));
    if (showMagnifier) {
        MagnifierHudState hudState;
        hudState.cursorPos = cursor;
        hudState.activeRect = active;
        hudState.monitorBounds = monitor.bounds;
        hudState.pixelColor = controller_.GetPixelRgbHex(cursor.x, cursor.y);
        hudState.dpi = monitor.dpi;
        hudState.copiedFeedback = copiedHexFeedback_;
        magnifier_.Draw(renderTarget_.Get(), d2d_.dwriteFactory(), frame_, hudState);
    }

    if (!statusFeedbackText_.empty() && hintFormat_) {
        const float monL = static_cast<float>(monitor.bounds.left - vb.left);
        const float monR = static_cast<float>(monitor.bounds.right - vb.left);
        const float monT = static_cast<float>(monitor.bounds.top - vb.top);
        const float monB = static_cast<float>(monitor.bounds.bottom - vb.top);
        const float toastW = std::min(340.0f * scale, std::max(120.0f, (monR - monL) - 24.0f));
        const float toastH = 30.0f * scale;
        const float toastX = monL + ((monR - monL) - toastW) * 0.5f;
        float toastY = monB - toastH - 54.0f * scale;

        // Keep status/error toasts clear of the floating toolbar on near-bottom selections.
        if (controller_.mode() == SelectionMode::Selected && !toolbar_.bounds().isEmpty()) {
            const float toolbarTop = static_cast<float>(toolbar_.bounds().top - vb.top);
            const float toolbarBottom = static_cast<float>(toolbar_.bounds().bottom - vb.top);
            if (toastY + toastH > toolbarTop - 8.0f * scale && toastY < toolbarBottom + 8.0f * scale) {
                toastY = std::max(monT + 12.0f * scale, toolbarTop - toastH - 10.0f * scale);
            }
        }

        const D2D1_RECT_F toastRect = D2D1::RectF(toastX, toastY, toastX + toastW, toastY + toastH);

        brush_->SetColor(HexColor(0x1E1E1E, 0.96f));
        renderTarget_->FillRoundedRectangle(D2D1::RoundedRect(toastRect, 6.0f * scale, 6.0f * scale), brush_.Get());
        brush_->SetColor(statusFeedbackIsError_ ? HexColor(0xEF4444, 1.0f) : HexColor(0x38BDF8, 1.0f));
        renderTarget_->DrawRoundedRectangle(D2D1::RoundedRect(toastRect, 6.0f * scale, 6.0f * scale), brush_.Get(), 1.25f);
        brush_->SetColor(statusFeedbackIsError_ ? HexColor(0xFCA5A5, 1.0f) : HexColor(0xF9FAFB, 1.0f));
        renderTarget_->DrawText(
            statusFeedbackText_.c_str(),
            static_cast<UINT32>(statusFeedbackText_.size()),
            hintFormat_.Get(),
            toastRect,
            brush_.Get());
    }

    const HRESULT hr = renderTarget_->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) {
        ReleaseRenderResources();
    }
}

void OverlayWindow::CommitActiveTextIfAny() {
    if (!activeText_.has_value()) {
        return;
    }
    if (!activeText_->text.empty()) {
        annotations_.Add(*activeText_);
    }
    activeText_.reset();
}

void OverlayWindow::CommitSelection() {
    CommitActiveTextIfAny();

    pikasnip::platform::RectI region = controller_.selectedRect();
    if (region.isEmpty() && controller_.mode() == SelectionMode::HoverWindow) {
        region = controller_.hoveredRect();
    }
    if (region.isEmpty()) {
        return;
    }

    pikasnip::capture::CapturedFrame flattened;
    if (!pikasnip::annotations::AnnotationRenderer::FlattenRegion(
            d2d_,
            frame_,
            region,
            annotations_,
            flattened)) {
        return;
    }

    const bool ok = pikasnip::platform::CopyImageToClipboard(
        hwnd_,
        d2d_.wicFactory(),
        flattened.bgraPixels.data(),
        flattened.width,
        flattened.height,
        flattened.strideBytes);
    Close(ok);
}

void OverlayWindow::SetSaveFileCallback(SaveFileFn fn) {
    saveFileFn_ = std::move(fn);
}

void OverlayWindow::SetPinCallback(PinCallback fn) {
    pinCallback_ = std::move(fn);
}

void OverlayWindow::SetOcrCallback(OcrCallback fn) {
    ocrCallback_ = std::move(fn);
}

bool OverlayWindow::FlattenActiveSelection(
    pikasnip::capture::CapturedFrame& outFlattened,
    pikasnip::platform::RectI& outRegion,
    const wchar_t* failMessage) {
    CommitActiveTextIfAny();

    pikasnip::platform::RectI region = controller_.selectedRect();
    if (region.isEmpty() && controller_.mode() == SelectionMode::HoverWindow) {
        region = controller_.hoveredRect();
    }
    if (region.isEmpty()) {
        return false;
    }

    if (!pikasnip::annotations::AnnotationRenderer::FlattenRegion(
            d2d_,
            frame_,
            region,
            annotations_,
            outFlattened)) {
        statusFeedbackText_ = failMessage;
        statusFeedbackIsError_ = true;
        if (hwnd_ != nullptr) {
            ::SetTimer(hwnd_, kStatusFeedbackTimerId, kStatusFeedbackMs, nullptr);
            ::InvalidateRect(hwnd_, nullptr, FALSE);
        }
        return false;
    }

    outRegion = region;
    return true;
}

void OverlayWindow::RequestPin() {
    pikasnip::capture::CapturedFrame flattened;
    pikasnip::platform::RectI region;
    if (!FlattenActiveSelection(flattened, region, L"Pin failed \u2014 unable to render image")) {
        return;
    }

    PinCallback cb = pinCallback_;
    Close(true);
    if (cb) {
        cb(std::move(flattened), region);
    }
}

void OverlayWindow::RequestOcr() {
    pikasnip::capture::CapturedFrame flattened;
    pikasnip::platform::RectI region;
    if (!FlattenActiveSelection(flattened, region, L"OCR failed \u2014 unable to render image")) {
        return;
    }

    OcrCallback cb = ocrCallback_;
    Close(true);
    if (cb) {
        cb(std::move(flattened));
    }
}

void OverlayWindow::SaveSelectionToFile() {
    CommitActiveTextIfAny();

    pikasnip::platform::RectI region = controller_.selectedRect();
    if (region.isEmpty() && controller_.mode() == SelectionMode::HoverWindow) {
        region = controller_.hoveredRect();
    }
    if (region.isEmpty()) {
        return;
    }

    pikasnip::capture::CapturedFrame flattened;
    if (!pikasnip::annotations::AnnotationRenderer::FlattenRegion(
            d2d_,
            frame_,
            region,
            annotations_,
            flattened)) {
        statusFeedbackText_ = L"Save failed \u2014 unable to render image";
        statusFeedbackIsError_ = true;
        if (hwnd_ != nullptr) {
            ::SetTimer(hwnd_, kStatusFeedbackTimerId, kStatusFeedbackMs, nullptr);
            ::InvalidateRect(hwnd_, nullptr, FALSE);
        }
        return;
    }

    const std::filesystem::path savedPath = saveFileFn_
        ? saveFileFn_(
              d2d_.wicFactory(),
              flattened.bgraPixels.data(),
              flattened.width,
              flattened.height,
              flattened.strideBytes)
        : pikasnip::platform::SaveTimestampedPngToDefaultDir(
              d2d_.wicFactory(),
              flattened.bgraPixels.data(),
              flattened.width,
              flattened.height,
              flattened.strideBytes);

    if (savedPath.empty()) {
        statusFeedbackText_ = L"Save failed \u2014 check folder permissions";
        statusFeedbackIsError_ = true;
        if (hwnd_ != nullptr) {
            ::SetTimer(hwnd_, kStatusFeedbackTimerId, kStatusFeedbackMs, nullptr);
            ::InvalidateRect(hwnd_, nullptr, FALSE);
        }
        return;
    }

    (void)pikasnip::platform::CopyImageToClipboard(
        hwnd_,
        d2d_.wicFactory(),
        flattened.bgraPixels.data(),
        flattened.width,
        flattened.height,
        flattened.strideBytes);

    Close(true);
}

void OverlayWindow::ExecuteToolbarAction(const ToolbarHit& hit) {
    if (hit.tool != pikasnip::annotations::AnnotationTool::None) {
        CommitActiveTextIfAny();
        isAuthoring_ = false;
        draftAnnotation_.reset();
        activeTool_ = (activeTool_ == hit.tool)
            ? pikasnip::annotations::AnnotationTool::None
            : hit.tool;
        return;
    }

    if (hit.isColor) {
        style_.color = hit.color;
        if (activeText_.has_value()) {
            activeText_->color = style_.color;
        }
        return;
    }

    if (hit.isThickness) {
        style_.thickness = hit.thickness;
        return;
    }

    switch (hit.kind) {
        case ToolbarActionKind::Undo:
            CommitActiveTextIfAny();
            (void)annotations_.Undo();
            break;
        case ToolbarActionKind::Redo:
            CommitActiveTextIfAny();
            (void)annotations_.Redo();
            break;
        case ToolbarActionKind::Pin:
            RequestPin();
            break;
        case ToolbarActionKind::Ocr:
            RequestOcr();
            break;
        case ToolbarActionKind::Save:
            SaveSelectionToFile();
            break;
        case ToolbarActionKind::Copy:
            CommitSelection();
            break;
        case ToolbarActionKind::Cancel:
            Close(false);
            break;
        default:
            break;
    }
}

void OverlayWindow::CopyCursorHex() {
    const PointI cursor = controller_.cursorPos();
    const PixelColorInfo info = controller_.GetPixelRgbHex(cursor.x, cursor.y);
    if (!info.valid) {
        return;
    }
    if (pikasnip::platform::CopyUnicodeTextToClipboard(hwnd_, info.wideHex)) {
        copiedHexFeedback_ = true;
        if (hwnd_ != nullptr) {
            ::SetTimer(hwnd_, kCopiedFeedbackTimerId, kCopiedFeedbackMs, nullptr);
            ::InvalidateRect(hwnd_, nullptr, FALSE);
        }
    }
}

void OverlayWindow::UpdateCursorShape(int32_t vx, int32_t vy) const noexcept {
    LPCWSTR cursorId = IDC_CROSS;
    if (controller_.mode() == SelectionMode::Selected) {
        if (toolbar_.Contains(vx, vy)) {
            cursorId = IDC_HAND;
        } else if (activeTool_ == pikasnip::annotations::AnnotationTool::Text) {
            cursorId = controller_.selectedRect().contains(vx, vy) ? IDC_IBEAM : IDC_CROSS;
        } else if (activeTool_ != pikasnip::annotations::AnnotationTool::None) {
            cursorId = IDC_CROSS;
        } else {
            switch (controller_.HitTestHandle(vx, vy)) {
                case ResizeHandle::TopLeft:
                case ResizeHandle::BottomRight:
                    cursorId = IDC_SIZENWSE;
                    break;
                case ResizeHandle::TopRight:
                case ResizeHandle::BottomLeft:
                    cursorId = IDC_SIZENESW;
                    break;
                case ResizeHandle::Top:
                case ResizeHandle::Bottom:
                    cursorId = IDC_SIZENS;
                    break;
                case ResizeHandle::Left:
                case ResizeHandle::Right:
                    cursorId = IDC_SIZEWE;
                    break;
                case ResizeHandle::None:
                    if (controller_.selectedRect().contains(vx, vy)) {
                        cursorId = IDC_SIZEALL;
                    }
                    break;
            }
        }
    }
    ::SetCursor(::LoadCursorW(nullptr, cursorId));
}

LRESULT OverlayWindow::HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
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
                const PointI c = controller_.cursorPos();
                UpdateCursorShape(c.x, c.y);
                return TRUE;
            }
            break;
        }

        case WM_MOUSEMOVE: {
            const int32_t vx = frame_.virtualBounds.left + GET_X_LPARAM(lParam);
            const int32_t vy = frame_.virtualBounds.top + GET_Y_LPARAM(lParam);

            if (pressedToolbar_) {
                toolbar_.SetHoveredPoint(vx, vy);
                UpdateCursorShape(vx, vy);
                ::InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }

            if (isAuthoring_ && draftAnnotation_.has_value()) {
                const auto pt = ClampPointToRect(vx, vy, controller_.selectedRect());
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

            toolbar_.SetHoveredPoint(vx, vy);
            controller_.OnPointerMove(vx, vy);
            UpdateCursorShape(vx, vy);
            ::InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }

        case WM_LBUTTONDOWN: {
            const int32_t vx = frame_.virtualBounds.left + GET_X_LPARAM(lParam);
            const int32_t vy = frame_.virtualBounds.top + GET_Y_LPARAM(lParam);

            if (controller_.mode() == SelectionMode::Selected &&
                !isAuthoring_ &&
                !controller_.isDragging()) {
                EnsureToolbarLayout();
                if (toolbar_.Contains(vx, vy)) {
                    pressedToolbar_ = true;
                    pressedToolbarAction_ = toolbar_.HitTest(vx, vy).kind;
                    ::SetCapture(hwnd);
                    ::InvalidateRect(hwnd, nullptr, FALSE);
                    return 0;
                }
            }

            if (controller_.mode() == SelectionMode::Selected &&
                activeTool_ != pikasnip::annotations::AnnotationTool::None) {
                if (controller_.selectedRect().contains(vx, vy)) {
                    CommitActiveTextIfAny();
                    const auto pt = ClampPointToRect(vx, vy, controller_.selectedRect());

                    if (activeTool_ == pikasnip::annotations::AnnotationTool::StepCounter) {
                        pikasnip::annotations::StepCounterAnnotation step;
                        step.center = pt;
                        step.number = annotations_.NextStepNumber();
                        step.fill = style_.color;
                        annotations_.Add(std::move(step));
                        ::InvalidateRect(hwnd, nullptr, FALSE);
                        return 0;
                    }

                    if (activeTool_ == pikasnip::annotations::AnnotationTool::Text) {
                        pikasnip::annotations::TextAnnotation txt;
                        txt.origin = pt;
                        txt.color = style_.color;
                        txt.fontSize = style_.fontSize;
                        activeText_ = std::move(txt);
                        ::InvalidateRect(hwnd, nullptr, FALSE);
                        return 0;
                    }

                    draftAnchor_ = {vx, vy};
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
                } else if (!annotations_.empty() || activeText_.has_value()) {
                    CommitActiveTextIfAny();
                    ::InvalidateRect(hwnd, nullptr, FALSE);
                    return 0;
                }
            } else if (controller_.mode() == SelectionMode::Selected &&
                       (!annotations_.empty() || activeText_.has_value()) &&
                       !controller_.selectedRect().contains(vx, vy) &&
                       controller_.HitTestHandle(vx, vy) == ResizeHandle::None) {
                // Protect existing annotations against accidental outside clicks.
                CommitActiveTextIfAny();
                ::InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }

            ::SetCapture(hwnd);
            controller_.OnLeftButtonDown(vx, vy);
            ::InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }

        case WM_LBUTTONUP: {
            const int32_t vx = frame_.virtualBounds.left + GET_X_LPARAM(lParam);
            const int32_t vy = frame_.virtualBounds.top + GET_Y_LPARAM(lParam);

            if (pressedToolbar_) {
                const ToolbarActionKind pressedKind = pressedToolbarAction_;
                pressedToolbar_ = false;
                pressedToolbarAction_ = ToolbarActionKind::None;
                ReleaseOverlayCaptureSafely(hwnd);

                const ToolbarHit hit = toolbar_.HitTest(vx, vy);
                if (hit.kind != ToolbarActionKind::None && hit.kind == pressedKind) {
                    ExecuteToolbarAction(hit);
                }
                if (hwnd_ != nullptr) {
                    ::InvalidateRect(hwnd, nullptr, FALSE);
                }
                return 0;
            }

            if (isAuthoring_) {
                isAuthoring_ = false;
                ReleaseOverlayCaptureSafely(hwnd);

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
                    }
                    draftAnnotation_.reset();
                }
                ::InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }

            const bool wasDraggingNew = (controller_.mode() == SelectionMode::DraggingNew);
            controller_.OnLeftButtonUp(vx, vy);
            ReleaseOverlayCaptureSafely(hwnd);

            const bool commitReq = controller_.consumeCommitRequest();
            if (mode_ == OverlayMode::QuickOcr) {
                if ((commitReq || wasDraggingNew) &&
                    controller_.mode() == SelectionMode::Selected &&
                    !controller_.selectedRect().isEmpty()) {
                    RequestOcr();
                    return 0;
                }
            } else if (commitReq) {
                if (activeTool_ == pikasnip::annotations::AnnotationTool::None &&
                    !activeText_.has_value()) {
                    CommitSelection();
                    return 0;
                }
            }
            EnsureToolbarLayout();
            ::InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }

        case WM_LBUTTONDBLCLK: {
            const int32_t vx = frame_.virtualBounds.left + GET_X_LPARAM(lParam);
            const int32_t vy = frame_.virtualBounds.top + GET_Y_LPARAM(lParam);
            if (activeTool_ == pikasnip::annotations::AnnotationTool::None &&
                !activeText_.has_value() &&
                !toolbar_.Contains(vx, vy) &&
                controller_.mode() == SelectionMode::Selected &&
                controller_.selectedRect().contains(vx, vy)) {
                CommitSelection();
            }
            return 0;
        }

        case WM_RBUTTONUP: {
            if (activeText_.has_value()) {
                activeText_.reset();
                ::InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            if (isAuthoring_ || draftAnnotation_.has_value()) {
                isAuthoring_ = false;
                draftAnnotation_.reset();
                ReleaseOverlayCaptureSafely(hwnd);
                ::InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            if (activeTool_ != pikasnip::annotations::AnnotationTool::None) {
                activeTool_ = pikasnip::annotations::AnnotationTool::None;
                ::InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            if (controller_.CancelSelection()) {
                annotations_.Reset();
                toolbar_.Reset();
                ::InvalidateRect(hwnd, nullptr, FALSE);
            } else {
                Close(false);
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
            const bool ctrl = (::GetKeyState(VK_CONTROL) & 0x8000) != 0;

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
                    ::InvalidateRect(hwnd, nullptr, FALSE);
                    return 0;
                }
                if (wParam == VK_ESCAPE) {
                    activeText_.reset();
                    ::InvalidateRect(hwnd, nullptr, FALSE);
                    return 0;
                }
                if (!ctrl) {
                    return 0;
                }
            }

            if (ctrl && wParam == 'Z') {
                if (annotations_.Undo()) {
                    ::InvalidateRect(hwnd, nullptr, FALSE);
                }
                return 0;
            }
            if (ctrl && wParam == 'Y') {
                if (annotations_.Redo()) {
                    ::InvalidateRect(hwnd, nullptr, FALSE);
                }
                return 0;
            }
            if (ctrl && wParam == 'S') {
                SaveSelectionToFile();
                return 0;
            }
            if (ctrl && wParam == 'C') {
                CommitSelection();
                return 0;
            }
            if (ctrl && wParam == 'P') {
                RequestPin();
                return 0;
            }
            if (ctrl && wParam == 'T') {
                RequestOcr();
                return 0;
            }

            if (wParam == VK_ESCAPE) {
                if (isAuthoring_ || draftAnnotation_.has_value()) {
                    isAuthoring_ = false;
                    draftAnnotation_.reset();
                    ReleaseOverlayCaptureSafely(hwnd);
                    ::InvalidateRect(hwnd, nullptr, FALSE);
                    return 0;
                }
                if (activeTool_ != pikasnip::annotations::AnnotationTool::None) {
                    activeTool_ = pikasnip::annotations::AnnotationTool::None;
                    ::InvalidateRect(hwnd, nullptr, FALSE);
                    return 0;
                }
                Close(false);
                return 0;
            }
            if (wParam == VK_RETURN) {
                if (mode_ == OverlayMode::QuickOcr) {
                    RequestOcr();
                } else {
                    CommitSelection();
                }
                return 0;
            }
            if (wParam == 'C' && !ctrl) {
                CopyCursorHex();
                return 0;
            }
            break;
        }

        case WM_TIMER: {
            if (wParam == kCopiedFeedbackTimerId) {
                ::KillTimer(hwnd, kCopiedFeedbackTimerId);
                copiedHexFeedback_ = false;
                ::InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            if (wParam == kStatusFeedbackTimerId) {
                ::KillTimer(hwnd, kStatusFeedbackTimerId);
                statusFeedbackText_.clear();
                statusFeedbackIsError_ = false;
                ::InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            break;
        }

        case WM_CAPTURECHANGED: {
            if (!handlingButtonUp_ && reinterpret_cast<HWND>(lParam) != hwnd) {
                if (pressedToolbar_) {
                    pressedToolbar_ = false;
                    pressedToolbarAction_ = ToolbarActionKind::None;
                    ::InvalidateRect(hwnd, nullptr, FALSE);
                } else if (isAuthoring_) {
                    isAuthoring_ = false;
                    draftAnnotation_.reset();
                    ::InvalidateRect(hwnd, nullptr, FALSE);
                } else if (controller_.mode() == SelectionMode::DraggingNew) {
                    (void)controller_.CancelSelection();
                    ::InvalidateRect(hwnd, nullptr, FALSE);
                } else if (controller_.mode() == SelectionMode::Selected) {
                    const PointI c = controller_.cursorPos();
                    controller_.OnLeftButtonUp(c.x, c.y);
                    (void)controller_.consumeCommitRequest();
                    ::InvalidateRect(hwnd, nullptr, FALSE);
                }
            }
            return 0;
        }

        case WM_NCDESTROY: {
            ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            hwnd_ = nullptr;
            ReleaseRenderResources();
            break;
        }

        default:
            break;
    }

    return ::DefWindowProcW(hwnd, msg, wParam, lParam);
}

} // namespace pikasnip::overlay

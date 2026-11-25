#pragma once

#include "pikasnip/annotations/AnnotationSession.h"
#include "pikasnip/annotations/AnnotationTypes.h"
#include "pikasnip/capture/CapturedFrame.h"
#include "pikasnip/capture/WindowDetector.h"
#include "pikasnip/overlay/FloatingToolbar.h"
#include "pikasnip/overlay/MagnifierHud.h"
#include "pikasnip/overlay/SelectionController.h"
#include "pikasnip/platform/D2dContext.h"

#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>

#include <filesystem>
#include <functional>
#include <optional>
#include <string>

namespace pikasnip::overlay {

enum class OverlayMode {
    Standard,
    QuickOcr
};

/// Full virtual-screen topmost overlay for frozen capture preview, window hover, region selection,
/// live pixel magnifier/color picker, 8-tool vector/mosaic authoring, and PNG/clipboard export.
class OverlayWindow {
public:
    using ClosedCallback = std::function<void(bool copied)>;
    using SaveFileFn = std::function<std::filesystem::path(
        IWICImagingFactory*,
        const uint8_t*,
        int32_t,
        int32_t,
        int32_t)>;
    using PinCallback = std::function<void(pikasnip::capture::CapturedFrame&&, pikasnip::platform::RectI)>;
    using OcrCallback = std::function<void(pikasnip::capture::CapturedFrame&&)>;

    explicit OverlayWindow(pikasnip::platform::D2dContext& d2d) noexcept;
    ~OverlayWindow();

    OverlayWindow(const OverlayWindow&) = delete;
    OverlayWindow& operator=(const OverlayWindow&) = delete;

    [[nodiscard]] bool Show(
        HINSTANCE hInstance,
        pikasnip::capture::CapturedFrame&& frame,
        pikasnip::capture::WindowDetector&& detector,
        ClosedCallback onClosed = {},
        OverlayMode mode = OverlayMode::Standard);

    void Close(bool copied = false) noexcept;
    void SetSaveFileCallback(SaveFileFn fn);
    void SetPinCallback(PinCallback fn);
    void SetOcrCallback(OcrCallback fn);

    [[nodiscard]] bool isOpen() const noexcept { return hwnd_ != nullptr; }
    [[nodiscard]] HWND hwnd() const noexcept { return hwnd_; }
    [[nodiscard]] const SelectionController& controller() const noexcept { return controller_; }
    [[nodiscard]] const pikasnip::annotations::AnnotationSession& annotations() const noexcept { return annotations_; }
    [[nodiscard]] const FloatingToolbar& toolbar() const noexcept { return toolbar_; }

private:
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    bool EnsureRenderResources();
    void ReleaseRenderResources() noexcept;
    void ReleaseOverlayCaptureSafely(HWND hwnd) noexcept;
    void EnsureToolbarLayout();
    void Render();
    void CommitActiveTextIfAny();
    bool FlattenActiveSelection(
        pikasnip::capture::CapturedFrame& outFlattened,
        pikasnip::platform::RectI& outRegion,
        const wchar_t* failMessage);
    void CommitSelection();
    void SaveSelectionToFile();
    void RequestPin();
    void RequestOcr();
    void ExecuteToolbarAction(const ToolbarHit& hit);
    void CopyCursorHex();
    void UpdateCursorShape(int32_t vx, int32_t vy) const noexcept;
    [[nodiscard]] pikasnip::capture::MonitorInfo FindMonitorAt(int32_t vx, int32_t vy) const noexcept;

    pikasnip::platform::D2dContext& d2d_;
    HWND hwnd_ = nullptr;
    pikasnip::capture::CapturedFrame frame_;
    pikasnip::capture::WindowDetector detector_;
    SelectionController controller_;
    MagnifierHud magnifier_;
    FloatingToolbar toolbar_;
    ClosedCallback onClosed_;
    SaveFileFn saveFileFn_;
    PinCallback pinCallback_;
    OcrCallback ocrCallback_;
    OverlayMode mode_ = OverlayMode::Standard;

    pikasnip::annotations::AnnotationSession annotations_;
    pikasnip::annotations::AnnotationTool activeTool_ = pikasnip::annotations::AnnotationTool::None;
    pikasnip::annotations::AnnotationStyle style_{};
    std::optional<pikasnip::annotations::AnnotationItem> draftAnnotation_;
    std::optional<pikasnip::annotations::TextAnnotation> activeText_;
    PointI draftAnchor_{};
    std::wstring statusFeedbackText_;
    bool statusFeedbackIsError_ = false;

    Microsoft::WRL::ComPtr<ID2D1HwndRenderTarget> renderTarget_;
    Microsoft::WRL::ComPtr<ID2D1Bitmap> desktopBitmap_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> badgeFormat_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> hintFormat_;

    bool copiedHexFeedback_ = false;
    bool handlingButtonUp_ = false;
    bool isAuthoring_ = false;
    bool pressedToolbar_ = false;
    ToolbarActionKind pressedToolbarAction_ = ToolbarActionKind::None;
    bool closing_ = false;
};

} // namespace pikasnip::overlay

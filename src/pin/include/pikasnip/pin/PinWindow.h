#pragma once

#include "pikasnip/annotations/AnnotationSession.h"
#include "pikasnip/annotations/AnnotationTypes.h"
#include "pikasnip/capture/CapturedFrame.h"
#include "pikasnip/platform/D2dContext.h"
#include "pikasnip/platform/Dpi.h"

#include <windows.h>
#include <d2d1_1.h>
#include <dwrite.h>
#include <wrl/client.h>

#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace pikasnip::overlay {
struct ToolbarHit;
}

namespace pikasnip::pin {

class PinToolbarWindow;

struct PinOptions {
    bool alwaysOnTop = true;
    float initialOpacity = 1.0f;
    float initialScale = 1.0f;
    std::optional<POINT> initialPosition = std::nullopt;
};

/// Borderless topmost floating screenshot pin window with zoom, opacity, drag, and post-pin editing tools.
class PinWindow {
public:
    using ClosedCallback = std::function<void(PinWindow*)>;
    using FeedbackCallback = std::function<void(const std::wstring& message, bool isError)>;
    using OcrCallback = std::function<void(pikasnip::capture::CapturedFrame&&)>;

    explicit PinWindow(pikasnip::platform::D2dContext& d2d) noexcept;
    ~PinWindow();

    PinWindow(const PinWindow&) = delete;
    PinWindow& operator=(const PinWindow&) = delete;

    [[nodiscard]] bool Create(
        HINSTANCE hInstance,
        const pikasnip::capture::CapturedFrame& image,
        const PinOptions& options = {},
        ClosedCallback onClosed = {},
        FeedbackCallback onFeedback = {},
        OcrCallback onOcr = {});

    void Close() noexcept;

    bool SetScale(float scale, std::optional<POINT> anchorScreenPt = std::nullopt) noexcept;
    bool SetOpacity(float opacity) noexcept;
    void SetEditMode(bool enabled);

    [[nodiscard]] bool isOpen() const noexcept { return hwnd_ != nullptr; }
    [[nodiscard]] bool isEditMode() const noexcept { return editMode_; }
    [[nodiscard]] HWND hwnd() const noexcept { return hwnd_; }
    [[nodiscard]] HWND toolbarHwnd() const noexcept;
    [[nodiscard]] float scale() const noexcept { return scale_; }
    [[nodiscard]] float opacity() const noexcept { return opacity_; }
    [[nodiscard]] pikasnip::platform::RectI windowRect() const noexcept;
    [[nodiscard]] const pikasnip::annotations::AnnotationSession& annotations() const noexcept { return annotations_; }
    [[nodiscard]] bool BuildFlattenedFrame(pikasnip::capture::CapturedFrame& outFrame) const;

private:
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    bool EnsureRenderResources();
    void ReleaseRenderResources() noexcept;
    void Render();
    void CopyToClipboard(HWND hwnd);
    void SaveToDefaultDir();
    void RequestOcr();
    void ShowContextMenu(POINT screenPt);
    void ExecuteToolbarAction(const pikasnip::overlay::ToolbarHit& hit);
    void SyncToolbarState() noexcept;
    void CommitActiveTextIfAny();
    [[nodiscard]] pikasnip::annotations::PointF ClientPointToImage(int32_t cx, int32_t cy) const noexcept;
    void ShowHudBadge(std::wstring text);

    pikasnip::platform::D2dContext& d2d_;
    HINSTANCE hInstance_ = nullptr;
    HWND hwnd_ = nullptr;
    pikasnip::capture::CapturedFrame image_{};
    PinOptions options_{};
    ClosedCallback onClosed_;
    FeedbackCallback onFeedback_;
    OcrCallback onOcr_;

    std::unique_ptr<PinToolbarWindow> toolbarWindow_;
    bool editMode_ = false;
    pikasnip::annotations::AnnotationTool activeTool_ = pikasnip::annotations::AnnotationTool::None;
    pikasnip::annotations::AnnotationStyle style_{};
    pikasnip::annotations::AnnotationSession annotations_;
    std::optional<pikasnip::annotations::AnnotationItem> draftAnnotation_;
    std::optional<pikasnip::annotations::TextAnnotation> activeText_;
    bool isAuthoring_ = false;

    float scale_ = 1.0f;
    float opacity_ = 1.0f;
    bool dragging_ = false;
    POINT dragOffset_{};
    bool rightPressed_ = false;
    bool middlePressed_ = false;
    bool isHovered_ = false;
    bool trackingMouseLeave_ = false;
    bool closing_ = false;

    std::wstring hudText_;
    bool showHud_ = false;

    Microsoft::WRL::ComPtr<ID2D1HwndRenderTarget> renderTarget_;
    Microsoft::WRL::ComPtr<ID2D1Bitmap> bitmap_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> hudFormat_;
};

} // namespace pikasnip::pin

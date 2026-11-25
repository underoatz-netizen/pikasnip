#pragma once

#include "pikasnip/annotations/AnnotationTypes.h"
#include "pikasnip/overlay/FloatingToolbar.h"
#include "pikasnip/platform/D2dContext.h"
#include "pikasnip/platform/Dpi.h"

#include <windows.h>
#include <d2d1.h>
#include <wrl/client.h>

#include <functional>

namespace pikasnip::pin {

/// Companion borderless topmost toolbar window owned by PinWindow during post-pin editing.
class PinToolbarWindow {
public:
    using ActionCallback = std::function<void(const pikasnip::overlay::ToolbarHit& hit)>;

    explicit PinToolbarWindow(pikasnip::platform::D2dContext& d2d) noexcept;
    ~PinToolbarWindow();

    PinToolbarWindow(const PinToolbarWindow&) = delete;
    PinToolbarWindow& operator=(const PinToolbarWindow&) = delete;

    [[nodiscard]] bool ShowForPin(
        HINSTANCE hInstance,
        HWND ownerPinHwnd,
        pikasnip::platform::RectI pinRect,
        ActionCallback onAction,
        pikasnip::annotations::AnnotationTool activeTool,
        const pikasnip::annotations::AnnotationStyle& activeStyle,
        bool canUndo,
        bool canRedo);

    void SyncPosition(pikasnip::platform::RectI pinRect);
    void SyncState(
        pikasnip::annotations::AnnotationTool activeTool,
        const pikasnip::annotations::AnnotationStyle& activeStyle,
        bool canUndo,
        bool canRedo) noexcept;
    void Hide() noexcept;

    [[nodiscard]] bool isOpen() const noexcept { return hwnd_ != nullptr; }
    [[nodiscard]] HWND hwnd() const noexcept { return hwnd_; }
    [[nodiscard]] const pikasnip::overlay::FloatingToolbar& toolbar() const noexcept { return toolbar_; }

private:
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    bool EnsureRenderResources();
    void ReleaseRenderResources() noexcept;
    void Render();

    pikasnip::platform::D2dContext& d2d_;
    HWND hwnd_ = nullptr;
    HWND ownerPinHwnd_ = nullptr;
    pikasnip::overlay::FloatingToolbar toolbar_{};
    ActionCallback onAction_;

    pikasnip::annotations::AnnotationTool activeTool_ = pikasnip::annotations::AnnotationTool::None;
    pikasnip::annotations::AnnotationStyle activeStyle_{};
    bool canUndo_ = false;
    bool canRedo_ = false;

    bool trackingMouseLeave_ = false;
    bool isPressed_ = false;
    pikasnip::overlay::ToolbarActionKind pressedAction_ = pikasnip::overlay::ToolbarActionKind::None;
    bool closing_ = false;

    Microsoft::WRL::ComPtr<ID2D1HwndRenderTarget> renderTarget_;
};

} // namespace pikasnip::pin

#pragma once

#include "pikasnip/capture/CapturedFrame.h"
#include "pikasnip/capture/WindowDetector.h"
#include "pikasnip/platform/Dpi.h"

#include <array>
#include <cstdint>
#include <string>

namespace pikasnip::overlay {

enum class SelectionMode {
    HoverWindow,
    DraggingNew,
    Selected
};

enum class ResizeHandle {
    None,
    TopLeft,
    Top,
    TopRight,
    Right,
    BottomRight,
    Bottom,
    BottomLeft,
    Left
};

struct PointI {
    int32_t x = 0;
    int32_t y = 0;

    [[nodiscard]] constexpr bool operator==(const PointI&) const noexcept = default;
};

struct PixelColorInfo {
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;
    std::string hex = "#000000";
    std::wstring wideHex = L"#000000";
    bool valid = false;
};

/// Pure testable state machine for snip region selection, window hover, resize/move, and pixel sampling.
class SelectionController {
public:
    void BeginSession(
        const pikasnip::capture::CapturedFrame* frame,
        const pikasnip::capture::WindowDetector* detector) noexcept;

    void Reset() noexcept;

    void OnPointerMove(int32_t vx, int32_t vy);
    void OnLeftButtonDown(int32_t vx, int32_t vy);
    void OnLeftButtonUp(int32_t vx, int32_t vy);

    /// Cancels an active or dragged selection back to HoverWindow.
    /// Returns true if a selection was cancelled, false if already in HoverWindow.
    [[nodiscard]] bool CancelSelection() noexcept;

    /// Returns true once if a stationary single-click inside Selected requested immediate commit.
    [[nodiscard]] bool consumeCommitRequest() noexcept;

    [[nodiscard]] SelectionMode mode() const noexcept { return mode_; }
    [[nodiscard]] bool isDragging() const noexcept { return dragAction_ != DragAction::None; }
    [[nodiscard]] PointI cursorPos() const noexcept { return cursorPos_; }
    [[nodiscard]] pikasnip::platform::RectI virtualBounds() const noexcept { return virtualBounds_; }
    [[nodiscard]] pikasnip::platform::RectI hoveredRect() const noexcept { return hoveredRect_; }
    [[nodiscard]] pikasnip::platform::RectI selectedRect() const noexcept { return selectedRect_; }

    /// Returns selectedRect when DraggingNew/Selected, or hoveredRect when HoverWindow.
    [[nodiscard]] pikasnip::platform::RectI activeRect() const noexcept;

    [[nodiscard]] ResizeHandle HitTestHandle(int32_t vx, int32_t vy, int32_t hitRadius = 6) const noexcept;
    [[nodiscard]] std::array<pikasnip::platform::RectI, 8> GetHandleRects(int32_t halfSize = 4) const noexcept;

    [[nodiscard]] PixelColorInfo GetPixelRgbHex(int32_t vx, int32_t vy) const;

private:
    enum class DragAction {
        None,
        NewRegion,
        MoveSelection,
        ResizeSelection
    };

    [[nodiscard]] PointI ClampPoint(int32_t vx, int32_t vy) const noexcept;
    void UpdateHoveredWindow(int32_t vx, int32_t vy) noexcept;

    const pikasnip::capture::CapturedFrame* frame_ = nullptr;
    const pikasnip::capture::WindowDetector* detector_ = nullptr;
    pikasnip::platform::RectI virtualBounds_{};

    SelectionMode mode_ = SelectionMode::HoverWindow;
    DragAction dragAction_ = DragAction::None;
    ResizeHandle activeHandle_ = ResizeHandle::None;

    PointI cursorPos_{};
    PointI dragAnchor_{};
    pikasnip::platform::RectI hoveredRect_{};
    pikasnip::platform::RectI pressHoveredRect_{};
    pikasnip::platform::RectI selectedRect_{};
    pikasnip::platform::RectI dragStartRect_{};
    bool commitRequested_ = false;
    /// Sticky per-gesture flag: true once the pointer travelled beyond the click
    /// threshold while a drag action was active. Dragging out and back must not
    /// be treated as a stationary click.
    bool dragMovedBeyondThreshold_ = false;
};

} // namespace pikasnip::overlay

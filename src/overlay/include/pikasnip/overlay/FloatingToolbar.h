#pragma once

#include "pikasnip/annotations/AnnotationTypes.h"
#include "pikasnip/platform/Dpi.h"

#include <d2d1.h>
#include <dwrite.h>

#include <cstdint>
#include <vector>

namespace pikasnip::overlay {

enum class ToolbarActionKind {
    None,
    ToolRectangle,
    ToolEllipse,
    ToolArrow,
    ToolFreehandPen,
    ToolHighlighter,
    ToolMosaic,
    ToolStepCounter,
    ToolText,
    ColorRed,
    ColorYellow,
    ColorGreen,
    ColorCyan,
    ColorWhite,
    ColorDark,
    ThicknessThin,
    ThicknessMedium,
    ThicknessThick,
    Undo,
    Redo,
    Pin,
    Ocr,
    Save,
    Copy,
    Cancel
};

struct ToolbarHit {
    ToolbarActionKind kind = ToolbarActionKind::None;
    pikasnip::annotations::AnnotationTool tool = pikasnip::annotations::AnnotationTool::None;
    pikasnip::annotations::ColorRgba color{};
    float thickness = 0.0f;
    bool isColor = false;
    bool isThickness = false;
};

/// Pure layout, hit-testing, and Direct2D renderer for the floating annotation toolbar.
class FloatingToolbar {
public:
    void ComputeLayout(
        pikasnip::platform::RectI selectionRect,
        pikasnip::platform::RectI monitorBounds,
        UINT dpi,
        bool includePinAction = true);

    void Reset() noexcept;

    [[nodiscard]] bool Contains(int32_t vx, int32_t vy) const noexcept;
    [[nodiscard]] ToolbarHit HitTest(int32_t vx, int32_t vy) const noexcept;
    void SetHoveredPoint(int32_t vx, int32_t vy) noexcept;

    [[nodiscard]] pikasnip::platform::RectI bounds() const noexcept { return bounds_; }
    [[nodiscard]] pikasnip::platform::RectI GetButtonRect(ToolbarActionKind kind) const noexcept;

    void Draw(
        ID2D1RenderTarget* rt,
        IDWriteFactory* dwrite,
        float offsetX,
        float offsetY,
        pikasnip::annotations::AnnotationTool activeTool,
        const pikasnip::annotations::AnnotationStyle& activeStyle,
        bool canUndo,
        bool canRedo) const;

private:
    struct ButtonEntry {
        ToolbarHit hit{};
        pikasnip::platform::RectI rect{};
        bool isSeparator = false;
    };

    pikasnip::platform::RectI bounds_{};
    std::vector<ButtonEntry> buttons_;
    ToolbarActionKind hoveredAction_ = ToolbarActionKind::None;
    float scale_ = 1.0f;
};

} // namespace pikasnip::overlay

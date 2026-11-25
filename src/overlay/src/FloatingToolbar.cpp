#include "pikasnip/overlay/FloatingToolbar.h"

#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace pikasnip::overlay {

namespace {

constexpr pikasnip::annotations::ColorRgba kPalette[6] = {
    {239, 68, 68, 255},   // #EF4444
    {250, 204, 21, 255},  // #FACC15
    {34, 197, 94, 255},   // #22C55E
    {56, 189, 248, 255},  // #38BDF8
    {255, 255, 255, 255}, // #FFFFFF
    {17, 24, 39, 255}     // #111827
};

constexpr float kThicknessPresets[3] = {2.0f, 4.0f, 7.0f};

D2D1_COLOR_F HexColor(uint32_t rgb, float alpha = 1.0f) noexcept {
    return D2D1::ColorF(
        static_cast<float>((rgb >> 16) & 0xFF) / 255.0f,
        static_cast<float>((rgb >> 8) & 0xFF) / 255.0f,
        static_cast<float>(rgb & 0xFF) / 255.0f,
        alpha);
}

D2D1_COLOR_F ToD2d(pikasnip::annotations::ColorRgba c) noexcept {
    return D2D1::ColorF(
        static_cast<float>(c.r) / 255.0f,
        static_cast<float>(c.g) / 255.0f,
        static_cast<float>(c.b) / 255.0f,
        static_cast<float>(c.a) / 255.0f);
}

struct ItemSpec {
    ToolbarHit hit{};
    int32_t baseWidth = 28;
    bool isSeparator = false;
};

std::vector<ItemSpec> BuildToolbarSpecs(bool includePinAction) {
    using pikasnip::annotations::AnnotationTool;
    std::vector<ItemSpec> specs;
    specs.reserve(27);

    auto addTool = [&](ToolbarActionKind kind, AnnotationTool tool) {
        ItemSpec s;
        s.hit.kind = kind;
        s.hit.tool = tool;
        s.baseWidth = 26;
        specs.push_back(s);
    };
    auto addSep = [&]() {
        ItemSpec s;
        s.baseWidth = 8;
        s.isSeparator = true;
        specs.push_back(s);
    };
    auto addColor = [&](ToolbarActionKind kind, size_t idx) {
        ItemSpec s;
        s.hit.kind = kind;
        s.hit.color = kPalette[idx];
        s.hit.isColor = true;
        s.baseWidth = 20;
        specs.push_back(s);
    };
    auto addThick = [&](ToolbarActionKind kind, size_t idx) {
        ItemSpec s;
        s.hit.kind = kind;
        s.hit.thickness = kThicknessPresets[idx];
        s.hit.isThickness = true;
        s.baseWidth = 22;
        specs.push_back(s);
    };
    auto addAction = [&](ToolbarActionKind kind) {
        ItemSpec s;
        s.hit.kind = kind;
        s.baseWidth = 26;
        specs.push_back(s);
    };

    addTool(ToolbarActionKind::ToolRectangle, AnnotationTool::Rectangle);
    addTool(ToolbarActionKind::ToolEllipse, AnnotationTool::Ellipse);
    addTool(ToolbarActionKind::ToolArrow, AnnotationTool::Arrow);
    addTool(ToolbarActionKind::ToolFreehandPen, AnnotationTool::FreehandPen);
    addTool(ToolbarActionKind::ToolHighlighter, AnnotationTool::Highlighter);
    addTool(ToolbarActionKind::ToolMosaic, AnnotationTool::Mosaic);
    addTool(ToolbarActionKind::ToolStepCounter, AnnotationTool::StepCounter);
    addTool(ToolbarActionKind::ToolText, AnnotationTool::Text);

    addSep();
    addColor(ToolbarActionKind::ColorRed, 0);
    addColor(ToolbarActionKind::ColorYellow, 1);
    addColor(ToolbarActionKind::ColorGreen, 2);
    addColor(ToolbarActionKind::ColorCyan, 3);
    addColor(ToolbarActionKind::ColorWhite, 4);
    addColor(ToolbarActionKind::ColorDark, 5);

    addSep();
    addThick(ToolbarActionKind::ThicknessThin, 0);
    addThick(ToolbarActionKind::ThicknessMedium, 1);
    addThick(ToolbarActionKind::ThicknessThick, 2);

    addSep();
    addAction(ToolbarActionKind::Undo);
    addAction(ToolbarActionKind::Redo);
    if (includePinAction) {
        addAction(ToolbarActionKind::Pin);
    }
    addAction(ToolbarActionKind::Ocr);
    addAction(ToolbarActionKind::Save);
    addAction(ToolbarActionKind::Copy);
    addAction(ToolbarActionKind::Cancel);

    return specs;
}

void DrawButtonIcon(
    ID2D1RenderTarget* rt,
    IDWriteFactory* dwrite,
    ID2D1SolidColorBrush* brush,
    const ToolbarHit& hit,
    const D2D1_RECT_F& rect,
    float scale,
    bool active,
    bool enabled,
    const pikasnip::annotations::AnnotationStyle& activeStyle) {
    const float cx = (rect.left + rect.right) * 0.5f;
    const float cy = (rect.top + rect.bottom) * 0.5f;
    const float iconR = 6.5f * scale;
    const float strokeW = 1.6f * scale;

    if (hit.isColor) {
        const float swatchR = 6.5f * scale;
        brush->SetColor(ToD2d(hit.color));
        rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), swatchR, swatchR), brush);
        const bool selected = (hit.color == activeStyle.color);
        if (selected) {
            brush->SetColor(HexColor(0xFFFFFF, 1.0f));
            rt->DrawEllipse(
                D2D1::Ellipse(D2D1::Point2F(cx, cy), swatchR + 2.0f * scale, swatchR + 2.0f * scale),
                brush,
                1.5f * scale);
        } else {
            brush->SetColor(HexColor(0x3A3A3A, 1.0f));
            rt->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), swatchR, swatchR), brush, 1.0f);
        }
        return;
    }

    if (hit.isThickness) {
        const bool selected = std::abs(hit.thickness - activeStyle.thickness) < 0.25f;
        const float dotR = std::clamp(hit.thickness * 0.72f + 1.0f, 2.0f, 6.0f) * scale;
        brush->SetColor(selected ? HexColor(0x38BDF8, 1.0f) : HexColor(0xE5E7EB, 1.0f));
        rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), dotR, dotR), brush);
        return;
    }

    D2D1_COLOR_F fg = enabled ? HexColor(0xE5E7EB, 1.0f) : HexColor(0x6B7280, 1.0f);
    if (active) {
        fg = HexColor(0x38BDF8, 1.0f);
    } else if (hit.kind == ToolbarActionKind::Copy) {
        fg = HexColor(0x22C55E, 1.0f);
    } else if (hit.kind == ToolbarActionKind::Cancel) {
        fg = HexColor(0xF87171, 1.0f);
    }
    brush->SetColor(fg);

    switch (hit.kind) {
        case ToolbarActionKind::ToolRectangle:
            rt->DrawRectangle(
                D2D1::RectF(cx - iconR, cy - iconR * 0.8f, cx + iconR, cy + iconR * 0.8f),
                brush,
                strokeW);
            break;

        case ToolbarActionKind::ToolEllipse:
            rt->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), iconR, iconR * 0.85f), brush, strokeW);
            break;

        case ToolbarActionKind::ToolArrow: {
            const float x0 = cx - iconR * 0.85f;
            const float y0 = cy + iconR * 0.85f;
            const float x1 = cx + iconR * 0.85f;
            const float y1 = cy - iconR * 0.85f;
            rt->DrawLine(D2D1::Point2F(x0, y0), D2D1::Point2F(x1, y1), brush, strokeW);
            rt->DrawLine(D2D1::Point2F(x1, y1), D2D1::Point2F(x1 - 5.5f * scale, y1 + 1.0f * scale), brush, strokeW);
            rt->DrawLine(D2D1::Point2F(x1, y1), D2D1::Point2F(x1 - 1.0f * scale, y1 + 5.5f * scale), brush, strokeW);
            break;
        }

        case ToolbarActionKind::ToolFreehandPen: {
            const float r = iconR * 0.9f;
            rt->DrawLine(
                D2D1::Point2F(cx - r, cy + r * 0.6f),
                D2D1::Point2F(cx - r * 0.2f, cy - r * 0.1f),
                brush,
                strokeW);
            rt->DrawLine(
                D2D1::Point2F(cx - r * 0.2f, cy - r * 0.1f),
                D2D1::Point2F(cx + r * 0.2f, cy + r * 0.3f),
                brush,
                strokeW);
            rt->DrawLine(
                D2D1::Point2F(cx + r * 0.2f, cy + r * 0.3f),
                D2D1::Point2F(cx + r, cy - r * 0.7f),
                brush,
                strokeW);
            break;
        }

        case ToolbarActionKind::ToolHighlighter: {
            const float r = iconR * 0.85f;
            rt->DrawLine(
                D2D1::Point2F(cx - r * 0.6f, cy + r * 0.4f),
                D2D1::Point2F(cx + r * 0.7f, cy - r * 0.8f),
                brush,
                strokeW * 2.0f);
            rt->DrawLine(
                D2D1::Point2F(cx - r, cy + r * 0.9f),
                D2D1::Point2F(cx + r, cy + r * 0.9f),
                brush,
                strokeW);
            break;
        }

        case ToolbarActionKind::ToolMosaic: {
            const float cell = 4.6f * scale;
            const float gap = 1.5f * scale;
            const float x0 = cx - cell - gap * 0.5f;
            const float y0 = cy - cell - gap * 0.5f;
            rt->FillRectangle(D2D1::RectF(x0, y0, x0 + cell, y0 + cell), brush);
            rt->FillRectangle(D2D1::RectF(x0 + cell + gap, y0 + cell + gap, x0 + 2 * cell + gap, y0 + 2 * cell + gap), brush);
            brush->SetColor(D2D1::ColorF(fg.r, fg.g, fg.b, 0.4f));
            rt->FillRectangle(D2D1::RectF(x0 + cell + gap, y0, x0 + 2 * cell + gap, y0 + cell), brush);
            rt->FillRectangle(D2D1::RectF(x0, y0 + cell + gap, x0 + cell, y0 + 2 * cell + gap), brush);
            break;
        }

        case ToolbarActionKind::ToolStepCounter: {
            rt->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), iconR, iconR), brush, strokeW);
            if (dwrite != nullptr) {
                Microsoft::WRL::ComPtr<IDWriteTextFormat> fmt;
                if (SUCCEEDED(dwrite->CreateTextFormat(
                        L"Segoe UI",
                        nullptr,
                        DWRITE_FONT_WEIGHT_BOLD,
                        DWRITE_FONT_STYLE_NORMAL,
                        DWRITE_FONT_STRETCH_NORMAL,
                        10.0f * scale,
                        L"",
                        fmt.ReleaseAndGetAddressOf())) &&
                    fmt) {
                    (void)fmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                    (void)fmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                    rt->DrawText(L"1", 1, fmt.Get(), rect, brush);
                }
            }
            break;
        }

        case ToolbarActionKind::ToolText: {
            if (dwrite != nullptr) {
                Microsoft::WRL::ComPtr<IDWriteTextFormat> fmt;
                if (SUCCEEDED(dwrite->CreateTextFormat(
                        L"Segoe UI",
                        nullptr,
                        DWRITE_FONT_WEIGHT_BOLD,
                        DWRITE_FONT_STYLE_NORMAL,
                        DWRITE_FONT_STRETCH_NORMAL,
                        14.0f * scale,
                        L"",
                        fmt.ReleaseAndGetAddressOf())) &&
                    fmt) {
                    (void)fmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                    (void)fmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                    rt->DrawText(L"T", 1, fmt.Get(), rect, brush);
                }
            }
            break;
        }

        case ToolbarActionKind::Undo: {
            const float r = iconR * 0.8f;
            rt->DrawLine(D2D1::Point2F(cx - r, cy - 1.0f * scale), D2D1::Point2F(cx + r * 0.6f, cy - 1.0f * scale), brush, strokeW);
            rt->DrawLine(D2D1::Point2F(cx + r * 0.6f, cy - 1.0f * scale), D2D1::Point2F(cx + r * 0.6f, cy + r * 0.7f), brush, strokeW);
            rt->DrawLine(D2D1::Point2F(cx - r, cy - 1.0f * scale), D2D1::Point2F(cx - r * 0.35f, cy - r * 0.75f), brush, strokeW);
            rt->DrawLine(D2D1::Point2F(cx - r, cy - 1.0f * scale), D2D1::Point2F(cx - r * 0.35f, cy + r * 0.5f), brush, strokeW);
            break;
        }

        case ToolbarActionKind::Redo: {
            const float r = iconR * 0.8f;
            rt->DrawLine(D2D1::Point2F(cx + r, cy - 1.0f * scale), D2D1::Point2F(cx - r * 0.6f, cy - 1.0f * scale), brush, strokeW);
            rt->DrawLine(D2D1::Point2F(cx - r * 0.6f, cy - 1.0f * scale), D2D1::Point2F(cx - r * 0.6f, cy + r * 0.7f), brush, strokeW);
            rt->DrawLine(D2D1::Point2F(cx + r, cy - 1.0f * scale), D2D1::Point2F(cx + r * 0.35f, cy - r * 0.75f), brush, strokeW);
            rt->DrawLine(D2D1::Point2F(cx + r, cy - 1.0f * scale), D2D1::Point2F(cx + r * 0.35f, cy + r * 0.5f), brush, strokeW);
            break;
        }

        case ToolbarActionKind::Pin: {
            // Geometric pushpin/thumbtack icon
            rt->DrawLine(D2D1::Point2F(cx - 4.0f * scale, cy - 5.5f * scale), D2D1::Point2F(cx + 4.0f * scale, cy - 5.5f * scale), brush, strokeW);
            rt->DrawLine(D2D1::Point2F(cx - 2.6f * scale, cy - 5.5f * scale), D2D1::Point2F(cx - 3.0f * scale, cy - 1.0f * scale), brush, strokeW);
            rt->DrawLine(D2D1::Point2F(cx + 2.6f * scale, cy - 5.5f * scale), D2D1::Point2F(cx + 3.0f * scale, cy - 1.0f * scale), brush, strokeW);
            rt->DrawLine(D2D1::Point2F(cx - 4.8f * scale, cy - 0.8f * scale), D2D1::Point2F(cx + 4.8f * scale, cy - 0.8f * scale), brush, strokeW);
            rt->DrawLine(D2D1::Point2F(cx, cy - 0.8f * scale), D2D1::Point2F(cx, cy + 6.5f * scale), brush, strokeW);
            break;
        }

        case ToolbarActionKind::Ocr: {
            // 4-corner text-scanner viewfinder frame with 2 horizontal text lines inside
            const float r = iconR * 0.9f;
            const float c = 3.2f * scale;
            rt->DrawLine(D2D1::Point2F(cx - r, cy - r + c), D2D1::Point2F(cx - r, cy - r), brush, strokeW);
            rt->DrawLine(D2D1::Point2F(cx - r, cy - r), D2D1::Point2F(cx - r + c, cy - r), brush, strokeW);

            rt->DrawLine(D2D1::Point2F(cx + r - c, cy - r), D2D1::Point2F(cx + r, cy - r), brush, strokeW);
            rt->DrawLine(D2D1::Point2F(cx + r, cy - r), D2D1::Point2F(cx + r, cy - r + c), brush, strokeW);

            rt->DrawLine(D2D1::Point2F(cx - r, cy + r - c), D2D1::Point2F(cx - r, cy + r), brush, strokeW);
            rt->DrawLine(D2D1::Point2F(cx - r, cy + r), D2D1::Point2F(cx - r + c, cy + r), brush, strokeW);

            rt->DrawLine(D2D1::Point2F(cx + r - c, cy + r), D2D1::Point2F(cx + r, cy + r), brush, strokeW);
            rt->DrawLine(D2D1::Point2F(cx + r, cy + r), D2D1::Point2F(cx + r, cy + r - c), brush, strokeW);

            rt->DrawLine(D2D1::Point2F(cx - 3.3f * scale, cy - 1.8f * scale), D2D1::Point2F(cx + 3.3f * scale, cy - 1.8f * scale), brush, strokeW * 0.9f);
            rt->DrawLine(D2D1::Point2F(cx - 3.3f * scale, cy + 1.8f * scale), D2D1::Point2F(cx + 2.0f * scale, cy + 1.8f * scale), brush, strokeW * 0.9f);
            break;
        }

        case ToolbarActionKind::Save: {
            const float r = iconR * 0.85f;
            rt->DrawLine(D2D1::Point2F(cx, cy - r), D2D1::Point2F(cx, cy + r * 0.4f), brush, strokeW);
            rt->DrawLine(D2D1::Point2F(cx, cy + r * 0.4f), D2D1::Point2F(cx - 4.0f * scale, cy - 0.5f * scale), brush, strokeW);
            rt->DrawLine(D2D1::Point2F(cx, cy + r * 0.4f), D2D1::Point2F(cx + 4.0f * scale, cy - 0.5f * scale), brush, strokeW);
            rt->DrawLine(D2D1::Point2F(cx - r, cy + r * 0.55f), D2D1::Point2F(cx - r, cy + r * 0.9f), brush, strokeW);
            rt->DrawLine(D2D1::Point2F(cx - r, cy + r * 0.9f), D2D1::Point2F(cx + r, cy + r * 0.9f), brush, strokeW);
            rt->DrawLine(D2D1::Point2F(cx + r, cy + r * 0.9f), D2D1::Point2F(cx + r, cy + r * 0.55f), brush, strokeW);
            break;
        }

        case ToolbarActionKind::Copy: {
            const float r = iconR * 0.85f;
            rt->DrawLine(D2D1::Point2F(cx - r, cy + 0.5f * scale), D2D1::Point2F(cx - 1.5f * scale, cy + r * 0.7f), brush, strokeW * 1.2f);
            rt->DrawLine(D2D1::Point2F(cx - 1.5f * scale, cy + r * 0.7f), D2D1::Point2F(cx + r, cy - r * 0.6f), brush, strokeW * 1.2f);
            break;
        }

        case ToolbarActionKind::Cancel: {
            const float r = iconR * 0.7f;
            rt->DrawLine(D2D1::Point2F(cx - r, cy - r), D2D1::Point2F(cx + r, cy + r), brush, strokeW * 1.15f);
            rt->DrawLine(D2D1::Point2F(cx + r, cy - r), D2D1::Point2F(cx - r, cy + r), brush, strokeW * 1.15f);
            break;
        }

        case ToolbarActionKind::None:
        default:
            break;
    }
}

} // namespace

void FloatingToolbar::Reset() noexcept {
    bounds_ = {};
    buttons_.clear();
    hoveredAction_ = ToolbarActionKind::None;
    scale_ = 1.0f;
}

void FloatingToolbar::ComputeLayout(
    pikasnip::platform::RectI selectionRect,
    pikasnip::platform::RectI monitorBounds,
    UINT dpi,
    bool includePinAction) {
    buttons_.clear();
    scale_ = static_cast<float>(dpi != 0 ? dpi : 96U) / 96.0f;

    const int32_t padX = std::max(4, static_cast<int32_t>(std::lround(6.0f * scale_)));
    const int32_t padY = std::max(4, static_cast<int32_t>(std::lround(4.0f * scale_)));
    const int32_t itemGap = std::max(2, static_cast<int32_t>(std::lround(2.0f * scale_)));
    const int32_t rowH = std::max(24, static_cast<int32_t>(std::lround(28.0f * scale_)));
    const int32_t margin = std::max(4, static_cast<int32_t>(std::lround(6.0f * scale_)));
    const int32_t gapFromSel = std::max(4, static_cast<int32_t>(std::lround(8.0f * scale_)));

    const int32_t maxMonitorW = monitorBounds.isEmpty()
        ? 1920
        : std::max(80, monitorBounds.width() - margin * 2);

    const std::vector<ItemSpec> specs = BuildToolbarSpecs(includePinAction);

    // Flow-wrap relative to (0, 0) so wide desktops get a single horizontal bar,
    // while narrow monitors wrap cleanly inside monitorBounds.
    struct RelItem {
        ToolbarHit hit;
        pikasnip::platform::RectI relRect;
        bool isSeparator = false;
    };
    std::vector<RelItem> relItems;
    relItems.reserve(specs.size());

    int32_t curX = padX;
    int32_t curY = padY;
    int32_t maxRowRight = padX;

    for (const auto& sp : specs) {
        const int32_t w = std::max(6, static_cast<int32_t>(std::lround(static_cast<float>(sp.baseWidth) * scale_)));
        if (curX > padX && (curX + w + padX) > maxMonitorW) {
            if (sp.isSeparator) {
                continue;
            }
            curX = padX;
            curY += rowH + itemGap;
        }
        relItems.push_back(RelItem{
            sp.hit,
            pikasnip::platform::RectI{curX, curY, curX + w, curY + rowH},
            sp.isSeparator
        });
        curX += w + itemGap;
        maxRowRight = std::max(maxRowRight, curX - itemGap);
    }

    const int32_t totalW = maxRowRight + padX;
    const int32_t totalH = curY + rowH + padY;

    int32_t originX = selectionRect.right - totalW;
    int32_t originY = selectionRect.bottom + gapFromSel;

    if (!monitorBounds.isEmpty()) {
        if (originY + totalH > monitorBounds.bottom - margin) {
            const int32_t aboveY = selectionRect.top - gapFromSel - totalH;
            if (aboveY >= monitorBounds.top + margin) {
                originY = aboveY;
            } else {
                originY = selectionRect.bottom - totalH - gapFromSel;
            }
        }

        const int32_t minX = monitorBounds.left + margin;
        const int32_t maxX = std::max(minX, monitorBounds.right - totalW - margin);
        const int32_t minY = monitorBounds.top + margin;
        const int32_t maxY = std::max(minY, monitorBounds.bottom - totalH - margin);
        originX = std::clamp(originX, minX, maxX);
        originY = std::clamp(originY, minY, maxY);
    }

    bounds_ = pikasnip::platform::RectI{originX, originY, originX + totalW, originY + totalH};
    buttons_.reserve(relItems.size());
    for (const auto& ri : relItems) {
        buttons_.push_back(ButtonEntry{
            ri.hit,
            pikasnip::platform::RectI{
                originX + ri.relRect.left,
                originY + ri.relRect.top,
                originX + ri.relRect.right,
                originY + ri.relRect.bottom
            },
            ri.isSeparator
        });
    }
}

bool FloatingToolbar::Contains(int32_t vx, int32_t vy) const noexcept {
    return !bounds_.isEmpty() && bounds_.contains(vx, vy);
}

ToolbarHit FloatingToolbar::HitTest(int32_t vx, int32_t vy) const noexcept {
    if (!Contains(vx, vy)) {
        return {};
    }
    for (const auto& b : buttons_) {
        if (!b.isSeparator && b.rect.contains(vx, vy)) {
            return b.hit;
        }
    }
    return {};
}

void FloatingToolbar::SetHoveredPoint(int32_t vx, int32_t vy) noexcept {
    hoveredAction_ = HitTest(vx, vy).kind;
}

pikasnip::platform::RectI FloatingToolbar::GetButtonRect(ToolbarActionKind kind) const noexcept {
    for (const auto& b : buttons_) {
        if (!b.isSeparator && b.hit.kind == kind) {
            return b.rect;
        }
    }
    return {};
}

void FloatingToolbar::Draw(
    ID2D1RenderTarget* rt,
    IDWriteFactory* dwrite,
    float offsetX,
    float offsetY,
    pikasnip::annotations::AnnotationTool activeTool,
    const pikasnip::annotations::AnnotationStyle& activeStyle,
    bool canUndo,
    bool canRedo) const {
    if (rt == nullptr || bounds_.isEmpty()) {
        return;
    }

    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush;
    if (FAILED(rt->CreateSolidColorBrush(HexColor(0x1E1E1E, 0.95f), brush.ReleaseAndGetAddressOf())) ||
        !brush) {
        return;
    }

    const D2D1_RECT_F outer = D2D1::RectF(
        static_cast<float>(bounds_.left) + offsetX,
        static_cast<float>(bounds_.top) + offsetY,
        static_cast<float>(bounds_.right) + offsetX,
        static_cast<float>(bounds_.bottom) + offsetY);
    const D2D1_ROUNDED_RECT roundedOuter = D2D1::RoundedRect(outer, 7.0f * scale_, 7.0f * scale_);

    brush->SetColor(HexColor(0x1E1E1E, 0.95f));
    rt->FillRoundedRectangle(roundedOuter, brush.Get());

    brush->SetColor(HexColor(0x3A3A3A, 1.0f));
    rt->DrawRoundedRectangle(roundedOuter, brush.Get(), 1.0f);

    for (const auto& b : buttons_) {
        const D2D1_RECT_F r = D2D1::RectF(
            static_cast<float>(b.rect.left) + offsetX,
            static_cast<float>(b.rect.top) + offsetY,
            static_cast<float>(b.rect.right) + offsetX,
            static_cast<float>(b.rect.bottom) + offsetY);

        if (b.isSeparator) {
            const float mx = (r.left + r.right) * 0.5f;
            brush->SetColor(HexColor(0x3A3A3A, 1.0f));
            rt->DrawLine(
                D2D1::Point2F(mx, r.top + 4.0f * scale_),
                D2D1::Point2F(mx, r.bottom - 4.0f * scale_),
                brush.Get(),
                1.0f);
            continue;
        }

        const bool isToolActive =
            (b.hit.tool != pikasnip::annotations::AnnotationTool::None && b.hit.tool == activeTool);
        bool enabled = true;
        if (b.hit.kind == ToolbarActionKind::Undo) {
            enabled = canUndo;
        } else if (b.hit.kind == ToolbarActionKind::Redo) {
            enabled = canRedo;
        }

        const bool hovered = enabled && (hoveredAction_ == b.hit.kind);
        const D2D1_ROUNDED_RECT btnRounded = D2D1::RoundedRect(r, 4.0f * scale_, 4.0f * scale_);

        if (isToolActive) {
            brush->SetColor(HexColor(0x252526, 1.0f));
            rt->FillRoundedRectangle(btnRounded, brush.Get());
            brush->SetColor(HexColor(0x38BDF8, 1.0f));
            rt->DrawRoundedRectangle(btnRounded, brush.Get(), 1.25f);
        } else if (hovered) {
            brush->SetColor(HexColor(0x2D2D30, 1.0f));
            rt->FillRoundedRectangle(btnRounded, brush.Get());
        }

        DrawButtonIcon(
            rt,
            dwrite,
            brush.Get(),
            b.hit,
            r,
            scale_,
            isToolActive,
            enabled,
            activeStyle);
    }
}

} // namespace pikasnip::overlay

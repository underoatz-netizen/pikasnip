#pragma once

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace pikasnip::annotations {

struct ColorRgba {
    uint8_t r = 255;
    uint8_t g = 255;
    uint8_t b = 255;
    uint8_t a = 255;

    [[nodiscard]] constexpr bool operator==(const ColorRgba&) const noexcept = default;
};

struct PointF {
    float x = 0.0f;
    float y = 0.0f;

    [[nodiscard]] constexpr bool operator==(const PointF&) const noexcept = default;
};

struct RectF {
    float left = 0.0f;
    float top = 0.0f;
    float right = 0.0f;
    float bottom = 0.0f;

    [[nodiscard]] constexpr bool operator==(const RectF&) const noexcept = default;
};

struct RectAnnotation {
    RectF bounds{};
    ColorRgba stroke{56, 189, 248, 255};
    float thickness = 3.0f;
    bool filled = false;

    [[nodiscard]] bool operator==(const RectAnnotation&) const = default;
};

struct EllipseAnnotation {
    RectF bounds{};
    ColorRgba stroke{56, 189, 248, 255};
    float thickness = 3.0f;
    bool filled = false;

    [[nodiscard]] bool operator==(const EllipseAnnotation&) const = default;
};

struct ArrowAnnotation {
    PointF start{};
    PointF end{};
    ColorRgba stroke{239, 68, 68, 255};
    float thickness = 3.0f;
    float headSize = 12.0f;

    [[nodiscard]] bool operator==(const ArrowAnnotation&) const = default;
};

struct FreehandPenAnnotation {
    std::vector<PointF> points;
    ColorRgba stroke{239, 68, 68, 255};
    float thickness = 3.0f;

    [[nodiscard]] bool operator==(const FreehandPenAnnotation&) const = default;
};

struct HighlighterAnnotation {
    std::vector<PointF> points;
    ColorRgba color{250, 204, 21, 110};
    float thickness = 16.0f;

    [[nodiscard]] bool operator==(const HighlighterAnnotation&) const = default;
};

struct MosaicAnnotation {
    RectF bounds{};
    int32_t blockSize = 10;

    [[nodiscard]] bool operator==(const MosaicAnnotation&) const = default;
};

struct StepCounterAnnotation {
    PointF center{};
    int32_t number = 1;
    ColorRgba fill{56, 189, 248, 255};
    ColorRgba textColor{255, 255, 255, 255};
    float radius = 14.0f;

    [[nodiscard]] bool operator==(const StepCounterAnnotation&) const = default;
};

struct TextAnnotation {
    PointF origin{};
    std::wstring text;
    ColorRgba color{255, 255, 255, 255};
    float fontSize = 18.0f;

    [[nodiscard]] bool operator==(const TextAnnotation&) const = default;
};

/// Active drawing tool selected in the toolbar/overlay.
enum class AnnotationTool {
    None,
    Rectangle,
    Ellipse,
    Arrow,
    FreehandPen,
    Highlighter,
    Mosaic,
    StepCounter,
    Text,
};

/// Style applied to newly created annotations.
struct AnnotationStyle {
    ColorRgba color{56, 189, 248, 255};
    float thickness = 3.0f;
    float fontSize = 18.0f;
    int32_t mosaicBlockSize = 10;

    [[nodiscard]] constexpr bool operator==(const AnnotationStyle&) const noexcept = default;
};

using AnnotationItem = std::variant<
    RectAnnotation,
    EllipseAnnotation,
    ArrowAnnotation,
    FreehandPenAnnotation,
    HighlighterAnnotation,
    MosaicAnnotation,
    StepCounterAnnotation,
    TextAnnotation>;

} // namespace pikasnip::annotations

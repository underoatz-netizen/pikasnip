#include "pikasnip/annotations/AnnotationRenderer.h"

#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <string>

namespace pikasnip::annotations {

namespace {

[[nodiscard]] RectF NormalizeRectF(RectF r) noexcept {
    const auto [l, rgt] = std::minmax(r.left, r.right);
    const auto [t, b] = std::minmax(r.top, r.bottom);
    return RectF{l, t, rgt, b};
}

[[nodiscard]] D2D1_COLOR_F ToD2dColor(ColorRgba c) noexcept {
    return D2D1::ColorF(
        static_cast<float>(c.r) / 255.0f,
        static_cast<float>(c.g) / 255.0f,
        static_cast<float>(c.b) / 255.0f,
        static_cast<float>(c.a) / 255.0f);
}

[[nodiscard]] pikasnip::platform::RectI ClampMosaicBounds(
    RectF rawBounds,
    pikasnip::platform::RectI bufferBounds) noexcept {
    const RectF norm = NormalizeRectF(rawBounds);
    const pikasnip::platform::RectI r{
        static_cast<int32_t>(std::floor(norm.left)),
        static_cast<int32_t>(std::floor(norm.top)),
        static_cast<int32_t>(std::ceil(norm.right)),
        static_cast<int32_t>(std::ceil(norm.bottom))
    };
    return pikasnip::platform::ClampRectToBounds(r, bufferBounds);
}

[[nodiscard]] std::array<uint8_t, 3> AverageBlockBgr(
    const uint8_t* bgra,
    int32_t strideBytes,
    int32_t localX0,
    int32_t localY0,
    int32_t localX1,
    int32_t localY1) noexcept {
    uint64_t sumB = 0;
    uint64_t sumG = 0;
    uint64_t sumR = 0;
    uint64_t count = 0;

    for (int32_t y = localY0; y < localY1; ++y) {
        const uint8_t* row = bgra + static_cast<size_t>(y) * static_cast<size_t>(strideBytes);
        for (int32_t x = localX0; x < localX1; ++x) {
            const size_t off = static_cast<size_t>(x) * 4U;
            sumB += row[off + 0];
            sumG += row[off + 1];
            sumR += row[off + 2];
            ++count;
        }
    }

    if (count == 0) {
        return {0, 0, 0};
    }
    return {
        static_cast<uint8_t>(sumB / count),
        static_cast<uint8_t>(sumG / count),
        static_cast<uint8_t>(sumR / count)
    };
}

void DrawPolyline(
    ID2D1RenderTarget* rt,
    ID2D1Factory* d2dFactory,
    ID2D1SolidColorBrush* brush,
    ID2D1StrokeStyle* roundStroke,
    const std::vector<PointF>& points,
    ColorRgba color,
    float thickness,
    float offsetX,
    float offsetY) {
    if (points.empty()) {
        return;
    }

    brush->SetColor(ToD2dColor(color));
    const float w = std::max(1.0f, thickness);

    if (points.size() == 1) {
        const float r = w * 0.5f;
        rt->FillEllipse(
            D2D1::Ellipse(D2D1::Point2F(points[0].x + offsetX, points[0].y + offsetY), r, r),
            brush);
        return;
    }

    if (d2dFactory != nullptr) {
        Microsoft::WRL::ComPtr<ID2D1PathGeometry> path;
        if (SUCCEEDED(d2dFactory->CreatePathGeometry(path.ReleaseAndGetAddressOf())) && path) {
            Microsoft::WRL::ComPtr<ID2D1GeometrySink> sink;
            if (SUCCEEDED(path->Open(sink.ReleaseAndGetAddressOf())) && sink) {
                sink->BeginFigure(
                    D2D1::Point2F(points[0].x + offsetX, points[0].y + offsetY),
                    D2D1_FIGURE_BEGIN_HOLLOW);
                for (size_t i = 1; i < points.size(); ++i) {
                    sink->AddLine(D2D1::Point2F(points[i].x + offsetX, points[i].y + offsetY));
                }
                sink->EndFigure(D2D1_FIGURE_END_OPEN);
                if (SUCCEEDED(sink->Close())) {
                    rt->DrawGeometry(path.Get(), brush, w, roundStroke);
                    return;
                }
            }
        }
    }

    for (size_t i = 1; i < points.size(); ++i) {
        rt->DrawLine(
            D2D1::Point2F(points[i - 1].x + offsetX, points[i - 1].y + offsetY),
            D2D1::Point2F(points[i].x + offsetX, points[i].y + offsetY),
            brush,
            w,
            roundStroke);
    }
}

void DrawArrow(
    ID2D1RenderTarget* rt,
    ID2D1Factory* d2dFactory,
    ID2D1SolidColorBrush* brush,
    ID2D1StrokeStyle* roundStroke,
    const ArrowAnnotation& arrow,
    float offsetX,
    float offsetY) {
    const float sx = arrow.start.x + offsetX;
    const float sy = arrow.start.y + offsetY;
    const float ex = arrow.end.x + offsetX;
    const float ey = arrow.end.y + offsetY;
    const float dx = ex - sx;
    const float dy = ey - sy;
    const float len = std::hypot(dx, dy);
    const float strokeW = std::max(1.0f, arrow.thickness);

    brush->SetColor(ToD2dColor(arrow.stroke));
    if (len < 1.0f) {
        const float r = strokeW * 0.6f;
        rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(sx, sy), r, r), brush);
        return;
    }

    const float ux = dx / len;
    const float uy = dy / len;
    const float px = -uy;
    const float py = ux;

    const float headLen = std::min(len * 0.75f, std::max(arrow.headSize, strokeW * 3.2f));
    const float headHalfW = headLen * 0.45f;
    const float shaftEndX = ex - ux * (headLen * 0.65f);
    const float shaftEndY = ey - uy * (headLen * 0.65f);

    rt->DrawLine(
        D2D1::Point2F(sx, sy),
        D2D1::Point2F(shaftEndX, shaftEndY),
        brush,
        strokeW,
        roundStroke);

    const D2D1_POINT_2F tip = D2D1::Point2F(ex, ey);
    const D2D1_POINT_2F baseL = D2D1::Point2F(
        ex - ux * headLen + px * headHalfW,
        ey - uy * headLen + py * headHalfW);
    const D2D1_POINT_2F baseR = D2D1::Point2F(
        ex - ux * headLen - px * headHalfW,
        ey - uy * headLen - py * headHalfW);

    if (d2dFactory != nullptr) {
        Microsoft::WRL::ComPtr<ID2D1PathGeometry> headGeo;
        if (SUCCEEDED(d2dFactory->CreatePathGeometry(headGeo.ReleaseAndGetAddressOf())) && headGeo) {
            Microsoft::WRL::ComPtr<ID2D1GeometrySink> sink;
            if (SUCCEEDED(headGeo->Open(sink.ReleaseAndGetAddressOf())) && sink) {
                sink->BeginFigure(tip, D2D1_FIGURE_BEGIN_FILLED);
                sink->AddLine(baseL);
                sink->AddLine(baseR);
                sink->EndFigure(D2D1_FIGURE_END_CLOSED);
                if (SUCCEEDED(sink->Close())) {
                    rt->FillGeometry(headGeo.Get(), brush);
                }
            }
        }
    }
}

void DrawStepCounter(
    ID2D1RenderTarget* rt,
    IDWriteFactory* dwriteFactory,
    ID2D1SolidColorBrush* brush,
    const StepCounterAnnotation& step,
    float offsetX,
    float offsetY) {
    const float cx = step.center.x + offsetX;
    const float cy = step.center.y + offsetY;
    const float r = std::max(8.0f, step.radius);
    const D2D1_ELLIPSE ellipse = D2D1::Ellipse(D2D1::Point2F(cx, cy), r, r);

    brush->SetColor(ToD2dColor(step.fill));
    rt->FillEllipse(ellipse, brush);

    brush->SetColor(D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f));
    rt->DrawEllipse(ellipse, brush, 1.5f);

    if (dwriteFactory == nullptr) {
        return;
    }

    Microsoft::WRL::ComPtr<IDWriteTextFormat> fmt;
    if (FAILED(dwriteFactory->CreateTextFormat(
            L"Segoe UI",
            nullptr,
            DWRITE_FONT_WEIGHT_BOLD,
            DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL,
            r * 1.15f,
            L"",
            fmt.ReleaseAndGetAddressOf())) ||
        !fmt) {
        return;
    }

    (void)fmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    (void)fmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    const std::wstring label = std::to_wstring(step.number);
    brush->SetColor(ToD2dColor(step.textColor));
    rt->DrawText(
        label.c_str(),
        static_cast<UINT32>(label.size()),
        fmt.Get(),
        D2D1::RectF(cx - r, cy - r, cx + r, cy + r),
        brush);
}

void DrawTextItem(
    ID2D1RenderTarget* rt,
    IDWriteFactory* dwriteFactory,
    ID2D1SolidColorBrush* brush,
    const TextAnnotation& item,
    float offsetX,
    float offsetY,
    bool showCaret) {
    if (dwriteFactory == nullptr) {
        return;
    }
    if (item.text.empty() && !showCaret) {
        return;
    }

    const float fontSize = std::max(8.0f, item.fontSize);
    Microsoft::WRL::ComPtr<IDWriteTextFormat> fmt;
    if (FAILED(dwriteFactory->CreateTextFormat(
            L"Leelawadee UI",
            nullptr,
            DWRITE_FONT_WEIGHT_SEMI_BOLD,
            DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL,
            fontSize,
            L"",
            fmt.ReleaseAndGetAddressOf())) ||
        !fmt) {
        return;
    }

    const float ox = item.origin.x + offsetX;
    const float oy = item.origin.y + offsetY;

    Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
    if (FAILED(dwriteFactory->CreateTextLayout(
            item.text.c_str(),
            static_cast<UINT32>(item.text.size()),
            fmt.Get(),
            4096.0f,
            4096.0f,
            layout.ReleaseAndGetAddressOf())) ||
        !layout) {
        return;
    }

    if (!item.text.empty()) {
        brush->SetColor(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.78f));
        rt->DrawTextLayout(D2D1::Point2F(ox + 1.0f, oy + 1.0f), layout.Get(), brush);

        brush->SetColor(ToD2dColor(item.color));
        rt->DrawTextLayout(D2D1::Point2F(ox, oy), layout.Get(), brush);
    }

    if (showCaret) {
        FLOAT caretX = 0.0f;
        FLOAT caretY = 0.0f;
        DWRITE_HIT_TEST_METRICS metrics{};
        if (item.text.empty() ||
            FAILED(layout->HitTestTextPosition(
                static_cast<UINT32>(item.text.size()),
                FALSE,
                &caretX,
                &caretY,
                &metrics))) {
            caretX = 0.0f;
            caretY = 0.0f;
            metrics.height = fontSize * 1.25f;
        }
        const float caretH = std::max(fontSize, metrics.height);
        brush->SetColor(ToD2dColor(item.color));
        rt->DrawLine(
            D2D1::Point2F(ox + caretX, oy + caretY),
            D2D1::Point2F(ox + caretX, oy + caretY + caretH),
            brush,
            2.0f);
    }
}

void RenderSingleItem(
    ID2D1RenderTarget* rt,
    ID2D1Factory* d2dFactory,
    IDWriteFactory* dwriteFactory,
    ID2D1SolidColorBrush* brush,
    ID2D1StrokeStyle* roundStroke,
    const pikasnip::capture::CapturedFrame* sourceFrame,
    const AnnotationItem& item,
    float offsetX,
    float offsetY,
    bool isActiveTextEdit) {
    std::visit(
        [&](const auto& ann) {
            using T = std::decay_t<decltype(ann)>;
            if constexpr (std::is_same_v<T, MosaicAnnotation>) {
                if (sourceFrame != nullptr) {
                    AnnotationRenderer::DrawMosaicPreview(rt, brush, *sourceFrame, ann, offsetX, offsetY);
                }
            } else if constexpr (std::is_same_v<T, RectAnnotation>) {
                const RectF norm = NormalizeRectF(ann.bounds);
                const D2D1_RECT_F rect = D2D1::RectF(
                    norm.left + offsetX,
                    norm.top + offsetY,
                    norm.right + offsetX,
                    norm.bottom + offsetY);
                brush->SetColor(ToD2dColor(ann.stroke));
                if (ann.filled) {
                    rt->FillRectangle(rect, brush);
                } else {
                    rt->DrawRectangle(rect, brush, std::max(1.0f, ann.thickness));
                }
            } else if constexpr (std::is_same_v<T, EllipseAnnotation>) {
                const RectF norm = NormalizeRectF(ann.bounds);
                const float rx = (norm.right - norm.left) * 0.5f;
                const float ry = (norm.bottom - norm.top) * 0.5f;
                const D2D1_ELLIPSE ellipse = D2D1::Ellipse(
                    D2D1::Point2F(norm.left + rx + offsetX, norm.top + ry + offsetY),
                    rx,
                    ry);
                brush->SetColor(ToD2dColor(ann.stroke));
                if (ann.filled) {
                    rt->FillEllipse(ellipse, brush);
                } else {
                    rt->DrawEllipse(ellipse, brush, std::max(1.0f, ann.thickness));
                }
            } else if constexpr (std::is_same_v<T, ArrowAnnotation>) {
                DrawArrow(rt, d2dFactory, brush, roundStroke, ann, offsetX, offsetY);
            } else if constexpr (std::is_same_v<T, FreehandPenAnnotation>) {
                DrawPolyline(
                    rt,
                    d2dFactory,
                    brush,
                    roundStroke,
                    ann.points,
                    ann.stroke,
                    ann.thickness,
                    offsetX,
                    offsetY);
            } else if constexpr (std::is_same_v<T, HighlighterAnnotation>) {
                DrawPolyline(
                    rt,
                    d2dFactory,
                    brush,
                    roundStroke,
                    ann.points,
                    ann.color,
                    ann.thickness,
                    offsetX,
                    offsetY);
            } else if constexpr (std::is_same_v<T, StepCounterAnnotation>) {
                DrawStepCounter(rt, dwriteFactory, brush, ann, offsetX, offsetY);
            } else if constexpr (std::is_same_v<T, TextAnnotation>) {
                DrawTextItem(rt, dwriteFactory, brush, ann, offsetX, offsetY, isActiveTextEdit);
            }
        },
        item);
}

} // namespace

void AnnotationRenderer::DrawMosaicPreview(
    ID2D1RenderTarget* rt,
    ID2D1SolidColorBrush* brush,
    const pikasnip::capture::CapturedFrame& sourceFrame,
    const MosaicAnnotation& mosaic,
    float offsetX,
    float offsetY) {
    if (rt == nullptr || brush == nullptr || !sourceFrame.isValid()) {
        return;
    }

    const pikasnip::platform::RectI clamped =
        ClampMosaicBounds(mosaic.bounds, sourceFrame.virtualBounds);
    if (clamped.isEmpty()) {
        return;
    }

    const int32_t blockSize = std::max<int32_t>(2, mosaic.blockSize);
    for (int32_t vy = clamped.top; vy < clamped.bottom; vy += blockSize) {
        const int32_t blockBottom = std::min(vy + blockSize, clamped.bottom);
        const int32_t localY0 = vy - sourceFrame.virtualBounds.top;
        const int32_t localY1 = blockBottom - sourceFrame.virtualBounds.top;

        for (int32_t vx = clamped.left; vx < clamped.right; vx += blockSize) {
            const int32_t blockRight = std::min(vx + blockSize, clamped.right);
            const int32_t localX0 = vx - sourceFrame.virtualBounds.left;
            const int32_t localX1 = blockRight - sourceFrame.virtualBounds.left;

            const auto bgr = AverageBlockBgr(
                sourceFrame.bgraPixels.data(),
                sourceFrame.strideBytes,
                localX0,
                localY0,
                localX1,
                localY1);

            brush->SetColor(D2D1::ColorF(
                static_cast<float>(bgr[2]) / 255.0f,
                static_cast<float>(bgr[1]) / 255.0f,
                static_cast<float>(bgr[0]) / 255.0f,
                1.0f));
            rt->FillRectangle(
                D2D1::RectF(
                    static_cast<float>(vx) + offsetX,
                    static_cast<float>(vy) + offsetY,
                    static_cast<float>(blockRight) + offsetX,
                    static_cast<float>(blockBottom) + offsetY),
                brush);
        }
    }
}

void AnnotationRenderer::ApplyMosaicsToBgra(
    uint8_t* bgra,
    int32_t width,
    int32_t height,
    int32_t strideBytes,
    pikasnip::platform::RectI bufferBounds,
    const std::vector<AnnotationItem>& items,
    const pikasnip::capture::CapturedFrame* sourceFrame) noexcept {
    if (bgra == nullptr || width <= 0 || height <= 0 || strideBytes < width * 4 ||
        bufferBounds.isEmpty()) {
        return;
    }

    const bool useSource = (sourceFrame != nullptr && sourceFrame->isValid());
    const pikasnip::platform::RectI sampleBounds =
        useSource ? sourceFrame->virtualBounds : bufferBounds;
    const uint8_t* sampleData = useSource ? sourceFrame->bgraPixels.data() : bgra;
    const int32_t sampleStride = useSource ? sourceFrame->strideBytes : strideBytes;

    for (const auto& item : items) {
        const auto* mosaic = std::get_if<MosaicAnnotation>(&item);
        if (mosaic == nullptr) {
            continue;
        }

        const pikasnip::platform::RectI gridBounds =
            ClampMosaicBounds(mosaic->bounds, sampleBounds);
        if (gridBounds.isEmpty()) {
            continue;
        }

        const int32_t blockSize = std::max<int32_t>(2, mosaic->blockSize);
        for (int32_t vy = gridBounds.top; vy < gridBounds.bottom; vy += blockSize) {
            const int32_t blockBottom = std::min(vy + blockSize, gridBounds.bottom);
            const int32_t sampleY0 = vy - sampleBounds.top;
            const int32_t sampleY1 = blockBottom - sampleBounds.top;

            const int32_t dstY0 = std::clamp(vy, bufferBounds.top, bufferBounds.bottom) - bufferBounds.top;
            const int32_t dstY1 = std::clamp(blockBottom, bufferBounds.top, bufferBounds.bottom) - bufferBounds.top;
            if (dstY0 >= dstY1) {
                continue;
            }

            for (int32_t vx = gridBounds.left; vx < gridBounds.right; vx += blockSize) {
                const int32_t blockRight = std::min(vx + blockSize, gridBounds.right);
                const int32_t dstX0 = std::clamp(vx, bufferBounds.left, bufferBounds.right) - bufferBounds.left;
                const int32_t dstX1 = std::clamp(blockRight, bufferBounds.left, bufferBounds.right) - bufferBounds.left;
                if (dstX0 >= dstX1) {
                    continue;
                }

                const int32_t sampleX0 = vx - sampleBounds.left;
                const int32_t sampleX1 = blockRight - sampleBounds.left;

                const auto bgr = AverageBlockBgr(
                    sampleData,
                    sampleStride,
                    sampleX0,
                    sampleY0,
                    sampleX1,
                    sampleY1);

                for (int32_t y = dstY0; y < dstY1; ++y) {
                    uint8_t* row = bgra + static_cast<size_t>(y) * static_cast<size_t>(strideBytes);
                    for (int32_t x = dstX0; x < dstX1; ++x) {
                        const size_t off = static_cast<size_t>(x) * 4U;
                        row[off + 0] = bgr[0];
                        row[off + 1] = bgr[1];
                        row[off + 2] = bgr[2];
                        row[off + 3] = 255U;
                    }
                }
            }
        }
    }
}

void AnnotationRenderer::RenderVectorAnnotations(
    ID2D1RenderTarget* rt,
    ID2D1Factory* d2dFactory,
    IDWriteFactory* dwriteFactory,
    const pikasnip::capture::CapturedFrame* sourceFrame,
    const std::vector<AnnotationItem>& items,
    const AnnotationItem* activeDraft,
    float offsetX,
    float offsetY,
    const TextAnnotation* activeTextEdit) {
    if (rt == nullptr) {
        return;
    }
    if (items.empty() && activeDraft == nullptr && activeTextEdit == nullptr) {
        return;
    }

    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush;
    if (FAILED(rt->CreateSolidColorBrush(
            D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f),
            brush.ReleaseAndGetAddressOf())) ||
        !brush) {
        return;
    }

    Microsoft::WRL::ComPtr<ID2D1StrokeStyle> roundStroke;
    if (d2dFactory != nullptr) {
        D2D1_STROKE_STYLE_PROPERTIES props{};
        props.startCap = D2D1_CAP_STYLE_ROUND;
        props.endCap = D2D1_CAP_STYLE_ROUND;
        props.dashCap = D2D1_CAP_STYLE_ROUND;
        props.lineJoin = D2D1_LINE_JOIN_ROUND;
        props.miterLimit = 4.0f;
        (void)d2dFactory->CreateStrokeStyle(props, nullptr, 0, roundStroke.ReleaseAndGetAddressOf());
    }

    // Pass 1: Draw all MosaicAnnotation items (and mosaic activeDraft) underneath vector items
    // so live overlay preview matches FlattenRegion's ApplyMosaicsToBgra -> vectors Z-order.
    if (sourceFrame != nullptr) {
        for (const auto& item : items) {
            if (std::holds_alternative<MosaicAnnotation>(item)) {
                RenderSingleItem(
                    rt,
                    d2dFactory,
                    dwriteFactory,
                    brush.Get(),
                    roundStroke.Get(),
                    sourceFrame,
                    item,
                    offsetX,
                    offsetY,
                    false);
            }
        }
        if (activeDraft != nullptr && std::holds_alternative<MosaicAnnotation>(*activeDraft)) {
            RenderSingleItem(
                rt,
                d2dFactory,
                dwriteFactory,
                brush.Get(),
                roundStroke.Get(),
                sourceFrame,
                *activeDraft,
                offsetX,
                offsetY,
                false);
        }
    }

    // Pass 2: Draw all non-mosaic vector annotations on top.
    for (const auto& item : items) {
        if (!std::holds_alternative<MosaicAnnotation>(item)) {
            RenderSingleItem(
                rt,
                d2dFactory,
                dwriteFactory,
                brush.Get(),
                roundStroke.Get(),
                nullptr,
                item,
                offsetX,
                offsetY,
                false);
        }
    }

    if (activeDraft != nullptr && !std::holds_alternative<MosaicAnnotation>(*activeDraft)) {
        RenderSingleItem(
            rt,
            d2dFactory,
            dwriteFactory,
            brush.Get(),
            roundStroke.Get(),
            nullptr,
            *activeDraft,
            offsetX,
            offsetY,
            false);
    }

    if (activeTextEdit != nullptr) {
        DrawTextItem(rt, dwriteFactory, brush.Get(), *activeTextEdit, offsetX, offsetY, true);
    }
}

bool AnnotationRenderer::FlattenRegion(
    pikasnip::platform::D2dContext& d2d,
    const pikasnip::capture::CapturedFrame& sourceFrame,
    pikasnip::platform::RectI region,
    const AnnotationSession& session,
    pikasnip::capture::CapturedFrame& outFlattened) {
    pikasnip::capture::CapturedFrame cropped = sourceFrame.CropBgra(region);
    if (!cropped.isValid()) {
        return false;
    }

    if (session.empty()) {
        outFlattened = std::move(cropped);
        return true;
    }

    if (!d2d.isInitialized() && FAILED(d2d.Initialize())) {
        return false;
    }
    if (d2d.wicFactory() == nullptr || d2d.d2dFactory() == nullptr) {
        return false;
    }

    ApplyMosaicsToBgra(
        cropped.bgraPixels.data(),
        cropped.width,
        cropped.height,
        cropped.strideBytes,
        cropped.virtualBounds,
        session.items(),
        &sourceFrame);

    Microsoft::WRL::ComPtr<IWICBitmap> wicBitmap;
    HRESULT hr = d2d.wicFactory()->CreateBitmapFromMemory(
        static_cast<UINT>(cropped.width),
        static_cast<UINT>(cropped.height),
        GUID_WICPixelFormat32bppPBGRA,
        static_cast<UINT>(cropped.strideBytes),
        static_cast<UINT>(cropped.bgraPixels.size()),
        cropped.bgraPixels.data(),
        wicBitmap.ReleaseAndGetAddressOf());
    if (FAILED(hr) || !wicBitmap) {
        return false;
    }

    const D2D1_RENDER_TARGET_PROPERTIES rtProps = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
        96.0f,
        96.0f);
    Microsoft::WRL::ComPtr<ID2D1RenderTarget> wicRt;
    hr = d2d.d2dFactory()->CreateWicBitmapRenderTarget(
        wicBitmap.Get(),
        rtProps,
        wicRt.ReleaseAndGetAddressOf());
    if (FAILED(hr) || !wicRt) {
        return false;
    }

    wicRt->BeginDraw();
    RenderVectorAnnotations(
        wicRt.Get(),
        d2d.d2dFactory(),
        d2d.dwriteFactory(),
        nullptr,
        session.items(),
        nullptr,
        -static_cast<float>(cropped.virtualBounds.left),
        -static_cast<float>(cropped.virtualBounds.top));
    hr = wicRt->EndDraw();
    if (FAILED(hr)) {
        return false;
    }

    WICRect lockRect{0, 0, cropped.width, cropped.height};
    Microsoft::WRL::ComPtr<IWICBitmapLock> lock;
    hr = wicBitmap->Lock(&lockRect, WICBitmapLockRead, lock.ReleaseAndGetAddressOf());
    if (FAILED(hr) || !lock) {
        return false;
    }

    UINT srcStride = 0;
    UINT bufSize = 0;
    BYTE* srcPtr = nullptr;
    if (FAILED(lock->GetStride(&srcStride)) ||
        FAILED(lock->GetDataPointer(&bufSize, &srcPtr)) ||
        srcPtr == nullptr) {
        return false;
    }

    const size_t copyRowBytes = static_cast<size_t>(cropped.width) * 4U;
    if (static_cast<size_t>(srcStride) < copyRowBytes ||
        static_cast<size_t>(bufSize) < static_cast<size_t>(srcStride) * static_cast<size_t>(cropped.height)) {
        return false;
    }

    for (int32_t y = 0; y < cropped.height; ++y) {
        const uint8_t* srcRow = srcPtr + static_cast<size_t>(y) * static_cast<size_t>(srcStride);
        uint8_t* dstRow = cropped.bgraPixels.data() +
                          static_cast<size_t>(y) * static_cast<size_t>(cropped.strideBytes);
        std::memcpy(dstRow, srcRow, copyRowBytes);
        auto* px = reinterpret_cast<uint32_t*>(dstRow);
        for (int32_t x = 0; x < cropped.width; ++x) {
            px[x] |= 0xFF000000U;
        }
    }

    outFlattened = std::move(cropped);
    return true;
}

} // namespace pikasnip::annotations

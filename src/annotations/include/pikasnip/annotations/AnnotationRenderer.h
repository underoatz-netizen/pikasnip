#pragma once

#include "pikasnip/annotations/AnnotationSession.h"
#include "pikasnip/annotations/AnnotationTypes.h"
#include "pikasnip/capture/CapturedFrame.h"
#include "pikasnip/platform/D2dContext.h"
#include "pikasnip/platform/Dpi.h"

#include <d2d1.h>
#include <dwrite.h>

#include <cstdint>
#include <vector>

namespace pikasnip::annotations {

class AnnotationRenderer {
public:
    static void DrawMosaicPreview(
        ID2D1RenderTarget* rt,
        ID2D1SolidColorBrush* brush,
        const pikasnip::capture::CapturedFrame& sourceFrame,
        const MosaicAnnotation& mosaic,
        float offsetX,
        float offsetY);

    static void ApplyMosaicsToBgra(
        uint8_t* bgra,
        int32_t width,
        int32_t height,
        int32_t strideBytes,
        pikasnip::platform::RectI bufferBounds,
        const std::vector<AnnotationItem>& items,
        const pikasnip::capture::CapturedFrame* sourceFrame = nullptr) noexcept;

    static void RenderVectorAnnotations(
        ID2D1RenderTarget* rt,
        ID2D1Factory* d2dFactory,
        IDWriteFactory* dwriteFactory,
        const pikasnip::capture::CapturedFrame* sourceFrame,
        const std::vector<AnnotationItem>& items,
        const AnnotationItem* activeDraft,
        float offsetX,
        float offsetY,
        const TextAnnotation* activeTextEdit = nullptr);

    [[nodiscard]] static bool FlattenRegion(
        pikasnip::platform::D2dContext& d2d,
        const pikasnip::capture::CapturedFrame& sourceFrame,
        pikasnip::platform::RectI region,
        const AnnotationSession& session,
        pikasnip::capture::CapturedFrame& outFlattened);
};

} // namespace pikasnip::annotations

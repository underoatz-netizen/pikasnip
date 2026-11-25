#include "pikasnip/capture/CapturedFrame.h"

#include <cstring>

namespace pikasnip::capture {

bool CapturedFrame::isValid() const noexcept {
    if (width <= 0 || height <= 0 || strideBytes < width * 4) {
        return false;
    }
    const size_t required = static_cast<size_t>(strideBytes) * static_cast<size_t>(height);
    return bgraPixels.size() >= required;
}

std::array<uint8_t, 4> CapturedFrame::GetPixelBgra(int32_t vx, int32_t vy) const noexcept {
    if (!isValid() || !virtualBounds.contains(vx, vy)) {
        return {0, 0, 0, 0};
    }
    const int32_t localX = vx - virtualBounds.left;
    const int32_t localY = vy - virtualBounds.top;
    if (localX < 0 || localX >= width || localY < 0 || localY >= height) {
        return {0, 0, 0, 0};
    }
    const size_t offset =
        static_cast<size_t>(localY) * static_cast<size_t>(strideBytes) +
        static_cast<size_t>(localX) * 4U;
    return {
        bgraPixels[offset + 0],
        bgraPixels[offset + 1],
        bgraPixels[offset + 2],
        bgraPixels[offset + 3]
    };
}

CapturedFrame CapturedFrame::CropBgra(pikasnip::platform::RectI region) const {
    CapturedFrame cropped;
    if (!isValid()) {
        return cropped;
    }
    const pikasnip::platform::RectI clamped =
        pikasnip::platform::ClampRectToBounds(region, virtualBounds);
    if (clamped.isEmpty()) {
        return cropped;
    }

    const int32_t cropW = clamped.width();
    const int32_t cropH = clamped.height();
    const int32_t cropStride = cropW * 4;
    const int32_t srcOffsetX = clamped.left - virtualBounds.left;
    const int32_t srcOffsetY = clamped.top - virtualBounds.top;

    cropped.virtualBounds = clamped;
    cropped.width = cropW;
    cropped.height = cropH;
    cropped.strideBytes = cropStride;
    cropped.captureMs = captureMs;
    cropped.usedDxgi = usedDxgi;
    cropped.bgraPixels.resize(static_cast<size_t>(cropStride) * static_cast<size_t>(cropH));

    for (int32_t y = 0; y < cropH; ++y) {
        const size_t srcOffset =
            static_cast<size_t>(srcOffsetY + y) * static_cast<size_t>(strideBytes) +
            static_cast<size_t>(srcOffsetX) * 4U;
        const size_t dstOffset =
            static_cast<size_t>(y) * static_cast<size_t>(cropStride);
        std::memcpy(
            cropped.bgraPixels.data() + dstOffset,
            bgraPixels.data() + srcOffset,
            static_cast<size_t>(cropStride));
    }

    return cropped;
}

} // namespace pikasnip::capture

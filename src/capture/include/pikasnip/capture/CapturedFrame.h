#pragma once

#include "pikasnip/platform/Dpi.h"

#include <windows.h>

#include <array>
#include <cstdint>
#include <vector>

namespace pikasnip::capture {

struct MonitorInfo {
    HMONITOR handle = nullptr;
    pikasnip::platform::RectI bounds{};
    UINT dpi = 96;
    bool isPrimary = false;
};

struct CapturedFrame {
    pikasnip::platform::RectI virtualBounds{};
    int32_t width = 0;
    int32_t height = 0;
    int32_t strideBytes = 0;
    std::vector<uint8_t> bgraPixels;
    std::vector<MonitorInfo> monitors;
    double captureMs = 0.0;
    bool usedDxgi = false;

    [[nodiscard]] bool isValid() const noexcept;

    /// Returns {B, G, R, A} at virtual screen coordinate (vx, vy), or {0,0,0,0} if out of bounds.
    [[nodiscard]] std::array<uint8_t, 4> GetPixelBgra(int32_t vx, int32_t vy) const noexcept;

    /// Crops the captured frame to the specified virtual-screen region (clamped to virtualBounds).
    [[nodiscard]] CapturedFrame CropBgra(pikasnip::platform::RectI region) const;
};

} // namespace pikasnip::capture

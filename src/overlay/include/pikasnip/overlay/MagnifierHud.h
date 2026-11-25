#pragma once

#include "pikasnip/capture/CapturedFrame.h"
#include "pikasnip/overlay/SelectionController.h"
#include "pikasnip/platform/Dpi.h"

#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>

#include <cstdint>

namespace pikasnip::overlay {

struct MagnifierHudState {
    PointI cursorPos{};
    pikasnip::platform::RectI activeRect{};
    pikasnip::platform::RectI monitorBounds{};
    PixelColorInfo pixelColor{};
    UINT dpi = 96;
    bool copiedFeedback = false;
};

/// Renders the live pixel-grid magnifier louver and dark-charcoal RGB/HEX HUD using Direct2D.
class MagnifierHud {
public:
    void ResetResources() noexcept;

    void Draw(
        ID2D1RenderTarget* rt,
        IDWriteFactory* dwrite,
        const pikasnip::capture::CapturedFrame& frame,
        const MagnifierHudState& state);

private:
    void EnsureResources(ID2D1RenderTarget* rt, IDWriteFactory* dwrite);

    Microsoft::WRL::ComPtr<ID2D1Bitmap> patchBitmap_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> monoFormat_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> uiFormat_;
    UINT cachedDpi_ = 0;
};

} // namespace pikasnip::overlay

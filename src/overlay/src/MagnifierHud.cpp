#include "pikasnip/overlay/MagnifierHud.h"

#include <algorithm>
#include <array>
#include <cstdio>

namespace pikasnip::overlay {

namespace {

constexpr int32_t kPatchSize = 15;
constexpr int32_t kPatchRadius = kPatchSize / 2;

D2D1_COLOR_F ColorFromHex(uint32_t rgb, float alpha = 1.0f) noexcept {
    return D2D1::ColorF(
        static_cast<float>((rgb >> 16) & 0xFF) / 255.0f,
        static_cast<float>((rgb >> 8) & 0xFF) / 255.0f,
        static_cast<float>(rgb & 0xFF) / 255.0f,
        alpha);
}

} // namespace

void MagnifierHud::ResetResources() noexcept {
    patchBitmap_.Reset();
    brush_.Reset();
    monoFormat_.Reset();
    uiFormat_.Reset();
    cachedDpi_ = 0;
}

void MagnifierHud::EnsureResources(ID2D1RenderTarget* rt, IDWriteFactory* dwrite) {
    if (!patchBitmap_) {
        D2D1_BITMAP_PROPERTIES props = D2D1::BitmapProperties(
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE));
        (void)rt->CreateBitmap(
            D2D1::SizeU(kPatchSize, kPatchSize),
            nullptr,
            0,
            &props,
            patchBitmap_.ReleaseAndGetAddressOf());
    }
    if (!brush_) {
        (void)rt->CreateSolidColorBrush(ColorFromHex(0xFFFFFF), brush_.ReleaseAndGetAddressOf());
    }
    if ((!monoFormat_ || !uiFormat_) && dwrite != nullptr) {
        (void)dwrite->CreateTextFormat(
            L"Consolas",
            nullptr,
            DWRITE_FONT_WEIGHT_SEMI_BOLD,
            DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL,
            12.0f,
            L"en-US",
            monoFormat_.ReleaseAndGetAddressOf());

        (void)dwrite->CreateTextFormat(
            L"Segoe UI",
            nullptr,
            DWRITE_FONT_WEIGHT_NORMAL,
            DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL,
            11.5f,
            L"en-US",
            uiFormat_.ReleaseAndGetAddressOf());
    }
}

void MagnifierHud::Draw(
    ID2D1RenderTarget* rt,
    IDWriteFactory* dwrite,
    const pikasnip::capture::CapturedFrame& frame,
    const MagnifierHudState& state) {
    if (rt == nullptr || !frame.isValid()) {
        return;
    }

    EnsureResources(rt, dwrite);
    if (!patchBitmap_ || !brush_) {
        return;
    }

    const float scale = static_cast<float>(state.dpi != 0 ? state.dpi : 96U) / 96.0f;
    if (cachedDpi_ != state.dpi && dwrite != nullptr) {
        cachedDpi_ = state.dpi;
        monoFormat_.Reset();
        uiFormat_.Reset();
        (void)dwrite->CreateTextFormat(
            L"Consolas",
            nullptr,
            DWRITE_FONT_WEIGHT_SEMI_BOLD,
            DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL,
            12.0f * scale,
            L"en-US",
            monoFormat_.ReleaseAndGetAddressOf());
        (void)dwrite->CreateTextFormat(
            L"Segoe UI",
            nullptr,
            DWRITE_FONT_WEIGHT_NORMAL,
            DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL,
            11.5f * scale,
            L"en-US",
            uiFormat_.ReleaseAndGetAddressOf());
    }

    std::array<uint8_t, kPatchSize * kPatchSize * 4> patchPixels{};
    for (int32_t py = 0; py < kPatchSize; ++py) {
        const int32_t vy = state.cursorPos.y + (py - kPatchRadius);
        for (int32_t px = 0; px < kPatchSize; ++px) {
            const int32_t vx = state.cursorPos.x + (px - kPatchRadius);
            const size_t idx = (static_cast<size_t>(py) * kPatchSize + static_cast<size_t>(px)) * 4U;
            if (frame.virtualBounds.contains(vx, vy)) {
                const auto bgra = frame.GetPixelBgra(vx, vy);
                patchPixels[idx + 0] = bgra[0];
                patchPixels[idx + 1] = bgra[1];
                patchPixels[idx + 2] = bgra[2];
                patchPixels[idx + 3] = 255;
            } else {
                patchPixels[idx + 0] = 0x1E;
                patchPixels[idx + 1] = 0x1E;
                patchPixels[idx + 2] = 0x1E;
                patchPixels[idx + 3] = 255;
            }
        }
    }

    const D2D1_RECT_U dstRect = D2D1::RectU(0, 0, kPatchSize, kPatchSize);
    (void)patchBitmap_->CopyFromMemory(&dstRect, patchPixels.data(), kPatchSize * 4);

    const float zoomBoxW = 150.0f * scale;
    const float zoomBoxH = 120.0f * scale;
    const float infoH = 68.0f * scale;
    const float totalW = zoomBoxW;
    const float totalH = zoomBoxH + infoH;
    const float offset = 20.0f * scale;
    const float margin = 8.0f * scale;

    const pikasnip::platform::RectI bounds = state.monitorBounds.isEmpty()
        ? frame.virtualBounds
        : state.monitorBounds;

    const float localCursorX = static_cast<float>(state.cursorPos.x - frame.virtualBounds.left);
    const float localCursorY = static_cast<float>(state.cursorPos.y - frame.virtualBounds.top);
    const float boundLeft = static_cast<float>(bounds.left - frame.virtualBounds.left);
    const float boundTop = static_cast<float>(bounds.top - frame.virtualBounds.top);
    const float boundRight = static_cast<float>(bounds.right - frame.virtualBounds.left);
    const float boundBottom = static_cast<float>(bounds.bottom - frame.virtualBounds.top);

    float hudX = localCursorX + offset;
    float hudY = localCursorY + offset;
    if (hudX + totalW + margin > boundRight) {
        hudX = localCursorX - offset - totalW;
    }
    if (hudY + totalH + margin > boundBottom) {
        hudY = localCursorY - offset - totalH;
    }
    hudX = std::clamp(hudX, boundLeft + margin, std::max(boundLeft + margin, boundRight - totalW - margin));
    hudY = std::clamp(hudY, boundTop + margin, std::max(boundTop + margin, boundBottom - totalH - margin));

    const D2D1_RECT_F outerRect = D2D1::RectF(hudX, hudY, hudX + totalW, hudY + totalH);
    const D2D1_RECT_F zoomRect = D2D1::RectF(hudX + 1.0f, hudY + 1.0f, hudX + zoomBoxW - 1.0f, hudY + zoomBoxH);
    const D2D1_RECT_F infoRect = D2D1::RectF(hudX + 1.0f, hudY + zoomBoxH, hudX + zoomBoxW - 1.0f, hudY + totalH - 1.0f);

    // Outer container background & 1px border
    brush_->SetColor(ColorFromHex(0x1E1E1E, 0.96f));
    rt->FillRectangle(outerRect, brush_.Get());

    // Zoomed pixel bitmap (nearest-neighbor for razor-sharp pixels)
    rt->DrawBitmap(
        patchBitmap_.Get(),
        zoomRect,
        1.0f,
        D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR);

    // Subtle pixel grid + cyan center crosshair
    const float cellW = (zoomRect.right - zoomRect.left) / static_cast<float>(kPatchSize);
    const float cellH = (zoomRect.bottom - zoomRect.top) / static_cast<float>(kPatchSize);

    brush_->SetColor(ColorFromHex(0xFFFFFF, 0.08f));
    for (int32_t i = 1; i < kPatchSize; ++i) {
        const float gx = zoomRect.left + static_cast<float>(i) * cellW;
        const float gy = zoomRect.top + static_cast<float>(i) * cellH;
        rt->DrawLine(D2D1::Point2F(gx, zoomRect.top), D2D1::Point2F(gx, zoomRect.bottom), brush_.Get(), 1.0f);
        rt->DrawLine(D2D1::Point2F(zoomRect.left, gy), D2D1::Point2F(zoomRect.right, gy), brush_.Get(), 1.0f);
    }

    const D2D1_RECT_F centerPixelRect = D2D1::RectF(
        zoomRect.left + static_cast<float>(kPatchRadius) * cellW,
        zoomRect.top + static_cast<float>(kPatchRadius) * cellH,
        zoomRect.left + static_cast<float>(kPatchRadius + 1) * cellW,
        zoomRect.top + static_cast<float>(kPatchRadius + 1) * cellH);
    brush_->SetColor(ColorFromHex(0x38BDF8, 1.0f));
    rt->DrawRectangle(centerPixelRect, brush_.Get(), std::max(1.5f, 1.5f * scale));

    // Info panel background
    brush_->SetColor(ColorFromHex(0x252526, 0.98f));
    rt->FillRectangle(infoRect, brush_.Get());

    // Divider line & outer 1px #3A3A3A border
    brush_->SetColor(ColorFromHex(0x3A3A3A, 1.0f));
    rt->DrawLine(
        D2D1::Point2F(hudX, hudY + zoomBoxH),
        D2D1::Point2F(hudX + totalW, hudY + zoomBoxH),
        brush_.Get(),
        1.0f);
    rt->DrawRectangle(outerRect, brush_.Get(), 1.0f);

    const float padX = 8.0f * scale;
    const float row1Y = infoRect.top + 5.0f * scale;
    const float row2Y = infoRect.top + 25.0f * scale;
    const float row3Y = infoRect.top + 46.0f * scale;

    if (monoFormat_) {
        wchar_t line1[64]{};
        std::swprintf(
            line1,
            std::size(line1),
            L"%d,%d  %dx%d",
            state.cursorPos.x,
            state.cursorPos.y,
            std::max(0, state.activeRect.width()),
            std::max(0, state.activeRect.height()));

        brush_->SetColor(ColorFromHex(0xE5E7EB, 1.0f));
        rt->DrawText(
            line1,
            static_cast<UINT32>(std::wcslen(line1)),
            monoFormat_.Get(),
            D2D1::RectF(infoRect.left + padX, row1Y, infoRect.right - padX, row2Y),
            brush_.Get());

        // Color swatch
        const float swatchSize = 11.0f * scale;
        const D2D1_RECT_F swatchRect = D2D1::RectF(
            infoRect.left + padX,
            row2Y + 2.0f * scale,
            infoRect.left + padX + swatchSize,
            row2Y + 2.0f * scale + swatchSize);
        brush_->SetColor(D2D1::ColorF(
            static_cast<float>(state.pixelColor.r) / 255.0f,
            static_cast<float>(state.pixelColor.g) / 255.0f,
            static_cast<float>(state.pixelColor.b) / 255.0f,
            1.0f));
        rt->FillRectangle(swatchRect, brush_.Get());
        brush_->SetColor(ColorFromHex(0x6B7280, 1.0f));
        rt->DrawRectangle(swatchRect, brush_.Get(), 1.0f);

        wchar_t line2[64]{};
        std::swprintf(
            line2,
            std::size(line2),
            L"%s  %u,%u,%u",
            state.pixelColor.wideHex.c_str(),
            static_cast<unsigned int>(state.pixelColor.r),
            static_cast<unsigned int>(state.pixelColor.g),
            static_cast<unsigned int>(state.pixelColor.b));

        brush_->SetColor(ColorFromHex(0xF9FAFB, 1.0f));
        rt->DrawText(
            line2,
            static_cast<UINT32>(std::wcslen(line2)),
            monoFormat_.Get(),
            D2D1::RectF(swatchRect.right + 6.0f * scale, row2Y, infoRect.right - padX, row3Y),
            brush_.Get());
    }

    if (uiFormat_) {
        const wchar_t* hintText = state.copiedFeedback ? L"Copied HEX!" : L"[C] Copy HEX";
        brush_->SetColor(state.copiedFeedback ? ColorFromHex(0x38BDF8, 1.0f) : ColorFromHex(0x9CA3AF, 1.0f));
        rt->DrawText(
            hintText,
            static_cast<UINT32>(std::wcslen(hintText)),
            uiFormat_.Get(),
            D2D1::RectF(infoRect.left + padX, row3Y, infoRect.right - padX, infoRect.bottom - 2.0f * scale),
            brush_.Get());
    }
}

} // namespace pikasnip::overlay

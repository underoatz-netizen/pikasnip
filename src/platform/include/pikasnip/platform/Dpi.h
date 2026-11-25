#pragma once

#include <windows.h>

#include <cstdint>

namespace pikasnip::platform {

struct RectI {
    int32_t left = 0;
    int32_t top = 0;
    int32_t right = 0;
    int32_t bottom = 0;

    [[nodiscard]] constexpr int32_t width() const noexcept { return right - left; }
    [[nodiscard]] constexpr int32_t height() const noexcept { return bottom - top; }
    [[nodiscard]] constexpr bool isEmpty() const noexcept { return width() <= 0 || height() <= 0; }
    [[nodiscard]] constexpr bool contains(int32_t x, int32_t y) const noexcept {
        return x >= left && x < right && y >= top && y < bottom;
    }

    [[nodiscard]] constexpr RECT toWin32() const noexcept {
        return RECT{left, top, right, bottom};
    }

    [[nodiscard]] static constexpr RectI fromWin32(const RECT& r) noexcept {
        return RectI{r.left, r.top, r.right, r.bottom};
    }

    [[nodiscard]] constexpr bool operator==(const RectI&) const noexcept = default;
};

[[nodiscard]] RectI GetVirtualScreenRect() noexcept;
[[nodiscard]] UINT GetWindowDpi(HWND hwnd) noexcept;
[[nodiscard]] int32_t ScaleForDpi(int32_t valueAt96Dpi, UINT dpi) noexcept;
[[nodiscard]] float ScaleForDpi(float valueAt96Dpi, UINT dpi) noexcept;
[[nodiscard]] RectI ClampRectToBounds(RectI rect, RectI bounds) noexcept;
[[nodiscard]] RectI NormalizeRect(int32_t x1, int32_t y1, int32_t x2, int32_t y2) noexcept;

} // namespace pikasnip::platform

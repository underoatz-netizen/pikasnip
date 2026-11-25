#include "pikasnip/platform/Dpi.h"

#include <algorithm>

namespace pikasnip::platform {

RectI GetVirtualScreenRect() noexcept {
    const int32_t x = ::GetSystemMetrics(SM_XVIRTUALSCREEN);
    const int32_t y = ::GetSystemMetrics(SM_YVIRTUALSCREEN);
    const int32_t cx = ::GetSystemMetrics(SM_CXVIRTUALSCREEN);
    const int32_t cy = ::GetSystemMetrics(SM_CYVIRTUALSCREEN);
    return RectI{x, y, x + cx, y + cy};
}

UINT GetWindowDpi(HWND hwnd) noexcept {
    if (hwnd != nullptr) {
        const UINT dpi = ::GetDpiForWindow(hwnd);
        if (dpi != 0) {
            return dpi;
        }
    }
    const UINT sysDpi = ::GetDpiForSystem();
    return sysDpi != 0 ? sysDpi : 96U;
}

int32_t ScaleForDpi(int32_t valueAt96Dpi, UINT dpi) noexcept {
    const UINT effectiveDpi = dpi != 0 ? dpi : 96U;
    return ::MulDiv(valueAt96Dpi, static_cast<int>(effectiveDpi), 96);
}

float ScaleForDpi(float valueAt96Dpi, UINT dpi) noexcept {
    const UINT effectiveDpi = dpi != 0 ? dpi : 96U;
    return valueAt96Dpi * (static_cast<float>(effectiveDpi) / 96.0f);
}

RectI ClampRectToBounds(RectI rect, RectI bounds) noexcept {
    if (bounds.isEmpty()) {
        return RectI{};
    }
    const int32_t left = std::clamp(rect.left, bounds.left, bounds.right);
    const int32_t top = std::clamp(rect.top, bounds.top, bounds.bottom);
    const int32_t right = std::clamp(rect.right, left, bounds.right);
    const int32_t bottom = std::clamp(rect.bottom, top, bounds.bottom);
    return RectI{left, top, right, bottom};
}

RectI NormalizeRect(int32_t x1, int32_t y1, int32_t x2, int32_t y2) noexcept {
    const auto [minX, maxX] = std::minmax(x1, x2);
    const auto [minY, maxY] = std::minmax(y1, y2);
    return RectI{minX, minY, maxX, maxY};
}

} // namespace pikasnip::platform

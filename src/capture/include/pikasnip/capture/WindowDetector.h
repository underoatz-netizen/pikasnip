#pragma once

#include "pikasnip/platform/Dpi.h"

#include <windows.h>

#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace pikasnip::capture {

struct DetectedWindow {
    HWND hwnd = nullptr;
    pikasnip::platform::RectI bounds{};
};

/// Snapshots visible, non-minimized, non-cloaked top-level windows in Z-order.
class WindowDetector {
public:
    WindowDetector() noexcept = default;
    explicit WindowDetector(std::vector<DetectedWindow> windows) noexcept
        : windows_(std::move(windows)) {}

    void Refresh(HWND excludeHwnd = nullptr);

    [[nodiscard]] std::optional<DetectedWindow> HitTestWindow(int32_t vx, int32_t vy) const noexcept;
    [[nodiscard]] const std::vector<DetectedWindow>& windows() const noexcept { return windows_; }

private:
    std::vector<DetectedWindow> windows_;
};

} // namespace pikasnip::capture

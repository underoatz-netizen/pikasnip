#include "pikasnip/capture/WindowDetector.h"

#include <dwmapi.h>

#include <algorithm>
#include <utility>

namespace pikasnip::capture {

namespace {

/// DWM returns physical pixels only for a DPI-aware thread; capture is
/// physical-pixel space, so switch while sampling bounds.
class ScopedDpiContext {
public:
    explicit ScopedDpiContext(DPI_AWARENESS_CONTEXT context) noexcept
        : previous_(::SetThreadDpiAwarenessContext(context)) {}

    ~ScopedDpiContext() {
        if (previous_ != nullptr) {
            ::SetThreadDpiAwarenessContext(previous_);
        }
    }

    ScopedDpiContext(const ScopedDpiContext&) = delete;
    ScopedDpiContext& operator=(const ScopedDpiContext&) = delete;

private:
    DPI_AWARENESS_CONTEXT previous_ = nullptr;
};

bool IsCloaked(HWND hwnd) noexcept {
    DWORD cloaked = 0;
    const HRESULT hr = ::DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
    return SUCCEEDED(hr) && cloaked != 0;
}

pikasnip::platform::RectI QueryWindowBounds(HWND hwnd) noexcept {
    RECT frameBounds{};
    if (SUCCEEDED(::DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &frameBounds,
                                          sizeof(frameBounds)))) {
        return pikasnip::platform::RectI::fromWin32(frameBounds);
    }

    RECT windowBounds{};
    if (::GetWindowRect(hwnd, &windowBounds) != FALSE) {
        return pikasnip::platform::RectI::fromWin32(windowBounds);
    }
    return {};
}

struct EnumContext {
    HWND excludeHwnd = nullptr;
    std::vector<DetectedWindow>* windows = nullptr;
};

BOOL CALLBACK EnumWindowProc(HWND hwnd, LPARAM param) {
    auto* context = reinterpret_cast<EnumContext*>(param);
    if (context == nullptr || context->windows == nullptr) {
        return FALSE;
    }

    if (hwnd == context->excludeHwnd) {
        return TRUE;
    }
    if (::IsWindowVisible(hwnd) == FALSE || ::IsIconic(hwnd) != FALSE || IsCloaked(hwnd)) {
        return TRUE;
    }

    const pikasnip::platform::RectI bounds = QueryWindowBounds(hwnd);
    if (bounds.isEmpty()) {
        return TRUE;
    }

    context->windows->push_back(DetectedWindow{hwnd, bounds});
    return TRUE;
}

} // namespace

void WindowDetector::Refresh(HWND excludeHwnd) {
    windows_.clear();

    const ScopedDpiContext dpiContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    EnumContext context;
    context.excludeHwnd = excludeHwnd;
    context.windows = &windows_;
    (void)::EnumWindows(&EnumWindowProc, reinterpret_cast<LPARAM>(&context));
}

std::optional<DetectedWindow> WindowDetector::HitTestWindow(int32_t vx, int32_t vy) const noexcept {
    for (const DetectedWindow& window : windows_) {
        if (window.bounds.contains(vx, vy)) {
            return window;
        }
    }
    return std::nullopt;
}

} // namespace pikasnip::capture

#pragma once

#include "pikasnip/pin/PinWindow.h"
#include "pikasnip/capture/CapturedFrame.h"
#include "pikasnip/platform/D2dContext.h"

#include <windows.h>

#include <cstddef>
#include <memory>
#include <vector>

namespace pikasnip::pin {

/// Manages the lifetime of active floating PinWindow instances.
class PinManager {
public:
    explicit PinManager(pikasnip::platform::D2dContext& d2d) noexcept;
    ~PinManager();

    PinManager(const PinManager&) = delete;
    PinManager& operator=(const PinManager&) = delete;

    bool CreatePin(
        HINSTANCE hInstance,
        HWND notifyHwnd,
        UINT cleanupMsg,
        const pikasnip::capture::CapturedFrame& image,
        const PinOptions& options = {},
        PinWindow::FeedbackCallback onFeedback = {},
        PinWindow::OcrCallback onOcr = {});

    void PruneClosed() noexcept;
    void CloseAll() noexcept;

    [[nodiscard]] size_t count() const noexcept { return pins_.size(); }
    [[nodiscard]] const std::vector<std::unique_ptr<PinWindow>>& pins() const noexcept { return pins_; }

private:
    pikasnip::platform::D2dContext& d2d_;
    std::vector<std::unique_ptr<PinWindow>> pins_;
};

} // namespace pikasnip::pin

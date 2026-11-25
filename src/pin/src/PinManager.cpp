#include "pikasnip/pin/PinManager.h"

#include <utility>

namespace pikasnip::pin {

PinManager::PinManager(pikasnip::platform::D2dContext& d2d) noexcept
    : d2d_(d2d) {}

PinManager::~PinManager() {
    CloseAll();
}

bool PinManager::CreatePin(
    HINSTANCE hInstance,
    HWND notifyHwnd,
    UINT cleanupMsg,
    const pikasnip::capture::CapturedFrame& image,
    const PinOptions& options,
    PinWindow::FeedbackCallback onFeedback,
    PinWindow::OcrCallback onOcr) {
    auto pin = std::make_unique<PinWindow>(d2d_);

    PinWindow::ClosedCallback onClosed;
    if (notifyHwnd != nullptr && cleanupMsg != 0U) {
        onClosed = [notifyHwnd, cleanupMsg](PinWindow*) {
            if (::IsWindow(notifyHwnd)) {
                ::PostMessageW(notifyHwnd, cleanupMsg, 0, 0);
            }
        };
    }

    if (!pin->Create(
            hInstance,
            image,
            options,
            std::move(onClosed),
            std::move(onFeedback),
            std::move(onOcr))) {
        return false;
    }

    pins_.push_back(std::move(pin));
    return true;
}

void PinManager::PruneClosed() noexcept {
    std::erase_if(pins_, [](const std::unique_ptr<PinWindow>& pin) noexcept {
        return !pin || !pin->isOpen();
    });
}

void PinManager::CloseAll() noexcept {
    std::vector<std::unique_ptr<PinWindow>> toClose = std::move(pins_);
    pins_.clear();
    for (auto& pin : toClose) {
        if (pin) {
            pin->Close();
        }
    }
}

} // namespace pikasnip::pin

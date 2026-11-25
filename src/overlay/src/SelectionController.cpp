#include "pikasnip/overlay/SelectionController.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace pikasnip::overlay {

namespace {

constexpr int32_t kClickThresholdPx = 3;
constexpr int32_t kMinSelectionSizePx = 2;

} // namespace

void SelectionController::BeginSession(
    const pikasnip::capture::CapturedFrame* frame,
    const pikasnip::capture::WindowDetector* detector) noexcept {
    Reset();
    frame_ = frame;
    detector_ = detector;
    if (frame_ != nullptr && frame_->isValid()) {
        virtualBounds_ = frame_->virtualBounds;
        cursorPos_ = {virtualBounds_.left, virtualBounds_.top};
    }
}

void SelectionController::Reset() noexcept {
    mode_ = SelectionMode::HoverWindow;
    dragAction_ = DragAction::None;
    activeHandle_ = ResizeHandle::None;
    hoveredRect_ = {};
    pressHoveredRect_ = {};
    selectedRect_ = {};
    dragStartRect_ = {};
    commitRequested_ = false;
    dragMovedBeyondThreshold_ = false;
}

PointI SelectionController::ClampPoint(int32_t vx, int32_t vy) const noexcept {
    if (virtualBounds_.isEmpty()) {
        return PointI{vx, vy};
    }
    return PointI{
        std::clamp(vx, virtualBounds_.left, virtualBounds_.right),
        std::clamp(vy, virtualBounds_.top, virtualBounds_.bottom)
    };
}

void SelectionController::UpdateHoveredWindow(int32_t vx, int32_t vy) noexcept {
    hoveredRect_ = {};
    if (detector_ == nullptr || virtualBounds_.isEmpty()) {
        return;
    }
    const auto hit = detector_->HitTestWindow(vx, vy);
    if (hit.has_value()) {
        hoveredRect_ = pikasnip::platform::ClampRectToBounds(hit->bounds, virtualBounds_);
    }
}

void SelectionController::OnPointerMove(int32_t vx, int32_t vy) {
    cursorPos_ = ClampPoint(vx, vy);

    if (dragAction_ != DragAction::None &&
        (std::abs(cursorPos_.x - dragAnchor_.x) > kClickThresholdPx ||
         std::abs(cursorPos_.y - dragAnchor_.y) > kClickThresholdPx)) {
        // Sticky: once the pointer left the click threshold during this gesture,
        // returning to the anchor must NOT degrade into a click-to-commit.
        dragMovedBeyondThreshold_ = true;
    }

    if (mode_ == SelectionMode::HoverWindow) {
        UpdateHoveredWindow(cursorPos_.x, cursorPos_.y);
        return;
    }

    if (dragAction_ == DragAction::NewRegion) {
        selectedRect_ = pikasnip::platform::ClampRectToBounds(
            pikasnip::platform::NormalizeRect(dragAnchor_.x, dragAnchor_.y, cursorPos_.x, cursorPos_.y),
            virtualBounds_);
        return;
    }

    if (dragAction_ == DragAction::MoveSelection) {
        const int32_t dx = cursorPos_.x - dragAnchor_.x;
        const int32_t dy = cursorPos_.y - dragAnchor_.y;
        const int32_t w = dragStartRect_.width();
        const int32_t h = dragStartRect_.height();

        const int32_t maxLeft = std::max(virtualBounds_.left, virtualBounds_.right - w);
        const int32_t maxTop = std::max(virtualBounds_.top, virtualBounds_.bottom - h);
        const int32_t newLeft = std::clamp(dragStartRect_.left + dx, virtualBounds_.left, maxLeft);
        const int32_t newTop = std::clamp(dragStartRect_.top + dy, virtualBounds_.top, maxTop);
        selectedRect_ = pikasnip::platform::RectI{newLeft, newTop, newLeft + w, newTop + h};
        return;
    }

    if (dragAction_ == DragAction::ResizeSelection) {
        int32_t left = dragStartRect_.left;
        int32_t top = dragStartRect_.top;
        int32_t right = dragStartRect_.right;
        int32_t bottom = dragStartRect_.bottom;

        switch (activeHandle_) {
            case ResizeHandle::TopLeft:
                left = cursorPos_.x;
                top = cursorPos_.y;
                break;
            case ResizeHandle::Top:
                top = cursorPos_.y;
                break;
            case ResizeHandle::TopRight:
                right = cursorPos_.x;
                top = cursorPos_.y;
                break;
            case ResizeHandle::Right:
                right = cursorPos_.x;
                break;
            case ResizeHandle::BottomRight:
                right = cursorPos_.x;
                bottom = cursorPos_.y;
                break;
            case ResizeHandle::Bottom:
                bottom = cursorPos_.y;
                break;
            case ResizeHandle::BottomLeft:
                left = cursorPos_.x;
                bottom = cursorPos_.y;
                break;
            case ResizeHandle::Left:
                left = cursorPos_.x;
                break;
            case ResizeHandle::None:
                break;
        }

        selectedRect_ = pikasnip::platform::ClampRectToBounds(
            pikasnip::platform::NormalizeRect(left, top, right, bottom),
            virtualBounds_);
    }
}

void SelectionController::OnLeftButtonDown(int32_t vx, int32_t vy) {
    cursorPos_ = ClampPoint(vx, vy);
    commitRequested_ = false;
    dragMovedBeyondThreshold_ = false;

    if (mode_ == SelectionMode::Selected) {
        const ResizeHandle hit = HitTestHandle(cursorPos_.x, cursorPos_.y);
        if (hit != ResizeHandle::None) {
            dragAction_ = DragAction::ResizeSelection;
            activeHandle_ = hit;
            dragAnchor_ = cursorPos_;
            dragStartRect_ = selectedRect_;
            return;
        }

        if (selectedRect_.contains(cursorPos_.x, cursorPos_.y)) {
            dragAction_ = DragAction::MoveSelection;
            activeHandle_ = ResizeHandle::None;
            dragAnchor_ = cursorPos_;
            dragStartRect_ = selectedRect_;
            return;
        }

        // Clicked outside current selection: start a fresh selection.
        pressHoveredRect_ = {};
        dragAnchor_ = cursorPos_;
        selectedRect_ = pikasnip::platform::RectI{cursorPos_.x, cursorPos_.y, cursorPos_.x, cursorPos_.y};
        mode_ = SelectionMode::DraggingNew;
        dragAction_ = DragAction::NewRegion;
        return;
    }

    UpdateHoveredWindow(cursorPos_.x, cursorPos_.y);
    pressHoveredRect_ = hoveredRect_;
    dragAnchor_ = cursorPos_;
    selectedRect_ = pikasnip::platform::RectI{cursorPos_.x, cursorPos_.y, cursorPos_.x, cursorPos_.y};
    mode_ = SelectionMode::DraggingNew;
    dragAction_ = DragAction::NewRegion;
}

void SelectionController::OnLeftButtonUp(int32_t vx, int32_t vy) {
    cursorPos_ = ClampPoint(vx, vy);

    if (mode_ == SelectionMode::DraggingNew && dragAction_ == DragAction::NewRegion) {
        const int32_t dx = std::abs(cursorPos_.x - dragAnchor_.x);
        const int32_t dy = std::abs(cursorPos_.y - dragAnchor_.y);
        if (dx <= kClickThresholdPx && dy <= kClickThresholdPx && !dragMovedBeyondThreshold_ &&
            !pressHoveredRect_.isEmpty()) {
            selectedRect_ = pressHoveredRect_;
            mode_ = SelectionMode::Selected;
            commitRequested_ = true;
        } else {
            selectedRect_ = pikasnip::platform::ClampRectToBounds(
                pikasnip::platform::NormalizeRect(dragAnchor_.x, dragAnchor_.y, cursorPos_.x, cursorPos_.y),
                virtualBounds_);
            if (selectedRect_.width() >= kMinSelectionSizePx &&
                selectedRect_.height() >= kMinSelectionSizePx) {
                mode_ = SelectionMode::Selected;
            } else {
                selectedRect_ = {};
                mode_ = SelectionMode::HoverWindow;
                UpdateHoveredWindow(cursorPos_.x, cursorPos_.y);
            }
        }
        dragAction_ = DragAction::None;
        activeHandle_ = ResizeHandle::None;
        pressHoveredRect_ = {};
        return;
    }

    if (mode_ == SelectionMode::Selected) {
        if (dragAction_ == DragAction::MoveSelection) {
            const int32_t dx = std::abs(cursorPos_.x - dragAnchor_.x);
            const int32_t dy = std::abs(cursorPos_.y - dragAnchor_.y);
            if (!dragMovedBeyondThreshold_ && dx <= kClickThresholdPx && dy <= kClickThresholdPx) {
                selectedRect_ = dragStartRect_;
                commitRequested_ = true;
            }
        }
        dragAction_ = DragAction::None;
        activeHandle_ = ResizeHandle::None;
        if (selectedRect_.width() < kMinSelectionSizePx ||
            selectedRect_.height() < kMinSelectionSizePx) {
            selectedRect_ = {};
            commitRequested_ = false;
            mode_ = SelectionMode::HoverWindow;
            UpdateHoveredWindow(cursorPos_.x, cursorPos_.y);
        }
    }
}

bool SelectionController::consumeCommitRequest() noexcept {
    const bool requested = commitRequested_;
    commitRequested_ = false;
    return requested;
}

bool SelectionController::CancelSelection() noexcept {
    if (mode_ == SelectionMode::HoverWindow && dragAction_ == DragAction::None) {
        return false;
    }
    Reset();
    UpdateHoveredWindow(cursorPos_.x, cursorPos_.y);
    return true;
}

pikasnip::platform::RectI SelectionController::activeRect() const noexcept {
    if (mode_ == SelectionMode::DraggingNew || mode_ == SelectionMode::Selected) {
        return selectedRect_;
    }
    return hoveredRect_;
}

std::array<pikasnip::platform::RectI, 8> SelectionController::GetHandleRects(int32_t halfSize) const noexcept {
    const auto r = selectedRect_;
    const int32_t midX = r.left + r.width() / 2;
    const int32_t midY = r.top + r.height() / 2;

    auto makeBox = [halfSize](int32_t cx, int32_t cy) noexcept {
        return pikasnip::platform::RectI{
            cx - halfSize,
            cy - halfSize,
            cx + halfSize,
            cy + halfSize
        };
    };

    return {
        makeBox(r.left, r.top),      // TopLeft
        makeBox(midX, r.top),        // Top
        makeBox(r.right, r.top),     // TopRight
        makeBox(r.right, midY),      // Right
        makeBox(r.right, r.bottom),  // BottomRight
        makeBox(midX, r.bottom),     // Bottom
        makeBox(r.left, r.bottom),   // BottomLeft
        makeBox(r.left, midY)        // Left
    };
}

ResizeHandle SelectionController::HitTestHandle(int32_t vx, int32_t vy, int32_t hitRadius) const noexcept {
    if (mode_ != SelectionMode::Selected || selectedRect_.isEmpty()) {
        return ResizeHandle::None;
    }

    constexpr std::array<ResizeHandle, 8> kHandles = {
        ResizeHandle::TopLeft,
        ResizeHandle::Top,
        ResizeHandle::TopRight,
        ResizeHandle::Right,
        ResizeHandle::BottomRight,
        ResizeHandle::Bottom,
        ResizeHandle::BottomLeft,
        ResizeHandle::Left
    };

    const auto boxes = GetHandleRects(hitRadius);
    for (size_t i = 0; i < boxes.size(); ++i) {
        if (boxes[i].contains(vx, vy)) {
            return kHandles[i];
        }
    }
    return ResizeHandle::None;
}

PixelColorInfo SelectionController::GetPixelRgbHex(int32_t vx, int32_t vy) const {
    PixelColorInfo info;
    if (frame_ == nullptr || !frame_->isValid()) {
        return info;
    }
    if (!virtualBounds_.contains(vx, vy)) {
        return info;
    }

    const auto bgra = frame_->GetPixelBgra(vx, vy);
    info.b = bgra[0];
    info.g = bgra[1];
    info.r = bgra[2];
    info.valid = true;

    char buf[8]{};
    std::snprintf(buf, sizeof(buf), "#%02X%02X%02X", info.r, info.g, info.b);
    info.hex = buf;

    wchar_t wbuf[8]{};
    std::swprintf(wbuf, std::size(wbuf), L"#%02X%02X%02X", info.r, info.g, info.b);
    info.wideHex = wbuf;
    return info;
}

} // namespace pikasnip::overlay

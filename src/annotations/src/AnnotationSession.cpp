#include "pikasnip/annotations/AnnotationSession.h"

#include <algorithm>
#include <type_traits>
#include <utility>

namespace pikasnip::annotations {

namespace {

void TranslateItem(AnnotationItem& item, float dx, float dy) noexcept {
    std::visit(
        [dx, dy](auto& ann) {
            using T = std::decay_t<decltype(ann)>;
            if constexpr (std::is_same_v<T, RectAnnotation> ||
                          std::is_same_v<T, EllipseAnnotation> ||
                          std::is_same_v<T, MosaicAnnotation>) {
                ann.bounds.left += dx;
                ann.bounds.right += dx;
                ann.bounds.top += dy;
                ann.bounds.bottom += dy;
            } else if constexpr (std::is_same_v<T, ArrowAnnotation>) {
                ann.start.x += dx;
                ann.start.y += dy;
                ann.end.x += dx;
                ann.end.y += dy;
            } else if constexpr (std::is_same_v<T, FreehandPenAnnotation> ||
                                 std::is_same_v<T, HighlighterAnnotation>) {
                for (auto& pt : ann.points) {
                    pt.x += dx;
                    pt.y += dy;
                }
            } else if constexpr (std::is_same_v<T, StepCounterAnnotation>) {
                ann.center.x += dx;
                ann.center.y += dy;
            } else if constexpr (std::is_same_v<T, TextAnnotation>) {
                ann.origin.x += dx;
                ann.origin.y += dy;
            }
        },
        item);
}

} // namespace

AnnotationSession::AnnotationSession(size_t maxHistory) noexcept
    : maxHistory_(std::max<size_t>(1, maxHistory)) {}

void AnnotationSession::PushUndo(HistoryEntry entry) {
    undoStack_.push_back(std::move(entry));
    while (undoStack_.size() > maxHistory_) {
        undoStack_.pop_front();
    }
}

void AnnotationSession::PushRedo(HistoryEntry entry) {
    redoStack_.push_back(std::move(entry));
    while (redoStack_.size() > maxHistory_) {
        redoStack_.pop_front();
    }
}

void AnnotationSession::Add(AnnotationItem item) {
    redoStack_.clear();
    items_.push_back(item);
    PushUndo(HistoryEntry{ActionKind::Add, {std::move(item)}});
}

bool AnnotationSession::RemoveLast() {
    if (items_.empty()) {
        return false;
    }
    redoStack_.clear();
    AnnotationItem removed = std::move(items_.back());
    items_.pop_back();
    PushUndo(HistoryEntry{ActionKind::RemoveLast, {std::move(removed)}});
    return true;
}

bool AnnotationSession::Clear() {
    if (items_.empty()) {
        return false;
    }
    redoStack_.clear();
    std::vector<AnnotationItem> previous = std::move(items_);
    items_.clear();
    PushUndo(HistoryEntry{ActionKind::Clear, std::move(previous)});
    return true;
}

bool AnnotationSession::Undo() {
    if (undoStack_.empty()) {
        return false;
    }
    HistoryEntry entry = std::move(undoStack_.back());
    undoStack_.pop_back();

    switch (entry.kind) {
        case ActionKind::Add:
            if (!items_.empty()) {
                items_.pop_back();
            }
            break;
        case ActionKind::RemoveLast:
            if (!entry.snapshot.empty()) {
                items_.push_back(entry.snapshot.front());
            }
            break;
        case ActionKind::Clear:
            items_ = entry.snapshot;
            break;
    }

    PushRedo(std::move(entry));
    return true;
}

bool AnnotationSession::Redo() {
    if (redoStack_.empty()) {
        return false;
    }
    HistoryEntry entry = std::move(redoStack_.back());
    redoStack_.pop_back();

    switch (entry.kind) {
        case ActionKind::Add:
            if (!entry.snapshot.empty()) {
                items_.push_back(entry.snapshot.front());
            }
            break;
        case ActionKind::RemoveLast:
            if (!items_.empty()) {
                items_.pop_back();
            }
            break;
        case ActionKind::Clear:
            items_.clear();
            break;
    }

    PushUndo(std::move(entry));
    return true;
}

void AnnotationSession::Reset() {
    items_.clear();
    undoStack_.clear();
    redoStack_.clear();
}

void AnnotationSession::TranslateAll(float dx, float dy) noexcept {
    if (dx == 0.0f && dy == 0.0f) {
        return;
    }
    for (auto& item : items_) {
        TranslateItem(item, dx, dy);
    }
    for (auto& entry : undoStack_) {
        for (auto& item : entry.snapshot) {
            TranslateItem(item, dx, dy);
        }
    }
    for (auto& entry : redoStack_) {
        for (auto& item : entry.snapshot) {
            TranslateItem(item, dx, dy);
        }
    }
}

int32_t AnnotationSession::NextStepNumber() const noexcept {
    int32_t maxStep = 0;
    for (const auto& item : items_) {
        if (const auto* step = std::get_if<StepCounterAnnotation>(&item)) {
            maxStep = std::max(maxStep, step->number);
        }
    }
    return maxStep + 1;
}

} // namespace pikasnip::annotations

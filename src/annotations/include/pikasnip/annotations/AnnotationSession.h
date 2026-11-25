#pragma once

#include "pikasnip/annotations/AnnotationTypes.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <vector>

namespace pikasnip::annotations {

class AnnotationSession {
public:
    static constexpr size_t kDefaultMaxHistory = 64;

    explicit AnnotationSession(size_t maxHistory = kDefaultMaxHistory) noexcept;

    void Add(AnnotationItem item);
    bool RemoveLast();
    bool Clear();

    [[nodiscard]] bool CanUndo() const noexcept { return !undoStack_.empty(); }
    [[nodiscard]] bool CanRedo() const noexcept { return !redoStack_.empty(); }

    bool Undo();
    bool Redo();

    void Reset();

    [[nodiscard]] const std::vector<AnnotationItem>& items() const noexcept { return items_; }
    [[nodiscard]] bool empty() const noexcept { return items_.empty(); }
    [[nodiscard]] size_t undoCount() const noexcept { return undoStack_.size(); }
    [[nodiscard]] size_t redoCount() const noexcept { return redoStack_.size(); }
    [[nodiscard]] int32_t NextStepNumber() const noexcept;

    /// Shifts every stored annotation (including undo/redo snapshots) by (dx, dy).
    void TranslateAll(float dx, float dy) noexcept;

private:
    enum class ActionKind {
        Add,
        RemoveLast,
        Clear
    };

    struct HistoryEntry {
        ActionKind kind = ActionKind::Add;
        std::vector<AnnotationItem> snapshot;
    };

    void PushUndo(HistoryEntry entry);
    void PushRedo(HistoryEntry entry);

    size_t maxHistory_ = kDefaultMaxHistory;
    std::vector<AnnotationItem> items_;
    std::deque<HistoryEntry> undoStack_;
    std::deque<HistoryEntry> redoStack_;
};

} // namespace pikasnip::annotations

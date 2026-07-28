#include "history.h"

void History::push(std::unique_ptr<Command> cmd) {
    if (!cmd) return;
    // Merge into the top entry when possible (e.g. a slider still moving).
    // On a false merge the caller still owns cmd — pass by reference.
    if (!undo_.empty() && undo_.back()->merge(*cmd)) {
        // Absorbed. The redo arm is still invalidated by the new live edit.
        redo_.clear();
        return;
    }
    memoryUsed_ += cmd->memoryBytes();
    undo_.push_back(std::move(cmd));
    // Any new edit invalidates the redo arm.
    redo_.clear();
    // Evict oldest until under budget.
    while (memoryUsed_ > memoryLimit_ && undo_.size() > 1) {
        memoryUsed_ -= undo_.front()->memoryBytes();
        undo_.pop_front();
    }
}

bool History::undo() {
    if (undo_.empty()) return false;
    auto cmd = std::move(undo_.back());
    undo_.pop_back();
    cmd->undo();
    redo_.push_back(std::move(cmd));
    return true;
}

bool History::redo() {
    if (redo_.empty()) return false;
    auto cmd = std::move(redo_.back());
    redo_.pop_back();
    cmd->redo();
    undo_.push_back(std::move(cmd));
    return true;
}

void History::clear() {
    undo_.clear();
    redo_.clear();
    memoryUsed_ = 0;
}

const Command* History::undoAt(size_t i) const {
    if (i >= undo_.size()) return nullptr;
    return undo_[undo_.size() - 1 - i].get();  // 0 = most recent
}

const Command* History::redoAt(size_t i) const {
    if (i >= redo_.size()) return nullptr;
    return redo_[redo_.size() - 1 - i].get();
}

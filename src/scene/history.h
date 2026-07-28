#pragma once
#include <cstddef>
#include <deque>
#include <memory>

// ---------------------------------------------------------------------------
// Undo/Redo history.
//
// A Command captures one user-visible edit (a whole brush stroke, one block
// placement, one gizmo drag). Tools mutate the scene first, then push a
// command holding enough data to reverse and re-apply the edit — push() does
// NOT call redo(); the edit is already live.
//
// History is a LIFO stack with a redo arm and a memory budget: pushing a new
// command clears the redo arm and evicts the oldest commands until the total
// reported memoryBytes() fits under the limit.
//
// This header is editor- and GL-free so the stack logic is unit-testable.
// ---------------------------------------------------------------------------

class Command {
public:
    virtual ~Command() = default;

    // Re-apply the edit (called by History::redo only — the first application
    // happened live in the tool before push()).
    virtual void redo() = 0;
    virtual void undo() = 0;

    // Approximate heap bytes held by this command (for the memory budget).
    virtual size_t memoryBytes() const = 0;
    // Short human-readable label for the History panel.
    virtual const char* name() const = 0;

    // Try to absorb `next` into this command (e.g. consecutive slider drags
    // on the same prop). `next` is already applied live. Returns true if
    // absorbed — the caller then discards it instead of pushing a new entry.
    // Takes a reference: on a false return the caller still owns `next`.
    virtual bool merge(const Command& next) {
        (void)next;
        return false;
    }
};

class History {
public:
    // Default budget: 256 MB of captured undo data.
    static constexpr size_t kDefaultMemoryLimit = 256ull << 20;

    void push(std::unique_ptr<Command> cmd);
    bool undo();
    bool redo();
    void clear();

    bool canUndo() const { return !undo_.empty(); }
    bool canRedo() const { return !redo_.empty(); }

    size_t undoCount() const { return undo_.size(); }
    size_t redoCount() const { return redo_.size(); }
    size_t memoryUsed() const { return memoryUsed_; }
    size_t memoryLimit() const { return memoryLimit_; }
    void setMemoryLimit(size_t bytes) { memoryLimit_ = bytes; }

    // Stack inspection for the History panel. Index 0 = most recent undoable.
    const Command* undoAt(size_t i) const;
    const Command* redoAt(size_t i) const;

private:
    std::deque<std::unique_ptr<Command>> undo_;  // back = most recent
    std::deque<std::unique_ptr<Command>> redo_;
    size_t memoryUsed_  = 0;
    size_t memoryLimit_ = kDefaultMemoryLimit;
};

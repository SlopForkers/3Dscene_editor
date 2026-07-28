#include <doctest/doctest.h>
#include "history.h"

// A minimal Command that adds/subtracts a delta on a shared counter.
class Counter : public Command {
public:
    Counter(int& v, int delta, size_t mem = 16) : v_(v), d_(delta), mem_(mem) {}
    void redo() override { v_ += d_; }
    void undo() override { v_ -= d_; }
    size_t memoryBytes() const override { return mem_; }
    const char* name() const override { return "counter"; }
private:
    int& v_; int d_; size_t mem_;
};

// A Command that merges with the previous one of the same kind.
class MergeCounter : public Command {
public:
    MergeCounter(int& v, int delta) : v_(v), d_(delta) {}
    void redo() override { v_ += d_; }
    void undo() override { v_ -= d_; }
    size_t memoryBytes() const override { return 16; }
    const char* name() const override { return "merge"; }
    bool merge(const Command& next) override {
        auto* n = dynamic_cast<const MergeCounter*>(&next);
        if (!n) return false;
        d_ += n->d_;
        return true;
    }
private:
    int& v_; int d_;
};

TEST_CASE("history: push does not execute redo (edit is already live)") {
    History h;
    int v = 0;
    h.push(std::make_unique<Counter>(v, 5));
    CHECK(v == 0);              // push must not re-apply
    CHECK(h.canUndo());
    CHECK(!h.canRedo());
}

TEST_CASE("history: undo/redo round trip") {
    History h;
    int v = 0;
    h.push(std::make_unique<Counter>(v, 5));
    REQUIRE(h.undo());
    CHECK(v == -5);
    CHECK(h.canRedo());
    REQUIRE(h.redo());
    CHECK(v == 0);
    CHECK(!h.canRedo());
    CHECK(h.canUndo());
}

TEST_CASE("history: new push clears the redo arm") {
    History h;
    int v = 0;
    h.push(std::make_unique<Counter>(v, 1));
    h.push(std::make_unique<Counter>(v, 10));
    REQUIRE(h.undo());
    CHECK(h.canRedo());
    h.push(std::make_unique<Counter>(v, 100));
    CHECK(!h.canRedo());
    // Only the surviving undo chain replays (+1, +100); the cleared +10
    // redo edit is gone for good.
    h.undo(); h.undo();
    CHECK(v == -111);
    h.redo(); h.redo();
    CHECK(v == -10);
}

TEST_CASE("history: LIFO order across multiple commands") {
    History h;
    int v = 0;
    h.push(std::make_unique<Counter>(v, 1));
    h.push(std::make_unique<Counter>(v, 2));
    h.push(std::make_unique<Counter>(v, 4));
    h.undo(); h.undo();
    CHECK(v == -6);
    h.redo();
    CHECK(v == -4);
    h.undo(); h.undo();
    CHECK(v == -7);   // -6 (two undos of 4+2) then -2 redo, -2 -1 undos
}

TEST_CASE("history: merge absorbs same-kind commands") {
    History h;
    int v = 0;
    h.push(std::make_unique<MergeCounter>(v, 1));
    h.push(std::make_unique<MergeCounter>(v, 2));
    h.push(std::make_unique<MergeCounter>(v, 3));
    // Three pushes, one stack entry.
    CHECK(h.undoCount() == 1);
    // Merged delta is 6; the absorbed edits are live but not yet applied
    // to the counter in this synthetic test (tools apply edits themselves),
    // so undo subtracts the merged total.
    h.undo();
    CHECK(v == -6);
    h.redo();
    CHECK(v == 0);
}

TEST_CASE("history: merge refuses different kinds") {
    History h;
    int v = 0;
    h.push(std::make_unique<MergeCounter>(v, 1));
    h.push(std::make_unique<Counter>(v, 2));   // not a MergeCounter
    CHECK(h.undoCount() == 2);
}

TEST_CASE("history: memory limit evicts oldest") {
    History h;
    h.setMemoryLimit(64);   // room for four 16-byte commands
    int v = 0;
    for (int i = 0; i < 6; ++i)
        h.push(std::make_unique<Counter>(v, 1, 16));
    CHECK(h.undoCount() == 4);
    CHECK(h.memoryUsed() <= 64);
}

TEST_CASE("history: empty-stack undo/redo are safe no-ops") {
    History h;
    CHECK(!h.undo());
    CHECK(!h.redo());
}

TEST_CASE("history: clear resets both arms and memory") {
    History h;
    int v = 0;
    h.push(std::make_unique<Counter>(v, 1));
    h.undo();
    h.clear();
    CHECK(!h.canUndo());
    CHECK(!h.canRedo());
    CHECK(h.memoryUsed() == 0);
}

TEST_CASE("history: stack inspection for the panel") {
    History h;
    int v = 0;
    h.push(std::make_unique<Counter>(v, 1));
    h.push(std::make_unique<Counter>(v, 2));
    REQUIRE(h.undoCount() == 2);
    CHECK(h.undoAt(0) != nullptr);   // most recent
    CHECK(h.undoAt(1) != nullptr);
    CHECK(h.undoAt(2) == nullptr);
    REQUIRE(h.undo());
    CHECK(h.redoAt(0) != nullptr);
    CHECK(h.redoAt(1) == nullptr);
}

#include <doctest/doctest.h>
#include "spawn.h"

TEST_CASE("spawn manager: add assigns unique, never-reused ids") {
    SpawnManager sm;
    int a = sm.addSpawn(SpawnPoint{});
    int b = sm.addSpawn(SpawnPoint{});
    CHECK(a != b);
    REQUIRE(sm.removeSpawn(a));
    int c = sm.addSpawn(SpawnPoint{});
    CHECK(c != a);   // ids are the game's stable key — never reused
    CHECK(c != b);
}

TEST_CASE("spawn manager: addSpawn ensures a root node") {
    SpawnManager sm;
    int id = sm.addSpawn(SpawnPoint{});
    SpawnPoint* sp = sm.findSpawn(id);
    REQUIRE(sp != nullptr);
    CHECK(sp->rootId >= 0);
    CHECK(sp->rootNode() != nullptr);
    CHECK(sp->rootNode()->kind == LogicNode::Root);
}

TEST_CASE("spawn manager: addSpawnWithId keeps id and bumps counter") {
    SpawnManager sm;
    SpawnPoint sp;
    sp.id = 5;
    sm.addSpawnWithId(sp);
    CHECK(sm.findSpawn(5) != nullptr);
    CHECK(sm.addSpawn(SpawnPoint{}) == 6);
}

TEST_CASE("spawn point: removeNode unlinks references, root is protected") {
    SpawnManager sm;
    int id = sm.addSpawn(SpawnPoint{});
    SpawnPoint* sp = sm.findSpawn(id);
    REQUIRE(sp != nullptr);

    int cond = sp->addNode(LogicNode::Cond, glm::vec2(200.0f));
    int act  = sp->addNode(LogicNode::Act, glm::vec2(400.0f));
    sp->rootNode()->nextTrue = cond;
    sp->findNode(cond)->nextTrue = act;
    sp->findNode(cond)->nextFalse = act;

    CHECK(!sp->removeNode(sp->rootId));   // root cannot be deleted

    REQUIRE(sp->removeNode(act));
    CHECK(sp->findNode(act) == nullptr);
    // Dangling links to the removed node were cleared.
    CHECK(sp->findNode(cond)->nextTrue == -1);
    CHECK(sp->findNode(cond)->nextFalse == -1);
    CHECK(sp->rootNode()->nextTrue == cond);
}

TEST_CASE("spawn graph: reachability detects cycles") {
    SpawnPoint sp;
    LogicNode root;
    root.kind = LogicNode::Root;
    root.id = 0;
    sp.rootId = 0;
    sp.nextNodeId = 1;
    sp.nodes.push_back(root);

    int a = sp.addNode(LogicNode::Cond, glm::vec2(0.0f));
    int b = sp.addNode(LogicNode::Act, glm::vec2(0.0f));
    int c = sp.addNode(LogicNode::Act, glm::vec2(0.0f));
    sp.rootNode()->nextTrue = a;
    sp.findNode(a)->nextTrue = b;
    sp.findNode(b)->nextTrue = c;

    CHECK(spawnGraphReachable(sp, a, c));        // a -> b -> c
    CHECK(spawnGraphReachable(sp, 0, c));        // root reaches everything
    CHECK(!spawnGraphReachable(sp, c, a));       // no path back
    CHECK(!spawnGraphReachable(sp, a, 999));     // unknown id
    // So a link c -> a would create a cycle and must be rejected by the UI:
    CHECK(spawnGraphReachable(sp, a, c));        // (the editor checks this
    //  with spawnGraphReachable(sp, target, source) before linking)
}

TEST_CASE("spawn summaries: every type renders a non-empty string") {
    for (int t = 0; t <= (int)Condition::PlayerNear; ++t) {
        Condition c;
        c.type = (Condition::Type)t;
        CHECK(condTypeName(t) != nullptr);
        CHECK(!condSummary(c).empty());
    }
    for (int t = 0; t <= (int)Action::PlaySound; ++t) {
        Action a;
        a.type = (Action::Type)t;
        CHECK(actTypeName(t) != nullptr);
        CHECK(!actSummary(a).empty());
    }
}

#include <doctest/doctest.h>
#include "sim.h"

// Build a marker with a root node (via the manager) and return it by id.
static int makeMarker(SpawnManager& sm, const char* name = "A",
                      glm::vec3 pos = glm::vec3(0.0f)) {
    SpawnPoint sp;
    sp.name = name;
    sp.position = pos;
    return sm.addSpawn(std::move(sp));
}

static int addAction(SpawnPoint& sp, Action::Type type) {
    int id = sp.addNode(LogicNode::Act, glm::vec2(0.0f));
    sp.findNode(id)->act.type = type;
    return id;
}

TEST_CASE("sim: root -> SetAnimation applies on the first update") {
    SpawnManager sm;
    int id = makeMarker(sm);
    SpawnPoint* sp = sm.findSpawn(id);
    int a = addAction(*sp, Action::SetAnimation);
    sp->findNode(a)->act.param = "start_emotion_happy";
    sp->rootNode()->nextTrue = a;

    SimController sim;
    sim.start(sm);
    sim.update(0.016f, sm, glm::vec3(0.0f));
    const SimController::SpawnSim* ss = sim.simFor(id);
    REQUIRE(ss != nullptr);
    CHECK(ss->anim == "start_emotion_happy");
    // Chain finished: no node left executing, and no re-runs happen.
    CHECK(ss->execNode == -1);
    size_t logSize = sim.log().size();
    sim.update(0.016f, sm, glm::vec3(0.0f));
    CHECK(sim.log().size() == logSize);
}

TEST_CASE("sim: flag condition drives branches, flag edit re-resolves live") {
    SpawnManager sm;
    int id = makeMarker(sm);
    SpawnPoint* sp = sm.findSpawn(id);
    int cond = sp->addNode(LogicNode::Cond, glm::vec2(0.0f));
    sp->findNode(cond)->cond.type = Condition::FlagEquals;
    sp->findNode(cond)->cond.flagId = 1;
    sp->findNode(cond)->cond.value = 1;
    int happy = addAction(*sp, Action::SetAnimation);
    sp->findNode(happy)->act.param = "happy";
    int sad = addAction(*sp, Action::SetAnimation);
    sp->findNode(sad)->act.param = "sad";
    sp->rootNode()->nextTrue = cond;
    sp->findNode(cond)->nextTrue = happy;
    sp->findNode(cond)->nextFalse = sad;

    SimController sim;
    sim.start(sm);
    sim.update(0.016f, sm, glm::vec3(0.0f));
    CHECK(sim.simFor(id)->anim == "sad");        // flag 1 missing = 0

    sim.flags()[1] = 1;                          // user flips the flag
    sim.update(0.016f, sm, glm::vec3(0.0f));
    CHECK(sim.simFor(id)->anim == "happy");

    sim.flags()[1] = 0;                          // and back
    sim.update(0.016f, sm, glm::vec3(0.0f));
    CHECK(sim.simFor(id)->anim == "sad");
}

TEST_CASE("sim: Spawn with delay applies at expiry; Despawn reverses") {
    SpawnManager sm;
    int id = makeMarker(sm);
    SpawnPoint* sp = sm.findSpawn(id);
    int a = addAction(*sp, Action::Spawn);
    sp->findNode(a)->act.floatParam = 0.5f;      // 0.5s delay
    sp->rootNode()->nextTrue = a;

    SimController sim;
    sim.start(sm);
    sim.update(0.2f, sm, glm::vec3(0.0f));
    CHECK(!sim.simFor(id)->spawned);             // still waiting
    sim.update(0.2f, sm, glm::vec3(0.0f));
    CHECK(!sim.simFor(id)->spawned);
    sim.update(0.2f, sm, glm::vec3(0.0f));       // 0.6s total
    CHECK(sim.simFor(id)->spawned);

    // Now a Despawn branch: flag flip switches to it.
    int cond = sp->addNode(LogicNode::Cond, glm::vec2(0.0f));
    sp->findNode(cond)->cond.type = Condition::FlagEquals;
    sp->findNode(cond)->cond.flagId = 2;
    sp->findNode(cond)->cond.value = 1;
    int despawn = addAction(*sp, Action::Despawn);
    sp->rootNode()->nextTrue = cond;
    sp->findNode(cond)->nextTrue = despawn;
    sp->findNode(cond)->nextFalse = a;

    sim.flags()[2] = 1;
    sim.update(0.016f, sm, glm::vec3(0.0f));
    CHECK(!sim.simFor(id)->spawned);
}

TEST_CASE("sim: Wait chains actions in order") {
    SpawnManager sm;
    int id = makeMarker(sm);
    SpawnPoint* sp = sm.findSpawn(id);
    int w = addAction(*sp, Action::Wait);
    sp->findNode(w)->act.floatParam = 0.5f;
    int a = addAction(*sp, Action::SetAnimation);
    sp->findNode(a)->act.param = "late";
    sp->rootNode()->nextTrue = w;
    sp->findNode(w)->nextTrue = a;

    SimController sim;
    sim.start(sm);
    sim.update(0.2f, sm, glm::vec3(0.0f));
    CHECK(sim.simFor(id)->anim.empty());
    sim.update(0.2f, sm, glm::vec3(0.0f));
    CHECK(sim.simFor(id)->anim.empty());
    sim.update(0.2f, sm, glm::vec3(0.0f));       // 0.6s total
    CHECK(sim.simFor(id)->anim == "late");
}

TEST_CASE("sim: SetFlag from one marker triggers another") {
    SpawnManager sm;
    int a = makeMarker(sm, "A");
    int b = makeMarker(sm, "B");
    SpawnPoint* pa = sm.findSpawn(a);
    SpawnPoint* pb = sm.findSpawn(b);

    int setFlag = addAction(*pa, Action::SetFlag);
    pa->findNode(setFlag)->act.intParam = 7;
    pa->findNode(setFlag)->act.intParam2 = 1;
    pa->rootNode()->nextTrue = setFlag;

    int cond = pb->addNode(LogicNode::Cond, glm::vec2(0.0f));
    pb->findNode(cond)->cond.type = Condition::FlagEquals;
    pb->findNode(cond)->cond.flagId = 7;
    pb->findNode(cond)->cond.value = 1;
    int anim = addAction(*pb, Action::SetAnimation);
    pb->findNode(anim)->act.param = "chained";
    pb->rootNode()->nextTrue = cond;
    pb->findNode(cond)->nextTrue = anim;

    SimController sim;
    sim.start(sm);
    sim.update(0.016f, sm, glm::vec3(0.0f));
    sim.update(0.016f, sm, glm::vec3(0.0f));   // settle (order-independent)
    CHECK(sim.flags()[7] == 1);
    CHECK(sim.simFor(b)->anim == "chained");
}

TEST_CASE("sim: RandomChance 0%/100% are deterministic and latched") {
    SpawnManager sm;
    int id = makeMarker(sm);
    SpawnPoint* sp = sm.findSpawn(id);
    int cond = sp->addNode(LogicNode::Cond, glm::vec2(0.0f));
    sp->findNode(cond)->cond.type = Condition::RandomChance;
    sp->findNode(cond)->cond.value = 0;          // never
    int yes = addAction(*sp, Action::SetAnimation);
    sp->findNode(yes)->act.param = "yes";
    int no = addAction(*sp, Action::SetAnimation);
    sp->findNode(no)->act.param = "no";
    sp->rootNode()->nextTrue = cond;
    sp->findNode(cond)->nextTrue = yes;
    sp->findNode(cond)->nextFalse = no;

    SimController sim;
    sim.start(sm);
    sim.update(0.016f, sm, glm::vec3(0.0f));
    CHECK(sim.simFor(id)->anim == "no");
    // Latched: no flicker and no re-roll when the percent is edited —
    // a re-roll happens only on simulation restart.
    sp->findNode(cond)->cond.value = 100;
    for (int i = 0; i < 10; ++i) sim.update(0.016f, sm, glm::vec3(0.0f));
    CHECK(sim.simFor(id)->anim == "no");

    sp->findNode(cond)->cond.value = 100;        // always
    sim.stop();
    sim.start(sm);                               // restart re-rolls
    sim.update(0.016f, sm, glm::vec3(0.0f));
    CHECK(sim.simFor(id)->anim == "yes");
    std::string before = sim.simFor(id)->anim;
    for (int i = 0; i < 10; ++i) sim.update(0.016f, sm, glm::vec3(0.0f));
    CHECK(sim.simFor(id)->anim == before);
}

TEST_CASE("sim: PlayerNear uses XZ distance to the player proxy") {
    SpawnManager sm;
    int id = makeMarker(sm, "A", glm::vec3(10.0f, 0.0f, 0.0f));
    SpawnPoint* sp = sm.findSpawn(id);
    int cond = sp->addNode(LogicNode::Cond, glm::vec2(0.0f));
    sp->findNode(cond)->cond.type = Condition::PlayerNear;
    sp->findNode(cond)->cond.value = 5;          // 5 m radius
    int near = addAction(*sp, Action::SetAnimation);
    sp->findNode(near)->act.param = "near";
    int far = addAction(*sp, Action::SetAnimation);
    sp->findNode(far)->act.param = "far";
    sp->rootNode()->nextTrue = cond;
    sp->findNode(cond)->nextTrue = near;
    sp->findNode(cond)->nextFalse = far;

    SimController sim;
    sim.start(sm);
    sim.update(0.016f, sm, glm::vec3(0.0f));     // player at origin: 10 m
    CHECK(sim.simFor(id)->anim == "far");
    sim.update(0.016f, sm, glm::vec3(6.0f, 99.0f, 0.0f));  // XZ: 4 m (Y ignored)
    CHECK(sim.simFor(id)->anim == "near");
}

TEST_CASE("sim: CameraFocus emits exactly one request") {
    SpawnManager sm;
    int id = makeMarker(sm, "A", glm::vec3(1.0f, 2.0f, 3.0f));
    SpawnPoint* sp = sm.findSpawn(id);
    int a = addAction(*sp, Action::CameraFocus);
    sp->findNode(a)->act.intParam = -1;          // to this marker
    sp->findNode(a)->act.floatParam = 2.0f;
    sp->rootNode()->nextTrue = a;

    SimController sim;
    sim.start(sm);
    sim.update(0.016f, sm, glm::vec3(0.0f));
    int camId = 0;
    glm::vec3 mp(0.0f);
    float blend = 0.0f;
    REQUIRE(sim.takeCamRequest(camId, mp, blend));
    CHECK(camId == -1);
    CHECK(mp == sp->position);
    CHECK(blend == 2.0f);
    CHECK(!sim.takeCamRequest(camId, mp, blend));   // consumed once
}

TEST_CASE("sim: dialog and sound actions log lines") {
    SpawnManager sm;
    int id = makeMarker(sm);
    SpawnPoint* sp = sm.findSpawn(id);
    int d = addAction(*sp, Action::DialogLine);
    sp->findNode(d)->act.param = "dlg_hello";
    int s = addAction(*sp, Action::PlaySound);
    sp->findNode(s)->act.param = "sfx_chime";
    sp->rootNode()->nextTrue = d;
    sp->findNode(d)->nextTrue = s;

    SimController sim;
    sim.start(sm);
    sim.update(0.016f, sm, glm::vec3(0.0f));
    bool hasDialog = false, hasSound = false;
    for (const auto& line : sim.log()) {
        if (line.find("dlg_hello") != std::string::npos) hasDialog = true;
        if (line.find("sfx_chime") != std::string::npos) hasSound = true;
    }
    CHECK(hasDialog);
    CHECK(hasSound);
}

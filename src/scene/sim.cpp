#include "sim.h"
#include <algorithm>
#include <cstdlib>

void SimController::start(const SpawnManager& /*spawns*/) {
    sims_.clear();
    running_ = true;
    logLine("--- simulation started ---");
}

void SimController::stop() {
    running_ = false;
    sims_.clear();   // spawned/anim visuals reset to defaults
    logLine("--- simulation stopped ---");
}

void SimController::logLine(const std::string& text) {
    log_.push_back(text);
    while (log_.size() > 200) log_.pop_front();
}

const SimController::SpawnSim* SimController::simFor(int spawnId) const {
    auto it = sims_.find(spawnId);
    return it != sims_.end() ? &it->second : nullptr;
}

bool SimController::takeCamRequest(int& cameraId, glm::vec3& markerPos,
                                   float& blend) {
    if (!camReqPending_) return false;
    cameraId = camReqCameraId_;
    markerPos = camReqMarkerPos_;
    blend = camReqBlend_;
    camReqPending_ = false;
    return true;
}

void SimController::update(float dt, const SpawnManager& spawns,
                           const glm::vec3& playerPos) {
    if (!running_) return;
    playerPos_ = playerPos;

    for (const auto& sp : spawns.spawns()) {
        SpawnSim& sim = sims_[sp.id];

        // Resolve the graph from the root to the action node that should be
        // running now; a changed resolution restarts execution there.
        const LogicNode* root = sp.findNode(sp.rootId);
        int head = root ? resolveFrom(sp, root->nextTrue, sim) : -1;
        if (head != sim.resolved) {
            sim.resolved = head;
            sim.execNode = -1;
            sim.timer = 0.0f;
            if (head >= 0) beginAction(sp, sim, head);
        }

        // Advance the running timed action (delay / wait).
        if (sim.execNode >= 0 && sim.timer > 0.0f) {
            sim.timer -= dt;
            if (sim.timer <= 0.0f) {
                const LogicNode* n = sp.findNode(sim.execNode);
                if (!n) {
                    sim.execNode = -1;
                } else {
                    // Deferred spawn/despawn effect applies at expiry.
                    if (n->act.type == Action::Spawn) {
                        sim.spawned = true;
                        logLine(sp.name + ": spawned");
                    } else if (n->act.type == Action::Despawn) {
                        sim.spawned = false;
                        logLine(sp.name + ": despawned");
                    }
                    advance(sp, sim, *n);
                }
            }
        }
    }
}

// Walk links from nodeId, evaluating conditions, until an action node is
// reached. Returns its id, or -1 at a dead end.
int SimController::resolveFrom(const SpawnPoint& sp, int nodeId,
                               SpawnSim& sim) {
    int cur = nodeId;
    int guard = 0;
    while (cur >= 0 && guard++ < 64) {
        const LogicNode* n = sp.findNode(cur);
        if (!n) return -1;
        if (n->kind == LogicNode::Act) return cur;
        if (n->kind == LogicNode::Root) {
            cur = n->nextTrue;
            continue;
        }
        cur = eval(*n, sp, sim) ? n->nextTrue : n->nextFalse;
    }
    return -1;
}

bool SimController::eval(const LogicNode& node, const SpawnPoint& sp,
                         SpawnSim& sim) {
    const Condition& c = node.cond;
    auto flagVal = [&](int id) {
        auto it = flags_.find(id);
        return it != flags_.end() ? it->second : 0;
    };
    switch (c.type) {
        case Condition::Always:        return true;
        case Condition::FlagEquals:    return flagVal(c.flagId) == c.value;
        case Condition::FlagNotEquals: return flagVal(c.flagId) != c.value;
        case Condition::FlagGreater:   return flagVal(c.flagId) > c.value;
        case Condition::FlagLess:      return flagVal(c.flagId) < c.value;
        case Condition::RandomChance: {
            // Roll once per node per session, then latch.
            auto it = sim.randomLatch.find(node.id);
            if (it == sim.randomLatch.end()) {
                bool win = (std::rand() % 100) < c.value;
                it = sim.randomLatch.emplace(node.id, win).first;
            }
            return it->second;
        }
        case Condition::PlayerNear: {
            // XZ distance (ground-plane radius), like most games.
            float dx = playerPos_.x - sp.position.x;
            float dz = playerPos_.z - sp.position.z;
            return dx * dx + dz * dz < float(c.value) * float(c.value);
        }
        default: return false;
    }
}

void SimController::beginAction(const SpawnPoint& sp, SpawnSim& sim,
                                int nodeId) {
    const LogicNode* n = sp.findNode(nodeId);
    if (!n || n->kind != LogicNode::Act) {
        sim.execNode = -1;
        return;
    }
    sim.execNode = nodeId;
    sim.timer = 0.0f;
    const Action& a = n->act;
    switch (a.type) {
        case Action::Spawn:
        case Action::Despawn:
            sim.timer = std::max(a.floatParam, 0.0f);
            if (sim.timer == 0.0f) {
                sim.spawned = (a.type == Action::Spawn);
                logLine(sp.name + (a.type == Action::Spawn ? ": spawned"
                                                           : ": despawned"));
                advance(sp, sim, *n);
            }
            // timer > 0: the effect applies at expiry (see update()).
            break;
        case Action::Wait:
            sim.timer = std::max(a.floatParam, 0.0f);
            if (sim.timer == 0.0f) advance(sp, sim, *n);
            break;
        case Action::SetAnimation:
            sim.anim = a.param;
            logLine(sp.name + ": anim '" + a.param + "'");
            advance(sp, sim, *n);
            break;
        case Action::SetFlag:
            flags_[a.intParam] = a.intParam2;
            logLine("flag " + std::to_string(a.intParam) + " = " +
                    std::to_string(a.intParam2));
            advance(sp, sim, *n);
            break;
        case Action::CameraFocus:
            camReqPending_ = true;
            camReqCameraId_ = a.intParam;
            camReqMarkerPos_ = sp.position;
            camReqBlend_ = std::max(a.floatParam, 0.0f);
            if (a.intParam < 0) logLine(sp.name + ": camera focus to marker");
            else logLine(sp.name + ": camera focus to camera #" +
                         std::to_string(a.intParam));
            advance(sp, sim, *n);
            break;
        case Action::DialogLine:
            logLine("[dialog] " + (a.param.empty() ? std::string("(empty)")
                                                   : a.param));
            advance(sp, sim, *n);
            break;
        case Action::PlaySound:
            logLine("[sound] " + (a.param.empty() ? std::string("(empty)")
                                                  : a.param));
            advance(sp, sim, *n);
            break;
        default:
            advance(sp, sim, *n);
            break;
    }
}

void SimController::advance(const SpawnPoint& sp, SpawnSim& sim,
                            const LogicNode& justDone) {
    int next = resolveFrom(sp, justDone.nextTrue, sim);
    if (next >= 0) beginAction(sp, sim, next);
    else sim.execNode = -1;   // chain finished — state persists
}

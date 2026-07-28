#include "spawn.h"
#include <algorithm>
#include <unordered_set>

const char* condTypeName(int type) {
    switch (type) {
        case Condition::Always:        return "Always";
        case Condition::FlagEquals:    return "Flag ==";
        case Condition::FlagNotEquals: return "Flag !=";
        case Condition::FlagGreater:   return "Flag >";
        case Condition::FlagLess:      return "Flag <";
        case Condition::RandomChance:  return "Random %";
        case Condition::PlayerNear:    return "Player near";
        default: return "?";
    }
}

const char* actTypeName(int type) {
    switch (type) {
        case Action::Spawn:        return "Spawn";
        case Action::Despawn:      return "Despawn";
        case Action::SetAnimation: return "Set animation";
        case Action::CameraFocus:  return "Camera focus";
        case Action::Wait:         return "Wait";
        case Action::SetFlag:      return "Set flag";
        case Action::DialogLine:   return "Dialog line";
        case Action::PlaySound:    return "Play sound";
        default: return "?";
    }
}

std::string condSummary(const Condition& c) {
    char buf[96];
    switch (c.type) {
        case Condition::Always:
            return "always";
        case Condition::FlagEquals:
            std::snprintf(buf, sizeof(buf), "flag %d == %d", c.flagId, c.value);
            return buf;
        case Condition::FlagNotEquals:
            std::snprintf(buf, sizeof(buf), "flag %d != %d", c.flagId, c.value);
            return buf;
        case Condition::FlagGreater:
            std::snprintf(buf, sizeof(buf), "flag %d > %d", c.flagId, c.value);
            return buf;
        case Condition::FlagLess:
            std::snprintf(buf, sizeof(buf), "flag %d < %d", c.flagId, c.value);
            return buf;
        case Condition::RandomChance:
            std::snprintf(buf, sizeof(buf), "random %d%%", c.value);
            return buf;
        case Condition::PlayerNear:
            std::snprintf(buf, sizeof(buf), "player < %d m", c.value);
            return buf;
        default:
            return "?";
    }
}

std::string actSummary(const Action& a) {
    char buf[128];
    switch (a.type) {
        case Action::Spawn:
            if (a.floatParam > 0.0f) {
                std::snprintf(buf, sizeof(buf), "after %.1fs", a.floatParam);
                return buf;
            }
            return "immediately";
        case Action::Despawn:
            std::snprintf(buf, sizeof(buf), "after %.1fs", a.floatParam);
            return buf;
        case Action::SetAnimation:
            return a.param.empty() ? std::string("(no animation)")
                                   : "'" + a.param + "'";
        case Action::CameraFocus:
            if (a.intParam < 0) return "to this marker";
            std::snprintf(buf, sizeof(buf), "to camera #%d (%.1fs)",
                          a.intParam, a.floatParam);
            return buf;
        case Action::Wait:
            std::snprintf(buf, sizeof(buf), "%.1f s", a.floatParam);
            return buf;
        case Action::SetFlag:
            std::snprintf(buf, sizeof(buf), "flag %d = %d", a.intParam, a.intParam2);
            return buf;
        case Action::DialogLine:
            return a.param.empty() ? std::string("(no dialog id)")
                                   : "'" + a.param + "'";
        case Action::PlaySound:
            return a.param.empty() ? std::string("(no sound)")
                                   : "'" + a.param + "'";
        default:
            return "?";
    }
}

int SpawnPoint::addNode(LogicNode::Kind kind, const glm::vec2& uiPos) {
    LogicNode n;
    n.id = nextNodeId++;
    n.kind = kind;
    n.uiPos = uiPos;
    nodes.push_back(n);
    return n.id;
}

bool SpawnPoint::removeNode(int nodeId) {
    if (nodeId == rootId) return false;   // the root is fixed
    auto it = std::find_if(nodes.begin(), nodes.end(),
                           [nodeId](const LogicNode& n) { return n.id == nodeId; });
    if (it == nodes.end()) return false;
    nodes.erase(it);
    // Unlink every reference to the removed node.
    for (auto& n : nodes) {
        if (n.nextTrue == nodeId)  n.nextTrue = -1;
        if (n.nextFalse == nodeId) n.nextFalse = -1;
    }
    return true;
}

LogicNode* SpawnPoint::findNode(int nodeId) {
    for (auto& n : nodes)
        if (n.id == nodeId) return &n;
    return nullptr;
}

const LogicNode* SpawnPoint::findNode(int nodeId) const {
    for (const auto& n : nodes)
        if (n.id == nodeId) return &n;
    return nullptr;
}

LogicNode* SpawnPoint::rootNode() {
    return findNode(rootId);
}

bool spawnGraphReachable(const SpawnPoint& sp, int fromId, int toId) {
    if (fromId < 0) return false;
    std::unordered_set<int> visited;
    std::vector<int> stack{fromId};
    while (!stack.empty()) {
        int cur = stack.back();
        stack.pop_back();
        if (cur == toId) return true;
        if (!visited.insert(cur).second) continue;
        const LogicNode* n = sp.findNode(cur);
        if (!n) continue;
        if (n->nextTrue >= 0)  stack.push_back(n->nextTrue);
        if (n->nextFalse >= 0) stack.push_back(n->nextFalse);
    }
    return false;
}

int SpawnManager::addSpawn(SpawnPoint sp) {
    sp.id = nextId_++;
    if (sp.rootId < 0 || !sp.findNode(sp.rootId)) {
        // Every marker owns a root node: the graph entry point.
        LogicNode root;
        root.kind = LogicNode::Root;
        root.id = sp.nextNodeId++;
        root.uiPos = glm::vec2(20.0f, 120.0f);
        sp.rootId = root.id;
        sp.nodes.insert(sp.nodes.begin(), root);
    }
    spawns_.push_back(std::move(sp));
    return spawns_.back().id;
}

void SpawnManager::addSpawnWithId(const SpawnPoint& sp) {
    spawns_.push_back(sp);
    nextId_ = std::max(nextId_, sp.id + 1);
}

bool SpawnManager::removeSpawn(int id) {
    auto it = std::find_if(spawns_.begin(), spawns_.end(),
                           [id](const SpawnPoint& s) { return s.id == id; });
    if (it == spawns_.end()) return false;
    spawns_.erase(it);
    return true;
}

SpawnPoint* SpawnManager::findSpawn(int id) {
    for (auto& s : spawns_)
        if (s.id == id) return &s;
    return nullptr;
}

const SpawnPoint* SpawnManager::findSpawn(int id) const {
    for (const auto& s : spawns_)
        if (s.id == id) return &s;
    return nullptr;
}

void SpawnManager::clear() {
    spawns_.clear();
    nextId_ = 0;
}

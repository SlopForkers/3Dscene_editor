#pragma once
#include "spawn.h"
#include <glm/glm.hpp>
#include <deque>
#include <string>
#include <unordered_map>

// ---------------------------------------------------------------------------
// Runtime simulation of spawn logic graphs, for in-editor testing.
//
// Continuous evaluation: every frame each marker's graph is resolved from the
// root through its conditions to the action node that should run now. When
// the resolution changes (a flag flipped, the player moved), the new action
// chain starts executing. Actions apply visible effects (spawned flag,
// current animation name, flag writes, log lines, camera focus requests) and
// chain through waits/delays. RandomChance results latch per session.
// ---------------------------------------------------------------------------

class SimController {
public:
    void start(const SpawnManager& spawns);
    void stop();
    bool running() const { return running_; }

    // playerPos feeds PlayerNear conditions (the editor passes the camera
    // target as the player proxy).
    void update(float dt, const SpawnManager& spawns, const glm::vec3& playerPos);

    // Game flags (missing = 0). Editable from the Simulation panel; saved in
    // the scene as the initial flag values.
    std::unordered_map<int, int>& flags() { return flags_; }
    const std::unordered_map<int, int>& flags() const { return flags_; }

    // Per-marker runtime state (rendering/panels); nullptr when unknown.
    struct SpawnSim {
        bool spawned = false;
        std::string anim;
        int execNode = -1;                    // action node being executed
        float timer = 0.0f;                   // delay/wait countdown
        int resolved = -2;                    // resolved action head last frame
        std::unordered_map<int, bool> randomLatch;  // nodeId -> roll
    };
    const SpawnSim* simFor(int spawnId) const;

    const std::deque<std::string>& log() const { return log_; }
    void logLine(const std::string& text);
    void clearLog() { log_.clear(); }

    // Camera focus requests (CameraFocus actions); consumed by the editor
    // each frame. cameraId -1 = focus the marker position instead.
    bool takeCamRequest(int& cameraId, glm::vec3& markerPos, float& blend);

private:
    int resolveFrom(const SpawnPoint& sp, int nodeId, SpawnSim& sim);
    bool eval(const LogicNode& node, const SpawnPoint& sp, SpawnSim& sim);
    void beginAction(const SpawnPoint& sp, SpawnSim& sim, int nodeId);
    void advance(const SpawnPoint& sp, SpawnSim& sim, const LogicNode& justDone);

    bool running_ = false;
    std::unordered_map<int, int> flags_;
    std::unordered_map<int, SpawnSim> sims_;
    std::deque<std::string> log_;
    glm::vec3 playerPos_ = glm::vec3(0.0f);

    bool camReqPending_ = false;
    int camReqCameraId_ = -1;
    glm::vec3 camReqMarkerPos_ = glm::vec3(0.0f);
    float camReqBlend_ = 0.0f;
};

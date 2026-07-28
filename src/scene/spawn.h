#pragma once
#include <glm/glm.hpp>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Character spawn markers with a condition/action logic graph.
//
// A spawn point marks where a character appears in the game. It may carry a
// 3D model + animations itself, or be a pure logic spawn (empty modelPath) —
// the game runtime then decides what (if anything) becomes visible.
//
// Each marker owns a small node graph evaluated by the game:
//   Root -> Condition (true/false branches) -> Action (chained)
// Example:  if flag1 start_emotion_happy else start_emotion_sad
//   ROOT -> [Flag 1 == 1] -true-> [SetAnimation "start_emotion_happy"]
//                        -false-> [SetAnimation "start_emotion_sad"]
// ---------------------------------------------------------------------------

struct Condition {
    enum Type : int {
        Always = 0,
        FlagEquals, FlagNotEquals, FlagGreater, FlagLess,
        RandomChance,   // value = percent 0..100
        PlayerNear      // value = radius (m) around the marker
    };
    Type type = Always;
    int flagId = 0;
    int value  = 0;
};

struct Action {
    enum Type : int {
        Spawn = 0,      // floatParam = delay (s) before appearing
        Despawn,        // floatParam = delay (s)
        SetAnimation,   // param = animation name (resolved by the game)
        CameraFocus,    // intParam: -1 = this marker, else scene-camera id;
                        // floatParam = blend time (s)
        Wait,           // floatParam = seconds before the next action
        SetFlag,        // intParam = flag id, intParam2 = value
        DialogLine,     // param = dialog/text id
        PlaySound       // param = sound name
    };
    Type type = Spawn;
    std::string param;      // animation / dialog / sound name
    int intParam  = -1;     // camera id (-1 = this marker) / flag id
    int intParam2 = 0;      // flag value (SetFlag)
    float floatParam = 0.0f;// delay / wait / blend seconds
};

struct LogicNode {
    // NB: the constants are Cond/Act, not Condition/Action — those would
    // shadow the Condition/Action struct types inside this struct's scope.
    enum Kind : int { Root = 0, Cond, Act };

    int id = -1;
    Kind kind = Root;
    Condition cond;   // kind == Condition
    Action act;       // kind == Action
    glm::vec2 uiPos = glm::vec2(0.0f);  // node-editor canvas position
    int nextTrue  = -1; // condition-true branch / action next / root child
    int nextFalse = -1; // condition-false branch only
};

const char* condTypeName(int type);
const char* actTypeName(int type);
// One-line summaries for node bodies and lists.
std::string condSummary(const Condition& c);
std::string actSummary(const Action& a);

struct SpawnPoint {
    int id = -1;
    std::string name = "Spawn";
    std::string tag;                        // free-form game metadata
    glm::vec3 position = glm::vec3(0.0f);
    float yaw = 0.0f;                       // facing, radians around Y

    // Optional character content; empty modelPath = pure logic spawn.
    std::string modelPath;
    float scale = 1.0f;
    std::string defaultAnim;                // animation at spawn

    // Logic graph (the game evaluates it when the scene starts / triggers).
    std::vector<LogicNode> nodes;
    int rootId = -1;
    int nextNodeId = 0;

    int addNode(LogicNode::Kind kind, const glm::vec2& uiPos);  // returns id
    bool removeNode(int nodeId);   // false for the root; unlinks references
    LogicNode* findNode(int nodeId);
    const LogicNode* findNode(int nodeId) const;
    LogicNode* rootNode();
};

// True when following nextTrue/nextFalse links from `fromId` can reach
// `toId` — used to reject links that would create a cycle.
bool spawnGraphReachable(const SpawnPoint& sp, int fromId, int toId);

class SpawnManager {
public:
    // Ensures the point has a root node. Returns the assigned id.
    int addSpawn(SpawnPoint sp);
    void addSpawnWithId(const SpawnPoint& sp);   // undo / scene load
    bool removeSpawn(int id);
    SpawnPoint* findSpawn(int id);
    const SpawnPoint* findSpawn(int id) const;
    const std::vector<SpawnPoint>& spawns() const { return spawns_; }
    void clear();

private:
    std::vector<SpawnPoint> spawns_;
    int nextId_ = 0;
};

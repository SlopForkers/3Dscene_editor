#pragma once
#include <glm/glm.hpp>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Procedural material graphs (GL-free, unit-tested).
//
// A material is a DAG of texture-space nodes evaluated on the CPU into a
// baked RGBA8 texture. The editor exports the bake as PNG and assigns it to
// terrain layers / the block texture library — so the game consumes plain
// textures and never evaluates the graph. Graphs themselves persist in the
// scene file for further editing.
//
// Evaluation: per pixel (u, v in [0,1]) each node produces a vec4; scalar
// values are carried as grey (r=g=b). Nodes are evaluated in topological
// order once per pixel; HeightToNormal additionally samples its input at
// four offsets (central differences on luminance).
// ---------------------------------------------------------------------------

enum class MatNodeType : int {
    Output = 0,     // terminal: the material's albedo
    Image,          // file texture; p = tileU,tileV,offU,offV; path
    SolidColor,     // color
    Noise,          // p = scale,persistence,lacunarity; ip = type,seed,octaves
    Checker,        // p[0] = scale
    Gradient,       // p[0] = angle (radians), linear 0..1
    Mix,            // in = A, B, Fac(pin or p[0])
    Multiply,       // in = A, B
    Add,            // in = A, B
    BrightContrast, // p = brightness (add), contrast (mul, pivot 0.5)
    Invert,
    Grayscale,
    HeightToNormal, // p[0] = strength; tangent-space normal from luminance
    Count
};

const char* matNodeTypeName(int type);
int  matNodeInputCount(MatNodeType t);          // 0..3 input pins
const char* matNodeInputName(MatNodeType t, int pin);

struct MatNode {
    int id = -1;
    MatNodeType type = MatNodeType::SolidColor;
    glm::vec2 uiPos = glm::vec2(0.0f);  // editor canvas position
    int in[4] = {-1, -1, -1, -1};       // input node ids (-1 = unconnected)
    glm::vec4 color = glm::vec4(1.0f);  // SolidColor value
    float p[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    int ip[4] = {0, 0, 0, 0};
    std::string path;                   // Image source (UTF-8)
};

struct MaterialGraph {
    int id = -1;
    std::string name = "Material";
    std::vector<MatNode> nodes;
    int outputId = -1;                  // the Output node
    int nextNodeId = 0;
    std::string bakedPath;              // where the last PNG bake was written

    int addNode(MatNodeType type, const glm::vec2& uiPos);  // returns id
    bool removeNode(int nodeId);        // false for the Output node
    MatNode* findNode(int nodeId);
    const MatNode* findNode(int nodeId) const;
};

class MaterialLibrary {
public:
    // Ensures the material has an Output node. Returns the assigned id.
    int addMaterial(MaterialGraph g);
    void addMaterialWithId(const MaterialGraph& g);   // undo / scene load
    bool removeMaterial(int id);
    MaterialGraph* findMaterial(int id);
    const MaterialGraph* findMaterial(int id) const;
    const std::vector<MaterialGraph>& materials() const { return materials_; }
    void clear();

private:
    std::vector<MaterialGraph> materials_;
    int nextId_ = 0;
};

// True when following `in[]` links from `fromId` can reach `toId` — used to
// reject links that would create a cycle.
bool matGraphReachable(const MaterialGraph& g, int fromId, int toId);

// Bake the Output node into RGBA8 pixels (row-major, w*h*4 bytes).
// False on structural problems (no Output node / bad size); cycles are
// impossible through the UI (matGraphReachable) and degrade to black via a
// recursion depth guard if a hand-edited file slips one through.
bool bakeMaterial(const MaterialGraph& g, int w, int h,
                  std::vector<uint8_t>& outPixels);

// Write RGBA8 pixels as a PNG. UTF-8 safe (goes through writeFileBytes).
bool writePng(const std::string& path, int w, int h,
              const std::vector<uint8_t>& pixels);

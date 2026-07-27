#pragma once
#include <glad/gl.h>
#include <glm/glm.hpp>
#include <vector>

class Shader;
class Terrain;

// Snap-based block building system. Foundation blocks sink into the terrain;
// further blocks stack on top of or beside existing blocks via face snapping.
class BuildSystem {
public:
    enum BlockType { Foundation = 0, Wall = 1 };

    struct Block {
        glm::vec3 position = glm::vec3(0.0f);   // centre
        glm::vec3 size     = glm::vec3(2.0f);   // full dimensions
        glm::vec3 color    = glm::vec3(0.6f);
        BlockType type     = Wall;
        int id             = 0;

        glm::vec3 min() const { return position - size * 0.5f; }
        glm::vec3 max() const { return position + size * 0.5f; }
        glm::vec3 top() const { return position + glm::vec3(0, size.y * 0.5f, 0); }
        glm::vec3 bottom() const { return position - glm::vec3(0, size.y * 0.5f, 0); }
    };

    BuildSystem() = default;
    ~BuildSystem();

    void create();
    void destroy();

    // Block dimensions used when placing new blocks (x = z = width, y = height).
    void setBlockSize(float w, float h) { blockW_ = w; blockH_ = h; }
    float blockWidth() const { return blockW_; }
    float blockHeight() const { return blockH_; }
    void  setSunkDepth(float s) { sunkDepth_ = glm::clamp(s, 0.0f, 1.0f); }
    float sunkDepth() const { return sunkDepth_; }
    void  setGridStep(float s) { gridStep_ = std::max(0.25f, s); }
    float gridStep() const { return gridStep_; }
    void  setColor(const glm::vec3& c) { color_ = c; }
    const glm::vec3& color() const { return color_; }

    // Compute a placement from a world ray. If it hits a block face, the new
    // block snaps adjacent on that face. Otherwise it raycasts the terrain and
    // produces a foundation block (sunk into the ground). Returns true if a
    // valid placement exists and fills out the placement centre/size/type.
    bool computePlacement(const Terrain& terrain,
                          const glm::vec3& rayOrigin, const glm::vec3& rayDir,
                          glm::vec3& outCenter, glm::vec3& outSize,
                          BlockType& outType) const;

    // Place a block with explicit parameters. Returns its id.
    int  placeBlock(const glm::vec3& center, const glm::vec3& size,
                    BlockType type, const glm::vec3& color);

    void removeBlock(int id);
    void clear();

    // Ray-pick the closest block (AABB slab test). On hit, returns the id, the
    // world hit point and the hit face normal (+X/-X/+Y/-Y/+Z/-Z).
    int  pick(const glm::vec3& rayOrigin, const glm::vec3& rayDir,
              glm::vec3& outHitPos, glm::vec3& outHitNormal) const;

    int  count() const { return (int)blocks_.size(); }
    const std::vector<Block>& blocks() const { return blocks_; }
    const Block* findBlock(int id) const;

    // Area placement: fill grid cells within `radius` (XZ disc) around
    // `worldPos` with foundation blocks, joining any adjacent foundation at the
    // same Y level. Skips cells already occupied. Returns the number placed.
    int  paintArea(const Terrain& terrain, const glm::vec3& worldPos,
                   float radius, BlockType type);
    // Fill a grid-aligned rectangle (world-space corners x0..x1, z0..z1) with
    // blocks. Joins any adjacent foundation at the same Y level. Returns count.
    int  fillRect(const Terrain& terrain,
                  float x0, float z0, float x1, float z1, BlockType type);
    // Erase all blocks within `radius` (XZ) of `worldPos`.
    int  eraseArea(const glm::vec3& worldPos, float radius);
    // Erase all blocks within a grid-aligned rectangle.
    int  eraseRect(float x0, float z0, float x1, float z1);
    // True if a block already occupies grid cell (x,z).
    bool cellOccupied(float x, float z) const;

    // Render all placed blocks (opaque) using the lit block shader.
    void render(const Shader& shader, const glm::mat4& viewProj,
                const glm::vec3& lightDir, const glm::vec3& camPos) const;

    // Render a semi-transparent ghost block plus a wireframe outline.
    void renderGhost(const Shader& shader, const glm::mat4& viewProj,
                     const glm::vec3& lightDir, const glm::vec3& camPos,
                     const glm::vec3& center, const glm::vec3& size,
                     const glm::vec3& color) const;

    // Render a wireframe outline around the block with the given id (used for
    // the selected block highlight).
    void renderWireframe(const Shader& shader, const glm::mat4& viewProj,
                         int id, const glm::vec3& color) const;

    // Render a wireframe outline around an arbitrary box (used for the ghost
    // preview outline).
    void renderWireframeBox(const Shader& shader, const glm::mat4& viewProj,
                            const glm::vec3& center, const glm::vec3& size,
                            const glm::vec3& color) const;

    // Reproject foundation blocks after terrain edits so they remain sunk at
    // the new height. Only blocks within radius of `center` are updated.
    void reproject(const Terrain& terrain, const glm::vec3& center, float radius);

private:
    std::vector<Block> blocks_;
    int nextId_ = 1;

    // Placement settings (mirrored in the UI).
    float blockW_   = 2.0f;
    float blockH_   = 2.0f;
    float sunkDepth_ = 0.7f;          // fraction of foundation height buried
    float gridStep_ = 2.0f;
    glm::vec3 color_ = glm::vec3(0.55f, 0.45f, 0.35f);

    // Shared cube mesh (positions + normals, indexed).
    GLuint vao_ = 0, vbo_ = 0, ibo_ = 0;
    int    indexCount_ = 0;
    // Edge-only cube for wireframe / outlines.
    GLuint wireVao_ = 0, wireVbo_ = 0;

    void  initCubeMesh();
    void  initWireCube();
    void  drawCube(const Shader& shader, const glm::mat4& viewProj,
                   const glm::vec3& lightDir, const glm::vec3& camPos,
                   const glm::vec3& center, const glm::vec3& size,
                   const glm::vec3& color, float alpha) const;
    float snapToGrid(float v) const;
};

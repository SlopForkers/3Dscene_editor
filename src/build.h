#pragma once
#include <glad/gl.h>
#include <glm/glm.hpp>
#include <string>
#include <vector>

class Shader;
class Terrain;

// Snap-based block building system. Foundation blocks sink into the terrain;
// further blocks stack on top of or beside existing blocks via face snapping.
class BuildSystem {
public:
    enum BlockType { Foundation = 0, Wall = 1 };
    enum Mode { ModeFoundation = 0, ModeWall = 1, ModeTexture = 2 };
    // Cube face indices (match the mesh face order + the shader's face test).
    enum Face { FacePX = 0, FaceNX = 1, FacePY = 2, FaceNY = 3, FacePZ = 4, FaceNZ = 5 };

    struct Block {
        glm::vec3 position = glm::vec3(0.0f);   // centre
        glm::vec3 size     = glm::vec3(2.0f);   // full dimensions (pre-yaw)
        glm::vec3 color    = glm::vec3(0.6f);
        BlockType type     = Wall;
        float     yaw      = 0.0f;              // rotation around Y (radians)
        int id             = 0;
        // Optional per-face texture: index into blockTextures_ (-1 = none) and
        // which cube face it paints onto (-1 = none).
        int textureIdx    = -1;
        int textureFace    = -1;
        float texScale     = 1.0f;              // UV tiling multiplier (Tile mode)
        int   texMode      = 0;                // 0 = Stretch, 1 = Tile

        // Axis-aligned footprint. For yaw near 90°/270° the X and Z swap.
        glm::vec3 aabbSize() const {
            float n = std::fmod(std::abs(yaw), 3.14159265f);
            bool rotated = n > 0.5f && n < 2.5f;
            return rotated ? glm::vec3(size.z, size.y, size.x) : size;
        }
        glm::vec3 min() const { return position - aabbSize() * 0.5f; }
        glm::vec3 max() const { return position + aabbSize() * 0.5f; }
        glm::vec3 top() const { return position + glm::vec3(0, size.y * 0.5f, 0); }
        glm::vec3 bottom() const { return position - glm::vec3(0, size.y * 0.5f, 0); }
    };

    struct BlockTexture {
        GLuint      glId = 0;
        std::string path;
        std::string name;
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

    // Active placement mode (foundation vs wall) and wall rotation.
    void setMode(Mode m) { mode_ = m; }
    Mode mode() const { return mode_; }
    void setWallThickness(float t) { wallThickness_ = std::max(0.1f, t); }
    float wallThickness() const { return wallThickness_; }
    // Wall edge rotation: 0=+X, 1=+Z, 2=-X, 3=-Z (which edge of the block the
    // wall sits on). R cycles through these 4 positions.
    int  wallEdge() const { return wallEdge_; }
    void setWallEdge(int e) { wallEdge_ = ((e % 4) + 4) % 4; }
    void rotateWallEdge() { wallEdge_ = (wallEdge_ + 1) % 4; }
    // Does the wall run along X (edges +Z/-Z) or Z (edges +X/-X)?
    bool wallAlongX() const { return wallEdge_ == 1 || wallEdge_ == 3; }
    // Signed offset of the wall centre from the block centre along the
    // perpendicular axis (in world units, including thickness/2).
    float wallEdgeOffset(float halfSize) const;

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
                    BlockType type, const glm::vec3& color, float yaw = 0.0f);

    void removeBlock(int id);
    void clear();

    // Ray-pick the closest block (AABB slab test). On hit, returns the id, the
    // world hit point and the hit face normal (+X/-X/+Y/-Y/+Z/-Z).
    int  pick(const glm::vec3& rayOrigin, const glm::vec3& rayDir,
              glm::vec3& outHitPos, glm::vec3& outHitNormal) const;

    int  count() const { return (int)blocks_.size(); }
    const std::vector<Block>& blocks() const { return blocks_; }
    const Block* findBlock(int id) const;
    Block* findBlockMutable(int id);

    // --- Block face texture library ---
    // Load an image file into the shared texture library. Returns its index
    // (or -1 on failure). Reuses an existing entry if the path already loaded.
    int  loadBlockTexture(const std::string& path);
    int  blockTextureCount() const { return (int)blockTextures_.size(); }
    GLuint blockTextureId(int i) const;
    const std::string& blockTextureName(int i) const;
    const std::string& blockTexturePath(int i) const;
    int  findBlockTextureByPath(const std::string& path) const;
    void removeBlockTexture(int idx);
    // Map a hit face normal to a Face enum index.
    static int faceFromNormal(const glm::vec3& n);

    // Currently active texture for the texture-paint mode (-1 = none).
    int  currentTexture() const { return currentTextureIdx_; }
    void setCurrentTexture(int idx) { currentTextureIdx_ = idx; }
    // Default UV scale applied when painting a texture onto a fresh block.
    float defaultTexScale() const { return defaultTexScale_; }
    void  setDefaultTexScale(float s) { defaultTexScale_ = std::max(0.01f, s); }
    // Default texture mode: 0 = Stretch (one copy fills the face),
    // 1 = Tile (repeat uTexScale times across the face).
    int   defaultTexMode() const { return defaultTexMode_; }
    void  setDefaultTexMode(int m) { defaultTexMode_ = (m == 1) ? 1 : 0; }

    // Per-block face texture assignment.
    void setBlockFaceTexture(int blockId, int textureIdx, int face);
    void clearBlockFaceTexture(int blockId);
    void setBlockTexScale(int blockId, float scale);
    void setBlockTexMode(int blockId, int mode);
    // Paint the currently-selected texture onto a block's face. Convenience
    // wrapper around setBlockFaceTexture using currentTexture().
    void paintCurrentTexture(int blockId, int face);
    // Apply the active texture to every block's `face` inside an XZ rectangle
    // (foundation-style drag) or along a 1-D line (wall-style drag). Returns
    // the number of blocks textured.
    int  applyTextureToRect(float x0, float z0, float x1, float z1, int face);
    int  applyTextureToLine(float startC, float endC, float fixedC,
                            bool alongX, int face);

    // Area placement: fill grid cells within `radius` (XZ disc) around
    // `worldPos` with foundation blocks, joining any adjacent foundation at the
    // same Y level. Skips cells already occupied. Returns the number placed.
    int  paintArea(const Terrain& terrain, const glm::vec3& worldPos,
                   float radius, BlockType type);
    // Fill a grid-aligned rectangle (world-space corners x0..x1, z0..z1) with
    // blocks. Joins any adjacent foundation at the same Y level. Returns count.
    int  fillRect(const Terrain& terrain,
                  float x0, float z0, float x1, float z1, BlockType type);
    // Place wall plates along a straight line on top of supporting blocks.
    // The line runs along X (alongX=true) or Z (alongX=false) at a fixed
    // coordinate (the edge of the supporting block). baseY is the supporting
    // top. Walls are thin plates oriented perpendicular to the edge. Returns
    // the number placed.
    int  fillWallLine(float startCoord, float endCoord, float fixedCoord,
                      float baseY, bool alongX);
    // Compute the wall line parameters (fixed coord + along axis) for placing a
    // wall on the edge of the picked block, given the current wall edge.
    void wallLineParamsFor(const Block& support, float& outFixed, bool& outAlongX) const;
    // Erase all blocks within `radius` (XZ) of `worldPos`.
    int  eraseArea(const glm::vec3& worldPos, float radius);
    // Erase all blocks within a grid-aligned rectangle.
    int  eraseRect(float x0, float z0, float x1, float z1);
    // True if a block already occupies grid cell (x,z).
    bool cellOccupied(float x, float z) const;

    // Render all placed blocks (opaque) using the lit block shader.
    void render(const Shader& shader, const glm::mat4& viewProj,
                const glm::vec3& lightDir, const glm::vec3& camPos) const;

    // Render a semi-transparent ghost block (caller draws the wireframe
    // outline separately via renderWireframeBox).
    void renderGhost(const Shader& shader, const glm::mat4& viewProj,
                     const glm::vec3& lightDir, const glm::vec3& camPos,
                     const glm::vec3& center, const glm::vec3& size,
                     const glm::vec3& color, float yaw) const;

    // Render a wireframe outline around the block with the given id (used for
    // the selected block highlight).
    void renderWireframe(const Shader& shader, const glm::mat4& viewProj,
                         int id, const glm::vec3& color) const;

    // Render a wireframe outline around an arbitrary box (used for the ghost
    // preview outline).
    void renderWireframeBox(const Shader& shader, const glm::mat4& viewProj,
                            const glm::vec3& center, const glm::vec3& size,
                            const glm::vec3& color, float yaw) const;

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

    Mode  mode_ = ModeFoundation;
    float wallThickness_ = 0.4f;
    int   wallEdge_ = 0;  // 0=+X, 1=+Z, 2=-X, 3=-Z
    // Texture-paint mode: which library texture is active.
    int   currentTextureIdx_ = -1;
    float defaultTexScale_ = 1.0f;
    int   defaultTexMode_ = 0;   // 0 = Stretch, 1 = Tile

    // Shared cube mesh (positions + normals, indexed).
    GLuint vao_ = 0, vbo_ = 0, ibo_ = 0;
    int    indexCount_ = 0;
    // Edge-only cube for wireframe / outlines.
    GLuint wireVao_ = 0, wireVbo_ = 0;
    // 1x1 white fallback so the sampler is always bound.
    GLuint defaultTex_ = 0;
    // Shared texture library referenced by Block::textureIdx.
    std::vector<BlockTexture> blockTextures_;

    void  initCubeMesh();
    void  initWireCube();
    void  initDefaultTex();
    void  drawCube(const Shader& shader, const glm::mat4& viewProj,
                   const glm::vec3& lightDir, const glm::vec3& camPos,
                   const glm::vec3& center, const glm::vec3& size,
                   const glm::vec3& color, float alpha, float yaw,
                   int textureIdx, int textureFace, float texScale,
                   int texMode) const;
    float snapToGrid(float v) const;
};

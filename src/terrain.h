#pragma once
#include <glad/gl.h>
#include <glm/glm.hpp>
#include <vector>
#include <string>

// A heightfield terrain rendered as a regular grid of triangles.
// Heights live in a flat array and are edited by brushes.
class Terrain {
public:
    struct BrushParams {
        enum Type { Raise = 0, Lower, Smooth, Flatten, Noise, Set, Texture, Vegetation };
        Type type = Raise;
        float radius   = 12.0f;   // world units
        float strength = 0.25f;   // height units per stroke step
        float target   = 0.0f;    // used by Flatten/Set
        int   textureLayer = 0;   // which splat layer the Texture brush paints
        enum Falloff { FalloffSmooth = 0, FalloffLinear, FalloffConstant };
        int falloff = FalloffSmooth;
    };

    // A texture layer blended onto the terrain via the splatmap. Up to 4 layers
    // are supported (one per RGBA channel of the splat texture).
    struct Layer {
        GLuint albedo = 0;
        GLuint normal = 0;
        float  tileSize = 8.0f;   // world units per texture tile
        std::string name;
        bool   hasNormal = false;
    };

    Terrain(int gridX = 256, int gridZ = 256, float worldSize = 200.0f);

    void create();
    void destroy();

    // Apply a brush stroke at a world-space (xz) location.
    // Returns true if the terrain was modified.
    bool applyBrush(const BrushParams& bp, const glm::vec3& worldPos);

    // Intersect a world-space ray with the terrain heightfield.
    // Returns true if hit; fills outPoint with the world hit location.
    bool raycast(const glm::vec3& rayOrigin, const glm::vec3& rayDir,
                 glm::vec3& outPoint) const;

    // Height sampling (bilinear) at world (x, z) coordinates.
    float heightAtWorld(float worldX, float worldZ) const;

    void draw() const;

    // Bind splatmap + layer textures to texture units and set the matching
    // sampler uniforms on the bound shader. Call after terrainShader_.use().
    void bindTextures(const class Shader& shader) const;

    // Mesh / grid info.
    int gridX() const { return gridX_; }
    int gridZ() const { return gridZ_; }
    float worldSize() const { return worldSize_; }
    float minHeight() const { return statsMin_; }
    float maxHeight() const { return statsMax_; }

    // ---- Vertex editing API ----
    // World-space position of grid vertex (ix, iz).
    glm::vec3 vertexPos(int ix, int iz) const;
    // Normal at grid vertex (ix, iz).
    glm::vec3 vertexNormal(int ix, int iz) const;
    // Get/set raw height. setHeightRaw only updates the height value and the
    // vertex position; callers must call refresh() afterwards to recompute
    // normals and upload the VBO.
    float getHeight(int ix, int iz) const;
    void setHeightRaw(int ix, int iz, float h);
    // Recompute normals over [x0,z0]..[x1,z1] (clamped) and upload the VBO.
    void refresh(int x0, int z0, int x1, int z1);
    // Snap a world-space point to the nearest grid vertex coordinates.
    void snapWorldToVertex(const glm::vec3& world, int& outIx, int& outIz) const;
    // World-space X/Z for grid coords (public for gizmo math).
    float worldXAt(int ix) const { return (float(ix) / float(gridX_ - 1) - 0.5f) * worldSize_; }
    float worldZAt(int iz) const { return (float(iz) / float(gridZ_ - 1) - 0.5f) * worldSize_; }

    // Texture layers.
    int  layerCount() const { return (int)layers_.size(); }
    const std::vector<Layer>& layers() const { return layers_; }
    // Load (or replace) a layer's albedo/normal texture from a file. Returns
    // true on success. Pass layerIndex in [0,3].
    bool loadLayerAlbedo(int layerIndex, const std::string& path);
    bool loadLayerNormal(int layerIndex, const std::string& path);
    void setLayerTileSize(int layerIndex, float tileSize);
    void resetSplat();   // clear splat to layer 0 only

    // Reset everything flat.
    void flatten(float height = 0.0f);
    // Generate a gentle starting landscape (a couple of hills) for demonstration.
    void generateHills();

    // Vertex layout used by the GPU buffer.
    struct Vertex {
        glm::vec3 position;
        glm::vec3 normal;
    };

private:
    int   gridX_     = 0;
    int   gridZ_     = 0;
    float worldSize_ = 0.0f;

    std::vector<float>  heights_;     // gridX_ * gridZ_
    std::vector<Vertex> vertices_;    // gridX_ * gridZ_
    std::vector<GLuint> indices_;

    GLuint vao_ = 0, vbo_ = 0, ebo_ = 0;
    int indexCount_ = 0;
    bool  dirty_ = false;

    // Texture layers + splatmap.
    std::vector<Layer>   layers_;     // up to 4
    std::vector<uint8_t> splat_;      // gridX_ * gridZ_ * 4 (RGBA8 weights)
    GLuint splatTex_ = 0;

    float statsMin_ = 0.0f;
    float statsMax_ = 0.0f;

    // Coordinate helpers.
    inline int idx(int ix, int iz) const { return iz * gridX_ + ix; }
    float worldX(int ix) const { return (float(ix) / float(gridX_ - 1) - 0.5f) * worldSize_; }
    float worldZ(int iz) const { return (float(iz) / float(gridZ_ - 1) - 0.5f) * worldSize_; }
    int   clampIX(int ix) const { return ix < 0 ? 0 : (ix >= gridX_ ? gridX_ - 1 : ix); }
    int   clampIZ(int iz) const { return iz < 0 ? 0 : (iz >= gridZ_ ? gridZ_ - 1 : iz); }

    float getH(int ix, int iz) const;
    void  setH(int ix, int iz, float h);

    float falloff(float dist, float radius, int mode) const;
    void  recomputeNormals(int x0, int z0, int x1, int z1);
    void  recomputeAllNormals();
    void  updateStats();
    void  uploadVertices(bool fullReupload = true);
    void  uploadSplat();
    void  initTextureLayers();   // procedural default textures + splat
    static GLuint makeProceduralTexture(const glm::vec3& baseColor, float variation);
    static GLuint loadTextureFile(const std::string& path);
};

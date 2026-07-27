#pragma once
#include <glad/gl.h>
#include <glm/glm.hpp>
#include <vector>
#include <string>

// A heightfield terrain rendered as a regular grid of triangles.
// Heights live in a flat array and are edited by brushes.
class Terrain {
public:
    static constexpr int MAX_LAYERS     = 16;   // total paintable texture layers
    static constexpr int NUM_SPLAT_MAPS = 4;    // RGBA splat textures (4 chan each)
    static constexpr int ARRAY_SIZE     = 512;  // every layer resampled to this size

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

    // A texture layer blended onto the terrain via the multi-splat map. Up to
    // MAX_LAYERS layers are supported (4 RGBA splat textures x 4 channels).
    // Each layer's albedo/normal is resampled to ARRAY_SIZE x ARRAY_SIZE and
    // also kept as CPU pixels so the GPU array textures can be rebuilt cheaply.
    struct Layer {
        GLuint albedo = 0;        // 2D texture (ARRAY_SIZE^2) for ImGui preview
        GLuint normal = 0;        // 2D texture (ARRAY_SIZE^2) for ImGui preview
        std::vector<uint8_t> albedoPix;  // ARRAY_SIZE*ARRAY_SIZE*4 (RGBA8)
        std::vector<uint8_t> normalPix;  // ARRAY_SIZE*ARRAY_SIZE*4 (RGBA8) or empty
        float  tileSize = 8.0f;   // world units per texture tile
        std::string name;
        std::string albedoPath;
        std::string normalPath;
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
    // true on success. layerIndex in [0, layerCount()).
    bool loadLayerAlbedo(int layerIndex, const std::string& path);
    bool loadLayerNormal(int layerIndex, const std::string& path);
    void setLayerTileSize(int layerIndex, float tileSize);
    // Add a new layer from an albedo image file. Returns the new index, or -1
    // on failure / when MAX_LAYERS is reached.
    int  addLayer(const std::string& albedoPath);
    // Remove a layer by index. Splats referencing it are not rewritten; the
    // caller may want to resetSplat() afterwards.
    void removeLayer(int layerIndex);
    void resetSplat();   // clear splat to layer 0 only

    // Scene serialization accessors.
    const std::vector<float>& heightsData() const { return heights_; }
    // Planar layout: NUM_SPLAT_MAPS blocks of (gridX*gridZ*4) bytes each.
    const std::vector<uint8_t>& splatData() const { return splat_; }
    void setHeights(const std::vector<float>& h);
    void setSplat(const std::vector<uint8_t>& s);
    void setLayerName(int i, const std::string& n) { if (i>=0&&i<(int)layers_.size()) layers_[i].name=n; }

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

    // Texture layers + multi-splat map.
    std::vector<Layer>   layers_;     // up to MAX_LAYERS
    std::vector<uint8_t> splat_;      // gridX*gridZ*4*NUM_SPLAT_MAPS (planar per map)
    GLuint splatTex_[NUM_SPLAT_MAPS] = {0,0,0,0};
    GLuint albedoArray_ = 0;          // GL_TEXTURE_2D_ARRAY, layers_.size() slices
    GLuint normalArray_ = 0;          // GL_TEXTURE_2D_ARRAY, layers_.size() slices

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
    void  rebuildArrays();   // recreate albedoArray_/normalArray_ from layers_
    static void resampleTo512(const uint8_t* src, int sw, int sh,
                              std::vector<uint8_t>& out);
    static void make2DFromPixels(const std::vector<uint8_t>& pix, GLuint& tex);
    static void fillProceduralAlbedo(std::vector<uint8_t>& out,
                                     const glm::vec3& baseColor, float variation);
    static void fillFlatNormal(std::vector<uint8_t>& out);
    void  initTextureLayers();   // procedural default textures + splat
};

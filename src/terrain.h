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
        enum Type { Raise = 0, Lower, Smooth, Flatten, Noise, Set };
        Type type = Raise;
        float radius   = 12.0f;   // world units
        float strength = 0.25f;   // height units per stroke step
        float target   = 0.0f;    // used by Flatten/Set
        enum Falloff { FalloffSmooth = 0, FalloffLinear, FalloffConstant };
        int falloff = FalloffSmooth;
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

    // Mesh / grid info.
    int gridX() const { return gridX_; }
    int gridZ() const { return gridZ_; }
    float worldSize() const { return worldSize_; }
    float minHeight() const { return statsMin_; }
    float maxHeight() const { return statsMax_; }

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
};

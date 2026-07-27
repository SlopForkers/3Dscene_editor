#pragma once
#include <glad/gl.h>
#include <glm/glm.hpp>
#include <string>
#include <vector>

class Shader;

// Cubemap skybox. Renders a cube around the camera (translation stripped from
// the view matrix) with a cubemap texture. Falls back to a procedurally
// generated vertical-gradient sky when no equirectangular image has been
// imported. Stored as GL_RGB16F so HDR sky maps keep their extended range
// (exposure slider in the UI scales the result).
class Skybox {
public:
    Skybox() = default;
    ~Skybox();

    // Create the cube VAO/VBO, the fullscreen-quad VAO used for equirect
    // conversion, and a procedural default cubemap.
    bool create();
    void destroy();

    // Discard any imported equirect and regenerate the procedural default.
    void resetToDefault();

    // Import an equirectangular panorama (.hdr / .png / .jpg / ...) and
    // convert it to a cubemap on the GPU via the provided convert shader.
    // Returns false if the image could not be loaded.
    bool loadEquirect(Shader& convertShader, const std::string& path);

    // Draw. Caller is responsible for setting glDepthFunc(GL_LEQUAL) before
    // and restoring GL_LESS afterwards; the shader writes depth = w (far).
    void draw(Shader& shader, const glm::mat4& viewProj, float exposure);

    GLuint texture() const { return tex_; }
    bool valid() const { return tex_ != 0; }
    bool isDefault() const { return imported_; }
    const std::string& importedPath() const { return importedPath_; }

private:
    GLuint vao_ = 0, vbo_ = 0;     // skybox cube
    GLuint quadVao_ = 0, quadVbo_ = 0; // fullscreen quad for equirect conversion
    GLuint tex_ = 0;
    int faceSize_ = 256;
    bool imported_ = false;        // true after a successful equirect import
    std::string importedPath_;

    void allocateCubemap(int size);
    void setParams();
    void uploadProceduralFace(int face, int size);
    static void genFaceProcedural(int face, int size, std::vector<float>& buf);
};

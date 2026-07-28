#pragma once
#include <glad/gl.h>
#include <glm/glm.hpp>
#include <string>
#include <unordered_map>

class Shader {
public:
    Shader() = default;
    ~Shader();

    bool loadFromFile(const std::string& vertexPath, const std::string& fragmentPath);
    // Explicitly release the GL program. Call before the GL context dies —
    // the destructor falls back to this but must not run after
    // glfwTerminate().
    void destroy();
    void use() const;

    void setInt(const std::string& name, int value) const;
    void setFloat(const std::string& name, float value) const;
    void setVec3(const std::string& name, const glm::vec3& v) const;
    void setVec4(const std::string& name, const glm::vec4& v) const;
    void setMat4(const std::string& name, const glm::mat4& m) const;
    // Upload an array of mat4 (e.g. skinning palettes). Uses the uniform
    // cache (the name should be the raw array name, "uX", not "uX[0]").
    void setMat4Array(const std::string& name, const float* data, int count) const;
    void setBool(const std::string& name, bool value) const;

    GLuint id() const { return program_; }

private:
    GLuint program_ = 0;
    mutable std::unordered_map<std::string, GLint> uniformCache_;

    GLint location(const std::string& name) const;
    static std::string readFile(const std::string& path);
    static GLuint compileStage(GLenum type, const std::string& source, const std::string& debugName);
};

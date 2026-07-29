#include "shader.h"
#include <fstream>
#include <sstream>
#include <iostream>

Shader::~Shader() {
    // NOTE: this must run while a GL context is current. App::shutdown()
    // calls destroy() explicitly before glfwTerminate(), so by the time the
    // destructor runs program_ is already 0.
    destroy();
}

void Shader::destroy() {
    if (program_) {
        glDeleteProgram(program_);
        program_ = 0;
    }
    uniformCache_.clear();
}

std::string Shader::readFile(const std::string& path) {
    std::ifstream file(path, std::ios::in);
    if (!file) {
        std::cerr << "Shader: cannot open file: " << path << std::endl;
        return {};
    }
    std::stringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

GLuint Shader::compileStage(GLenum type, const std::string& source, const std::string& debugName) {
    GLuint shader = glCreateShader(type);
    const char* src = source.c_str();
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);

    GLint success = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char log[2048];
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        std::cerr << "Shader compile error (" << debugName << "):\n" << log << std::endl;
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

bool Shader::loadFromFile(const std::string& vertexPath, const std::string& fragmentPath) {
    std::string vsrc = readFile(vertexPath);
    std::string fsrc = readFile(fragmentPath);
    if (vsrc.empty() || fsrc.empty()) return false;

    GLuint vs = compileStage(GL_VERTEX_SHADER, vsrc, vertexPath);
    GLuint fs = compileStage(GL_FRAGMENT_SHADER, fsrc, fragmentPath);
    if (!vs || !fs) {
        if (vs) glDeleteShader(vs);
        if (fs) glDeleteShader(fs);
        return false;
    }

    // Release any previously loaded program (and its cached locations) so a
    // reload doesn't leak it.
    destroy();
    program_ = glCreateProgram();
    glAttachShader(program_, vs);
    glAttachShader(program_, fs);
    glLinkProgram(program_);

    GLint success = 0;
    glGetProgramiv(program_, GL_LINK_STATUS, &success);
    if (!success) {
        char log[2048];
        glGetProgramInfoLog(program_, sizeof(log), nullptr, log);
        std::cerr << "Shader link error (" << vertexPath << ", " << fragmentPath << "):\n" << log << std::endl;
        glDeleteProgram(program_);
        program_ = 0;
    }
    glDeleteShader(vs);
    glDeleteShader(fs);
    return program_ != 0;
}

void Shader::use() const {
    glUseProgram(program_);
}

GLint Shader::location(const std::string& name) const {
    auto it = uniformCache_.find(name);
    if (it != uniformCache_.end()) return it->second;
    GLint loc = glGetUniformLocation(program_, name.c_str());
    uniformCache_[name] = loc;
    return loc;
}

void Shader::setInt(const std::string& name, int value) const {
    glUniform1i(location(name), value);
}
void Shader::setFloat(const std::string& name, float value) const {
    glUniform1f(location(name), value);
}
void Shader::setVec2(const std::string& name, const glm::vec2& v) const {
    glUniform2fv(location(name), 1, &v[0]);
}
void Shader::setVec3(const std::string& name, const glm::vec3& v) const {
    glUniform3fv(location(name), 1, &v[0]);
}
void Shader::setVec4(const std::string& name, const glm::vec4& v) const {
    glUniform4fv(location(name), 1, &v[0]);
}
void Shader::setMat4(const std::string& name, const glm::mat4& m) const {
    glUniformMatrix4fv(location(name), 1, GL_FALSE, &m[0][0]);
}
void Shader::setMat4Array(const std::string& name, const float* data, int count) const {
    // glGetUniformLocation on the bare array name resolves element 0.
    glUniformMatrix4fv(location(name), count, GL_FALSE, data);
}
void Shader::setBool(const std::string& name, bool value) const {
    setInt(name, value ? 1 : 0);
}

#pragma once
#include <glad/gl.h>
#include <utility>

// ---------------------------------------------------------------------------
// RAII-style wrappers for OpenGL objects.
//
// Every wrapper stores a GLuint handle, defaulting to 0.
// Explicit create() / destroy() calls are required — destructors call
// destroy() defensively, so code must zero handles while the GL context is
// still current (i.e. call destroy() in App::shutdown, before glfwTerminate).
//
// Wrappers are move-only (copy deleted).  operator GLuint() and id() allow
// passing them straight to glBind*/glUniform* calls.
// ---------------------------------------------------------------------------

struct GlBuffer {
    GLuint id_ = 0;

    GlBuffer() = default;
    ~GlBuffer() { destroy(); }

    GlBuffer(const GlBuffer&) = delete;
    GlBuffer& operator=(const GlBuffer&) = delete;
    GlBuffer(GlBuffer&& o) noexcept : id_(o.id_) { o.id_ = 0; }
    GlBuffer& operator=(GlBuffer&& o) noexcept {
        if (this != &o) { destroy(); id_ = o.id_; o.id_ = 0; }
        return *this;
    }

    void create()  { glGenBuffers(1, &id_); }
    void destroy() { if (id_) { glDeleteBuffers(1, &id_); id_ = 0; } }

    GLuint id() const { return id_; }
    operator GLuint() const { return id_; }

    void bind(GLenum target) const { glBindBuffer(target, id_); }
    void upload(const void* data, size_t bytes, GLenum usage) {
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)bytes, data, usage);
    }
    void uploadIndexed(const void* data, size_t bytes, GLenum usage) {
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)bytes, data, usage);
    }
};

struct GlVertexArray {
    GLuint id_ = 0;

    GlVertexArray() = default;
    ~GlVertexArray() { destroy(); }

    GlVertexArray(const GlVertexArray&) = delete;
    GlVertexArray& operator=(const GlVertexArray&) = delete;
    GlVertexArray(GlVertexArray&& o) noexcept : id_(o.id_) { o.id_ = 0; }
    GlVertexArray& operator=(GlVertexArray&& o) noexcept {
        if (this != &o) { destroy(); id_ = o.id_; o.id_ = 0; }
        return *this;
    }

    void create()  { glGenVertexArrays(1, &id_); }
    void destroy() { if (id_) { glDeleteVertexArrays(1, &id_); id_ = 0; } }

    GLuint id() const { return id_; }
    operator GLuint() const { return id_; }

    void bind() const { glBindVertexArray(id_); }
};

struct GlTexture {
    GLuint id_ = 0;

    GlTexture() = default;
    ~GlTexture() { destroy(); }

    GlTexture(const GlTexture&) = delete;
    GlTexture& operator=(const GlTexture&) = delete;
    GlTexture(GlTexture&& o) noexcept : id_(o.id_) { o.id_ = 0; }
    GlTexture& operator=(GlTexture&& o) noexcept {
        if (this != &o) { destroy(); id_ = o.id_; o.id_ = 0; }
        return *this;
    }

    void create()  { glGenTextures(1, &id_); }
    void destroy() { if (id_) { glDeleteTextures(1, &id_); id_ = 0; } }

    GLuint id() const { return id_; }
    operator GLuint() const { return id_; }

    void bind(GLenum target) const { glBindTexture(target, id_); }
    void bindUnit(GLenum target, GLuint unit) {
        glActiveTexture(GL_TEXTURE0 + unit);
        glBindTexture(target, id_);
    }
};

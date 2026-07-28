#include "model.h"
#include "shader.h"
#include "sys_util.h"
#define CGLTF_IMPLEMENTATION
#include <cgltf.h>
#include <stb_image.h>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/quaternion.hpp>
#include <iostream>
#include <cmath>
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <functional>

// cgltf file IO routed through the UTF-8 aware reader so models and their
// external .bin buffers load from non-ANSI paths on Windows (the default
// cgltf file callbacks use plain fopen).
static cgltf_result cgltfReadFile(const cgltf_memory_options* mem,
                                  const cgltf_file_options*,
                                  const char* path, cgltf_size* size,
                                  void** data) {
    std::vector<char> bytes;
    if (!readFileBytes(path, bytes) || bytes.empty())
        return cgltf_result_file_not_found;
    // Some cgltf entry points (cgltf_load_buffers) forward the caller's
    // options WITHOUT filling in the default allocators, so alloc_func may
    // legitimately be null here. Fall back to cgltf's own defaults (they're
    // visible in this TU because of CGLTF_IMPLEMENTATION).
    void* (*allocFn)(void*, cgltf_size) =
        mem->alloc_func ? mem->alloc_func : &cgltf_default_alloc;
    void* buf = allocFn(mem->user_data, bytes.size());
    if (!buf) return cgltf_result_out_of_memory;
    std::memcpy(buf, bytes.data(), bytes.size());
    *size = bytes.size();
    *data = buf;
    return cgltf_result_success;
}

static void cgltfReleaseFile(const cgltf_memory_options* mem,
                             const cgltf_file_options*, void* data) {
    void (*freeFn)(void*, void*) =
        mem->free_func ? mem->free_func : &cgltf_default_free;
    freeFn(mem->user_data, data);
}

static glm::mat4 nodeLocalMatrix(const cgltf_node* node) {
    if (node->has_matrix) {
        // glTF stores matrices column-major; glm::mat4 is also column-major,
        // so a direct copy is correct (NO transpose — transposing would
        // invert rotations and corrupt the hierarchy).
        glm::mat4 m;
        for (int c = 0; c < 16; ++c)
            (&m[0][0])[c] = (float)node->matrix[c];
        return m;
    }
    glm::vec3 T(0.0f);
    glm::quat R(1.0f, 0.0f, 0.0f, 0.0f);
    glm::vec3 S(1.0f);
    if (node->has_translation)
        T = glm::vec3((float)node->translation[0], (float)node->translation[1], (float)node->translation[2]);
    if (node->has_rotation)
        R = glm::quat((float)node->rotation[3], (float)node->rotation[0],
                      (float)node->rotation[1], (float)node->rotation[2]);
    if (node->has_scale)
        S = glm::vec3((float)node->scale[0], (float)node->scale[1], (float)node->scale[2]);

    glm::mat4 m(1.0f);
    m = glm::translate(m, T);
    m *= glm::mat4(R);
    m = glm::scale(m, S);
    return m;
}

Model::~Model() { destroy(); }

void Model::destroy() {
    for (GLuint t : textures_) if (t) glDeleteTextures(1, &t);
    textures_.clear();
    for (auto& p : primitives_) {
        if (p.vao)       glDeleteVertexArrays(1, &p.vao);
        if (p.vboPos)    glDeleteBuffers(1, &p.vboPos);
        if (p.vboNorm)   glDeleteBuffers(1, &p.vboNorm);
        if (p.vboTan)    glDeleteBuffers(1, &p.vboTan);
        if (p.vboUv)     glDeleteBuffers(1, &p.vboUv);
        if (p.vboJoints) glDeleteBuffers(1, &p.vboJoints);
        if (p.vboWeights)glDeleteBuffers(1, &p.vboWeights);
        if (p.ebo)       glDeleteBuffers(1, &p.ebo);
    }
    primitives_.clear();
    materials_.clear();
    skins_.clear();
    nodes_.clear();
    meshes_.clear();
    rootNodes_.clear();
    texCache_.clear();
    aabbMin_ = aabbMax_ = glm::vec3(0.0f);
    hasAabb_ = false;
    loaded_ = false;
}

GLuint Model::loadTextureFromImage(cgltf_image* image, const std::string& baseDir) {
    if (!image) return 0;

    // De-duplicate: the same glTF image referenced by several material slots
    // is uploaded to the GPU only once per model load.
    auto it = texCache_.find(image);
    if (it != texCache_.end()) return it->second;

    // Prefer cgltf's buffer_view (embedded) when present, else load from URI.
    stbi_uc* data = nullptr;
    int dataLen = 0;
    std::string path;       // declared at function scope (used by file path branch)
    bool fromFile = false;
    // Must live at function scope: `data` points into it and is consumed by
    // stbi_load_from_memory below, after the uri branch has ended.
    std::vector<unsigned char> decoded;

    if (image->buffer_view && image->buffer_view->buffer && image->buffer_view->buffer->data) {
        cgltf_buffer_view* bv = image->buffer_view;
        data = (stbi_uc*)((char*)bv->buffer->data + bv->offset);
        dataLen = (int)bv->size;
    } else if (image->uri) {
        if (strncmp(image->uri, "data:", 5) == 0) {
            // Embedded base64 data URI. Decode into a buffer.
            const char* comma = strchr(image->uri, ',');
            if (!comma) return 0;
            const char* b64 = comma + 1;
            size_t b64len = strlen(b64);
            decoded.reserve((b64len * 3) / 4);
            static const int8_t tbl[256] = {
                -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
                -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
                -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,-1,-1,63,
                52,53,54,55,56,57,58,59,60,61,-1,-1,-1,-1,-1,-1,
                -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,
                15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,
                -1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,
                41,42,43,44,45,46,47,48,49,50,51,-1,-1,-1,-1,-1,
                -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
                -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
                -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
                -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
                -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
                -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
                -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
                -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
            };
            int val = 0, bits = 0;
            for (size_t i = 0; i < b64len; ++i) {
                int8_t d = tbl[(unsigned char)b64[i]];
                if (d < 0) continue;
                val = (val << 6) | d;
                bits += 6;
                if (bits >= 8) { bits -= 8; decoded.push_back((unsigned char)((val >> bits) & 0xFF)); }
            }
            data = decoded.data();
            dataLen = (int)decoded.size();
        } else {
            // glTF URIs are percent-encoded; decode so paths with spaces or
            // non-ASCII characters resolve to real files.
            std::string uri;
            uri.reserve(strlen(image->uri));
            for (const char* u = image->uri; *u; ++u) {
                if (*u == '%' && u[1] && u[2]) {
                    auto hex = [](char c) -> int {
                        if (c >= '0' && c <= '9') return c - '0';
                        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                        return -1;
                    };
                    int hi = hex(u[1]), lo = hex(u[2]);
                    if (hi >= 0 && lo >= 0) { uri += (char)(hi * 16 + lo); u += 2; continue; }
                }
                uri += *u;
            }
            path = baseDir + "/" + uri;
            fromFile = true;
        }
    }

    int w = 0, h = 0, ch = 0;
    stbi_uc* pixels = nullptr;
    std::vector<char> fileBytes;   // keeps the buffer alive until decode done
    if (fromFile) {
        std::replace(path.begin(), path.end(), '\\', '/');
        if (readFileBytes(path, fileBytes)) {
            pixels = stbi_load_from_memory((const stbi_uc*)fileBytes.data(),
                                           (int)fileBytes.size(), &w, &h, &ch, 4);
        }
    } else if (data) {
        pixels = stbi_load_from_memory(data, dataLen, &w, &h, &ch, 4);
    }
    if (!pixels) {
        std::cerr << "Model: failed to load texture: "
                  << (image->uri ? image->uri : "<embedded>") << "\n";
        return 0;
    }

    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D, 0);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);   // restore GL default

    stbi_image_free(pixels);
    textures_.push_back(tex);
    texCache_[image] = tex;
    return tex;
}

// Read an accessor into a flat float vector.
static bool readAccessorFloats(cgltf_accessor* acc, std::vector<float>& out) {
    if (!acc) return false;
    cgltf_size count = acc->count;
    int comps = cgltf_num_components(acc->type);
    out.resize(count * comps);
    cgltf_float tmp[16];
    for (cgltf_size i = 0; i < count; ++i) {
        // cgltf_accessor_read_float returns cgltf_bool: 1 = success, 0 = failure.
        if (cgltf_accessor_read_float(acc, i, tmp, 16) == 0) return false;
        for (int c = 0; c < comps; ++c) out[i * comps + c] = tmp[c];
    }
    return true;
}

// Read an accessor of unsigned int indices (joints) into floats for the GPU.
// Joint indices are clamped to [0, 255] (the shader's uJointMatrices size) so
// a corrupt file cannot index out of the uniform array.
static bool readAccessorJoints(cgltf_accessor* acc, std::vector<float>& out) {
    if (!acc) return false;
    cgltf_size count = acc->count;
    out.resize(count * 4);
    cgltf_uint tmp[4];
    for (cgltf_size i = 0; i < count; ++i) {
        // cgltf_accessor_read_uint returns cgltf_bool: 1 = success, 0 = failure.
        if (cgltf_accessor_read_uint(acc, i, tmp, 4) == 0) return false;
        for (int c = 0; c < 4; ++c)
            out[i * 4 + c] = (float)std::min(tmp[c], (cgltf_uint)255);
    }
    return true;
}

bool Model::loadFromFile(const std::string& path) {
    destroy();
    cgltf_options options;
    memset(&options, 0, sizeof(options));
    options.file.read = &cgltfReadFile;
    options.file.release = &cgltfReleaseFile;
    cgltf_data* data = nullptr;

    std::string p = path;
    std::replace(p.begin(), p.end(), '\\', '/');
    cgltf_result res = cgltf_parse_file(&options, p.c_str(), &data);
    if (res != cgltf_result_success) {
        std::cerr << "cgltf_parse_file failed: " << res << " (" << path << ")\n";
        if (data) cgltf_free(data);
        return false;
    }

    res = cgltf_load_buffers(&options, data, p.c_str());
    if (res != cgltf_result_success) {
        std::cerr << "cgltf_load_buffers failed: " << res << "\n";
        cgltf_free(data);
        return false;
    }

    // Base dir for relative texture URIs.
    std::filesystem::path fp(p);
    std::string baseDir = fp.parent_path().string();

    name_ = fp.filename().string();
    sourcePath_ = p;

    // --- Textures & materials ---
    materials_.resize(data->materials_count);
    for (cgltf_size i = 0; i < data->materials_count; ++i) {
        cgltf_material* m = &data->materials[i];
        Material& mat = materials_[i];
        // cgltf pre-fills spec defaults (cutoff 0.5, scales 1.0), so an
        // explicit 0 in the file must be honoured — no `?: default` here.
        mat.alphaCutoff = (float)m->alpha_cutoff;
        mat.alphaMode = (m->alpha_mode == cgltf_alpha_mode_mask) ? 1 :
                        (m->alpha_mode == cgltf_alpha_mode_blend) ? 2 : 0;
        mat.doubleSided = m->double_sided != 0;
        mat.unlit = (m->unlit != 0);
        if (m->emissive_texture.texture) {
            cgltf_texture* t = m->emissive_texture.texture;
            if (t && t->image) {
                mat.emissiveTex = loadTextureFromImage(t->image, baseDir);
                mat.hasEmissiveTex = mat.emissiveTex != 0;
            }
        }
        mat.emissiveFactor = glm::vec3((float)m->emissive_factor[0],
                                       (float)m->emissive_factor[1],
                                       (float)m->emissive_factor[2]);
        if (m->has_pbr_metallic_roughness) {
            cgltf_pbr_metallic_roughness* pbr = &m->pbr_metallic_roughness;
            mat.baseColorFactor = glm::vec4((float)pbr->base_color_factor[0],
                                             (float)pbr->base_color_factor[1],
                                             (float)pbr->base_color_factor[2],
                                             (float)pbr->base_color_factor[3]);
            mat.metallic = (float)pbr->metallic_factor;
            mat.roughness = (float)pbr->roughness_factor;
            if (pbr->base_color_texture.texture) {
                cgltf_texture* t = pbr->base_color_texture.texture;
                if (t && t->image) {
                    mat.baseColorTex = loadTextureFromImage(t->image, baseDir);
                    mat.hasBaseColorTex = mat.baseColorTex != 0;
                }
            }
            if (pbr->metallic_roughness_texture.texture) {
                cgltf_texture* t = pbr->metallic_roughness_texture.texture;
                if (t && t->image) {
                    mat.metalRoughTex = loadTextureFromImage(t->image, baseDir);
                    mat.hasMetalRoughTex = mat.metalRoughTex != 0;
                }
            }
        }
        if (m->normal_texture.texture) {
            cgltf_texture* t = m->normal_texture.texture;
            if (t && t->image) {
                mat.normalTex = loadTextureFromImage(t->image, baseDir);
                mat.hasNormalTex = mat.normalTex != 0;
                mat.normalScale = (float)m->normal_texture.scale;
            }
        }
        if (m->occlusion_texture.texture) {
            cgltf_texture* t = m->occlusion_texture.texture;
            if (t && t->image) {
                mat.occlusionTex = loadTextureFromImage(t->image, baseDir);
                mat.hasOcclusionTex = mat.occlusionTex != 0;
                mat.occlusionStrength = (float)m->occlusion_texture.scale;
            }
        }
    }

    // --- Nodes ---
    nodes_.resize(data->nodes_count);
    // Map cgltf_node* -> index
    for (cgltf_size i = 0; i < data->nodes_count; ++i) {
        Node& n = nodes_[i];
        n.local = nodeLocalMatrix(&data->nodes[i]);
        n.name = data->nodes[i].name ? data->nodes[i].name : "";
        for (cgltf_size c = 0; c < data->nodes[i].children_count; ++c)
            n.children.push_back((int)(data->nodes[i].children[c] - data->nodes));
    }

    // --- Meshes (map cgltf_mesh* -> index into meshes_) ---
    std::vector<int> meshIndexByPtr(data->meshes_count, -1);
    for (cgltf_size i = 0; i < data->meshes_count; ++i) {
        meshIndexByPtr[i] = (int)meshes_.size();
        meshes_.push_back(Mesh{});
    }

    // --- Skins ---
    skins_.resize(data->skins_count);
    for (cgltf_size i = 0; i < data->skins_count; ++i) {
        cgltf_skin* s = &data->skins[i];
        if (s->joints_count > 256) {
            std::cerr << "Model: skin has " << s->joints_count
                      << " joints; only 256 supported. Truncating.\n";
        }
        cgltf_size jc = std::min<cgltf_size>(s->joints_count, 256);
        skins_[i].jointNodeIndex.resize(jc);
        skins_[i].inverseBind.resize(jc, glm::mat4(1.0f));
        for (cgltf_size j = 0; j < jc; ++j) {
            skins_[i].jointNodeIndex[j] = (int)(s->joints[j] - data->nodes);
        }
        if (s->inverse_bind_matrices) {
            std::vector<float> ibm;
            // Guard against malformed files whose IBM accessor holds fewer
            // matrices than the skin has joints.
            if (readAccessorFloats(s->inverse_bind_matrices, ibm) &&
                ibm.size() >= jc * 16) {
                for (cgltf_size j = 0; j < jc; ++j) {
                    // glTF stores matrices column-major; glm::mat4 is also
                    // column-major, so copy directly (NO transpose).
                    glm::mat4 m;
                    for (int c = 0; c < 16; ++c)
                        (&m[0][0])[c] = ibm[j * 16 + c];
                    skins_[i].inverseBind[j] = m;
                }
            }
        }
    }

    // --- Primitives ---
    for (cgltf_size mi = 0; mi < data->meshes_count; ++mi) {
        cgltf_mesh* mesh = &data->meshes[mi];
        int myMeshIndex = meshIndexByPtr[mi];
        for (cgltf_size pi = 0; pi < mesh->primitives_count; ++pi) {
            cgltf_primitive* prim = &mesh->primitives[pi];
            primitives_.emplace_back();
            Primitive& P = primitives_.back();
            P.materialIndex = prim->material ? (int)(prim->material - data->materials) : -1;
            meshes_[myMeshIndex].primitiveIndices.push_back((int)(primitives_.size() - 1));

            // POSITION (required)
            std::vector<float> positions;
            for (cgltf_size a = 0; a < prim->attributes_count; ++a) {
                cgltf_attribute* attr = &prim->attributes[a];
                if (attr->type == cgltf_attribute_type_position) {
                    if (!readAccessorFloats(attr->data, positions)) {
                        std::cerr << "[gltf] POSITION read failed\n";
                    }
                }
            }
            cgltf_size vertCount = positions.size() / 3;

            // Per-primitive local AABB (node transforms are applied later,
            // once computeNodeGlobals() has run — see below).
            for (cgltf_size v = 0; v < vertCount; ++v) {
                glm::vec3 p(positions[v * 3 + 0], positions[v * 3 + 1], positions[v * 3 + 2]);
                if (v == 0) { P.aabbMin = p; P.aabbMax = p; }
                else { P.aabbMin = glm::min(P.aabbMin, p); P.aabbMax = glm::max(P.aabbMax, p); }
            }

            // NORMAL
            std::vector<float> normals(vertCount * 3, 0.0f);
            for (cgltf_size a = 0; a < prim->attributes_count; ++a) {
                cgltf_attribute* attr = &prim->attributes[a];
                if (attr->type == cgltf_attribute_type_normal)
                    readAccessorFloats(attr->data, normals);
            }

            // TANGENT
            std::vector<float> tangents(vertCount * 4, 0.0f);
            bool hasTangent = false;
            for (cgltf_size a = 0; a < prim->attributes_count; ++a) {
                cgltf_attribute* attr = &prim->attributes[a];
                if (attr->type == cgltf_attribute_type_tangent) {
                    readAccessorFloats(attr->data, tangents);
                    hasTangent = true;
                }
            }
            P.hasTangent = hasTangent;

            // TEXCOORD_0
            std::vector<float> uvs(vertCount * 2, 0.0f);
            bool hasUv = false;
            for (cgltf_size a = 0; a < prim->attributes_count; ++a) {
                cgltf_attribute* attr = &prim->attributes[a];
                if (attr->type == cgltf_attribute_type_texcoord && attr->index == 0) {
                    readAccessorFloats(attr->data, uvs);
                    hasUv = true;
                }
            }
            P.hasUv = hasUv;

            // JOINTS_0 + WEIGHTS_0
            std::vector<float> joints(vertCount * 4, 0.0f);
            std::vector<float> weights(vertCount * 4, 0.0f);
            bool hasJoints = false;
            for (cgltf_size a = 0; a < prim->attributes_count; ++a) {
                cgltf_attribute* attr = &prim->attributes[a];
                if (attr->type == cgltf_attribute_type_joints && attr->index == 0)
                    readAccessorJoints(attr->data, joints);
                if (attr->type == cgltf_attribute_type_weights && attr->index == 0) {
                    readAccessorFloats(attr->data, weights);
                    hasJoints = true;
                }
            }
            P.hasJoints = hasJoints;

            // Indices. The EBO is ALWAYS uploaded as 32-bit (indices are
            // widened to GLuint on read), so the draw call must use
            // GL_UNSIGNED_INT regardless of the source component type.
            std::vector<GLuint> indices;
            if (prim->indices) {
                cgltf_accessor* acc = prim->indices;
                indices.resize(acc->count);
                for (cgltf_size k = 0; k < acc->count; ++k) {
                    cgltf_uint idx = 0;   // read fails (e.g. sparse) -> keep 0
                    cgltf_accessor_read_uint(acc, k, &idx, 1);
                    indices[k] = (GLuint)idx;
                }
                P.indexCount = (int)acc->count;
                P.indexType = GL_UNSIGNED_INT;
            } else {
                // non-indexed: synthesize
                indices.resize(vertCount);
                for (cgltf_size k = 0; k < vertCount; ++k) indices[k] = (GLuint)k;
                P.indexCount = (int)vertCount;
                P.indexType = GL_UNSIGNED_INT;
            }

            // Build GL buffers
            glGenVertexArrays(1, &P.vao);
            glBindVertexArray(P.vao);

            auto upload = [](GLuint& buf, const void* data, size_t sz, int index, int comps, GLenum type = GL_FLOAT) {
                glGenBuffers(1, &buf);
                glBindBuffer(GL_ARRAY_BUFFER, buf);
                glBufferData(GL_ARRAY_BUFFER, sz, data, GL_STATIC_DRAW);
                glEnableVertexAttribArray(index);
                glVertexAttribPointer(index, comps, type, GL_FALSE, 0, (void*)0);
            };

            upload(P.vboPos, positions.data(), positions.size() * sizeof(float), 0, 3);
            upload(P.vboNorm, normals.data(), normals.size() * sizeof(float), 1, 3);
            if (hasTangent)
                upload(P.vboTan, tangents.data(), tangents.size() * sizeof(float), 2, 4);
            if (hasUv)
                upload(P.vboUv, uvs.data(), uvs.size() * sizeof(float), 3, 2);
            if (hasJoints) {
                upload(P.vboJoints, joints.data(), joints.size() * sizeof(float), 4, 4);
                upload(P.vboWeights, weights.data(), weights.size() * sizeof(float), 5, 4);
            }

            glGenBuffers(1, &P.ebo);
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, P.ebo);
            glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(GLuint),
                         indices.data(), GL_STATIC_DRAW);

            glBindVertexArray(0);
        }
    }

    // Link nodes to meshes and skins
    for (cgltf_size i = 0; i < data->nodes_count; ++i) {
        cgltf_node* n = &data->nodes[i];
        if (n->mesh) {
            nodes_[i].meshIndex = meshIndexByPtr[n->mesh - data->meshes];
        }
        if (n->skin) {
            nodes_[i].skinIndex = (int)(n->skin - data->skins);
        }
    }

    // Root nodes of default scene
    cgltf_scene* scene = data->scene ? data->scene :
                        (data->scenes_count > 0 ? &data->scenes[0] : nullptr);
    if (scene) {
        for (cgltf_size i = 0; i < scene->nodes_count; ++i)
            rootNodes_.push_back((int)(scene->nodes[i] - data->nodes));
    } else {
        // Fall back: nodes with no parent
        for (cgltf_size i = 0; i < data->nodes_count; ++i) {
            if (data->nodes[i].parent == nullptr)
                rootNodes_.push_back((int)i);
        }
    }

    computeNodeGlobals();
    computeSkinMatrices();

    // Model-space AABB: transform each primitive's local AABB by the global
    // matrix of EVERY node that instances its mesh (a mesh may be referenced
    // by several nodes; each placement contributes to the bounds).
    hasAabb_ = false;
    for (const auto& node : nodes_) {
        if (node.meshIndex < 0) continue;
        for (int pi : meshes_[node.meshIndex].primitiveIndices) {
            const Primitive& P = primitives_[pi];
            for (int corner = 0; corner < 8; ++corner) {
                glm::vec3 c((corner & 1) ? P.aabbMax.x : P.aabbMin.x,
                            (corner & 2) ? P.aabbMax.y : P.aabbMin.y,
                            (corner & 4) ? P.aabbMax.z : P.aabbMin.z);
                glm::vec3 w = glm::vec3(node.global * glm::vec4(c, 1.0f));
                if (!hasAabb_) { aabbMin_ = w; aabbMax_ = w; hasAabb_ = true; }
                else { aabbMin_ = glm::min(aabbMin_, w); aabbMax_ = glm::max(aabbMax_, w); }
            }
        }
    }
    if (!hasAabb_) { aabbMin_ = aabbMax_ = glm::vec3(0.0f); }

    cgltf_free(data);
    loaded_ = true;
    return true;
}

void Model::computeNodeGlobals() {
    // Recursive global-matrix computation from roots.
    std::function<void(int, const glm::mat4&)> walk = [&](int ni, const glm::mat4& parent) {
        Node& n = nodes_[ni];
        n.global = parent * n.local;
        for (int c : n.children) walk(c, n.global);
    };
    for (int r : rootNodes_) walk(r, glm::mat4(1.0f));
}

void Model::computeSkinMatrices() {
    for (auto& s : skins_) {
        s.jointMatrices.resize(s.jointNodeIndex.size());
        for (size_t j = 0; j < s.jointNodeIndex.size(); ++j) {
            int ni = s.jointNodeIndex[j];
            if (ni >= 0 && ni < (int)nodes_.size())
                s.jointMatrices[j] = nodes_[ni].global * s.inverseBind[j];
            else
                s.jointMatrices[j] = glm::mat4(1.0f);
        }
    }
}

void Model::applyMaterial(const Material& m, const Shader& shader,
                          const glm::vec3& /*lightDir*/, const glm::vec3& /*camPos*/) const {
    shader.setBool("uUnlit", m.unlit);
    shader.setBool("uDoubleSided", m.doubleSided);
    shader.setFloat("uAlphaCutoff", m.alphaCutoff);
    shader.setInt("uAlphaMode", m.alphaMode);

    shader.setVec4("uBaseColorFactor", m.baseColorFactor);
    shader.setVec3("uEmissiveFactor", m.emissiveFactor);
    shader.setFloat("uMetallic", m.metallic);
    shader.setFloat("uRoughness", m.roughness);
    shader.setFloat("uNormalScale", m.normalScale);
    shader.setFloat("uOcclusionStrength", m.occlusionStrength);

    auto bindTex = [&](const char* hasUniform, const char* texUniform, GLuint tex, int unit) {
        shader.setBool(hasUniform, tex != 0);
        if (tex) {
            glActiveTexture(GL_TEXTURE0 + unit);
            glBindTexture(GL_TEXTURE_2D, tex);
            shader.setInt(texUniform, unit);
        }
    };
    bindTex("uHasBaseColorTex", "uBaseColorTex", m.baseColorTex, 0);
    bindTex("uHasMetalRoughTex", "uMetalRoughTex", m.metalRoughTex, 1);
    bindTex("uHasNormalTex", "uNormalTex", m.normalTex, 2);
    bindTex("uHasEmissiveTex", "uEmissiveTex", m.emissiveTex, 3);
    bindTex("uHasOcclusionTex", "uOcclusionTex", m.occlusionTex, 4);

    if (m.alphaMode == 2) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    } else {
        glDisable(GL_BLEND);
    }
    if (!m.doubleSided) glEnable(GL_CULL_FACE);
    else glDisable(GL_CULL_FACE);
}

void Model::applyDefaultMaterial(const Shader& shader) const {
    // Full reset of every material uniform so state never leaks from the
    // previously drawn primitive.
    shader.setBool("uUnlit", false);
    shader.setBool("uDoubleSided", false);
    shader.setFloat("uAlphaCutoff", 0.5f);
    shader.setInt("uAlphaMode", 0);
    shader.setVec4("uBaseColorFactor", glm::vec4(0.8f));
    shader.setVec3("uEmissiveFactor", glm::vec3(0.0f));
    shader.setFloat("uMetallic", 0.0f);
    shader.setFloat("uRoughness", 1.0f);
    shader.setFloat("uNormalScale", 1.0f);
    shader.setFloat("uOcclusionStrength", 1.0f);
    shader.setBool("uHasBaseColorTex", false);
    shader.setBool("uHasMetalRoughTex", false);
    shader.setBool("uHasNormalTex", false);
    shader.setBool("uHasEmissiveTex", false);
    shader.setBool("uHasOcclusionTex", false);
    glDisable(GL_BLEND);
    glEnable(GL_CULL_FACE);
}

void Model::renderPrimitive(const Shader& shader, const Primitive& P,
                            const Node& node, GLuint instanceVbo,
                            int instanceCount) const {
    // Per the glTF 2.0 spec, skinned meshes are transformed ONLY by the
    // joint matrices (jointGlobal * inverseBind); the mesh node's own world
    // transform must be IDENTITY. Applying the node transform on top of the
    // skin double-transforms vertices and stretches the model. For
    // non-skinned meshes the node's global transform IS the model matrix.
    // Skinning also requires JOINTS_0/WEIGHTS_0 on the primitive itself.
    bool useSkin = P.hasJoints && node.skinIndex >= 0 &&
                   node.skinIndex < (int)skins_.size();
    if (useSkin) {
        const Skin& s = skins_[node.skinIndex];
        // Cached-location upload of the joint palette (256 max, see loader).
        shader.setMat4Array("uJointMatrices",
                            (const GLfloat*)s.jointMatrices.data(),
                            (int)s.jointMatrices.size());
        shader.setBool("uHasSkin", true);
        shader.setMat4("uModel", glm::mat4(1.0f));   // identity for skinned
    } else {
        shader.setBool("uHasSkin", false);
        shader.setMat4("uModel", node.global);
    }

    const Material* mat = (P.materialIndex >= 0 && P.materialIndex < (int)materials_.size())
                          ? &materials_[P.materialIndex] : nullptr;
    if (mat) applyMaterial(*mat, shader, glm::vec3(0), glm::vec3(0));
    else applyDefaultMaterial(shader);

    glBindVertexArray(P.vao);
    if (instanceCount > 0) {
        // Attach instance buffer as mat4 at locations 6..9 with divisor 1.
        glBindBuffer(GL_ARRAY_BUFFER, instanceVbo);
        for (int c = 0; c < 4; ++c) {
            glEnableVertexAttribArray(6 + c);
            glVertexAttribPointer(6 + c, 4, GL_FLOAT, GL_FALSE, sizeof(glm::mat4),
                                   (void*)(sizeof(glm::vec4) * c));
            glVertexAttribDivisor(6 + c, 1);
        }
        glDrawElementsInstanced(GL_TRIANGLES, P.indexCount, P.indexType, 0,
                                instanceCount);
        for (int c = 0; c < 4; ++c) {
            glDisableVertexAttribArray(6 + c);
            glVertexAttribDivisor(6 + c, 0);
        }
    } else {
        glDrawElements(GL_TRIANGLES, P.indexCount, P.indexType, 0);
    }
    glBindVertexArray(0);
}

void Model::render(const Shader& shader) const {
    if (!loaded_) return;
    // Walk scene nodes so a mesh referenced by several nodes renders once
    // per placement (glTF node -> mesh instancing).
    for (const auto& node : nodes_) {
        if (node.meshIndex < 0) continue;
        for (int pi : meshes_[node.meshIndex].primitiveIndices)
            renderPrimitive(shader, primitives_[pi], node);
    }
    // Restore the app's default GL state (culling OFF, blending OFF).
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
}

void Model::renderInstanced(const Shader& shader, GLuint instanceVbo,
                            int instanceCount) const {
    if (!loaded_ || instanceCount <= 0) return;
    shader.setBool("uInstanced", true);
    for (const auto& node : nodes_) {
        if (node.meshIndex < 0) continue;
        for (int pi : meshes_[node.meshIndex].primitiveIndices)
            renderPrimitive(shader, primitives_[pi], node, instanceVbo,
                            instanceCount);
    }
    shader.setBool("uInstanced", false);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
}

#include "model.h"
#include "shader.h"
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
    loaded_ = false;
}

GLuint Model::loadTextureFromImage(cgltf_image* image, const std::string& baseDir) {
    if (!image) return 0;

    // Prefer cgltf's buffer_view (embedded) when present, else load from URI.
    stbi_uc* data = nullptr;
    int dataLen = 0;
    std::string path;       // declared at function scope (used by file path branch)
    bool fromFile = false;

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
            std::vector<unsigned char> decoded;
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
            path = baseDir + "/" + image->uri;
            fromFile = true;
        }
    }

    int w = 0, h = 0, ch = 0;
    stbi_uc* pixels = nullptr;
    if (fromFile) {
        std::replace(path.begin(), path.end(), '\\', '/');
        pixels = stbi_load(path.c_str(), &w, &h, &ch, 4);
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

    stbi_image_free(pixels);
    textures_.push_back(tex);
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
static bool readAccessorJoints(cgltf_accessor* acc, std::vector<float>& out) {
    if (!acc) return false;
    cgltf_size count = acc->count;
    out.resize(count * 4);
    cgltf_uint tmp[4];
    for (cgltf_size i = 0; i < count; ++i) {
        // cgltf_accessor_read_uint returns cgltf_bool: 1 = success, 0 = failure.
        if (cgltf_accessor_read_uint(acc, i, tmp, 4) == 0) return false;
        for (int c = 0; c < 4; ++c) out[i * 4 + c] = (float)tmp[c];
    }
    return true;
}

bool Model::loadFromFile(const std::string& path) {
    destroy();
    cgltf_options options;
    memset(&options, 0, sizeof(options));
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

    // --- Textures & materials ---
    materials_.resize(data->materials_count);
    for (cgltf_size i = 0; i < data->materials_count; ++i) {
        cgltf_material* m = &data->materials[i];
        Material& mat = materials_[i];
        mat.alphaCutoff = m->alpha_cutoff ? (float)m->alpha_cutoff : 0.5f;
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
                mat.normalScale = m->normal_texture.scale ? (float)m->normal_texture.scale : 1.0f;
            }
        }
        if (m->occlusion_texture.texture) {
            cgltf_texture* t = m->occlusion_texture.texture;
            if (t && t->image) {
                mat.occlusionTex = loadTextureFromImage(t->image, baseDir);
                mat.hasOcclusionTex = mat.occlusionTex != 0;
                mat.occlusionStrength = m->occlusion_texture.scale ? (float)m->occlusion_texture.scale : 1.0f;
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
            if (readAccessorFloats(s->inverse_bind_matrices, ibm)) {
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

            // AABB accumulation
            for (cgltf_size v = 0; v < vertCount; ++v) {
                glm::vec3 p(positions[v * 3 + 0], positions[v * 3 + 1], positions[v * 3 + 2]);
                if (!hasAabb_) { aabbMin_ = p; aabbMax_ = p; hasAabb_ = true; }
                else { aabbMin_ = glm::min(aabbMin_, p); aabbMax_ = glm::max(aabbMax_, p); }
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

            // Indices
            std::vector<GLuint> indices;
            if (prim->indices) {
                cgltf_accessor* acc = prim->indices;
                indices.resize(acc->count);
                for (cgltf_size k = 0; k < acc->count; ++k) {
                    cgltf_uint idx;
                    cgltf_accessor_read_uint(acc, k, &idx, 1);
                    indices[k] = (GLuint)idx;
                }
                P.indexCount = (int)acc->count;
                switch (acc->component_type) {
                    case cgltf_component_type_r_8u:  P.indexType = GL_UNSIGNED_BYTE; break;
                    case cgltf_component_type_r_16u: P.indexType = GL_UNSIGNED_SHORT; break;
                    case cgltf_component_type_r_32u: P.indexType = GL_UNSIGNED_INT; break;
                    default: P.indexType = GL_UNSIGNED_INT; break;
                }
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

    // Assign each primitive its node index (the node that owns its mesh).
    for (int ni = 0; ni < (int)nodes_.size(); ++ni) {
        if (nodes_[ni].meshIndex >= 0) {
            for (int pi : meshes_[nodes_[ni].meshIndex].primitiveIndices) {
                primitives_[pi].nodeIndex = ni;
                primitives_[pi].skinIndex = nodes_[ni].skinIndex;
            }
        }
    }

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

void Model::render(const Shader& shader) const {
    if (!loaded_) return;
    // Bind joint matrices per primitive that has a skin. We set them once
    // per primitive for simplicity.
    for (const auto& P : primitives_) {
        if (P.nodeIndex < 0) continue;

        const Material* mat = (P.materialIndex >= 0 && P.materialIndex < (int)materials_.size())
                              ? &materials_[P.materialIndex] : nullptr;

        glm::mat4 model = nodes_[P.nodeIndex].global;

        // Per the glTF 2.0 spec, skinned meshes are transformed ONLY by the
        // joint matrices (jointGlobal * inverseBind); the mesh node's own
        // world transform must be IDENTITY. Applying the node transform on
        // top of the skin double-transforms vertices and stretches the model.
        // For non-skinned meshes the node's global transform IS the model matrix.
        glm::mat4 effectiveModel;
        if (P.skinIndex >= 0 && P.skinIndex < (int)skins_.size()) {
            const Skin& s = skins_[P.skinIndex];
            GLint loc = glGetUniformLocation(shader.id(), "uJointMatrices[0]");
            if (loc >= 0) {
                glUniformMatrix4fv(loc, (GLsizei)s.jointMatrices.size(), GL_FALSE,
                                  (const GLfloat*)s.jointMatrices.data());
            }
            shader.setBool("uHasSkin", true);
            effectiveModel = glm::mat4(1.0f);   // identity for skinned meshes
        } else {
            shader.setBool("uHasSkin", false);
            effectiveModel = model;
        }
        shader.setMat4("uModel", effectiveModel);

        if (mat) applyMaterial(*mat, shader, glm::vec3(0), glm::vec3(0));
        else {
            shader.setBool("uHasSkin", P.skinIndex >= 0);
            shader.setBool("uUnlit", false);
            shader.setBool("uDoubleSided", false);
            shader.setInt("uAlphaMode", 0);
            shader.setBool("uHasBaseColorTex", false);
            shader.setVec4("uBaseColorFactor", glm::vec4(0.8f));
            glDisable(GL_BLEND);
            glEnable(GL_CULL_FACE);
        }

        glBindVertexArray(P.vao);
        glDrawElements(GL_TRIANGLES, P.indexCount, P.indexType, 0);
        glBindVertexArray(0);
    }
    // Restore default GL state.
    glDisable(GL_BLEND);
    glEnable(GL_CULL_FACE);
}

void Model::renderInstanced(const Shader& shader, GLuint instanceVbo,
                            int instanceCount) const {
    if (!loaded_ || instanceCount <= 0) return;
    shader.setBool("uInstanced", true);
    for (const auto& P : primitives_) {
        if (P.nodeIndex < 0) continue;

        const Material* mat = (P.materialIndex >= 0 && P.materialIndex < (int)materials_.size())
                              ? &materials_[P.materialIndex] : nullptr;

        glm::mat4 effectiveModel;
        if (P.skinIndex >= 0 && P.skinIndex < (int)skins_.size()) {
            const Skin& s = skins_[P.skinIndex];
            GLint loc = glGetUniformLocation(shader.id(), "uJointMatrices[0]");
            if (loc >= 0) {
                glUniformMatrix4fv(loc, (GLsizei)s.jointMatrices.size(), GL_FALSE,
                                   (const GLfloat*)s.jointMatrices.data());
            }
            shader.setBool("uHasSkin", true);
            effectiveModel = glm::mat4(1.0f);
        } else {
            shader.setBool("uHasSkin", false);
            effectiveModel = nodes_[P.nodeIndex].global;
        }
        shader.setMat4("uModel", effectiveModel);

        if (mat) applyMaterial(*mat, shader, glm::vec3(0), glm::vec3(0));
        else {
            shader.setBool("uUnlit", false);
            shader.setBool("uDoubleSided", false);
            shader.setInt("uAlphaMode", 0);
            shader.setBool("uHasBaseColorTex", false);
            shader.setVec4("uBaseColorFactor", glm::vec4(0.8f));
            glDisable(GL_BLEND);
            glEnable(GL_CULL_FACE);
        }

        glBindVertexArray(P.vao);
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
        glBindVertexArray(0);
    }
    shader.setBool("uInstanced", false);
    glDisable(GL_BLEND);
    glEnable(GL_CULL_FACE);
}

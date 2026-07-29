#include "material_graph.h"
#include "noise.h"
#include "sys_util.h"
#include <stb_image.h>
#include <stb_image_write.h>
#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <unordered_set>

const char* matNodeTypeName(int type) {
    switch ((MatNodeType)type) {
        case MatNodeType::Output:         return "Output";
        case MatNodeType::Image:          return "Image";
        case MatNodeType::SolidColor:     return "Solid color";
        case MatNodeType::Noise:          return "Noise";
        case MatNodeType::Checker:        return "Checker";
        case MatNodeType::Gradient:       return "Gradient";
        case MatNodeType::Mix:            return "Mix";
        case MatNodeType::Multiply:       return "Multiply";
        case MatNodeType::Add:            return "Add";
        case MatNodeType::BrightContrast: return "Bright/Contrast";
        case MatNodeType::Invert:         return "Invert";
        case MatNodeType::Grayscale:      return "Grayscale";
        case MatNodeType::HeightToNormal: return "Height to Normal";
        default: return "?";
    }
}

int matNodeInputCount(MatNodeType t) {
    switch (t) {
        case MatNodeType::Output:         return 1;
        case MatNodeType::Image:          return 0;
        case MatNodeType::SolidColor:     return 0;
        case MatNodeType::Noise:          return 0;
        case MatNodeType::Checker:        return 0;
        case MatNodeType::Gradient:       return 0;
        case MatNodeType::Mix:            return 3;
        case MatNodeType::Multiply:       return 2;
        case MatNodeType::Add:            return 2;
        case MatNodeType::BrightContrast: return 1;
        case MatNodeType::Invert:         return 1;
        case MatNodeType::Grayscale:      return 1;
        case MatNodeType::HeightToNormal: return 1;
        default: return 0;
    }
}

const char* matNodeInputName(MatNodeType t, int pin) {
    switch (t) {
        case MatNodeType::Output:   return pin == 0 ? "Albedo" : "?";
        case MatNodeType::Mix:
            return pin == 0 ? "A" : pin == 1 ? "B" : pin == 2 ? "Factor" : "?";
        case MatNodeType::Multiply:
        case MatNodeType::Add:
            return pin == 0 ? "A" : pin == 1 ? "B" : "?";
        case MatNodeType::BrightContrast:
        case MatNodeType::Invert:
        case MatNodeType::Grayscale:
        case MatNodeType::HeightToNormal:
            return pin == 0 ? "In" : "?";
        default: return "?";
    }
}

int MaterialGraph::addNode(MatNodeType type, const glm::vec2& uiPos) {
    MatNode n;
    n.id = nextNodeId++;
    n.type = type;
    n.uiPos = uiPos;
    nodes.push_back(n);
    return n.id;
}

bool MaterialGraph::removeNode(int nodeId) {
    if (nodeId == outputId) return false;   // the Output node is fixed
    auto it = std::find_if(nodes.begin(), nodes.end(),
                           [nodeId](const MatNode& n) { return n.id == nodeId; });
    if (it == nodes.end()) return false;
    nodes.erase(it);
    for (auto& n : nodes)
        for (int& inp : n.in)
            if (inp == nodeId) inp = -1;
    return true;
}

MatNode* MaterialGraph::findNode(int nodeId) {
    for (auto& n : nodes)
        if (n.id == nodeId) return &n;
    return nullptr;
}

const MatNode* MaterialGraph::findNode(int nodeId) const {
    for (const auto& n : nodes)
        if (n.id == nodeId) return &n;
    return nullptr;
}

int MaterialLibrary::addMaterial(MaterialGraph g) {
    g.id = nextId_++;
    if (g.outputId < 0 || !g.findNode(g.outputId)) {
        MatNode out;
        out.type = MatNodeType::Output;
        out.id = g.nextNodeId++;
        out.uiPos = glm::vec2(420.0f, 120.0f);
        g.outputId = out.id;
        g.nodes.insert(g.nodes.begin(), out);
    }
    materials_.push_back(std::move(g));
    return materials_.back().id;
}

void MaterialLibrary::addMaterialWithId(const MaterialGraph& g) {
    materials_.push_back(g);
    nextId_ = std::max(nextId_, g.id + 1);
}

bool MaterialLibrary::removeMaterial(int id) {
    auto it = std::find_if(materials_.begin(), materials_.end(),
                           [id](const MaterialGraph& g) { return g.id == id; });
    if (it == materials_.end()) return false;
    materials_.erase(it);
    return true;
}

MaterialGraph* MaterialLibrary::findMaterial(int id) {
    for (auto& g : materials_)
        if (g.id == id) return &g;
    return nullptr;
}

const MaterialGraph* MaterialLibrary::findMaterial(int id) const {
    for (const auto& g : materials_)
        if (g.id == id) return &g;
    return nullptr;
}

void MaterialLibrary::clear() {
    materials_.clear();
    nextId_ = 0;
}

bool matGraphReachable(const MaterialGraph& g, int fromId, int toId) {
    if (fromId < 0) return false;
    std::unordered_set<int> visited;
    std::vector<int> stack{fromId};
    while (!stack.empty()) {
        int cur = stack.back();
        stack.pop_back();
        if (cur == toId) return true;
        if (!visited.insert(cur).second) continue;
        const MatNode* n = g.findNode(cur);
        if (!n) continue;
        for (int inp : n->in)
            if (inp >= 0) stack.push_back(inp);
    }
    return false;
}

// ============================================================================
// Baking.
// ============================================================================

namespace {

struct LoadedImage {
    std::vector<uint8_t> pix;   // RGBA8
    int w = 0, h = 0;
};

// Load an image file as RGBA8 (UTF-8 safe; nearest fallback on failure).
bool loadImage(const std::string& path, LoadedImage& out) {
    std::vector<char> bytes;
    if (!readFileBytes(path, bytes)) return false;
    int w = 0, h = 0, comp = 0;
    stbi_uc* pix = stbi_load_from_memory(
        reinterpret_cast<const stbi_uc*>(bytes.data()), (int)bytes.size(),
        &w, &h, &comp, 4);
    if (!pix) return false;
    out.pix.assign(pix, pix + (size_t)w * h * 4);
    out.w = w;
    out.h = h;
    stbi_image_free(pix);
    return true;
}

float wrap01(float v) {
    float t = std::fmod(v, 1.0f);
    return t < 0.0f ? t + 1.0f : t;
}

glm::vec4 sampleImage(const LoadedImage& img, float u, float v) {
    if (img.w <= 0 || img.h <= 0) return glm::vec4(1.0f, 0.0f, 1.0f, 1.0f);
    // Bilinear with wrap-around addressing.
    float fx = wrap01(u) * img.w - 0.5f;
    float fy = wrap01(v) * img.h - 0.5f;
    int x0 = (int)std::floor(fx), y0 = (int)std::floor(fy);
    float tx = fx - x0, ty = fy - y0;
    auto px = [&](int x, int y) {
        x = ((x % img.w) + img.w) % img.w;
        y = ((y % img.h) + img.h) % img.h;
        const uint8_t* p = &img.pix[((size_t)y * img.w + x) * 4];
        return glm::vec4(p[0] / 255.0f, p[1] / 255.0f,
                         p[2] / 255.0f, p[3] / 255.0f);
    };
    glm::vec4 c00 = px(x0, y0), c10 = px(x0 + 1, y0);
    glm::vec4 c01 = px(x0, y0 + 1), c11 = px(x0 + 1, y0 + 1);
    return glm::mix(glm::mix(c00, c10, tx), glm::mix(c01, c11, tx), ty);
}

struct BakeCtx {
    const MaterialGraph& g;
    std::unordered_map<int, LoadedImage> images;    // node id -> pixels
    std::unordered_map<int, std::vector<int>> perms; // noise seed -> perm table

    const int* permFor(int seed) {
        auto it = perms.find(seed);
        if (it == perms.end()) {
            std::vector<int> t(512);
            Noise::buildPerm(seed, t.data());
            it = perms.emplace(seed, std::move(t)).first;
        }
        return it->second.data();
    }
};

glm::vec4 evalSubtree(const MatNode& n, float u, float v, BakeCtx& ctx,
                      int depth);

// Non-HTN evaluation used both by the topo loop and by HeightToNormal's
// offset re-sampling (which re-evaluates the input subtree recursively).
glm::vec4 evalSimple(const MatNode& n, float u, float v, BakeCtx& ctx,
                     int depth) {
    switch (n.type) {
        case MatNodeType::Output: {
            const MatNode* in = n.in[0] >= 0 ? ctx.g.findNode(n.in[0]) : nullptr;
            if (!in || depth >= 32)
                return glm::vec4(1.0f, 0.0f, 1.0f, 1.0f);   // unconnected = magenta
            return evalSubtree(*in, u, v, ctx, depth + 1);
        }
        case MatNodeType::Image: {
            auto it = ctx.images.find(n.id);
            if (it == ctx.images.end())
                return glm::vec4(1.0f, 0.0f, 1.0f, 1.0f);
            float tu = u * (n.p[0] != 0.0f ? n.p[0] : 1.0f) + n.p[2];
            float tv = v * (n.p[1] != 0.0f ? n.p[1] : 1.0f) + n.p[3];
            return sampleImage(it->second, tu, tv);
        }
        case MatNodeType::SolidColor:
            return n.color;
        case MatNodeType::Noise: {
            Noise::Params np;
            np.type = (Noise::Type)std::clamp(n.ip[0], 0,
                                              (int)Noise::TypeCount - 1);
            np.seed = n.ip[1];
            np.octaves = std::clamp(n.ip[2], 1, 8);
            np.frequency = n.p[0] != 0.0f ? n.p[0] : 4.0f;
            np.persistence = n.p[1] != 0.0f ? n.p[1] : 0.5f;
            np.lacunarity = n.p[2] != 0.0f ? n.p[2] : 2.0f;
            float s = Noise::sampleRawWithPerm(np, u, v, ctx.permFor(np.seed));
            float g01 = std::clamp(s * 0.5f + 0.5f, 0.0f, 1.0f);
            return glm::vec4(g01, g01, g01, 1.0f);
        }
        case MatNodeType::Checker: {
            float s = n.p[0] != 0.0f ? n.p[0] : 8.0f;
            int c = ((int)std::floor(u * s) + (int)std::floor(v * s)) & 1;
            return glm::vec4((float)c, (float)c, (float)c, 1.0f);
        }
        case MatNodeType::Gradient: {
            float d = u * std::cos(n.p[0]) + v * std::sin(n.p[0]);
            float g01 = std::clamp(d, 0.0f, 1.0f);
            return glm::vec4(g01, g01, g01, 1.0f);
        }
        case MatNodeType::Mix:
        case MatNodeType::Multiply:
        case MatNodeType::Add:
        case MatNodeType::BrightContrast:
        case MatNodeType::Invert:
        case MatNodeType::Grayscale:
            // Handled by evalSubtree (needs inputs).
            if (depth >= 32) return glm::vec4(0.0f);
            return evalSubtree(n, u, v, ctx, depth + 1);
        case MatNodeType::HeightToNormal: {
            // Height map (input luminance) -> tangent-space normal.
            if (n.in[0] < 0 || depth >= 32) return glm::vec4(0.5f, 0.5f, 1.0f, 1.0f);
            const MatNode* in = ctx.g.findNode(n.in[0]);
            if (!in) return glm::vec4(0.5f, 0.5f, 1.0f, 1.0f);
            const float du = 1.0f / 256.0f;
            auto lum = [&](float uu, float vv) {
                glm::vec4 c = evalSimple(*in, uu, vv, ctx, depth + 1);
                return 0.299f * c.r + 0.587f * c.g + 0.114f * c.b;
            };
            float hl = lum(u - du, v), hr = lum(u + du, v);
            float hd = lum(u, v - du), hu = lum(u, v + du);
            float k = n.p[0] != 0.0f ? n.p[0] : 2.0f;
            glm::vec3 nrm = glm::normalize(glm::vec3((hl - hr) * k,
                                                     (hd - hu) * k, 1.0f));
            return glm::vec4(nrm * 0.5f + 0.5f, 1.0f);
        }
        default:
            return glm::vec4(0.0f);
    }
}

glm::vec4 evalSubtree(const MatNode& n, float u, float v, BakeCtx& ctx,
                      int depth) {
    if (depth >= 32) return glm::vec4(0.0f);
    if (n.type != MatNodeType::Mix && n.type != MatNodeType::Multiply &&
        n.type != MatNodeType::Add && n.type != MatNodeType::BrightContrast &&
        n.type != MatNodeType::Invert && n.type != MatNodeType::Grayscale)
        return evalSimple(n, u, v, ctx, depth);

    auto inVal = [&](int pin, float def) {
        if (n.in[pin] < 0) return glm::vec4(def);
        const MatNode* m = ctx.g.findNode(n.in[pin]);
        if (!m) return glm::vec4(def);
        return evalSimple(*m, u, v, ctx, depth + 1);
    };
    switch (n.type) {
        case MatNodeType::Mix: {
            glm::vec4 a = inVal(0, 0.0f);
            glm::vec4 b = inVal(1, 0.0f);
            float f = n.in[2] >= 0 ? inVal(2, 0.5f).r : n.p[0];
            return glm::mix(a, b, std::clamp(f, 0.0f, 1.0f));
        }
        case MatNodeType::Multiply:
            return inVal(0, 1.0f) * inVal(1, 1.0f);
        case MatNodeType::Add:
            return glm::clamp(inVal(0, 0.0f) + inVal(1, 0.0f),
                              glm::vec4(0.0f), glm::vec4(1.0f));
        case MatNodeType::BrightContrast: {
            glm::vec4 c = inVal(0, 0.0f);
            float br = n.p[0], ct = n.p[1] != 0.0f ? n.p[1] : 1.0f;
            glm::vec4 r = (c + glm::vec4(br)) - glm::vec4(0.5f);
            r = r * ct + glm::vec4(0.5f);
            return glm::vec4(glm::clamp(glm::vec3(r), 0.0f, 1.0f), c.a);
        }
        case MatNodeType::Invert: {
            glm::vec4 c = inVal(0, 0.0f);
            return glm::vec4(1.0f - c.r, 1.0f - c.g, 1.0f - c.b, c.a);
        }
        case MatNodeType::Grayscale: {
            glm::vec4 c = inVal(0, 0.0f);
            float l = 0.299f * c.r + 0.587f * c.g + 0.114f * c.b;
            return glm::vec4(l, l, l, c.a);
        }
        default:
            return evalSimple(n, u, v, ctx, depth);
    }
}

}  // namespace

bool bakeMaterial(const MaterialGraph& g, int w, int h,
                  std::vector<uint8_t>& outPixels) {
    if (w <= 0 || h <= 0 || w > 4096 || h > 4096) return false;
    const MatNode* out = g.findNode(g.outputId);
    if (!out || out->type != MatNodeType::Output) return false;

    BakeCtx ctx{g, {}, {}};

    // Load every Image node's pixels once per bake.
    for (const auto& n : g.nodes) {
        if (n.type != MatNodeType::Image || n.path.empty()) continue;
        LoadedImage img;
        if (loadImage(n.path, img)) ctx.images[n.id] = std::move(img);
    }

    outPixels.resize((size_t)w * h * 4);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            float u = (x + 0.5f) / float(w);
            float v = (y + 0.5f) / float(h);
            glm::vec4 c = glm::clamp(evalSubtree(*out, u, v, ctx, 0),
                                     0.0f, 1.0f);
            uint8_t* p = &outPixels[((size_t)y * w + x) * 4];
            p[0] = (uint8_t)(c.r * 255.0f + 0.5f);
            p[1] = (uint8_t)(c.g * 255.0f + 0.5f);
            p[2] = (uint8_t)(c.b * 255.0f + 0.5f);
            p[3] = (uint8_t)(c.a * 255.0f + 0.5f);
        }
    }
    return true;
}

bool writePng(const std::string& path, int w, int h,
              const std::vector<uint8_t>& pixels) {
    if (pixels.size() < (size_t)w * h * 4) return false;
    std::vector<uint8_t> out;
    auto cb = [](void* ctx, void* data, int size) {
        auto* v = static_cast<std::vector<uint8_t>*>(ctx);
        const uint8_t* b = static_cast<const uint8_t*>(data);
        v->insert(v->end(), b, b + size);
    };
    int ok = stbi_write_png_to_func(cb, &out, w, h, 4, pixels.data(), w * 4);
    if (!ok) return false;
    return writeFileBytes(path, out.data(), out.size());
}

#include "app.h"
#include "model.h"
#include "file_dialog.h"
#include "sys_util.h"
#include <glm/glm.hpp>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <algorithm>
#include <filesystem>
#include <nlohmann/json.hpp>

// ---------------------------------------------------------------------------
// Scene save / load
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Scene save / load
// ---------------------------------------------------------------------------

static std::string baseDirOf(const std::string& path) {
    std::string p = path;
    std::replace(p.begin(), p.end(), '\\', '/');
    size_t slash = p.find_last_of('/');
    return (slash != std::string::npos) ? p.substr(0, slash) : ".";
}

static std::string relPath(const std::string& absPath, const std::string& baseDir) {
    // Simple relative path: if the file is under baseDir, strip the prefix.
    std::string a = absPath;
    std::string b = baseDir;
    std::replace(a.begin(), a.end(), '\\', '/');
    std::replace(b.begin(), b.end(), '\\', '/');
    if (!b.empty() && b.back() != '/') b += '/';
    if (a.size() > b.size() && a.compare(0, b.size(), b) == 0)
        return a.substr(b.size());
    return a;
}

static std::string absPath(const std::string& relOrAbs, const std::string& baseDir) {
    std::filesystem::path p(relOrAbs);
    if (p.is_absolute()) return relOrAbs;
    return (std::filesystem::path(baseDir) / relOrAbs).string();
}

bool App::saveScene(const std::string& path) {
    std::string baseDir = baseDirOf(path);

    // Build JSON metadata (everything except heights/splat binary blobs).
    nlohmann::json root = nlohmann::json::object();
    root["version"] = 2;

    // Terrain metadata.
    nlohmann::json terrain = nlohmann::json::object();
    terrain["gridX"] = terrain_.gridX();
    terrain["gridZ"] = terrain_.gridZ();
    terrain["worldSize"] = terrain_.worldSize();

    nlohmann::json layers = nlohmann::json::array();
    for (int i = 0; i < terrain_.layerCount(); ++i) {
        const auto& L = terrain_.layers()[i];
        nlohmann::json layer = nlohmann::json::object();
        layer["name"] = L.name;
        layer["albedo"] = relPath(L.albedoPath, baseDir);
        layer["normal"] = relPath(L.normalPath, baseDir);
        layer["tileSize"] = L.tileSize;
        layers.push_back(layer);
    }
    terrain["layers"] = layers;
    root["terrain"] = terrain;

    // Skybox.
    nlohmann::json sky = nlohmann::json::object();
    if (!skybox_.isDefault()) {
        sky["path"] = relPath(skybox_.importedPath(), baseDir);
    } else {
        sky["path"] = "";
    }
    sky["exposure"] = skyExposure_;
    root["skybox"] = sky;

    // Lighting.
    nlohmann::json light = nlohmann::json::object();
    light["azimuth"] = lightAzimuth_;
    light["elevation"] = lightElevation_;
    root["light"] = light;

    // Camera.
    nlohmann::json cam = nlohmann::json::object();
    cam["tx"] = camera_.target().x;
    cam["ty"] = camera_.target().y;
    cam["tz"] = camera_.target().z;
    cam["yaw"] = camera_.yaw();
    cam["pitch"] = camera_.pitch();
    cam["distance"] = camera_.distance();
    root["camera"] = cam;

    // Props.
    nlohmann::json propsArr = nlohmann::json::array();
    for (const auto& p : props_.props()) {
        if (!p.model) continue;
        nlohmann::json prop = nlohmann::json::object();
        prop["path"] = relPath(p.model->sourcePath(), baseDir);
        prop["px"] = p.position.x;
        prop["py"] = p.position.y;
        prop["pz"] = p.position.z;
        prop["rx"] = p.rotationEuler.x;
        prop["ry"] = p.rotationEuler.y;
        prop["rz"] = p.rotationEuler.z;
        prop["sx"] = p.scale.x;
        prop["sy"] = p.scale.y;
        prop["sz"] = p.scale.z;
        prop["name"] = p.displayName;
        propsArr.push_back(prop);
    }
    root["props"] = propsArr;

    // Details.
    nlohmann::json details = nlohmann::json::object();
    nlohmann::json protos = nlohmann::json::array();
    for (int i = 0; i < details_.prototypeCount(); ++i) {
        const auto& p = details_.prototype(i);
        nlohmann::json proto = nlohmann::json::object();
        proto["path"] = p.model ? relPath(p.model->sourcePath(), baseDir) : "";
        proto["name"] = p.name;
        proto["targetSize"] = p.targetSize;
        proto["minScale"] = p.minScale;
        proto["maxScale"] = p.maxScale;
        proto["randomYaw"] = p.randomYaw;
        protos.push_back(proto);
    }
    details["prototypes"] = protos;

    nlohmann::json insts = nlohmann::json::array();
    for (const auto& inst : details_.instances()) {
        nlohmann::json iv = nlohmann::json::object();
        iv["p"] = inst.prototypeIndex;
        iv["x"] = inst.position.x;
        iv["y"] = inst.position.y;
        iv["z"] = inst.position.z;
        iv["yaw"] = inst.yaw;
        iv["scale"] = inst.scale;
        insts.push_back(iv);
    }
    details["instances"] = insts;
    root["details"] = details;

    // Blocks (build system).
    nlohmann::json btArr = nlohmann::json::array();
    for (int i = 0; i < build_.blockTextureCount(); ++i) {
        nlohmann::json entry = nlohmann::json::object();
        entry["path"] = build_.blockTexturePath(i);
        btArr.push_back(entry);
    }
    root["blockTextures"] = btArr;

    nlohmann::json blocksArr = nlohmann::json::array();
    for (const auto& b : build_.blocks()) {
        nlohmann::json bk = nlohmann::json::object();
        bk["type"] = (int)b.type;
        bk["cx"] = b.position.x;
        bk["cy"] = b.position.y;
        bk["cz"] = b.position.z;
        bk["sx"] = b.size.x;
        bk["sy"] = b.size.y;
        bk["sz"] = b.size.z;
        bk["r"]  = b.color.r;
        bk["g"]  = b.color.g;
        bk["b"]  = b.color.b;
        bk["yaw"] = b.yaw;
        bk["ti"] = b.textureIdx;
        bk["tf"] = b.textureFace;
        bk["ts"] = b.texScale;
        bk["tm"] = b.texMode;
        blocksArr.push_back(bk);
    }
    root["blocks"] = blocksArr;

    std::string jsonStr = root.dump(2);

    // --- Assemble single binary file: magic + version + JSON + heights + splat ---
    // Built in memory first so a mid-write failure can't leave a truncated
    // file half-flushed by the stream.
    std::vector<char> file;
    auto appendRaw = [&](const void* data, size_t size) {
        const char* p = reinterpret_cast<const char*>(data);
        file.insert(file.end(), p, p + size);
    };
    auto appendU32 = [&](uint32_t v) { appendRaw(&v, sizeof(v)); };

    // Magic + version.
    const char magic[4] = {'S','C','N','E'};
    appendRaw(magic, 4);
    appendU32(2);

    // JSON: size prefix + data.
    appendU32((uint32_t)jsonStr.size());
    appendRaw(jsonStr.data(), jsonStr.size());

    // Heights: size prefix + float data.
    const auto& heights = terrain_.heightsData();
    appendU32((uint32_t)(heights.size() * sizeof(float)));
    appendRaw(heights.data(), heights.size() * sizeof(float));

    // Splat: size prefix + byte data.
    const auto& splat = terrain_.splatData();
    appendU32((uint32_t)splat.size());
    appendRaw(splat.data(), splat.size());

    // UTF-8 aware write (Windows paths with non-ASCII characters work).
    if (!writeFileBytes(path, file.data(), file.size())) {
        std::cerr << "Cannot write scene: " << path << "\n";
        return false;
    }

    std::cerr << "[SAVE] " << path << "  blocks=" << build_.count() << "\n";
    return true;
}

bool App::loadScene(const std::string& path) {
    // Read entire file into memory (UTF-8 aware: works with non-ASCII paths).
    std::vector<char> buf;
    if (!readFileBytes(path, buf)) {
        std::cerr << "Cannot open scene: " << path << "\n";
        return false;
    }
    if (buf.size() < 16) { std::cerr << "Scene file too small\n"; return false; }

    // Bounds-checked cursor over the buffer. Every length field comes from
    // the file and must be validated before use — a truncated or hostile
    // .scene must not read past the buffer.
    size_t off = 0;
    auto readU32 = [&](uint32_t& v) -> bool {
        if (buf.size() - off < sizeof(v)) return false;
        std::memcpy(&v, buf.data() + off, sizeof(v));
        off += sizeof(v);
        return true;
    };
    auto readBlob = [&](uint32_t bytes, const char*& ptr) -> bool {
        if ((size_t)bytes > buf.size() - off) return false;   // also catches underflow
        ptr = buf.data() + off;
        off += bytes;
        return true;
    };

    // Parse header: magic + version.
    if (std::memcmp(buf.data(), "SCNE", 4) != 0) {
        std::cerr << "Scene file: bad magic\n"; return false;
    }
    off += 4;
    uint32_t version;
    if (!readU32(version)) { std::cerr << "Scene file truncated\n"; return false; }
    if (version != 1 && version != 2) {
        std::cerr << "Unsupported scene version: " << version << "\n";
        return false;
    }

    // JSON: size prefix + data.
    uint32_t jsonSize;
    const char* jsonPtr = nullptr;
    if (!readU32(jsonSize) || !readBlob(jsonSize, jsonPtr)) {
        std::cerr << "Scene file truncated (JSON)\n"; return false;
    }
    std::string jsonStr(jsonPtr, jsonSize);

    nlohmann::json root = nlohmann::json::parse(jsonStr, nullptr, false);
    if (!root.is_object()) { std::cerr << "Invalid scene JSON\n"; return false; }

    // Heights: size prefix + float data.
    uint32_t heightsBytes;
    const char* heightsPtr = nullptr;
    if (!readU32(heightsBytes) || !readBlob(heightsBytes, heightsPtr)) {
        std::cerr << "Scene file truncated (heights)\n"; return false;
    }

    // Splat: size prefix + byte data.
    uint32_t splatBytes;
    const char* splatPtr = nullptr;
    if (!readU32(splatBytes) || !readBlob(splatBytes, splatPtr)) {
        std::cerr << "Scene file truncated (splat)\n"; return false;
    }

    std::string baseDir = baseDirOf(path);

    // --- Apply terrain ---
    const auto& t = root["terrain"];
    if (t.is_object()) {
        // Sanity-clamp grid dims before they are used in size math so a
        // hostile file can't overflow int in gx*gz.
        int gx = std::clamp(t.value("gridX", terrain_.gridX()), 1, 4096);
        int gz = std::clamp(t.value("gridZ", terrain_.gridZ()), 1, 4096);

        // Load heights from embedded binary blob. If the grid dims don't
        // match the current terrain the heightfield is left as-is (a warning
        // is better than silently mixing a new scene with the old terrain).
        if (heightsBytes == (uint32_t)((size_t)gx * gz * sizeof(float)) &&
            gx == terrain_.gridX() && gz == terrain_.gridZ()) {
            std::vector<float> heights((size_t)gx * gz);
            std::memcpy(heights.data(), heightsPtr, heightsBytes);
            terrain_.setHeights(heights);
        }

        // Load splat from embedded binary blob. Version 2 uses planar
        // multi-splat (4 RGBA maps = 16 channels, gx*gz*16 bytes). Version 1
        // used a single RGBA map (gx*gz*4); migrate it into map 0 of the new
        // 16-channel planar layout and leave maps 1-3 zero.
        if (splatBytes == (uint32_t)((size_t)gx * gz * 16)) {
            std::vector<uint8_t> splat((size_t)splatBytes);
            std::memcpy(splat.data(), splatPtr, splatBytes);
            terrain_.setSplat(splat);
        } else if (splatBytes == (uint32_t)((size_t)gx * gz * 4)) {
            std::vector<uint8_t> splat16((size_t)gx * gz * 16, 0);
            size_t map0 = 0;
            for (size_t p = 0; p < (size_t)gx * gz; ++p)
                std::memcpy(&splat16[map0 + p * 4], splatPtr + p * 4, 4);
            terrain_.setSplat(splat16);
        }

        // Layers (library, up to MAX_LAYERS). Replace existing slot textures,
        // append brand-new layers, then trim any leftover procedural layers
        // beyond the saved set so the loaded layer list matches exactly.
        const auto& layers = t["layers"];
        if (layers.is_array()) {
            size_t savedN = layers.size();
            for (size_t i = 0; i < savedN; ++i) {
                const auto& L = layers[i];
                std::string albedo = L.value("albedo", "");
                std::string normal = L.value("normal", "");
                std::string nm = L.value("name", "");
                float ts = L.value("tileSize", 8.0f);
                if ((int)i >= terrain_.layerCount()) {
                    int idx = albedo.empty() ? -1 : terrain_.addLayer(absPath(albedo, baseDir));
                    if (idx < 0) continue;
                    if (!normal.empty()) terrain_.loadLayerNormal(idx, absPath(normal, baseDir));
                    terrain_.setLayerName(idx, nm);
                    terrain_.setLayerTileSize(idx, ts);
                } else {
                    if (!albedo.empty()) terrain_.loadLayerAlbedo((int)i, absPath(albedo, baseDir));
                    if (!normal.empty()) terrain_.loadLayerNormal((int)i, absPath(normal, baseDir));
                    terrain_.setLayerName((int)i, nm);
                    terrain_.setLayerTileSize((int)i, ts);
                }
            }
            while (terrain_.layerCount() > (int)savedN)
                terrain_.removeLayer(terrain_.layerCount() - 1);
        }
    }

    // --- Skybox ---
    const auto& sky = root["skybox"];
    if (sky.is_object()) {
        skyExposure_ = sky.value("exposure", 1.0f);
        std::string skyPath = sky.value("path", "");
        if (!skyPath.empty()) skybox_.loadEquirect(skyboxConvertShader_, absPath(skyPath, baseDir));
        else skybox_.resetToDefault();
    }

    // --- Lighting ---
    const auto& light = root["light"];
    if (light.is_object()) {
        lightAzimuth_   = light.value("azimuth", 0.6f);
        lightElevation_ = light.value("elevation", 0.9f);
    }

    // --- Camera ---
    const auto& cam = root["camera"];
    if (cam.is_object()) {
        glm::vec3 target(cam.value("tx", 0.0f), cam.value("ty", 0.0f), cam.value("tz", 0.0f));
        camera_.setTarget(target);
        camera_.setYaw(cam.value("yaw", -0.6f));
        camera_.setPitch(cam.value("pitch", 0.6f));
        camera_.setDistance(cam.value("distance", 60.0f));
    }

    // --- Clear existing props + details + blocks + model library ---
    // (The library must be cleared too: it owns GL buffers of every imported
    // model, so keeping it across loads leaks GPU memory on every load.)
    props_.clear();
    details_.clearInstances();
    details_.clearPrototypes();
    build_.clear();
    modelLibrary_.clear();
    selectedBlockId_ = -1;
    selectedBlockFace_ = -1;

    // --- Block texture library (reload paths before blocks reference them) ---
    const auto& btLib = root["blockTextures"];
    if (btLib.is_array()) {
        for (size_t i = 0; i < btLib.size(); ++i) {
            const auto& e = btLib[i];
            std::string p = absPath(e.value("path", ""), baseDir);
            if (!p.empty()) build_.loadBlockTexture(p);
        }
    }

    // --- Props ---
    const auto& props = root["props"];
    if (props.is_array()) {
        for (size_t i = 0; i < props.size(); ++i) {
            const auto& p = props[i];
            std::string modelPath = absPath(p.value("path", ""), baseDir);
            if (modelPath.empty()) continue;
            auto model = std::make_shared<Model>();
            if (!model->loadFromFile(modelPath)) {
                std::cerr << "Scene load: failed prop model: " << modelPath << "\n";
                continue;
            }
            modelLibrary_.push_back(model);
            glm::vec3 pos(p.value("px", 0.0f), p.value("py", 0.0f), p.value("pz", 0.0f));
            std::string name = p.value("name", "");
            int id = props_.addProp(model, pos, pos.y, 0.0f, name);
            if (id >= 0) {
                Prop* prop = props_.findProp(id);
                if (prop) {
                    prop->rotationEuler = glm::vec3(p.value("rx", 0.0f), p.value("ry", 0.0f), p.value("rz", 0.0f));
                    prop->scale = glm::vec3(p.value("sx", 0.0f), p.value("sy", 0.0f), p.value("sz", 0.0f));
                    prop->position = pos;
                }
            }
        }
    }

    // --- Details ---
    const auto& det = root["details"];
    if (det.is_object()) {
        const auto& protos = det["prototypes"];
        if (protos.is_array()) {
            for (size_t i = 0; i < protos.size(); ++i) {
                const auto& p = protos[i];
                std::string modelPath = absPath(p.value("path", ""), baseDir);
                if (modelPath.empty()) continue;
                auto model = std::make_shared<Model>();
                if (!model->loadFromFile(modelPath)) {
                    std::cerr << "Scene load: failed detail model: " << modelPath << "\n";
                    continue;
                }
                modelLibrary_.push_back(model);
                details_.addPrototype(model, p.value("name", ""), p.value("targetSize", 2.0f), modelPath);
                int pi = details_.prototypeCount() - 1;
                auto* proto = details_.prototypeMutable(pi);
                if (proto) {
                    proto->minScale  = p.value("minScale", 0.8f);
                    proto->maxScale  = p.value("maxScale", 1.2f);
                    proto->randomYaw = p.value("randomYaw", 1.0f);
                }
            }
        }
        // Instances.
        const auto& insts = det["instances"];
        if (insts.is_array()) {
            for (size_t i = 0; i < insts.size(); ++i) {
                const auto& iv = insts[i];
                DetailSystem::Instance inst;
                inst.prototypeIndex = iv.value("p", 0);
                inst.position = glm::vec3(iv.value("x", 0.0f), iv.value("y", 0.0f), iv.value("z", 0.0f));
                inst.yaw = iv.value("yaw", 0.0f);
                inst.scale = iv.value("scale", 1.0f);
                details_.addInstance(inst);
            }
        }
    }

    // --- Blocks ---
    const auto& blocks = root["blocks"];
    if (blocks.is_array()) {
        for (size_t i = 0; i < blocks.size(); ++i) {
            const auto& bk = blocks[i];
            glm::vec3 center(bk.value("cx", 0.0f), bk.value("cy", 0.0f), bk.value("cz", 0.0f));
            glm::vec3 size(bk.value("sx", 0.0f), bk.value("sy", 0.0f), bk.value("sz", 0.0f));
            glm::vec3 color(bk.value("r", 0.55f), bk.value("g", 0.45f), bk.value("b", 0.35f));
            BuildSystem::BlockType type = (BuildSystem::BlockType)bk.value("type", (int)BuildSystem::Wall);
            float yaw = bk.value("yaw", 0.0f);
            int id = build_.placeBlock(center, size, type, color, yaw);
            // Restore per-block face texture. Validate against the LOADED
            // library size — if a texture file was missing its entry is
            // skipped and saved indices no longer line up; drop those refs.
            int ti = bk.value("ti", -1);
            int tf = bk.value("tf", -1);
            float ts = bk.value("ts", 1.0f);
            int tm = bk.value("tm", 0);
            if (ti >= build_.blockTextureCount()) ti = -1;
            if (ti >= 0 && tf >= 0 && id >= 0) {
                build_.setBlockFaceTexture(id, ti, tf);
                build_.setBlockTexScale(id, ts);
                build_.setBlockTexMode(id, tm);
            }
        }
    }

    return true;
}

#include "scene_io.h"
#include "terrain.h"
#include "skybox.h"
#include "camera.h"
#include "scene_camera.h"
#include "spawn.h"
#include "sim.h"
#include "prop.h"
#include "detail.h"
#include "build.h"
#include "model.h"
#include "shader.h"
#include "sys_util.h"
#include <glm/glm.hpp>
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <nlohmann/json.hpp>

// ---------------------------------------------------------------------------
// Scene save / load — free functions, not App members.
// ---------------------------------------------------------------------------

static std::string baseDirOf(const std::string& path) {
    std::string p = path;
    std::replace(p.begin(), p.end(), '\\', '/');
    size_t slash = p.find_last_of('/');
    return (slash != std::string::npos) ? p.substr(0, slash) : ".";
}

static std::string relPath(const std::string& absPath, const std::string& baseDir) {
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

bool saveScene(const std::string& path, const SceneContext& ctx) {
    std::string baseDir = baseDirOf(path);

    nlohmann::json root = nlohmann::json::object();
    root["version"] = 2;

    // Terrain metadata.
    nlohmann::json terrain = nlohmann::json::object();
    terrain["gridX"] = ctx.terrain.gridX();
    terrain["gridZ"] = ctx.terrain.gridZ();
    terrain["worldSize"] = ctx.terrain.worldSize();

    nlohmann::json layers = nlohmann::json::array();
    for (int i = 0; i < ctx.terrain.layerCount(); ++i) {
        const auto& L = ctx.terrain.layers()[i];
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
    if (!ctx.skybox.isDefault()) {
        sky["path"] = relPath(ctx.skybox.importedPath(), baseDir);
    } else {
        sky["path"] = "";
    }
    sky["exposure"] = ctx.skyExposure;
    root["skybox"] = sky;

    // Lighting.
    nlohmann::json light = nlohmann::json::object();
    light["azimuth"] = ctx.lightAzimuth;
    light["elevation"] = ctx.lightElevation;
    root["light"] = light;

    // Camera.
    nlohmann::json cam = nlohmann::json::object();
    cam["tx"] = ctx.camera.target().x;
    cam["ty"] = ctx.camera.target().y;
    cam["tz"] = ctx.camera.target().z;
    cam["yaw"] = ctx.camera.yaw();
    cam["pitch"] = ctx.camera.pitch();
    cam["distance"] = ctx.camera.distance();
    root["camera"] = cam;

    // Scene cameras (game reads these; id is the stable key).
    nlohmann::json camsArr = nlohmann::json::array();
    for (const auto& c : ctx.cameraRig.cameras()) {
        nlohmann::json sc = nlohmann::json::object();
        sc["id"]   = c.id;
        sc["name"] = c.name;
        sc["tag"]  = c.tag;
        sc["px"] = c.position.x;
        sc["py"] = c.position.y;
        sc["pz"] = c.position.z;
        sc["tx"] = c.target.x;
        sc["ty"] = c.target.y;
        sc["tz"] = c.target.z;
        sc["fov"]  = c.fov;
        sc["near"] = c.nearPlane;
        sc["far"]  = c.farPlane;
        camsArr.push_back(sc);
    }
    root["cameras"] = camsArr;
    root["activeCamera"] = ctx.cameraRig.activeId();

    // Character spawn markers (with their condition/action logic graphs).
    nlohmann::json spawnsArr = nlohmann::json::array();
    for (const auto& s : ctx.spawns.spawns()) {
        nlohmann::json sj = nlohmann::json::object();
        sj["id"]   = s.id;
        sj["name"] = s.name;
        sj["tag"]  = s.tag;
        sj["px"] = s.position.x;
        sj["py"] = s.position.y;
        sj["pz"] = s.position.z;
        sj["yaw"]   = s.yaw;
        sj["model"] = relPath(s.modelPath, baseDir);
        sj["scale"] = s.scale;
        sj["anim"]  = s.defaultAnim;
        sj["root"]  = s.rootId;
        nlohmann::json nodesArr = nlohmann::json::array();
        for (const auto& n : s.nodes) {
            nlohmann::json nj = nlohmann::json::object();
            nj["id"] = n.id;
            nj["kind"] = (int)n.kind;
            nj["ct"] = (int)n.cond.type;
            nj["flag"] = n.cond.flagId;
            nj["val"] = n.cond.value;
            nj["at"] = (int)n.act.type;
            nj["param"] = n.act.param;
            nj["ip"]  = n.act.intParam;
            nj["ip2"] = n.act.intParam2;
            nj["fp"]  = n.act.floatParam;
            nj["ux"] = n.uiPos.x;
            nj["uy"] = n.uiPos.y;
            nj["t"] = n.nextTrue;
            nj["f"] = n.nextFalse;
            nodesArr.push_back(nj);
        }
        sj["nodes"] = nodesArr;
        spawnsArr.push_back(sj);
    }
    root["spawns"] = spawnsArr;

    // Simulation flags (initial values for the game / editor testing).
    nlohmann::json flagsObj = nlohmann::json::object();
    for (const auto& kv : ctx.sim.flags())
        flagsObj[std::to_string(kv.first)] = kv.second;
    root["flags"] = flagsObj;

    // Props.
    nlohmann::json propsArr = nlohmann::json::array();
    for (const auto& p : ctx.props.props()) {
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
    for (int i = 0; i < ctx.details.prototypeCount(); ++i) {
        const auto& p = ctx.details.prototype(i);
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
    for (const auto& inst : ctx.details.instances()) {
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
    for (int i = 0; i < ctx.build.blockTextureCount(); ++i) {
        nlohmann::json entry = nlohmann::json::object();
        entry["path"] = ctx.build.blockTexturePath(i);
        btArr.push_back(entry);
    }
    root["blockTextures"] = btArr;

    nlohmann::json blocksArr = nlohmann::json::array();
    for (const auto& b : ctx.build.blocks()) {
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

    // --- Assemble single binary file ---
    std::vector<char> file;
    auto appendRaw = [&](const void* data, size_t size) {
        const char* p = reinterpret_cast<const char*>(data);
        file.insert(file.end(), p, p + size);
    };
    auto appendU32 = [&](uint32_t v) { appendRaw(&v, sizeof(v)); };

    const char magic[4] = {'S','C','N','E'};
    appendRaw(magic, 4);
    appendU32(2);

    appendU32((uint32_t)jsonStr.size());
    appendRaw(jsonStr.data(), jsonStr.size());

    const auto& heights = ctx.terrain.heightsData();
    appendU32((uint32_t)(heights.size() * sizeof(float)));
    appendRaw(heights.data(), heights.size() * sizeof(float));

    const auto& splat = ctx.terrain.splatData();
    appendU32((uint32_t)splat.size());
    appendRaw(splat.data(), splat.size());

    if (!writeFileBytes(path, file.data(), file.size())) {
        std::cerr << "Cannot write scene: " << path << "\n";
        return false;
    }

    std::cerr << "[SAVE] " << path << "  blocks=" << ctx.build.count() << "\n";
    return true;
}

bool loadScene(const std::string& path, SceneContext& ctx) {
    std::vector<char> buf;
    if (!readFileBytes(path, buf)) {
        std::cerr << "Cannot open scene: " << path << "\n";
        return false;
    }
    if (buf.size() < 16) { std::cerr << "Scene file too small\n"; return false; }

    size_t off = 0;
    auto readU32 = [&](uint32_t& v) -> bool {
        if (buf.size() - off < sizeof(v)) return false;
        std::memcpy(&v, buf.data() + off, sizeof(v));
        off += sizeof(v);
        return true;
    };
    auto readBlob = [&](uint32_t bytes, const char*& ptr) -> bool {
        if ((size_t)bytes > buf.size() - off) return false;
        ptr = buf.data() + off;
        off += bytes;
        return true;
    };

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

    uint32_t jsonSize;
    const char* jsonPtr = nullptr;
    if (!readU32(jsonSize) || !readBlob(jsonSize, jsonPtr)) {
        std::cerr << "Scene file truncated (JSON)\n"; return false;
    }
    std::string jsonStr(jsonPtr, jsonSize);

    nlohmann::json root = nlohmann::json::parse(jsonStr, nullptr, false);
    if (!root.is_object()) { std::cerr << "Invalid scene JSON\n"; return false; }

    uint32_t heightsBytes;
    const char* heightsPtr = nullptr;
    if (!readU32(heightsBytes) || !readBlob(heightsBytes, heightsPtr)) {
        std::cerr << "Scene file truncated (heights)\n"; return false;
    }

    uint32_t splatBytes;
    const char* splatPtr = nullptr;
    if (!readU32(splatBytes) || !readBlob(splatBytes, splatPtr)) {
        std::cerr << "Scene file truncated (splat)\n"; return false;
    }

    std::string baseDir = baseDirOf(path);

    // --- Apply terrain ---
    const auto& t = root["terrain"];
    if (t.is_object()) {
        int gx = std::clamp(t.value("gridX", ctx.terrain.gridX()), 1, 4096);
        int gz = std::clamp(t.value("gridZ", ctx.terrain.gridZ()), 1, 4096);

        if (heightsBytes == (uint32_t)((size_t)gx * gz * sizeof(float)) &&
            gx == ctx.terrain.gridX() && gz == ctx.terrain.gridZ()) {
            std::vector<float> heights((size_t)gx * gz);
            std::memcpy(heights.data(), heightsPtr, heightsBytes);
            ctx.terrain.setHeights(heights);
        }

        if (splatBytes == (uint32_t)((size_t)gx * gz * 16)) {
            std::vector<uint8_t> splat((size_t)splatBytes);
            std::memcpy(splat.data(), splatPtr, splatBytes);
            ctx.terrain.setSplat(splat);
        } else if (splatBytes == (uint32_t)((size_t)gx * gz * 4)) {
            std::vector<uint8_t> splat16((size_t)gx * gz * 16, 0);
            size_t map0 = 0;
            for (size_t p = 0; p < (size_t)gx * gz; ++p)
                std::memcpy(&splat16[map0 + p * 4], splatPtr + p * 4, 4);
            ctx.terrain.setSplat(splat16);
        }

        const auto& layers = t["layers"];
        if (layers.is_array()) {
            size_t savedN = layers.size();
            for (size_t i = 0; i < savedN; ++i) {
                const auto& L = layers[i];
                std::string albedo = L.value("albedo", "");
                std::string normal = L.value("normal", "");
                std::string nm = L.value("name", "");
                float ts = L.value("tileSize", 8.0f);
                if ((int)i >= ctx.terrain.layerCount()) {
                    int idx = albedo.empty() ? -1 : ctx.terrain.addLayer(absPath(albedo, baseDir));
                    if (idx < 0) continue;
                    if (!normal.empty()) ctx.terrain.loadLayerNormal(idx, absPath(normal, baseDir));
                    ctx.terrain.setLayerName(idx, nm);
                    ctx.terrain.setLayerTileSize(idx, ts);
                } else {
                    if (!albedo.empty()) ctx.terrain.loadLayerAlbedo((int)i, absPath(albedo, baseDir));
                    if (!normal.empty()) ctx.terrain.loadLayerNormal((int)i, absPath(normal, baseDir));
                    ctx.terrain.setLayerName((int)i, nm);
                    ctx.terrain.setLayerTileSize((int)i, ts);
                }
            }
            while (ctx.terrain.layerCount() > (int)savedN)
                ctx.terrain.removeLayer(ctx.terrain.layerCount() - 1);
        }
    }

    // --- Skybox ---
    const auto& sky = root["skybox"];
    if (sky.is_object()) {
        ctx.skyExposure = sky.value("exposure", 1.0f);
        std::string skyPath = sky.value("path", "");
        if (!skyPath.empty()) ctx.skybox.loadEquirect(ctx.skyboxConvertShader, absPath(skyPath, baseDir));
        else ctx.skybox.resetToDefault();
    }

    // --- Lighting ---
    const auto& light = root["light"];
    if (light.is_object()) {
        ctx.lightAzimuth   = light.value("azimuth", 0.6f);
        ctx.lightElevation = light.value("elevation", 0.9f);
    }

    // --- Camera ---
    const auto& cam = root["camera"];
    if (cam.is_object()) {
        glm::vec3 target(cam.value("tx", 0.0f), cam.value("ty", 0.0f), cam.value("tz", 0.0f));
        ctx.camera.setTarget(target);
        ctx.camera.setYaw(cam.value("yaw", -0.6f));
        ctx.camera.setPitch(cam.value("pitch", 0.6f));
        ctx.camera.setDistance(cam.value("distance", 60.0f));
    }

    // --- Clear existing ---
    ctx.props.clear();
    ctx.details.clearInstances();
    ctx.details.clearPrototypes();
    ctx.build.clear();
    ctx.cameraRig.clear();
    ctx.spawns.clear();
    ctx.modelLibrary.clear();
    ctx.selectedBlockId = -1;
    ctx.selectedBlockFace = -1;

    // --- Block texture library ---
    const auto& btLib = root["blockTextures"];
    if (btLib.is_array()) {
        for (size_t i = 0; i < btLib.size(); ++i) {
            const auto& e = btLib[i];
            std::string p = absPath(e.value("path", ""), baseDir);
            if (!p.empty()) ctx.build.loadBlockTexture(p);
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
            ctx.modelLibrary.push_back(model);
            glm::vec3 pos(p.value("px", 0.0f), p.value("py", 0.0f), p.value("pz", 0.0f));
            std::string name = p.value("name", "");
            int id = ctx.props.addProp(model, pos, pos.y, 0.0f, name);
            if (id >= 0) {
                Prop* prop = ctx.props.findProp(id);
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
                ctx.modelLibrary.push_back(model);
                ctx.details.addPrototype(model, p.value("name", ""), p.value("targetSize", 2.0f), modelPath);
                int pi = ctx.details.prototypeCount() - 1;
                auto* proto = ctx.details.prototypeMutable(pi);
                if (proto) {
                    proto->minScale  = p.value("minScale", 0.8f);
                    proto->maxScale  = p.value("maxScale", 1.2f);
                    proto->randomYaw = p.value("randomYaw", 1.0f);
                }
            }
        }
        const auto& insts = det["instances"];
        if (insts.is_array()) {
            for (size_t i = 0; i < insts.size(); ++i) {
                const auto& iv = insts[i];
                DetailSystem::Instance inst;
                inst.prototypeIndex = iv.value("p", 0);
                inst.position = glm::vec3(iv.value("x", 0.0f), iv.value("y", 0.0f), iv.value("z", 0.0f));
                inst.yaw = iv.value("yaw", 0.0f);
                inst.scale = iv.value("scale", 1.0f);
                ctx.details.addInstance(inst);
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
            int id = ctx.build.placeBlock(center, size, type, color, yaw);
            int ti = bk.value("ti", -1);
            int tf = bk.value("tf", -1);
            float ts = bk.value("ts", 1.0f);
            int tm = bk.value("tm", 0);
            if (ti >= ctx.build.blockTextureCount()) ti = -1;
            if (ti >= 0 && tf >= 0 && id >= 0) {
                ctx.build.setBlockFaceTexture(id, ti, tf);
                ctx.build.setBlockTexScale(id, ts);
                ctx.build.setBlockTexMode(id, tm);
            }
        }
    }

    // --- Scene cameras ---
    const auto& cams = root["cameras"];
    if (cams.is_array()) {
        for (size_t i = 0; i < cams.size(); ++i) {
            const auto& sc = cams[i];
            SceneCamera c;
            c.name = sc.value("name", "Camera");
            c.tag  = sc.value("tag", "");
            c.position = glm::vec3(sc.value("px", 0.0f), sc.value("py", 10.0f),
                                   sc.value("pz", -10.0f));
            c.target   = glm::vec3(sc.value("tx", 0.0f), sc.value("ty", 0.0f),
                                   sc.value("tz", 0.0f));
            c.fov       = std::clamp(sc.value("fov", 60.0f), 1.0f, 179.0f);
            c.nearPlane = std::max(sc.value("near", 0.1f), 1e-4f);
            c.farPlane  = std::max(sc.value("far", 500.0f), c.nearPlane * 2.0f);
            // Keep the saved id when it is usable; a missing/duplicate id
            // gets a fresh one rather than breaking the stable-key contract.
            int savedId = sc.value("id", -1);
            if (savedId >= 0 && !ctx.cameraRig.findCamera(savedId)) {
                c.id = savedId;
                ctx.cameraRig.addCameraWithId(c);
            } else {
                ctx.cameraRig.addCamera(c);
            }
        }
        ctx.cameraRig.setActive(root.value("activeCamera", -1));
    }

    // --- Spawn markers ---
    const auto& spawns = root["spawns"];
    if (spawns.is_array()) {
        for (size_t i = 0; i < spawns.size(); ++i) {
            const auto& sj = spawns[i];
            SpawnPoint s;
            s.name = sj.value("name", "Spawn");
            s.tag  = sj.value("tag", "");
            s.position = glm::vec3(sj.value("px", 0.0f), sj.value("py", 0.0f),
                                   sj.value("pz", 0.0f));
            s.yaw   = sj.value("yaw", 0.0f);
            std::string mdl = sj.value("model", "");
            s.modelPath = mdl.empty() ? "" : absPath(mdl, baseDir);
            s.scale = std::clamp(sj.value("scale", 1.0f), 0.01f, 100.0f);
            s.defaultAnim = sj.value("anim", "");
            s.rootId = sj.value("root", -1);
            int maxNodeId = -1;
            const auto& nodes = sj["nodes"];
            if (nodes.is_array()) {
                for (size_t k = 0; k < nodes.size(); ++k) {
                    const auto& nj = nodes[k];
                    LogicNode n;
                    n.id = nj.value("id", -1);
                    if (n.id < 0) continue;
                    int kind = nj.value("kind", (int)LogicNode::Root);
                    n.kind = (kind >= 0 && kind <= 2) ? (LogicNode::Kind)kind
                                                      : LogicNode::Act;
                    n.cond.type = (Condition::Type)std::clamp(
                        nj.value("ct", 0), 0, (int)Condition::PlayerNear);
                    n.cond.flagId = nj.value("flag", 0);
                    n.cond.value  = nj.value("val", 0);
                    n.act.type = (Action::Type)std::clamp(
                        nj.value("at", 0), 0, (int)Action::PlaySound);
                    n.act.param     = nj.value("param", "");
                    n.act.intParam  = nj.value("ip", -1);
                    n.act.intParam2 = nj.value("ip2", 0);
                    n.act.floatParam = nj.value("fp", 0.0f);
                    n.uiPos = glm::vec2(nj.value("ux", 0.0f),
                                        nj.value("uy", 0.0f));
                    n.nextTrue  = nj.value("t", -1);
                    n.nextFalse = nj.value("f", -1);
                    maxNodeId = std::max(maxNodeId, n.id);
                    s.nodes.push_back(n);
                }
            }
            s.nextNodeId = maxNodeId + 1;
            // Validate links: drop references to nodes that do not exist.
            for (auto& n : s.nodes) {
                if (n.nextTrue >= 0 && !s.findNode(n.nextTrue))   n.nextTrue = -1;
                if (n.nextFalse >= 0 && !s.findNode(n.nextFalse)) n.nextFalse = -1;
            }
            int savedId = sj.value("id", -1);
            if (savedId >= 0 && !ctx.spawns.findSpawn(savedId)) {
                s.id = savedId;
                // addSpawnWithId keeps ids; ensure the root survives too.
                if (s.rootId < 0 || !s.findNode(s.rootId)) {
                    LogicNode root;
                    root.kind = LogicNode::Root;
                    root.id = s.nextNodeId++;
                    root.uiPos = glm::vec2(20.0f, 120.0f);
                    s.rootId = root.id;
                    s.nodes.insert(s.nodes.begin(), root);
                }
                ctx.spawns.addSpawnWithId(s);
            } else {
                ctx.spawns.addSpawn(std::move(s));
            }
        }
    }

    // --- Simulation flags ---
    ctx.sim.flags().clear();
    const auto& flags = root["flags"];
    if (flags.is_object()) {
        for (const auto& el : flags.items()) {
            if (!el.value().is_number_integer()) continue;
            int key = std::atoi(el.key().c_str());
            ctx.sim.flags()[key] = el.value().get<int>();
        }
    }

    return true;
}

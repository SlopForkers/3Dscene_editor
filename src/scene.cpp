#include "app.h"
#include "model.h"
#include "file_dialog.h"
#include <glm/glm.hpp>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <map>
#include <algorithm>
#include <filesystem>

// ---------------------------------------------------------------------------
// Minimal JSON parser / serializer (no external dependency).
// Supports: objects, arrays, strings, numbers, bools, null.
// ---------------------------------------------------------------------------

namespace json {

class Value;
using Object = std::map<std::string, Value>;
using Array  = std::vector<Value>;

class Value {
public:
    enum Type { Null, Bool, Number, String, ArrayT, ObjectT };
    Type type = Null;
    bool b = false;
    double num = 0.0;
    std::string str;
    std::vector<Value> arr;
    std::map<std::string, Value> obj;

    Value() {}
    Value(Type t) : type(t) {}
    Value(bool v) : type(Bool), b(v) {}
    Value(double v) : type(Number), num(v) {}
    Value(int v) : type(Number), num((double)v) {}
    Value(const std::string& v) : type(String), str(v) {}
    Value(const char* v) : type(String), str(v) {}

    bool isObj() const { return type == ObjectT; }
    bool isArr() const { return type == ArrayT; }
    bool isStr() const { return type == String; }
    bool isNum() const { return type == Number; }

    const Value& operator[](const char* key) const {
        static Value nullVal;
        auto it = obj.find(key);
        return it != obj.end() ? it->second : nullVal;
    }
    const Value& operator[](size_t i) const {
        static Value nullVal;
        return i < arr.size() ? arr[i] : nullVal;
    }
    size_t size() const { return arr.size(); }

    double asNum(double def = 0) const { return type == Number ? num : def; }
    const std::string& asStr() const { return str; }
    bool asBool(bool def = false) const { return type == Bool ? b : def; }
};

// --- Parser ---

struct Parser {
    const char* s;
    size_t pos;

    Parser(const std::string& text) : s(text.c_str()), pos(0) {}

    void skipWs() {
        while (s[pos] && (s[pos] == ' ' || s[pos] == '\t' || s[pos] == '\n' || s[pos] == '\r'))
            ++pos;
    }

    Value parse() {
        skipWs();
        if (!s[pos]) return Value();
        char c = s[pos];
        if (c == '{') return parseObj();
        if (c == '[') return parseArr();
        if (c == '"') return parseStr();
        if (c == 't' || c == 'f') return parseBool();
        if (c == 'n') { pos += 4; return Value(); }
        return parseNum();
    }

    Value parseObj() {
        Value v; v.type = Value::ObjectT;
        ++pos; // {
        skipWs();
        if (s[pos] == '}') { ++pos; return v; }
        while (s[pos]) {
            skipWs();
            Value key = parseStr();
            skipWs();
            if (s[pos] == ':') ++pos;
            Value val = parse();
            v.obj[key.str] = val;
            skipWs();
            if (s[pos] == ',') { ++pos; continue; }
            if (s[pos] == '}') { ++pos; break; }
            break;
        }
        return v;
    }

    Value parseArr() {
        Value v; v.type = Value::ArrayT;
        ++pos; // [
        skipWs();
        if (s[pos] == ']') { ++pos; return v; }
        while (s[pos]) {
            v.arr.push_back(parse());
            skipWs();
            if (s[pos] == ',') { ++pos; continue; }
            if (s[pos] == ']') { ++pos; break; }
            break;
        }
        return v;
    }

    Value parseStr() {
        Value v; v.type = Value::String;
        ++pos; // opening "
        while (s[pos] && s[pos] != '"') {
            if (s[pos] == '\\') {
                ++pos;
                char esc = s[pos++];
                switch (esc) {
                    case 'n': v.str += '\n'; break;
                    case 't': v.str += '\t'; break;
                    case 'r': v.str += '\r'; break;
                    case '\\': v.str += '\\'; break;
                    case '"': v.str += '"'; break;
                    case '/': v.str += '/'; break;
                    default: v.str += esc; break;
                }
            } else {
                v.str += s[pos++];
            }
        }
        if (s[pos] == '"') ++pos;
        return v;
    }

    Value parseBool() {
        if (s[pos] == 't') { pos += 4; return Value(true); }
        pos += 5; return Value(false);
    }

    Value parseNum() {
        Value v; v.type = Value::Number;
        char* end;
        v.num = std::strtod(s + pos, &end);
        pos = (size_t)(end - s);
        return v;
    }
};

// --- Serializer ---

struct Writer {
    std::string out;
    int indent = 0;

    void w(const char* s) { out += s; }
    void w(char c) { out += c; }
    void nl() { out += '\n'; for (int i = 0; i < indent; ++i) out += "  "; }

    void writeStr(const std::string& s) {
        out += '"';
        for (char c : s) {
            switch (c) {
                case '\\': out += "\\\\"; break;
                case '"':  out += "\\\""; break;
                case '\n': out += "\\n"; break;
                case '\t': out += "\\t"; break;
                case '\r': out += "\\r"; break;
                default: out += c; break;
            }
        }
        out += '"';
    }

    void write(const Value& v) {
        switch (v.type) {
            case Value::Null:   w("null"); break;
            case Value::Bool:   w(v.b ? "true" : "false"); break;
            case Value::Number: {
                char buf[64];
                double n = v.num;
                if (n == (double)(long long)n && std::abs(n) < 1e15)
                    std::snprintf(buf, sizeof(buf), "%lld", (long long)n);
                else
                    std::snprintf(buf, sizeof(buf), "%.6g", n);
                w(buf);
                break;
            }
            case Value::String: writeStr(v.str); break;
            case Value::ArrayT: {
                w('[');
                for (size_t i = 0; i < v.arr.size(); ++i) {
                    if (i) w(", ");
                    write(v.arr[i]);
                }
                w(']');
                break;
            }
            case Value::ObjectT: {
                w('{'); indent++;
                bool first = true;
                for (auto& [k, val] : v.obj) {
                    if (!first) w(',');
                    nl();
                    writeStr(k); w(": ");
                    write(val);
                    first = false;
                }
                indent--; nl();
                w('}');
                break;
            }
        }
    }
};

Value parse(const std::string& text) {
    Parser p(text);
    return p.parse();
}

std::string dump(const Value& v) {
    Writer w;
    w.write(v);
    return w.out;
}

} // namespace json

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
    json::Value root(json::Value::ObjectT);
    root.obj["version"] = json::Value(1);

    // Terrain metadata.
    json::Value terrain(json::Value::ObjectT);
    terrain.obj["gridX"] = json::Value(terrain_.gridX());
    terrain.obj["gridZ"] = json::Value(terrain_.gridZ());
    terrain.obj["worldSize"] = json::Value(terrain_.worldSize());

    json::Value layers(json::Value::ArrayT);
    for (int i = 0; i < terrain_.layerCount(); ++i) {
        const auto& L = terrain_.layers()[i];
        json::Value layer(json::Value::ObjectT);
        layer.obj["name"] = json::Value(L.name);
        layer.obj["albedo"] = json::Value(relPath(L.albedoPath, baseDir));
        layer.obj["normal"] = json::Value(relPath(L.normalPath, baseDir));
        layer.obj["tileSize"] = json::Value(L.tileSize);
        layers.arr.push_back(layer);
    }
    terrain.obj["layers"] = layers;
    root.obj["terrain"] = terrain;

    // Skybox.
    json::Value sky(json::Value::ObjectT);
    if (!skybox_.isDefault()) {
        sky.obj["path"] = json::Value(relPath(skybox_.importedPath(), baseDir));
    } else {
        sky.obj["path"] = json::Value("");
    }
    sky.obj["exposure"] = json::Value(skyExposure_);
    root.obj["skybox"] = sky;

    // Lighting.
    json::Value light(json::Value::ObjectT);
    light.obj["azimuth"] = json::Value(lightAzimuth_);
    light.obj["elevation"] = json::Value(lightElevation_);
    root.obj["light"] = light;

    // Camera.
    json::Value cam(json::Value::ObjectT);
    cam.obj["tx"] = json::Value(camera_.target().x);
    cam.obj["ty"] = json::Value(camera_.target().y);
    cam.obj["tz"] = json::Value(camera_.target().z);
    cam.obj["yaw"] = json::Value(camera_.yaw());
    cam.obj["pitch"] = json::Value(camera_.pitch());
    cam.obj["distance"] = json::Value(camera_.distance());
    root.obj["camera"] = cam;

    // Props.
    json::Value props(json::Value::ArrayT);
    for (const auto& p : props_.props()) {
        if (!p.model) continue;
        json::Value prop(json::Value::ObjectT);
        prop.obj["path"] = json::Value(relPath(p.model->sourcePath(), baseDir));
        prop.obj["px"] = json::Value(p.position.x);
        prop.obj["py"] = json::Value(p.position.y);
        prop.obj["pz"] = json::Value(p.position.z);
        prop.obj["rx"] = json::Value(p.rotationEuler.x);
        prop.obj["ry"] = json::Value(p.rotationEuler.y);
        prop.obj["rz"] = json::Value(p.rotationEuler.z);
        prop.obj["sx"] = json::Value(p.scale.x);
        prop.obj["sy"] = json::Value(p.scale.y);
        prop.obj["sz"] = json::Value(p.scale.z);
        prop.obj["name"] = json::Value(p.displayName);
        props.arr.push_back(prop);
    }
    root.obj["props"] = props;

    // Details.
    json::Value details(json::Value::ObjectT);
    json::Value protos(json::Value::ArrayT);
    for (int i = 0; i < details_.prototypeCount(); ++i) {
        const auto& p = details_.prototype(i);
        json::Value proto(json::Value::ObjectT);
        proto.obj["path"] = json::Value(p.model ? relPath(p.model->sourcePath(), baseDir) : "");
        proto.obj["name"] = json::Value(p.name);
        proto.obj["targetSize"] = json::Value(p.targetSize);
        proto.obj["minScale"] = json::Value(p.minScale);
        proto.obj["maxScale"] = json::Value(p.maxScale);
        proto.obj["randomYaw"] = json::Value(p.randomYaw);
        protos.arr.push_back(proto);
    }
    details.obj["prototypes"] = protos;

    json::Value insts(json::Value::ArrayT);
    for (const auto& inst : details_.instances()) {
        json::Value iv(json::Value::ObjectT);
        iv.obj["p"] = json::Value(inst.prototypeIndex);
        iv.obj["x"] = json::Value(inst.position.x);
        iv.obj["y"] = json::Value(inst.position.y);
        iv.obj["z"] = json::Value(inst.position.z);
        iv.obj["yaw"] = json::Value(inst.yaw);
        iv.obj["scale"] = json::Value(inst.scale);
        insts.arr.push_back(iv);
    }
    details.obj["instances"] = insts;
    root.obj["details"] = details;

    // Blocks (build system).
    // First the shared block-texture library (paths only; textures reload on
    // load).
    json::Value btArr(json::Value::ArrayT);
    for (int i = 0; i < build_.blockTextureCount(); ++i) {
        json::Value entry(json::Value::ObjectT);
        entry.obj["path"] = json::Value(build_.blockTexturePath(i));
        btArr.arr.push_back(entry);
    }
    root.obj["blockTextures"] = btArr;

    json::Value blocksArr(json::Value::ArrayT);
    for (const auto& b : build_.blocks()) {
        json::Value bk(json::Value::ObjectT);
        bk.obj["type"] = json::Value((int)b.type);
        bk.obj["cx"] = json::Value(b.position.x);
        bk.obj["cy"] = json::Value(b.position.y);
        bk.obj["cz"] = json::Value(b.position.z);
        bk.obj["sx"] = json::Value(b.size.x);
        bk.obj["sy"] = json::Value(b.size.y);
        bk.obj["sz"] = json::Value(b.size.z);
        bk.obj["r"]  = json::Value(b.color.r);
        bk.obj["g"]  = json::Value(b.color.g);
        bk.obj["b"]  = json::Value(b.color.b);
        bk.obj["yaw"] = json::Value(b.yaw);
        bk.obj["ti"] = json::Value(b.textureIdx);
        bk.obj["tf"] = json::Value(b.textureFace);
        bk.obj["ts"] = json::Value(b.texScale);
        bk.obj["tm"] = json::Value(b.texMode);
        blocksArr.arr.push_back(bk);
    }
    root.obj["blocks"] = blocksArr;

    std::string jsonStr = json::dump(root);

    // --- Write single binary file: magic + version + JSON + heights + splat ---
    std::ofstream f(path, std::ios::binary);
    if (!f) { std::cerr << "Cannot write scene: " << path << "\n"; return false; }

    // Magic + version.
    const char magic[4] = {'S','C','N','E'};
    f.write(magic, 4);
    uint32_t version = 1;
    f.write(reinterpret_cast<const char*>(&version), sizeof(version));

    // JSON: size prefix + data.
    uint32_t jsonSize = (uint32_t)jsonStr.size();
    f.write(reinterpret_cast<const char*>(&jsonSize), sizeof(jsonSize));
    f.write(jsonStr.data(), jsonSize);

    // Heights: size prefix + float data.
    const auto& heights = terrain_.heightsData();
    uint32_t heightsBytes = (uint32_t)(heights.size() * sizeof(float));
    f.write(reinterpret_cast<const char*>(&heightsBytes), sizeof(heightsBytes));
    f.write(reinterpret_cast<const char*>(heights.data()), heightsBytes);

    // Splat: size prefix + byte data.
    const auto& splat = terrain_.splatData();
    uint32_t splatBytes = (uint32_t)splat.size();
    f.write(reinterpret_cast<const char*>(&splatBytes), sizeof(splatBytes));
    f.write(reinterpret_cast<const char*>(splat.data()), splatBytes);

    f.flush();
    f.close();

    std::cerr << "[SAVE] " << path << "  blocks=" << build_.count() << "\n";
    return true;
}

bool App::loadScene(const std::string& path) {
    // Read entire file into memory.
    std::ifstream f(path, std::ios::binary);
    if (!f) { std::cerr << "Cannot open scene: " << path << "\n"; return false; }
    std::vector<char> buf((std::istreambuf_iterator<char>(f)),
                          std::istreambuf_iterator<char>());
    f.close();
    if (buf.size() < 16) { std::cerr << "Scene file too small\n"; return false; }

    // Parse header: magic + version.
    size_t off = 0;
    if (std::memcmp(buf.data(), "SCNE", 4) != 0) {
        std::cerr << "Scene file: bad magic\n"; return false;
    }
    off += 4;
    uint32_t version;
    std::memcpy(&version, buf.data() + off, sizeof(version)); off += 4;
    if (version != 1) { std::cerr << "Unsupported scene version: " << version << "\n"; return false; }

    // JSON: size prefix + data.
    uint32_t jsonSize;
    std::memcpy(&jsonSize, buf.data() + off, sizeof(jsonSize)); off += 4;
    std::string jsonStr(buf.data() + off, jsonSize);
    off += jsonSize;

    json::Value root = json::parse(jsonStr);
    if (!root.isObj()) { std::cerr << "Invalid scene JSON\n"; return false; }

    // Heights: size prefix + float data.
    uint32_t heightsBytes;
    std::memcpy(&heightsBytes, buf.data() + off, sizeof(heightsBytes)); off += 4;
    const char* heightsPtr = buf.data() + off;
    off += heightsBytes;

    // Splat: size prefix + byte data.
    uint32_t splatBytes;
    std::memcpy(&splatBytes, buf.data() + off, sizeof(splatBytes)); off += 4;
    const char* splatPtr = buf.data() + off;

    std::string baseDir = baseDirOf(path);

    // --- Apply terrain ---
    const json::Value& t = root["terrain"];
    if (t.isObj()) {
        int gx = (int)t["gridX"].asNum(terrain_.gridX());
        int gz = (int)t["gridZ"].asNum(terrain_.gridZ());

        // Load heights from embedded binary blob.
        if (heightsBytes == (uint32_t)(gx * gz * sizeof(float)) &&
            gx == terrain_.gridX() && gz == terrain_.gridZ()) {
            std::vector<float> heights((size_t)gx * gz);
            std::memcpy(heights.data(), heightsPtr, heightsBytes);
            terrain_.setHeights(heights);
        }

        // Load splat from embedded binary blob.
        if (splatBytes == (uint32_t)(gx * gz * 4)) {
            std::vector<uint8_t> splat((size_t)splatBytes);
            std::memcpy(splat.data(), splatPtr, splatBytes);
            terrain_.setSplat(splat);
        }

        // Layers.
        const json::Value& layers = t["layers"];
        if (layers.isArr()) {
            for (size_t i = 0; i < layers.size() && i < 4; ++i) {
                const json::Value& L = layers[i];
                terrain_.setLayerName((int)i, L["name"].asStr());
                terrain_.setLayerTileSize((int)i, (float)L["tileSize"].asNum(8.0));
                std::string albedo = L["albedo"].asStr();
                std::string normal = L["normal"].asStr();
                if (!albedo.empty()) terrain_.loadLayerAlbedo((int)i, absPath(albedo, baseDir));
                if (!normal.empty()) terrain_.loadLayerNormal((int)i, absPath(normal, baseDir));
            }
        }
    }

    // --- Skybox ---
    const json::Value& sky = root["skybox"];
    if (sky.isObj()) {
        skyExposure_ = (float)sky["exposure"].asNum(1.0);
        std::string skyPath = sky["path"].asStr();
        if (!skyPath.empty()) skybox_.loadEquirect(skyboxConvertShader_, absPath(skyPath, baseDir));
        else skybox_.resetToDefault();
    }

    // --- Lighting ---
    const json::Value& light = root["light"];
    if (light.isObj()) {
        lightAzimuth_   = (float)light["azimuth"].asNum(0.6);
        lightElevation_ = (float)light["elevation"].asNum(0.9);
    }

    // --- Camera ---
    const json::Value& cam = root["camera"];
    if (cam.isObj()) {
        glm::vec3 target((float)cam["tx"].asNum(), (float)cam["ty"].asNum(), (float)cam["tz"].asNum());
        camera_.setTarget(target);
        camera_.setYaw((float)cam["yaw"].asNum(-0.6));
        camera_.setPitch((float)cam["pitch"].asNum(0.6));
        camera_.setDistance((float)cam["distance"].asNum(60.0));
    }

    // --- Clear existing props + details + blocks ---
    props_.clear();
    details_.clearInstances();
    details_.clearPrototypes();
    build_.clear();
    selectedBlockId_ = -1;
    selectedBlockFace_ = -1;

    // --- Block texture library (reload paths before blocks reference them) ---
    const json::Value& btLib = root["blockTextures"];
    if (btLib.isArr()) {
        for (size_t i = 0; i < btLib.size(); ++i) {
            const json::Value& e = btLib[i];
            std::string p = absPath(e["path"].asStr(), baseDir);
            if (!p.empty()) build_.loadBlockTexture(p);
        }
    }

    // --- Props ---
    const json::Value& props = root["props"];
    if (props.isArr()) {
        for (size_t i = 0; i < props.size(); ++i) {
            const json::Value& p = props[i];
            std::string modelPath = absPath(p["path"].asStr(), baseDir);
            if (modelPath.empty()) continue;
            auto model = std::make_shared<Model>();
            if (!model->loadFromFile(modelPath)) {
                std::cerr << "Scene load: failed prop model: " << modelPath << "\n";
                continue;
            }
            modelLibrary_.push_back(model);
            glm::vec3 pos((float)p["px"].asNum(), (float)p["py"].asNum(), (float)p["pz"].asNum());
            std::string name = p["name"].asStr();
            int id = props_.addProp(model, pos, pos.y, 0.0f, name);
            if (id >= 0) {
                Prop* prop = props_.findProp(id);
                if (prop) {
                    prop->rotationEuler = glm::vec3((float)p["rx"].asNum(), (float)p["ry"].asNum(), (float)p["rz"].asNum());
                    prop->scale = glm::vec3((float)p["sx"].asNum(), (float)p["sy"].asNum(), (float)p["sz"].asNum());
                    prop->position = pos;
                }
            }
        }
    }

    // --- Details ---
    const json::Value& det = root["details"];
    if (det.isObj()) {
        const json::Value& protos = det["prototypes"];
        if (protos.isArr()) {
            for (size_t i = 0; i < protos.size(); ++i) {
                const json::Value& p = protos[i];
                std::string modelPath = absPath(p["path"].asStr(), baseDir);
                if (modelPath.empty()) continue;
                auto model = std::make_shared<Model>();
                if (!model->loadFromFile(modelPath)) {
                    std::cerr << "Scene load: failed detail model: " << modelPath << "\n";
                    continue;
                }
                modelLibrary_.push_back(model);
                details_.addPrototype(model, p["name"].asStr(), (float)p["targetSize"].asNum(2.0), modelPath);
                int pi = details_.prototypeCount() - 1;
                auto* proto = details_.prototypeMutable(pi);
                if (proto) {
                    proto->minScale  = (float)p["minScale"].asNum(0.8);
                    proto->maxScale  = (float)p["maxScale"].asNum(1.2);
                    proto->randomYaw = (float)p["randomYaw"].asNum(1.0);
                }
            }
        }
        // Instances.
        const json::Value& insts = det["instances"];
        if (insts.isArr()) {
            for (size_t i = 0; i < insts.size(); ++i) {
                const json::Value& iv = insts[i];
                DetailSystem::Instance inst;
                inst.prototypeIndex = (int)iv["p"].asNum();
                inst.position = glm::vec3((float)iv["x"].asNum(), (float)iv["y"].asNum(), (float)iv["z"].asNum());
                inst.yaw = (float)iv["yaw"].asNum();
                inst.scale = (float)iv["scale"].asNum(1.0);
                details_.addInstance(inst);
            }
        }
    }

    // --- Blocks ---
    const json::Value& blocks = root["blocks"];
    if (blocks.isArr()) {
        for (size_t i = 0; i < blocks.size(); ++i) {
            const json::Value& bk = blocks[i];
            glm::vec3 center((float)bk["cx"].asNum(), (float)bk["cy"].asNum(), (float)bk["cz"].asNum());
            glm::vec3 size((float)bk["sx"].asNum(), (float)bk["sy"].asNum(), (float)bk["sz"].asNum());
            glm::vec3 color((float)bk["r"].asNum(0.55f), (float)bk["g"].asNum(0.45f), (float)bk["b"].asNum(0.35f));
            BuildSystem::BlockType type = (BuildSystem::BlockType)(int)bk["type"].asNum(BuildSystem::Wall);
            float yaw = (float)bk["yaw"].asNum(0.0);
            int id = build_.placeBlock(center, size, type, color, yaw);
            // Restore per-block face texture (indices match the loaded library).
            int ti = (int)bk["ti"].asNum(-1.0);
            int tf = (int)bk["tf"].asNum(-1.0);
            float ts = (float)bk["ts"].asNum(1.0);
            int tm = (int)bk["tm"].asNum(0.0);
            if (ti >= 0 && tf >= 0 && id >= 0) {
                build_.setBlockFaceTexture(id, ti, tf);
                build_.setBlockTexScale(id, ts);
                build_.setBlockTexMode(id, tm);
            }
        }
    }

    return true;
}

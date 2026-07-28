#pragma once
#include <memory>
#include <string>
#include <vector>

class Terrain;
class Skybox;
class Camera;
class PropManager;
class DetailSystem;
class BuildSystem;
class Shader;
class Model;

struct SceneContext {
    Terrain& terrain;
    Skybox& skybox;
    Camera& camera;
    PropManager& props;
    DetailSystem& details;
    BuildSystem& build;
    std::vector<std::shared_ptr<Model>>& modelLibrary;
    Shader& skyboxConvertShader;
    float& skyExposure;
    float& lightAzimuth;
    float& lightElevation;
    int& selectedBlockId;
    int& selectedBlockFace;
};

bool saveScene(const std::string& path, const SceneContext& ctx);
bool loadScene(const std::string& path, SceneContext& ctx);

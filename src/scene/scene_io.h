#pragma once
#include <memory>
#include <string>
#include <vector>

class Terrain;
class Skybox;
class Camera;
class CameraRig;
class PropManager;
class DetailSystem;
class BuildSystem;
class SpawnManager;
class SimController;
struct WeatherParams;
class MaterialLibrary;
class Shader;
class Model;

struct SceneContext {
    Terrain& terrain;
    Skybox& skybox;
    Camera& camera;
    CameraRig& cameraRig;
    PropManager& props;
    DetailSystem& details;
    BuildSystem& build;
    SpawnManager& spawns;
    SimController& sim;
    WeatherParams& weather;
    MaterialLibrary& materials;
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

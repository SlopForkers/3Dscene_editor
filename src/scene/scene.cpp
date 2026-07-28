#include "app.h"
#include "scene_io.h"

bool App::saveScene(const std::string& path) {
    SceneContext ctx{
        terrain_,
        skybox_,
        camera_,
        props_,
        details_,
        build_,
        modelLibrary_,
        skyboxConvertShader_,
        skyExposure_,
        lightAzimuth_,
        lightElevation_,
        selectedBlockId_,
        selectedBlockFace_,
    };
    return ::saveScene(path, ctx);
}

bool App::loadScene(const std::string& path) {
    SceneContext ctx{
        terrain_,
        skybox_,
        camera_,
        props_,
        details_,
        build_,
        modelLibrary_,
        skyboxConvertShader_,
        skyExposure_,
        lightAzimuth_,
        lightElevation_,
        selectedBlockId_,
        selectedBlockFace_,
    };
    return ::loadScene(path, ctx);
}

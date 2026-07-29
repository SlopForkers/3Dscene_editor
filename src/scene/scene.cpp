#include "app.h"
#include "scene_io.h"

bool App::saveScene(const std::string& path) {
    SceneContext ctx{
        terrain_,
        skybox_,
        camera_,
        cameraRig_,
        props_,
        details_,
        build_,
        spawns_,
        sim_,
        weather_.params,
        materials_,
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
        cameraRig_,
        props_,
        details_,
        build_,
        spawns_,
        sim_,
        weather_.params,
        materials_,
        modelLibrary_,
        skyboxConvertShader_,
        skyExposure_,
        lightAzimuth_,
        lightElevation_,
        selectedBlockId_,
        selectedBlockFace_,
    };
    bool ok = ::loadScene(path, ctx);
    // Commands captured against the previous scene would resurrect stale
    // state — the new scene starts with a clean history.
    if (ok) {
        history_.clear();
        selectedCameraId_ = -1;
        selectedSpawnId_ = -1;
        selectedMaterialId_ = -1;
        markCamPreviewsStale();
    }
    return ok;
}

#pragma once
#include <string>
#include <vector>

class App;

// Save the current scene to a .scene file (JSON metadata + binary terrain data).
// Returns true on success.
bool saveScene(App& app, const std::string& path);

// Load a .scene file and restore terrain, props, details, skybox, camera.
// Returns true on success.
bool loadScene(App& app, const std::string& path);

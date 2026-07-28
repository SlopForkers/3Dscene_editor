#pragma once
#include <string>

// Show a native "open file" dialog. Returns an empty string if cancelled.
// `ownerWindow` is a native window handle (HWND on Windows) used to keep the
// dialog modal to the app; pass nullptr for an unowned dialog.
// Returned paths are UTF-8.
std::string openFileDialog(const char* filterDescription = "glTF / VRM",
                           const char* filterPatterns =
                               "*.gltf;*.glb;*.vrm",
                           void* ownerWindow = nullptr);

// Show a native "save file" dialog. Returns an empty string if cancelled.
// `defaultExtension` (without dot) is appended by the dialog when the user
// types a bare filename, e.g. "scene".
std::string saveFileDialog(const char* filterDescription = "Scene",
                           const char* filterPatterns = "*.scene",
                           const char* defaultExtension = "scene",
                           void* ownerWindow = nullptr);

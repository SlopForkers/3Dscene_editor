#pragma once
#include <string>

// Show a native "open file" dialog. Returns empty string if cancelled.
std::string openFileDialog(const char* filterDescription = "glTF / VRM",
                            const char* filterPatterns =
                                "*.gltf;*.glb;*.vrm");

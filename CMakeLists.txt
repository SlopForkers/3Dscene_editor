cmake_minimum_required(VERSION 3.20)
project(scene_editor LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Use the preferred output dir layout for multi-config / single config.
if(NOT CMAKE_RUNTIME_OUTPUT_DIRECTORY)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
endif()

# ---------------------------------------------------------------------------
# Dependencies fetched via FetchContent
# ---------------------------------------------------------------------------
include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

# GLFW
FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG        3.4
    GIT_SHALLOW    TRUE
)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(glfw)

# glm (header-only math library, version 1.0.3)
FetchContent_Declare(
    glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG        1.0.3
    GIT_SHALLOW    TRUE
)
set(GLM_BUILD_LIBRARY OFF CACHE BOOL "" FORCE)
set(GLM_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLM_BUILD_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(glm)
# glm's own interface target uses plain (non-SYSTEM) includes; re-adding the
# same dir as SYSTEM keeps -Wall -Wextra -Wpedantic silent on glm headers
# (GCC/Clang ignore the -I form when the dir is also passed via -isystem).
target_include_directories(glm SYSTEM INTERFACE ${glm_SOURCE_DIR})

# Dear ImGui. Docking + multi-viewport are NOT in mainline 1.9x — they live
# in the docking branch, released as vX.Y.Z-docking tags.
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.92.5-docking
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(imgui)

# cgltf (single-header glTF 2.0 parser, used for glTF/VRM import)
FetchContent_Declare(
    cgltf
    GIT_REPOSITORY https://github.com/jkuhlmann/cgltf.git
    GIT_TAG        v1.15
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(cgltf)

# stb (single-header libraries, in particular stb_image for textures)
FetchContent_Declare(
    stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG        31c1ad37456438565541f4919958214b6e762fb4
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(stb)

# nlohmann_json (JSON for Modern C++, header-only)
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(nlohmann_json)

# ---------------------------------------------------------------------------
# GLAD2 loader (pre-generated, see external/glad)
# ---------------------------------------------------------------------------
add_library(glad STATIC ${CMAKE_SOURCE_DIR}/external/glad/src/gl.c)
target_include_directories(glad SYSTEM PUBLIC ${CMAKE_SOURCE_DIR}/external/glad/include)

# ---------------------------------------------------------------------------
# ImGui library (sources selected manually, no upstream CMake)
# ---------------------------------------------------------------------------
set(IMGUI_DIR ${imgui_SOURCE_DIR})
add_library(dear_imgui STATIC
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
    ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
)
target_include_directories(dear_imgui SYSTEM PUBLIC ${IMGUI_DIR} ${IMGUI_DIR}/backends)
target_link_libraries(dear_imgui PUBLIC glfw glad)

# ---------------------------------------------------------------------------
# stb_image (texture loading, single header from the stb FetchContent)
# ---------------------------------------------------------------------------
add_library(stb_image STATIC
    ${CMAKE_SOURCE_DIR}/src/platform/stb_image_impl.cpp)
target_include_directories(stb_image SYSTEM PUBLIC ${stb_SOURCE_DIR})
target_link_libraries(stb_image PUBLIC glad)

# cgltf (header only; implementation compiled in the app)
add_library(cgltf INTERFACE)
target_include_directories(cgltf SYSTEM INTERFACE ${cgltf_SOURCE_DIR})

# ---------------------------------------------------------------------------
# Main application
# ---------------------------------------------------------------------------
set(SOURCES
    src/editor/main.cpp
    src/editor/app.cpp
    src/editor/app.h
    src/editor/app_ui.cpp
    src/editor/app_panels.cpp
    src/editor/ui_icons.cpp
    src/editor/ui_icons.h
    src/editor/ui_common.h
    src/editor/tools.cpp
    src/editor/tools.h
    src/scene/terrain.cpp
    src/scene/terrain.h
    src/scene/brush.cpp
    src/scene/brush.h
    src/scene/vertex_edit.cpp
    src/scene/vertex_edit.h
    src/scene/model.cpp
    src/scene/model.h
    src/scene/prop.cpp
    src/scene/prop.h
    src/scene/detail.cpp
    src/scene/detail.h
    src/scene/build.cpp
    src/scene/build.h
    src/scene/skybox.cpp
    src/scene/skybox.h
    src/scene/gizmo.cpp
    src/scene/gizmo.h
    src/scene/scene.cpp
    src/scene/scene_io.cpp
    src/scene/scene_io.h
    src/scene/scene_camera.cpp
    src/scene/scene_camera.h
    src/scene/history.cpp
    src/scene/history.h
    src/scene/commands.cpp
    src/scene/commands.h
    src/scene/scene.h
    src/platform/camera.cpp
    src/platform/camera.h
    src/platform/shader.cpp
    src/platform/shader.h
    src/platform/input.cpp
    src/platform/input.h
    src/platform/file_dialog.cpp
    src/platform/file_dialog.h
    src/platform/sys_util.cpp
    src/platform/sys_util.h
)

add_executable(scene_editor ${SOURCES})
target_link_libraries(scene_editor PRIVATE
    glfw glad dear_imgui glm stb_image cgltf nlohmann_json::nlohmann_json)
target_include_directories(scene_editor PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/editor
    ${CMAKE_SOURCE_DIR}/src/scene
    ${CMAKE_SOURCE_DIR}/src/platform
)
# Prevent GLFW from pulling in the system GL header; glad provides GL.
target_compile_definitions(scene_editor PRIVATE
    GLFW_INCLUDE_NONE
    CGLTF_WRITE_UNCOMPRESSED_BINARY
)

# Enable strict warnings on our code (dependencies are SYSTEM-included above).
target_compile_options(scene_editor PRIVATE -Wall -Wextra -Wpedantic)

# Win32 common dialogs for the open-file dialog.
if(WIN32)
    target_link_libraries(scene_editor PRIVATE comdlg32)
endif()

# Copy shaders next to the executable so they can be loaded at runtime.
add_custom_command(TARGET scene_editor POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/shaders
            $<TARGET_FILE_DIR:scene_editor>/shaders)

# ---------------------------------------------------------------------------
# Unit tests (doctest)
# ---------------------------------------------------------------------------
option(BUILD_TESTS "Build unit tests" ON)
if(BUILD_TESTS)
    FetchContent_Declare(
        doctest
        GIT_REPOSITORY https://github.com/doctest/doctest.git
        GIT_TAG        v2.4.11
        GIT_SHALLOW    TRUE
    )
    set(DOCTEST_WITH_TESTS OFF CACHE BOOL "" FORCE)
    set(DOCTEST_NO_INSTALL ON CACHE BOOL "" FORCE)
    # doctest v2.4.11 declares cmake_minimum_required(3.0) which CMake >= 4
    # rejects; these are the documented escape hatches for legacy dependencies.
    set(_save_warn_deprecated ${CMAKE_WARN_DEPRECATED})
    set(CMAKE_WARN_DEPRECATED OFF)
    set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
    FetchContent_MakeAvailable(doctest)
    set(CMAKE_WARN_DEPRECATED ${_save_warn_deprecated})
    unset(CMAKE_POLICY_VERSION_MINIMUM)
    # Same trick as glm: keep doctest headers warning-free under -Wpedantic.
    target_include_directories(doctest SYSTEM INTERFACE ${doctest_SOURCE_DIR})

    add_executable(tests
        tests/test_main.cpp
        tests/test_noise.cpp
        tests/test_history.cpp
        tests/test_scene_camera.cpp
        ${CMAKE_SOURCE_DIR}/src/scene/history.cpp
        ${CMAKE_SOURCE_DIR}/src/scene/scene_camera.cpp
    )
    target_include_directories(tests PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/src/platform
        ${CMAKE_SOURCE_DIR}/src/scene
    )
    target_link_libraries(tests PRIVATE glm doctest::doctest)
    target_compile_options(tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

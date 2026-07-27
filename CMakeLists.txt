cmake_minimum_required(VERSION 3.20)
project(scene_editor LANGUAGES C CXX)

# glm is treated as header-only (we only download it), which still uses the
# older FetchContent_Populate; keep that working without a deprecation error.
cmake_policy(SET CMP0169 OLD)

set(CMAKE_CXX_STANDARD 17)
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
    GIT_TAG        3.3.9
    GIT_SHALLOW    TRUE
)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(glfw)

# glm (header only math library). We only download the source and use its
# include directory; we deliberately do NOT configure its CMakeLists so we
# sidestep its old cmake_minimum_required that conflicts with CMake 4.x.
FetchContent_Declare(
    glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG        0.9.9.8
    GIT_SHALLOW    TRUE
)
FetchContent_GetProperties(glm)
if(NOT glm_POPULATED)
    FetchContent_Populate(glm)
endif()
add_library(glm INTERFACE)
target_include_directories(glm INTERFACE ${glm_SOURCE_DIR})

# Dear ImGui
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.91.5
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
    GIT_TAG        master
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(stb)

# ---------------------------------------------------------------------------
# GLAD2 loader (pre-generated, see external/glad)
# ---------------------------------------------------------------------------
add_library(glad STATIC ${CMAKE_SOURCE_DIR}/external/glad/src/gl.c)
target_include_directories(glad PUBLIC ${CMAKE_SOURCE_DIR}/external/glad/include)

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
target_include_directories(dear_imgui PUBLIC ${IMGUI_DIR} ${IMGUI_DIR}/backends)
target_link_libraries(dear_imgui PUBLIC glfw glad)

# ---------------------------------------------------------------------------
# stb_image (texture loading, single header from the stb FetchContent)
# ---------------------------------------------------------------------------
add_library(stb_image STATIC
    ${CMAKE_SOURCE_DIR}/src/stb_image_impl.cpp)
target_include_directories(stb_image PUBLIC ${stb_SOURCE_DIR})
target_link_libraries(stb_image PUBLIC glad)

# cgltf (header only; implementation compiled in the app)
add_library(cgltf INTERFACE)
target_include_directories(cgltf INTERFACE ${cgltf_SOURCE_DIR})

# ---------------------------------------------------------------------------
# Main application
# ---------------------------------------------------------------------------
set(SOURCES
    src/main.cpp
    src/app.cpp
    src/app.h
    src/camera.cpp
    src/camera.h
    src/shader.cpp
    src/shader.h
    src/terrain.cpp
    src/terrain.h
    src/brush.cpp
    src/brush.h
    src/input.cpp
    src/input.h
    src/model.cpp
    src/model.h
    src/prop.cpp
    src/prop.h
    src/gizmo.cpp
    src/gizmo.h
    src/file_dialog.cpp
    src/file_dialog.h
)

add_executable(scene_editor ${SOURCES})
target_link_libraries(scene_editor PRIVATE
    glfw glad dear_imgui glm stb_image cgltf)
target_include_directories(scene_editor PRIVATE ${CMAKE_SOURCE_DIR}/src)
# Prevent GLFW from pulling in the system GL header; glad provides GL.
target_compile_definitions(scene_editor PRIVATE
    GLFW_INCLUDE_NONE
    CGLTF_WRITE_UNCOMPRESSED_BINARY
)

# Win32 common dialogs for the open-file dialog.
if(WIN32)
    target_link_libraries(scene_editor PRIVATE comdlg32)
endif()

# Copy shaders next to the executable so they can be loaded at runtime.
add_custom_command(TARGET scene_editor POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/shaders
            $<TARGET_FILE_DIR:scene_editor>/shaders)

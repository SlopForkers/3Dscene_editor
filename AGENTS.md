# AGENTS.md — Scene Editor

Guidance for coding agents working in this repository.

## What this is

A single-window 3D scene authoring tool: heightfield terrain sculpting and
texture painting, glTF/VRM prop placement, instanced vegetation painting,
snap-based block building, HDR skybox import, and .scene save/load.
C++20, OpenGL 3.3 core, Dear ImGui, GLFW, GLM 1.0.3, cgltf, stb. Unit tests
(doctest, `tests/`); integration verification is by building and running.

## Build & run

```bash
cmake -B build -S .
cmake --build build --config Release
# binary: build/bin/scene_editor.exe (shaders/ are copied next to it)
```

- The app loads shaders from `<cwd>/shaders` — run it from `build/bin`.
- Dependencies are FetchContent'd (GLFW 3.4, GLM 1.0.3, ImGui v1.91.5,
  cgltf v1.15, stb — pinned commit, nlohmann_json v3.11.3, doctest v2.4.11).
  GLAD2 is vendored in `external/glad/`. doctest's legacy
  `cmake_minimum_required(3.0)` needs the `CMAKE_POLICY_VERSION_MINIMUM`
  escape hatch in CMakeLists (expect one harmless upstream warning).
- CLI smoke test: `scene_editor model.glb sky.hdr test.savetest`
  (.savetest = save the scene then immediately reload it).
- Unit tests: `cmake --build build --target tests && build/bin/tests.exe`.

## Architecture

Layered by responsibility; lower layers know nothing about the editor:

```
src/
 ├─ editor/
 │   ├─ main.cpp ................. entry point
 │   ├─ app.cpp / app.h .......... core loop, init/shutdown, input dispatch, render
 │   ├─ tools.cpp/h .............. ITool + TerrainTool/PropTool/VertexTool/BuildTool
 │   ├─ app_ui.cpp ............... ImGui frame, left rail, brush bar, help
 │   ├─ app_panels.cpp ........... one draw*Content() per rail category
 │   ├─ ui_icons.cpp/h ........... ImDrawList vector icons
 │   └─ ui_common.h .............. shared UI name helpers
 ├─ scene/ (subsystems – no UI knowledge)
 │   ├─ terrain.cpp/h ............ heightfield, brushes, splat layers
 │   ├─ brush.cpp/h .............. on-terrain brush ring cursor
 │   ├─ vertex_edit.cpp/h ........ per-vertex terrain editing gizmo
 │   ├─ model.cpp/h .............. glTF/VRM loader (cgltf), skinning
 │   ├─ prop.cpp/h ............... placed prop instances + selection
 │   ├─ detail.cpp/h ............. instanced vegetation
 │   ├─ build.cpp/h .............. snap-based blocks + face textures
 │   ├─ skybox.cpp/h ............. cubemap sky + equirect→cubemap
 │   ├─ gizmo.cpp/h .............. translate/rotate/scale manipulator
 │   ├─ scene.cpp/h .............. App::saveScene/loadScene wrappers (format: scene.h)
 │   └─ scene_io.cpp/h ........... free save/load functions over SceneContext refs
 └─ platform/ (foundation – zero editor dependencies)
     ├─ camera.cpp/h ............. orbit camera + screenToRay
     ├─ input.cpp/h .............. GLFW callbacks → per-frame state
     ├─ shader.cpp/h ............. GL program + cached uniform locations
     ├─ gl_resource.h ............ move-only RAII: GlBuffer/GlVertexArray/GlTexture
     ├─ sys_util.cpp/h ........... UTF-8 file IO (std::filesystem + u8string paths)
     ├─ file_dialog.cpp/h ........ native open/save dialogs (Win32)
     ├─ noise.h .................. procedural noise (Perlin/Simplex/…)
     └─ stb_image_impl.cpp ....... stb_image implementation unit
```

**App is a single class split across four translation units**
(`editor/app.cpp`, `editor/app_ui.cpp`, `editor/app_panels.cpp`, plus
`scene/scene.cpp` for serialization). New editor features usually mean:
a subsystem class in `scene/` + a `draw*Content()` panel in `editor/` +
wiring in `App::handleInput` / `renderScene`.

**Tools**: `App::handleInput` dispatches to `activeTool_` (an `ITool*`
pointing at one of the four tool structs in `tools.h`). Each tool owns its
drag state (e.g. `BuildTool::buildDragging_`); `cancelDrag()` must leave no
in-progress interaction behind. Sub-gizmo drag state (`Gizmo`,
`VertexEditor`) lives on App and is cancelled explicitly on tool switch —
tools only see App while active, so they can't self-cancel after a switch.

**Include resolution**: CMake adds `src/editor`, `src/scene`, and
`src/platform` to the include path, so all headers are included by short
name (`#include "model.h"`) regardless of which subdirectory the includer
lives in.

### Data flow per frame

1. `Input::newFrame()` → `glfwPollEvents()` (callbacks accumulate deltas).
2. `App::handleInput(dt)` — hotkeys, camera, then `activeTool_->handleInput`.
3. `renderScene()` — **shadow depth pass** (skipped when `showShadows_` off):
   light-ortho VP → `renderDepthPass()` re-renders terrain/props/details/
   blocks into the 2048² depth FBO with color writes off → then main pass:
   skybox → terrain → props → details → blocks → ghost/drag previews →
   selection boxes → gizmos → brush cursor.
4. `renderImGui()` — left rail panel + brush bar + help overlay.

### Conventions that matter (violations caused real bugs)

- **GL default state**: `GL_CULL_FACE` is OFF app-wide (the skybox cube is
  drawn from inside), `GL_DEPTH_TEST` ON. Any renderer that enables culling
  or blending for a draw MUST restore the default afterwards (see
  `Model::render` / `DetailSystem::render` tail).
- **Shaders**: no uniform initializers (GLSL 330). Every uniform must be set
  explicitly by the caller each frame — `line.frag`'s `uAlpha` is the
  canonical example.
- **HiDPI**: mouse coords from `Input` are in *window* pixels; the camera
  viewport is in *framebuffer* pixels. Use `App::cursorRay()`; sub-gizmos
  get the ratio via `setDpiScale()` once per frame (`App::handleInput`).
- **Hotkeys**: global shortcuts must be gated on `!ImGui::GetIO().WantTextInput`
  (see `App::handleInput`), or they fire while typing into text fields.
- **Drag state**: captured at mouse-press (e.g. `buildDragErase_`), never
  re-read modifiers at release. Tool switches (Tab/category click) must
  cancel in-progress drags (`Gizmo::cancelDrag`, `VertexEditor::cancelDrag`,
  `buildDragging_ = false`).
- **Vector-pointer invalidation**: `PropManager`/`BuildSystem` store elements
  in `std::vector` and hand out raw pointers (`findProp`, `selected()`).
  Never keep such a pointer across a call that may `push_back` (e.g.
  `addProp`) — copy the values first.
- **Resource lifetime**: all GL objects are released in `App::shutdown()`
  while the context is current, *before* `glfwTerminate()`. Classes with GL
  resources implement `create()`/`destroy()`; destructors call `destroy()`
  defensively but must find zeroed handles by then. `Shader::~Shader` relies
  on this — do not reorder.
- **File paths**: all file IO accepts UTF-8 and goes through
  `sys_util::readFileBytes`/`writeFileBytes` or `stbi_load_from_memory`;
  cgltf uses custom file callbacks (`model.cpp`). Never use plain
  `fopen`/`ifstream`/`stbi_load` for user-supplied paths (ANSI on Windows).
- **Scene loading**: every length/index field from a `.scene` file is
  validated against the buffer before use (see the `readU32`/`readBlob`
  cursors in `scene_io.cpp::loadScene`). Keep it that way.
- **Shadow map**: lives on texture unit 7 (`App::kShadowTexUnit`) — terrain
  owns units 0-5, prop materials 0-4, block texture 0. The depth FBO texture
  uses `GL_TEXTURE_COMPARE_MODE` (required by `sampler2DShadow`); it must be
  unbound from the unit before it is rendered into (feedback loop), and the
  depth pass runs with `uEnableShadow = 0`. Front-face culling during the
  depth pass must be re-asserted per subsystem — `Model::render` toggles
  `GL_CULL_FACE` internally.
- **Build & CI**: `-Wall -Wextra -Wpedantic` on `scene_editor`; dependency
  includes are `SYSTEM` (warnings only on our code). New code must be
  warning-clean. `.clang-format` is provided (Google-based, 4-space, K&R).

### .scene format

Single binary file: `"SCNE"` magic + version + JSON metadata + heights blob
+ splat blob. The authoritative description is the comment in `src/scene/scene.h`.

## Common tasks

- **New brush type**: `Terrain::BrushParams::Type` + `applyBrush` branch +
  icon in `ui_icons.cpp` + entry in `brushTypeName` + brush bar/hotkeys.
- **New rail panel**: `App::Category` enum + `draw*Content()` in
  `app_panels.cpp` + icon + `catIcon`/`catName` + switch in `drawLeftPanel`.
- **New uniform**: no registration needed; `Shader::set*` caches locations.
  Remember to set it every frame (no initializers in GLSL).

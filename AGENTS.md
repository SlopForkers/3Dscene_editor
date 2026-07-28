# AGENTS.md — Scene Editor

Guidance for coding agents working in this repository.

## What this is

A single-window 3D scene authoring tool: heightfield terrain sculpting and
texture painting, glTF/VRM prop placement, instanced vegetation painting,
snap-based block building, HDR skybox import, and .scene save/load.
C++17, OpenGL 3.3 core, Dear ImGui, GLFW, GLM, cgltf, stb. No tests;
verification is by building and running.

## Build & run

```bash
cmake -B build -S .
cmake --build build --config Release
# binary: build/bin/scene_editor.exe (shaders/ are copied next to it)
```

- The app loads shaders from `<cwd>/shaders` — run it from `build/bin`.
- Dependencies are FetchContent'd (GLFW, GLM, ImGui, cgltf, stb); GLAD2 is
  vendored in `external/glad/`. `cmake_policy(SET CMP0169 OLD)` is required
  for the glm download path — do not "modernize" it away.
- CLI smoke test: `scene_editor model.glb sky.hdr test.savetest`
  (.savetest = save the scene then immediately reload it).

## Architecture

Layered by responsibility; lower layers know nothing about the editor:

```
main.cpp
  └─ App (application shell — owns everything below)
       ├─ core loop & lifecycle ...... src/app.cpp
       │    (initWindow/initOpenGL/initImGui/shutdown/run,
       │     handleInput/tool dispatch, renderScene, importModel)
       ├─ UI shell ................... src/app_ui.cpp
       │    (ImGui frame, left rail, brush bar, help overlay)
       ├─ UI panels .................. src/app_panels.cpp
       │    (one draw*Content() per rail category)
       ├─ icons / UI helpers ......... src/ui_icons.*, src/ui_common.h
       ├─ scene save/load ............ src/scene.cpp (format: src/scene.h)
       └─ subsystems (one class each):
            Camera ................... orbit camera + screenToRay
            Input .................... GLFW callbacks → per-frame state
            Terrain .................. heightfield, brushes, splat layers
            VertexEditor ............. per-vertex terrain editing gizmo
            Model / PropManager ...... glTF load (cgltf) + placed props
            Gizmo .................... translate/rotate/scale manipulator
            DetailSystem ............. instanced vegetation
            BuildSystem .............. snap-based blocks + face textures
            Skybox ................... cubemap sky + equirect→cubemap
            Shader ................... program + cached uniform locations
            BrushCursor .............. on-terrain brush ring
            sys_util ................. UTF-8 file IO (Win32-wide fopen)
            file_dialog .............. native open/save dialogs (Win32)
```

**App is a single class split across four translation units**
(`app.cpp`, `app_ui.cpp`, `app_panels.cpp`, plus `scene.cpp` for
serialization). New editor features usually mean: a subsystem class + a
`draw*Content()` panel + wiring in `App::handleInput` / `renderScene`.

### Data flow per frame

1. `Input::newFrame()` → `glfwPollEvents()` (callbacks accumulate deltas).
2. `App::handleInput(dt)` — hotkeys, camera, tool-specific mouse handling.
3. `renderScene()` — skybox → terrain → props → details → blocks →
   ghost/drag previews → selection boxes → gizmos → brush cursor.
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
  cursors in `App::loadScene`). Keep it that way.

### .scene format

Single binary file: `"SCNE"` magic + version + JSON metadata + heights blob
+ splat blob. The authoritative description is the comment in `src/scene.h`.

## Common tasks

- **New brush type**: `Terrain::BrushParams::Type` + `applyBrush` branch +
  icon in `ui_icons.cpp` + entry in `brushTypeName` + brush bar/hotkeys.
- **New rail panel**: `App::Category` enum + `draw*Content()` in
  `app_panels.cpp` + icon + `catIcon`/`catName` + switch in `drawLeftPanel`.
- **New uniform**: no registration needed; `Shader::set*` caches locations.
  Remember to set it every frame (no initializers in GLSL).

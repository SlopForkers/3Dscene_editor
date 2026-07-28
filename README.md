# Scene Editor

A real-time 3D scene authoring tool built on an OpenGL 3.3 renderer with an
immediate-mode UI (Dear ImGui). Sculpt a heightfield terrain, paint up to 16
texture layers, import glTF/VRM models as props, paint instanced vegetation,
snap together building blocks, generate terrain from procedural noise, and
light the whole scene under an imported HDR sky — all in one editor.

![OpenGL 3.3](https://img.shields.io/badge/OpenGL-3.3-core-blue)
![C++17](https://img.shields.io/badge/C%2B%2B-17-blue)
![CMake 3.20+](https://img.shields.io/badge/CMake-3.20%2B-green)
![License: MIT](https://img.shields.io/badge/License-MIT-yellow)

## Features

### Terrain
- **Heightfield terrain** — 256×256 grid over a 200×200 world-space area with
  bilinear height sampling and ray-based mouse picking.
- **8 brush types** — Raise, Lower, Smooth, Flatten, Noise, Set Height,
  Texture (splat painting), and Vegetation.
- **3 falloff modes** — Smooth, Linear, and Constant brush falloff curves.
- **Framerate-independent strokes** — brush strength is scaled by frame time
  so sculpting behaves consistently regardless of FPS.
- **Texture layers** — up to 16 layers blended through 4 RGBA splat maps,
  each with an albedo + optional normal map and per-layer tile size.
- **Vertex editing** — in wireframe mode, select individual terrain vertices
  (Ctrl+click to add to the selection) and drag them with a gizmo in Free XYZ,
  Vertical, or Normal modes. The brush radius/falloff controls how neighbours
  follow the dragged centre for organic deformation.
- **Procedural noise generation** — Perlin, Simplex, Value, Worley, and Ridge
  noise with fractal Brownian motion (octaves, persistence, lacunarity), blend
  modes (Replace/Add/Subtract/Multiply/Min/Max), exponent shaping, invert, and
  a live preview thumbnail. Frequency is measured in cycles across the whole
  terrain, so the default settings are alias-free at the 256×256 grid.

### Props & Models
- **glTF 2.0 / VRM import** — parsed with cgltf. Skinned meshes render in the
  bind (rest) pose; morph targets are parsed and applied at weight 0.
- **Prop placement** — drop imported models onto the terrain, auto-scaled to a
  target size. Ray-pick to select, then transform with a 3D gizmo.
- **Transform gizmo** — Translate, Rotate, and Scale modes with three coloured
  world axes (X=red, Y=green, Z=blue) and constant on-screen size.

### Vegetation
- **Instanced detail painting** — load glTF prototypes (grass, rocks, trees…)
  into a palette and paint instances onto the terrain with the Vegetation
  brush (Ctrl to erase). Hardware instancing keeps large counts cheap.
- **Auto-reproject** — instances and foundation blocks automatically follow
  the heightfield after terrain edits.

### Building
- **Snap-based blocks** — foundation blocks sink into the terrain; further
  blocks stack on top of or beside existing blocks via face snapping.
- **Foundation, wall & texture modes** — place single blocks, drag rectangles
  (foundations), drag lines (walls), or paint textures onto block faces.
- **Face textures** — load images into a shared texture library and assign
  them per block-face with Stretch or Tile UV modes.
- **Grid-snapped placement** with adjustable block size, sunk depth, and a
  rotatable wall edge (R cycles the 4 edges).

### Environment & Scene
- **HDR skybox** — import an equirectangular panorama (.hdr/.png/.jpg/…) and
  convert it to a cubemap on the GPU (stored as GL_RGB16F with an exposure
  slider). Falls back to a procedural vertical-gradient sky.
- **Terrain shading** — height-based color gradient, snow caps on peaks,
  hemisphere ambient fill, directional diffuse lighting, rim light, and
  distance fog.
- **Scene save/load** — custom `.scene` format (JSON metadata + binary terrain
  data) serializes the heightfield, splat maps, props, vegetation, blocks,
  textures, and camera.

### Editor
- **Orbit camera** — rotate (right-drag), pan (middle-drag), zoom (scroll),
  and move the orbit target with WASD.
- **Brush cursor** — a ring rendered flat on the terrain surface follows the
  mouse to preview the affected area.
- **Left-rail UI** — categorized panels for Brush, Vertex, Props, Vegetation,
  Build, Terrain, Noise, Layers, Env, View, and File operations.
- **Wireframe & help overlay** — toggle wireframe rendering and an in-app help
  panel.

## Controls

| Action | Input |
|---|---|
| Paint / place / pick (context: active tool) | Left-drag |
| Orbit camera | Right-drag |
| Pan camera | Middle-drag |
| Zoom | Scroll wheel |
| Move camera target | `W` `A` `S` `D` |
| Adjust brush radius | `Shift` + scroll |
| Adjust brush strength | `Ctrl` + scroll |
| Cycle tool (Brush→Prop→Vertex→Build) | `Tab` |
| Select brush type | `1`–`8` |
| Toggle wireframe | `F` |
| Toggle help overlay | `H` |
| Prop gizmo: Translate / Rotate / Scale | `T` / `R` / `S` |
| Vertex drag mode: Free / Vertical / Normal | `V` / `B` / `N` |
| Build mode: Foundation / Wall / Texture | `Z` / `X` / `C` |
| Rotate wall edge | `R` |
| Delete selected block | `Delete` |
| Erase vegetation / add to vertex selection | `Ctrl` + click |
| Quit | `Esc` |

## Dependencies

All dependencies are fetched or vendored automatically by CMake — no manual
setup is required.

| Library | Version | Source |
|---|---|---|
| [GLFW](https://github.com/glfw/glfw) | 3.3.9 | CMake `FetchContent` |
| [GLM](https://github.com/g-truc/glm) | 0.9.9.8 | CMake `FetchContent` |
| [Dear ImGui](https://github.com/ocornut/imgui) | v1.91.5 | CMake `FetchContent` |
| [cgltf](https://github.com/jkuhlmann/cgltf) | v1.15 | CMake `FetchContent` |
| [stb](https://github.com/nothings/stb) | master (stb_image) | CMake `FetchContent` |
| [GLAD](https://glad.dav1d.de/) | 2 (GL 3.3 core) | Vendored in `external/glad/` |

> An OpenGL 3.3+ capable GPU and driver are required.

## Building

```bash
cmake -B build -S .
cmake --build build --config Release
```

The executable is output to `build/bin/scene_editor.exe`. CMake copies the
`shaders/` directory next to the binary as a post-build step, so the app can
load shaders at runtime from the working directory.

### Command-line usage

```
scene_editor [model_or_sky_or_scene ...]
```

Files passed on the command line are imported at startup, dispatching by
extension:

| Extension | Action |
|---|---|
| `.gltf` `.glb` `.vrm` | Import as a model and spawn a prop at the camera target |
| `.hdr` `.png` `.jpg` `.jpeg` `.tga` `.bmp` | Import as the equirectangular skybox |
| `.scene` | Load a saved scene |
| `.savetest` | Save the current scene then reload it (round-trip test) |

## Project Structure

```
.
├── CMakeLists.txt
├── external/
│   └── glad/                 # Vendored GLAD2 loader (GL 3.3 core)
├── shaders/
│   ├── terrain.vert/.frag    # Terrain (splat blending, fog, rim, gradient)
│   ├── line.vert/.frag       # Brush cursor, gizmos, wireframe outlines
│   ├── prop.vert/.frag       # glTF props/vegetation (PBR-ish, skinning)
│   ├── block.vert/.frag      # Building blocks (face textures)
│   └── skybox*.vert/.frag     # Skybox draw + equirect→cubemap conversion
├── assets/                   # (gitignored) sample HDR sky & VRM model
└── src/
    ├── editor/
    │   ├── main.cpp              # Entry point
    │   ├── app.{cpp,h}           # Core loop, init/shutdown, input dispatch, render
    │   ├── app_ui.cpp            # ImGui frame, left rail, brush bar, help overlay
    │   ├── app_panels.cpp        # One draw*Content() panel per rail category
    │   ├── ui_icons.{cpp,h}      # ImDrawList vector icons (brushes, categories)
    │   └── ui_common.h           # Shared UI name helpers
    ├── scene/
    │   ├── terrain.{cpp,h}       # Heightfield, brushes, splat layers
    │   ├── model.{cpp,h}         # glTF/VRM loader (cgltf), skinning
    │   ├── prop.{cpp,h}          # Placed prop instances + selection
    │   ├── detail.{cpp,h}        # Instanced vegetation system
    │   ├── build.{cpp,h}         # Snap-based block building + face textures
    │   ├── skybox.{cpp,h}        # Cubemap skybox + HDR equirect import
    │   ├── gizmo.{cpp,h}         # Translate/Rotate/Scale manipulator
    │   ├── vertex_edit.{cpp,h}   # Vertex-level terrain editing gizmo
    │   ├── brush.{cpp,h}         # Brush cursor ring rendering
    │   └── scene.{cpp,h}         # .scene save/load (format specification)
    └── platform/
        ├── camera.{cpp,h}        # Orbit camera with ray casting
        ├── input.{cpp,h}         # Centralized GLFW input state
        ├── shader.{cpp,h}        # Shader loading and uniform helpers
        ├── sys_util.{cpp,h}      # UTF-8 file IO (wide Win32 APIs on Windows)
        ├── file_dialog.{cpp,h}   # Native open/save file dialogs (Win32, UTF-8)
        ├── noise.h               # Header-only procedural noise (Perlin/Simplex/…)
        └── stb_image_impl.cpp    # stb_image implementation unit
```

## Architecture Notes

- **Input** is centralized in a single `Input` singleton fed by GLFW callbacks
  and polled each frame, accumulating mouse deltas across multiple callbacks so
  fast motion isn't lost.
- **Terrain** stores heights in a flat array and uploads vertices to a
  persistent VBO/EBO (buffer orphaning on edit to avoid pipeline stalls).
  Brush edits recompute normals incrementally over the edited bounding box
  rather than the whole grid. Texture layers are kept as CPU pixels and
  rebuilt into a GL texture array cheaply when edited.
- **Picking** unprojects the cursor to a world-space ray and intersects it
  with the heightfield for painting, the brush cursor, vegetation, and block
  placement; prop selection uses an AABB slab test against the ray.
- **Models** share a `Model` library so a loaded glTF is kept alive while
  referenced by multiple props/vegetation prototypes, and can be rendered
  either standalone or hardware-instanced via a per-instance mat4 VBO.
- **Building** snaps new blocks adjacent to the picked face of an existing
  block (or onto the terrain for foundations), and reprojection keeps
  foundation blocks sunk at the right height after terrain edits.
- **ImGui integration** defers to `io.WantCaptureMouse` / `io.WantTextInput`
  so camera and paint drags don't trigger while interacting with the control
  panel or typing in a text field.

## License

Licensed under the [MIT License](./LICENSE.md) — see the `LICENSE.md` file for
details. Third-party dependencies retain their own licenses (GLFW, GLM, Dear
ImGui, cgltf, stb, GLAD).

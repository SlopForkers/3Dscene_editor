# Scene Editor

A real-time 3D terrain sculpting editor with an OpenGL 3.3 renderer and an
immediate-mode UI built on Dear ImGui. Sculpt heightfields in real time with
multiple brush types, navigate with an orbit camera, and tweak lighting and
display options on the fly.

![OpenGL 3.3](https://img.shields.io/badge/OpenGL-3.3-core-blue)
![C++17](https://img.shields.io/badge/C%2B%2B-17-blue)
![CMake 3.20+](https://img.shields.io/badge/CMake-3.20%2B-green)
![License: MIT](https://img.shields.io/badge/License-MIT-yellow)

## Features

- **Heightfield terrain** — 256x256 grid over a 200x200 world-space area with
  bilinear height sampling and ray-based mouse picking.
- **6 brush types** — Raise, Lower, Smooth, Flatten, Noise, and Set Height.
- **3 falloff modes** — Smooth, Linear, and Constant brush falloff curves.
- **Framerate-independent strokes** — brush strength is scaled by frame time so
  sculpting behaves consistently regardless of FPS.
- **Orbit camera** — rotate, pan, and zoom with the mouse; screen-to-world ray
  casting for accurate terrain interaction.
- **Terrain shading** — height-based color gradient (low/brown to grassy green),
  snow caps on peaks, hemisphere ambient fill, directional diffuse lighting, rim
  light, and distance fog.
- **Brush cursor** — a ring rendered flat on the terrain surface follows the
  mouse to preview the affected area.
- **Wireframe & help overlay** — toggle wireframe rendering and an in-app help
  panel.
- **ImGui control panel** — adjust brush radius/strength/falloff, terrain
  generation, lighting azimuth/elevation, cursor color, and display options.

## Controls

| Action | Input |
|---|---|
| Paint terrain | Left-drag |
| Orbit camera | Right-drag |
| Pan camera | Middle-drag |
| Zoom | Scroll wheel |
| Select brush type | `1`–`5` |
| Toggle wireframe | `F` |
| Toggle help overlay | `H` |
| Quit | `Esc` |

## Dependencies

All dependencies are fetched or vendored automatically by CMake — no manual
setup is required.

| Library | Version | Source |
|---|---|---|
| [GLFW](https://github.com/glfw/glfw) | 3.3.9 | CMake `FetchContent` |
| [GLM](https://github.com/g-truc/glm) | 0.9.9.8 | CMake `FetchContent` |
| [Dear ImGui](https://github.com/ocornut/imgui) | v1.91.5 | CMake `FetchContent` |
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

## Project Structure

```
.
├── CMakeLists.txt
├── external/
│   └── glad/              # Vendored GLAD2 loader (GL 3.3 core)
├── shaders/
│   ├── terrain.vert      # Terrain vertex shader
│   ├── terrain.frag      # Terrain fragment shader (gradient, fog, rim)
│   ├── line.vert         # Line shader (brush cursor)
│   └── line.frag
└── src/
    ├── main.cpp           # Entry point
    ├── app.{cpp,h}        # Application loop, ImGui panels, input handling
    ├── camera.{cpp,h}     # Orbit camera with ray casting
    ├── terrain.{cpp,h}    # Heightfield mesh, brushes, raycast, normals
    ├── brush.{cpp,h}      # Brush cursor ring rendering
    ├── shader.{cpp,h}     # Shader loading and uniform helpers
    └── input.{cpp,h}      # Centralized GLFW input state
```

## Architecture Notes

- **Input** is centralized in a single `Input` singleton fed by GLFW callbacks
  and polled each frame, accumulating mouse deltas across multiple callbacks so
  fast motion isn't lost.
- **Terrain** stores heights in a flat array and uploads vertices to a static
  VBO/EBO. Brush edits dirty only the affected region, and normals are
  recomputed incrementally over the edited bounding box rather than over the
  whole grid.
- **Picking** unprojects the cursor to a world-space ray and intersects it with
  the heightfield for both painting and the brush cursor.
- **ImGui integration** defers to `io.WantCaptureMouse` so camera and paint
  drags don't trigger while interacting with the control panel.

## License

Licensed under the [MIT License](./LICENSE) — see the `LICENSE` file for
details. Third-party dependencies retain their own licenses (GLFW, GLM, Dear
ImGui, GLAD).
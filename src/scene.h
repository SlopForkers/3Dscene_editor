#pragma once

// ---------------------------------------------------------------------------
// .scene file format (single binary file, little-endian, host float layout)
// ---------------------------------------------------------------------------
//
//   offset  size      contents
//   0       4         magic "SCNE"
//   4       u32       format version (1 = single RGBA splat, 2 = 16-channel
//                     planar splat; loader migrates v1)
//   8       u32       JSON metadata size in bytes
//   12      N         JSON metadata: terrain grid dims + texture layers,
//                     skybox path + exposure, light angles, camera, props,
//                     vegetation prototypes + instances, block texture
//                     library + blocks. Asset paths are stored relative to
//                     the scene file when possible.
//   12+N    u32       heightfield blob size (gridX * gridZ * sizeof(float))
//   ...     blob      heights, row-major floats
//   ...     u32       splat blob size (gridX * gridZ * 16 for v2,
//                     gridX * gridZ * 4 for v1)
//   ...     blob      splat weights, planar per splat-map (4 RGBA maps)
//
// The save/load implementation lives in scene.cpp as App::saveScene() /
// App::loadScene() (declared in app.h). Loading validates every length field
// against the remaining file size before reading.
// ---------------------------------------------------------------------------

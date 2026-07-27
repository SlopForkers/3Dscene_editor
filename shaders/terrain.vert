#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;

uniform mat4 uViewProj;
uniform mat4 uModel;
uniform float uTerrainSize;

out vec3 vNormal;
out vec3 vWorldPos;
out float vHeight;
out vec2 vGridUv;    // 0..1 across the grid (splat sampling)
out vec2 vWorldXZ;   // world XZ (tiled layer textures)

void main() {
    vec4 world = uModel * vec4(aPos, 1.0);
    vWorldPos = world.xyz;
    vNormal   = mat3(uModel) * aNormal;
    vHeight   = aPos.y;
    vWorldXZ  = aPos.xz;
    vGridUv   = (aPos.xz + uTerrainSize * 0.5) / uTerrainSize;
    gl_Position = uViewProj * world;
}

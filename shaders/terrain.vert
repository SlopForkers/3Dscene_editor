#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;

uniform mat4 uViewProj;
uniform mat4 uModel;

out vec3 vNormal;
out vec3 vWorldPos;
out float vHeight;

void main() {
    vec4 world = uModel * vec4(aPos, 1.0);
    vWorldPos = world.xyz;
    vNormal   = mat3(uModel) * aNormal;
    vHeight   = aPos.y;
    gl_Position = uViewProj * world;
}

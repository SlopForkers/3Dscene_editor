#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;

uniform mat4 uModel;
uniform mat4 uViewProj;

out vec3 vNormal;
out vec3 vWorldPos;
out vec3 vLocalPos;

void main() {
    vec4 wp = uModel * vec4(aPos, 1.0);
    vWorldPos = wp.xyz;
    vLocalPos = aPos;
    // Walls are non-uniformly scaled (thin plates), so the normal matrix
    // (inverse-transpose) is required — plain mat3(uModel) skews normals.
    vNormal = normalize(transpose(inverse(mat3(uModel))) * aNormal);
    gl_Position = uViewProj * wp;
}

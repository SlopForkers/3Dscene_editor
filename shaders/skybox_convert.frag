#version 330 core
in vec2 vUv;
out vec4 FragColor;
uniform sampler2D uEquirect;
uniform int uFace;
const float PI = 3.14159265359;
void main() {
    float tx = vUv.x * 2.0 - 1.0;
    float ty = vUv.y * 2.0 - 1.0;
    vec3 dir;
    if      (uFace == 0) dir = vec3( 1.0, -ty, -tx); // +X
    else if (uFace == 1) dir = vec3(-1.0, -ty,  tx); // -X
    else if (uFace == 2) dir = vec3( tx,  1.0, -ty); // +Y
    else if (uFace == 3) dir = vec3( tx, -1.0,  ty); // -Y
    else if (uFace == 4) dir = vec3( tx, -ty,  1.0); // +Z
    else                 dir = vec3(-tx, -ty, -1.0); // -Z
    dir = normalize(dir);
    float phi = atan(dir.z, dir.x);
    float theta = asin(clamp(dir.y, -1.0, 1.0));
    vec2 eqUv = vec2(phi / (2.0 * PI) + 0.5, theta / PI + 0.5);
    FragColor = vec4(texture(uEquirect, eqUv).rgb, 1.0);
}

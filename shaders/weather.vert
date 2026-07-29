#version 330 core
layout(location = 0) in vec3 aPos;

uniform mat4 uViewProj;
uniform int uMode;           // 0 = snow points, 1 = rain lines
uniform float uPointScale;   // pixels per world unit at distance 1

void main() {
    vec4 clip = uViewProj * vec4(aPos, 1.0);
    gl_Position = clip;
    if (uMode == 0)
        gl_PointSize = clamp(uPointScale / max(clip.w, 0.1), 1.0, 12.0);
}

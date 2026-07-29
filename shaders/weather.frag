#version 330 core

uniform int uMode;           // 0 = snow points, 1 = rain lines
uniform vec3 uColor;
uniform float uAlpha;

out vec4 FragColor;

void main() {
    float a = uAlpha;
    if (uMode == 0) {
        // Soft round flake.
        vec2 d = gl_PointCoord - vec2(0.5);
        a *= smoothstep(0.5, 0.15, length(d));
    }
    FragColor = vec4(uColor, a);
}

#version 330 core
uniform vec3  uColor;
// No initializer: uniform initializers require GLSL 4.20+, and every caller
// sets this explicitly before drawing.
uniform float uAlpha;
out vec4 FragColor;

void main() {
    FragColor = vec4(uColor, uAlpha);
}

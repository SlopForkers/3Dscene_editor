#version 330 core
uniform vec3  uColor;
uniform float uAlpha = 1.0;
out vec4 FragColor;

void main() {
    FragColor = vec4(uColor, uAlpha);
}

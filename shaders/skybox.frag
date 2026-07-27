#version 330 core
in vec3 vDir;
out vec4 FragColor;
uniform samplerCube uSky;
uniform float uExposure;
void main() {
    vec3 c = texture(uSky, normalize(vDir)).rgb;
    FragColor = vec4(c * uExposure, 1.0);
}

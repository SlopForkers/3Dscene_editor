#version 330 core
layout(location = 0) in vec3 aPos;
out vec3 vDir;
uniform mat4 uViewProj;
void main() {
    vDir = aPos;
    vec4 pos = uViewProj * vec4(aPos, 1.0);
    // Force depth to far plane so the skybox sits behind everything;
    // caller uses glDepthFunc(GL_LEQUAL) for this to pass the depth test.
    gl_Position = pos.xyww;
}

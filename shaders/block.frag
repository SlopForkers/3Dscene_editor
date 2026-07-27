#version 330 core
in vec3 vNormal;
in vec3 vWorldPos;

uniform vec3 uColor;
uniform vec3 uLightDir;
uniform vec3 uCamPos;
uniform float uAlpha;

out vec4 FragColor;

void main() {
    vec3 N = normalize(vNormal);
    vec3 L = normalize(uLightDir);
    float NdotL = max(dot(N, L), 0.0);
    float hemi = 0.5 + 0.5 * N.y;
    vec3 ambient = mix(vec3(0.18, 0.20, 0.26), vec3(0.40, 0.44, 0.50), hemi);
    vec3 color = uColor * ambient + uColor * NdotL * 0.8;

    float dist = length(uCamPos - vWorldPos);
    float fog = clamp((dist - 120.0) / 600.0, 0.0, 0.7);
    color = mix(color, vec3(0.55, 0.62, 0.70), fog);

    FragColor = vec4(color, uAlpha);
}

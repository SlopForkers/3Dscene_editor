#version 330 core
in vec3 vNormal;
in vec3 vWorldPos;
in float vHeight;

uniform vec3 uLightDir;
uniform vec3 uCamPos;
uniform vec3 uBaseColor;
uniform vec3 uLowColor;
uniform float uMaxHeight;

out vec4 FragColor;

void main() {
    vec3 N = normalize(vNormal);
    vec3 L = normalize(uLightDir);

    float diff = max(dot(N, L), 0.0);
    // Soft ambient + hemisphere fill.
    float hemi = 0.5 + 0.5 * N.y;
    vec3 ambient = mix(vec3(0.18, 0.20, 0.26), vec3(0.35, 0.38, 0.45), hemi);

    // Height-based color: low areas darker/brownish, high areas grassy, peaks lighter.
    float t = clamp((vHeight + 2.0) / max(uMaxHeight + 2.0, 1.0), 0.0, 1.0);
    vec3 base = mix(uLowColor, uBaseColor, smoothstep(0.0, 0.55, t));
    // Snow caps on the tallest points.
    float snow = smoothstep(0.78, 1.0, t);
    base = mix(base, vec3(0.9, 0.92, 0.95), snow * 0.8);

    vec3 color = base * (ambient + diff * 0.9);

    // Subtle rim/fog to add depth.
    vec3 V = normalize(uCamPos - vWorldPos);
    float rim = pow(1.0 - max(dot(N, V), 0.0), 3.0) * 0.15;
    color += rim;

    // Distance fog.
    float dist = length(uCamPos - vWorldPos);
    float fog = clamp((dist - 120.0) / 600.0, 0.0, 0.7);
    color = mix(color, vec3(0.55, 0.62, 0.70), fog);

    FragColor = vec4(color, 1.0);
}

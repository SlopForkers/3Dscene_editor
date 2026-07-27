#version 330 core
in vec3 vNormal;
in vec3 vWorldPos;
in float vHeight;
in vec2 vGridUv;
in vec2 vWorldXZ;

uniform vec3 uLightDir;
uniform vec3 uCamPos;
uniform float uMaxHeight;
uniform float uTerrainSize;

uniform sampler2DArray uAlbedo;
uniform sampler2DArray uNormal;
uniform sampler2D uSplat0; uniform sampler2D uSplat1;
uniform sampler2D uSplat2; uniform sampler2D uSplat3;
uniform float uTileSize[16];
uniform int   uLayerCount;

out vec4 FragColor;

// Weight of layer i (0..15): which splat map (i/4) and channel (i%4).
float layerWeight(int i, vec4 s0, vec4 s1, vec4 s2, vec4 s3) {
    int m = i / 4;
    int c = i % 4;
    vec4 sm;
    if (m == 0) sm = s0; else if (m == 1) sm = s1;
    else if (m == 2) sm = s2; else sm = s3;
    if (c == 0) return sm.r;
    if (c == 1) return sm.g;
    if (c == 2) return sm.b;
    return sm.a;
}

void main() {
    vec4 s0 = texture(uSplat0, vGridUv);
    vec4 s1 = texture(uSplat1, vGridUv);
    vec4 s2 = texture(uSplat2, vGridUv);
    vec4 s3 = texture(uSplat3, vGridUv);

    int n = min(uLayerCount, 16);

    // Accumulate weighted albedo + tangent-space normal.
    vec3 albedo = vec3(0.0);
    vec3 tn     = vec3(0.0);
    float wsum = 0.0;

    for (int i = 0; i < 16; ++i) {
        if (i >= n) break;
        float w = layerWeight(i, s0, s1, s2, s3);
        if (w <= 0.001) continue;
        vec2 uv = vWorldXZ / uTileSize[i];
        albedo += w * texture(uAlbedo, vec3(uv, float(i))).rgb;
        tn     += w * (texture(uNormal, vec3(uv, float(i))).xyz * 2.0 - 1.0);
        wsum   += w;
    }

    if (wsum > 0.001) {
        albedo /= wsum;
        tn     /= wsum;
    } else if (n > 0) {
        // No splat weight anywhere: fall back to layer 0.
        vec2 uv0 = vWorldXZ / uTileSize[0];
        albedo = texture(uAlbedo, vec3(uv0, 0.0)).rgb;
        tn = texture(uNormal, vec3(uv0, 0.0)).xyz * 2.0 - 1.0;
    } else {
        albedo = vec3(0.4, 0.4, 0.4);
        tn = vec3(0.0, 0.0, 1.0);
    }

    vec3 N = normalize(vNormal);
    // Perturb normal with the blended tangent-space normal.
    vec3 Q = normalize(dFdx(vWorldPos));
    vec3 T = normalize(dFdy(vWorldPos));
    vec3 B = normalize(cross(N, T));
    if (dot(cross(N, T), B) < 0.0) B = -B;
    mat3 TBN = mat3(T, B, N);
    N = normalize(TBN * normalize(tn));

    vec3 L = normalize(uLightDir);
    float diff = max(dot(N, L), 0.0);
    float hemi = 0.5 + 0.5 * N.y;
    vec3 ambient = mix(vec3(0.18, 0.20, 0.26), vec3(0.35, 0.38, 0.45), hemi);

    // Snow caps on the tallest points (height-based, on top of textures).
    float t = clamp((vHeight + 2.0) / max(uMaxHeight + 2.0, 1.0), 0.0, 1.0);
    float snow = smoothstep(0.80, 1.0, t);
    albedo = mix(albedo, vec3(0.90, 0.92, 0.95), snow * 0.85);

    vec3 color = albedo * (ambient + diff * 0.9);

    // Subtle rim.
    vec3 V = normalize(uCamPos - vWorldPos);
    float rim = pow(1.0 - max(dot(N, V), 0.0), 3.0) * 0.15;
    color += rim;

    // Distance fog.
    float dist = length(uCamPos - vWorldPos);
    float fog = clamp((dist - 120.0) / 600.0, 0.0, 0.7);
    color = mix(color, vec3(0.55, 0.62, 0.70), fog);

    FragColor = vec4(color, 1.0);
}

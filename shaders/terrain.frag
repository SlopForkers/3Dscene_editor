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

uniform sampler2D uSplat;
uniform sampler2D uLayerTex0; uniform sampler2D uLayerTex1;
uniform sampler2D uLayerTex2; uniform sampler2D uLayerTex3;
uniform sampler2D uLayerNormal0; uniform sampler2D uLayerNormal1;
uniform sampler2D uLayerNormal2; uniform sampler2D uLayerNormal3;
uniform float uTileSize0; uniform float uTileSize1;
uniform float uTileSize2; uniform float uTileSize3;
uniform bool  uHasLayerNormal0; uniform bool uHasLayerNormal1;
uniform bool  uHasLayerNormal2; uniform bool uHasLayerNormal3;
uniform int   uLayerCount;

out vec4 FragColor;

vec3 layerAlbedo(int i, vec2 uv) {
    if (i == 0) return texture(uLayerTex0, uv).rgb;
    if (i == 1) return texture(uLayerTex1, uv).rgb;
    if (i == 2) return texture(uLayerTex2, uv).rgb;
    return texture(uLayerTex3, uv).rgb;
}

vec3 layerNormal(int i, vec2 uv) {
    if (i == 0) return uHasLayerNormal0 ? texture(uLayerNormal0, uv).xyz * 2.0 - 1.0 : vec3(0.0, 0.0, 1.0);
    if (i == 1) return uHasLayerNormal1 ? texture(uLayerNormal1, uv).xyz * 2.0 - 1.0 : vec3(0.0, 0.0, 1.0);
    if (i == 2) return uHasLayerNormal2 ? texture(uLayerNormal2, uv).xyz * 2.0 - 1.0 : vec3(0.0, 0.0, 1.0);
    return uHasLayerNormal3 ? texture(uLayerNormal3, uv).xyz * 2.0 - 1.0 : vec3(0.0, 0.0, 1.0);
}

float layerTile(int i) {
    if (i == 0) return uTileSize0;
    if (i == 1) return uTileSize1;
    if (i == 2) return uTileSize2;
    return uTileSize3;
}

void main() {
    vec4 splat = texture(uSplat, vGridUv);
    float w0 = splat.r, w1 = splat.g, w2 = splat.b, w3 = splat.a;
    float wsum = max(w0 + w1 + w2 + w3, 0.0001);
    w0 /= wsum; w1 /= wsum; w2 /= wsum; w3 /= wsum;

    int n = uLayerCount;
    vec3 albedo = vec3(0.0);
    vec3 tn = vec3(0.0);   // accumulated tangent-space normal contribution

    if (n > 0) {
        vec2 uv0 = vWorldXZ / uTileSize0;
        albedo += w0 * layerAlbedo(0, uv0);
        tn     += w0 * layerNormal(0, uv0);
    }
    if (n > 1) {
        vec2 uv1 = vWorldXZ / uTileSize1;
        albedo += w1 * layerAlbedo(1, uv1);
        tn     += w1 * layerNormal(1, uv1);
    }
    if (n > 2) {
        vec2 uv2 = vWorldXZ / uTileSize2;
        albedo += w2 * layerAlbedo(2, uv2);
        tn     += w2 * layerNormal(2, uv2);
    }
    if (n > 3) {
        vec2 uv3 = vWorldXZ / uTileSize3;
        albedo += w3 * layerAlbedo(3, uv3);
        tn     += w3 * layerNormal(3, uv3);
    }

    vec3 N = normalize(vNormal);
    // Perturb normal with the blended tangent-space normal. A simple screen-space
    // reorientation keeps lighting consistent with the surface.
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

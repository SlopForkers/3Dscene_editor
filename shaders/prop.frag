#version 330 core
in vec3 vNormal;
in vec2 vUv;
in vec3 vWorldPos;
in mat3 vTBN;
in float vHasTangent;

uniform vec3 uLightDir;
uniform vec3 uCamPos;

uniform vec4 uBaseColorFactor;
uniform vec3 uEmissiveFactor;
uniform float uMetallic;
uniform float uRoughness;
uniform float uNormalScale;
uniform float uOcclusionStrength;
uniform float uAlphaCutoff;
uniform int  uAlphaMode;   // 0 opaque, 1 mask, 2 blend
uniform bool uUnlit;
uniform bool uDoubleSided;

uniform bool uHasBaseColorTex;  uniform sampler2D uBaseColorTex;
uniform bool uHasMetalRoughTex; uniform sampler2D uMetalRoughTex;
uniform bool uHasNormalTex;     uniform sampler2D uNormalTex;
uniform bool uHasEmissiveTex;   uniform sampler2D uEmissiveTex;
uniform bool uHasOcclusionTex;  uniform sampler2D uOcclusionTex;

out vec4 FragColor;

const float PI = 3.14159265359;

void main() {
    vec4 base = uBaseColorFactor;
    if (uHasBaseColorTex) base *= texture(uBaseColorTex, vUv);
    if (uAlphaMode == 1 && base.a < uAlphaCutoff) discard;

    vec3 N = normalize(vNormal);
    if (uDoubleSided && !gl_FrontFacing) N = -N;

    if (uHasNormalTex && vHasTangent > 0.5) {
        vec3 tn = texture(uNormalTex, vUv).xyz * 2.0 - 1.0;
        tn.xy *= uNormalScale;
        N = normalize(vTBN * tn);
        if (uDoubleSided && !gl_FrontFacing) N = -N;
    }

    if (uUnlit) {
        FragColor = vec4(base.rgb, base.a);
        return;
    }

    float met = clamp(uMetallic, 0.0, 1.0);
    float rough = clamp(uRoughness, 0.045, 1.0);
    if (uHasMetalRoughTex) {
        vec4 mr = texture(uMetalRoughTex, vUv);
        met = clamp(mr.b, 0.0, 1.0);
        rough = clamp(mr.g, 0.045, 1.0);
    }

    vec3 L = normalize(uLightDir);
    float NdotL = max(dot(N, L), 0.0);
    float hemi = 0.5 + 0.5 * N.y;
    vec3 ambient = mix(vec3(0.18, 0.20, 0.26), vec3(0.40, 0.44, 0.50), hemi);

    vec3 albedo = base.rgb;
    vec3 F0 = mix(vec3(0.04), albedo, met);
    vec3 V = normalize(uCamPos - vWorldPos);
    vec3 H = normalize(L + V);
    float NdotH = max(dot(N, H), 0.0);
    float shininess = mix(8.0, 256.0, 1.0 - rough);
    float specPow = pow(NdotH, shininess);
    vec3 F = F0 + (1.0 - F0) * pow(1.0 - max(dot(H, V), 0.0), 5.0);
    vec3 kD = (1.0 - F) * (1.0 - met);
    vec3 diffuse = albedo * kD * NdotL / PI;
    vec3 specular = F * specPow * NdotL;

    vec3 color = albedo * ambient + (diffuse + specular) * 1.4;

    if (uHasOcclusionTex) {
        float ao = texture(uOcclusionTex, vUv).r;
        color *= mix(1.0, ao, uOcclusionStrength);
    }
    if (uHasEmissiveTex) {
        color += texture(uEmissiveTex, vUv).rgb * uEmissiveFactor;
    }

    // Distance fog matching the terrain.
    float dist = length(uCamPos - vWorldPos);
    float fog = clamp((dist - 120.0) / 600.0, 0.0, 0.7);
    color = mix(color, vec3(0.55, 0.62, 0.70), fog);

    FragColor = vec4(color, base.a);
}

#version 330 core
in vec3 vNormal;
in vec3 vWorldPos;
in vec3 vLocalPos;

uniform vec3  uColor;
uniform vec3  uLightDir;
uniform vec3  uCamPos;
uniform float uAlpha;

uniform int          uHasTexture;   // 0 or 1
uniform int          uTextureFace;  // 0..5, which face is textured
uniform int          uTexMode;      // 0 = Stretch (one copy), 1 = Tile (repeat)
uniform sampler2D   uTex;
uniform float        uTexScale;     // repeats per face (Tile mode only)

out vec4 FragColor;

// Map a normal to a face index (matches BuildSystem::Face).
int faceIndex(vec3 n) {
    vec3 a = abs(n);
    if (a.x >= a.y && a.x >= a.z) return n.x > 0.0 ? 0 : 1; // +X / -X
    if (a.y >= a.x && a.y >= a.z) return n.y > 0.0 ? 2 : 3; // +Y / -Y
    return n.z > 0.0 ? 4 : 5;                               // +Z / -Z
}

void main() {
    vec3 N = normalize(vNormal);
    vec3 L = normalize(uLightDir);
    float NdotL = max(dot(N, L), 0.0);
    float hemi = 0.5 + 0.5 * N.y;
    vec3 ambient = mix(vec3(0.18, 0.20, 0.26), vec3(0.40, 0.44, 0.50), hemi);

    vec3 baseColor = uColor;
    if (uHasTexture == 1 && faceIndex(N) == uTextureFace) {
        // Face-plane local coords in [0, 1] across the whole face.
        vec2 uvBase;
        if (abs(N.x) > 0.5) {
            uvBase = vec2(vLocalPos.z + 0.5, vLocalPos.y + 0.5);
        } else if (abs(N.y) > 0.5) {
            uvBase = vec2(vLocalPos.x + 0.5, vLocalPos.z + 0.5);
        } else {
            uvBase = vec2(vLocalPos.x + 0.5, vLocalPos.y + 0.5);
        }
        // Stretch: one copy fills the face. Tile: repeat uTexScale times.
        vec2 uv = (uTexMode == 0) ? uvBase : uvBase * uTexScale;
        // Rotate the image 180° (flip both axes) so it renders upright.
        uv = vec2(1.0 - uv.x, 1.0 - uv.y);
        vec4 tex = texture(uTex, uv);
        if (tex.a > 0.01) baseColor = tex.rgb;
    }

    vec3 color = baseColor * ambient + baseColor * NdotL * 0.8;

    float dist = length(uCamPos - vWorldPos);
    float fog = clamp((dist - 120.0) / 600.0, 0.0, 0.7);
    color = mix(color, vec3(0.55, 0.62, 0.70), fog);

    FragColor = vec4(color, uAlpha);
}

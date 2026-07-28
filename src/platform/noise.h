#pragma once
// Procedural noise generators for terrain height synthesis.
//
// All samplers are 2D, deterministic (seeded), and return values roughly in
// [-1, 1]. The high-level sample2D() runs fractal Brownian motion (fBm) over
// the chosen base noise and applies inversion / exponent shaping. A seeded
// permutation table is rebuilt per call which is cheap (256 ints) and keeps
// the module stateless and thread-agnostic.
//
// This header is self-contained (header-only, inline) so it can be included
// from any TU without a matching .cpp.

#include <cmath>
#include <cstdint>
#include <algorithm>

namespace Noise {

enum Type { Perlin = 0, Simplex, Value, Worley, Ridge, TypeCount };

enum BlendMode { Replace = 0, Add, Subtract, Multiply, Min, Max, BlendCount };

struct Params {
    Type      type        = Perlin;
    BlendMode blend       = Replace;
    int       seed        = 1;
    float     frequency   = 1.0f;   // cycles across the terrain world size
    float     amplitude   = 10.0f;  // height units (peak, before fbm sum)
    int       octaves     = 5;
    float     persistence = 0.5f;   // amplitude decay per octave
    float     lacunarity  = 2.0f;   // frequency growth per octave
    float     offsetX     = 0.0f;
    float     offsetZ     = 0.0f;
    bool      invert      = false;
    float     exponent    = 1.0f;   // shaping: out = sign(v)*|v|^exp
    bool      ridged      = false;  // ridge the fbm (1-|v|) regardless of base
};

// Build a 256-entry permutation table (duplicated to 512 for fast indexing)
// seeded by `seed`. Deterministic across runs for the same seed.
inline void buildPerm(int seed, int perm[512]) {
    int p[256];
    for (int i = 0; i < 256; ++i) p[i] = i;
    // LCG with the seed to shuffle deterministically.
    uint32_t s = (uint32_t)seed * 2654435761u + 12345u;
    for (int i = 255; i > 0; --i) {
        s = s * 1664525u + 1013904223u;
        int j = (int)(s >> 16) % (i + 1);
        if (j < 0) j += i + 1;
        std::swap(p[i], p[j]);
    }
    for (int i = 0; i < 512; ++i) perm[i] = p[i & 255];
}

inline float fade(float t) { return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f); }
inline float lerp(float a, float b, float t) { return a + t * (b - a); }

inline float grad2(int hash, float x, float y) {
    // 8 gradient directions.
    int h = hash & 7;
    float u = (h < 4) ? x : y;
    float v = (h < 4) ? y : x;
    return ((h & 1) ? -u : u) + ((h & 2) ? -2.0f * v : 2.0f * v);
}

// Classic Perlin 2D, output ~[-1, 1].
inline float perlin2(float x, float y, const int perm[512]) {
    int X = (int)std::floor(x) & 255;
    int Y = (int)std::floor(y) & 255;
    float xf = x - std::floor(x);
    float yf = y - std::floor(y);
    float u = fade(xf);
    float v = fade(yf);
    int aa = perm[perm[X] + Y];
    int ab = perm[perm[X] + Y + 1];
    int ba = perm[perm[X + 1] + Y];
    int bb = perm[perm[X + 1] + Y + 1];
    float x1 = lerp(grad2(aa, xf, yf),       grad2(ba, xf - 1.0f, yf),       u);
    float x2 = lerp(grad2(ab, xf, yf - 1.0f),grad2(bb, xf - 1.0f, yf - 1.0f),u);
    return lerp(x1, x2, v);
}

// Simplex 2D (Stefan Gustavson's algorithm), output ~[-1, 1].
inline float simplex2(float x, float y, const int perm[512]) {
    const float F2 = 0.366025403f;  // 0.5 * (sqrt(3) - 1)
    const float G2 = 0.211324865f;  // (3 - sqrt(3)) / 6
    float s = (x + y) * F2;
    int i = (int)std::floor(x + s);
    int j = (int)std::floor(y + s);
    float t = (i + j) * G2;
    float X0 = x - (i - t);
    float Y0 = y - (j - t);
    int i1, j1;
    if (X0 > Y0) { i1 = 1; j1 = 0; } else { i1 = 0; j1 = 1; }
    float x1 = X0 - i1 + G2;
    float y1 = Y0 - j1 + G2;
    float x2 = X0 - 1.0f + 2.0f * G2;
    float y2 = Y0 - 1.0f + 2.0f * G2;
    int ii = i & 255;
    int jj = j & 255;
    int gi0 = perm[ii + perm[jj]] % 12;
    int gi1 = perm[ii + i1 + perm[jj + j1]] % 12;
    int gi2 = perm[ii + 1 + perm[jj + 1]] % 12;
    // 12 gradient directions for 2D.
    static const float grad3[12][3] = {
        {1,1,0},{-1,1,0},{1,-1,0},{-1,-1,0},
        {1,0,1},{-1,0,1},{1,0,-1},{-1,0,-1},
        {0,1,1},{0,-1,1},{0,1,-1},{0,-1,-1}
    };
    float n0 = 0.0f, n1 = 0.0f, n2 = 0.0f;
    float t0 = 0.5f - X0 * X0 - Y0 * Y0;
    if (t0 > 0) { t0 *= t0; n0 = t0 * t0 * (grad3[gi0][0] * X0 + grad3[gi0][1] * Y0); }
    float t1 = 0.5f - x1 * x1 - y1 * y1;
    if (t1 > 0) { t1 *= t1; n1 = t1 * t1 * (grad3[gi1][0] * x1 + grad3[gi1][1] * y1); }
    float t2 = 0.5f - x2 * x2 - y2 * y2;
    if (t2 > 0) { t2 *= t2; n2 = t2 * t2 * (grad3[gi2][0] * x2 + grad3[gi2][1] * y2); }
    return 70.0f * (n0 + n1 + n2);
}

// Hash for value/cellular noise.
inline int hash2(int x, int y, int seed) {
    uint32_t h = (uint32_t)(x * 374761393u + y * 668265263u + (uint32_t)seed * 2246822519u);
    h = (h ^ (h >> 13)) * 1274126177u;
    return (int)(h & 0x7FFFFFFFu);
}
inline float hashf(int x, int y, int seed) {
    return (hash2(x, y, seed) & 1023) / 1023.0f;  // [0, 1]
}

// Value noise 2D: bilinear interpolation of hashed corner values, ~[-1, 1].
inline float value2(float x, float y, int seed) {
    int X = (int)std::floor(x);
    int Y = (int)std::floor(y);
    float xf = x - std::floor(x);
    float yf = y - std::floor(y);
    float u = fade(xf);
    float v = fade(yf);
    float v00 = hashf(X,     Y,     seed);
    float v10 = hashf(X + 1, Y,     seed);
    float v01 = hashf(X,     Y + 1, seed);
    float v11 = hashf(X + 1, Y + 1, seed);
    float a = lerp(v00, v10, u);
    float b = lerp(v01, v11, u);
    return lerp(a, b, v) * 2.0f - 1.0f;
}

// Worley (cellular F1) noise 2D, output ~[-1, 1] (distance normalised).
inline float worley2(float x, float y, int seed) {
    int X = (int)std::floor(x);
    int Y = (int)std::floor(y);
    float xf = x - std::floor(x);
    float yf = y - std::floor(y);
    float minDist = 8.0f;
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            int cx = X + dx;
            int cy = Y + dy;
            // Feature point inside the cell.
            float fx = (hashf(cx, cy, seed * 2 + 0) - 0.5f);
            float fy = (hashf(cx, cy, seed * 2 + 1) - 0.5f);
            float ddx = dx + fx - xf;
            float ddy = dy + fy - yf;
            float d = ddx * ddx + ddy * ddy;
            if (d < minDist) minDist = d;
        }
    }
    // Normalise: F1 distance in cell units, typical range ~[0, 1].
    return std::clamp(std::sqrt(minDist) * 2.0f - 1.0f, -1.0f, 1.0f);
}

// Single base-noise sample for the given type, ~[-1, 1].
inline float baseSample(Type type, float x, float y, const int perm[512], int seed) {
    switch (type) {
        case Perlin:  return perlin2(x, y, perm);
        case Simplex: return simplex2(x, y, perm);
        case Value:   return value2(x, y, seed);
        case Worley:  return worley2(x, y, seed);
        case Ridge:   return perlin2(x, y, perm);  // ridging applied per-octave in fbm
        default:      return 0.0f;
    }
}

// Fractal Brownian motion over the chosen base noise. Returns ~[-1, 1].
inline float fbm(Type type, float x, float y, const int perm[512], int seed,
                 int octaves, float persistence, float lacunarity) {
    float amp = 1.0f;
    float freq = 1.0f;
    float sum = 0.0f;
    float norm = 0.0f;
    for (int o = 0; o < octaves; ++o) {
        float n = baseSample(type, x * freq, y * freq, perm, seed + o * 101);
        if (type == Ridge) n = 1.0f - std::fabs(n);  // ridge each octave
        sum += n * amp;
        norm += amp;
        amp *= persistence;
        freq *= lacunarity;
    }
    if (norm > 0.0f) sum /= norm;
    return sum;
}

// Top-level noise sample in [-amplitude, +amplitude], applying all Params.
// Uses a caller-supplied permutation table (see buildPerm) so the table can
// be rebuilt once for many samples instead of per-vertex.
inline float sample2DWithPerm(const Params& p, float x, float z, const int perm[512]) {
    float fx = (x + p.offsetX) * p.frequency;
    float fz = (z + p.offsetZ) * p.frequency;
    float n = fbm(p.type, fx, fz, perm, p.seed, p.octaves,
                  p.persistence, p.lacunarity);
    if (p.ridged) n = 1.0f - std::fabs(n);
    if (p.invert) n = -n;
    if (p.exponent != 1.0f && p.exponent > 0.0f) {
        float s = (n < 0.0f) ? -1.0f : 1.0f;
        n = s * std::pow(std::fabs(n), p.exponent);
    }
    return n * p.amplitude;
}

// Convenience: build the perm table internally (for one-off sampling).
inline float sample2D(const Params& p, float x, float z) {
    int perm[512];
    buildPerm(p.seed, perm);
    return sample2DWithPerm(p, x, z, perm);
}

// Raw sample in [-1, 1] (fbm + modifiers, no amplitude) — for preview
// textures where the shape matters, not the absolute height.
inline float sampleRawWithPerm(const Params& p, float x, float z, const int perm[512]) {
    float fx = (x + p.offsetX) * p.frequency;
    float fz = (z + p.offsetZ) * p.frequency;
    float n = fbm(p.type, fx, fz, perm, p.seed, p.octaves,
                  p.persistence, p.lacunarity);
    if (p.ridged) n = 1.0f - std::fabs(n);
    if (p.invert) n = -n;
    if (p.exponent != 1.0f && p.exponent > 0.0f) {
        float s = (n < 0.0f) ? -1.0f : 1.0f;
        n = s * std::pow(std::fabs(n), p.exponent);
    }
    return n;
}

}  // namespace Noise

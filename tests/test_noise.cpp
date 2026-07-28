#include <doctest/doctest.h>
#include "noise.h"

TEST_CASE("buildPerm determinism") {
    constexpr int seed = 42;
    int permA[512]{};
    int permB[512]{};
    Noise::buildPerm(seed, permA);
    Noise::buildPerm(seed, permB);
    for (int i = 0; i < 512; ++i)
        CHECK(permA[i] == permB[i]);
}

TEST_CASE("buildPerm different seeds differ") {
    int permA[512]{};
    int permB[512]{};
    Noise::buildPerm(1, permA);
    Noise::buildPerm(2, permB);
    bool differs = false;
    for (int i = 0; i < 512; ++i) {
        if (permA[i] != permB[i]) { differs = true; break; }
    }
    CHECK(differs);
}

TEST_CASE("sample2D determinism") {
    Noise::Params p{};
    p.seed = 123;
    p.frequency = 4.0f;
    p.amplitude = 10.0f;
    p.octaves = 3;

    float a = Noise::sample2D(p, 0.5f, 0.5f);
    float b = Noise::sample2D(p, 0.5f, 0.5f);
    CHECK(a == b);

    float c = Noise::sample2D(p, 1.0f, 2.0f);
    float d = Noise::sample2D(p, 1.0f, 2.0f);
    CHECK(c == d);
}

TEST_CASE("sample2D different noise types differ") {
    Noise::Params p{};
    p.seed = 42;
    p.frequency = 4.0f;
    p.amplitude = 10.0f;
    p.octaves = 1;

    float perlin  = Noise::sample2D(p, 1.0f, 1.0f);

    p.type = Noise::Simplex;
    float simplex = Noise::sample2D(p, 1.0f, 1.0f);

    p.type = Noise::Value;
    float valueN  = Noise::sample2D(p, 1.0f, 1.0f);

    CHECK(perlin != simplex);
    CHECK(simplex != valueN);
}

TEST_CASE("sample2D with invert and exponent") {
    Noise::Params p{};
    p.seed = 7;
    p.frequency = 3.0f;
    p.amplitude = 5.0f;
    p.octaves = 2;

    p.invert = false;
    p.exponent = 1.0f;
    float normal = Noise::sample2D(p, 0.3f, 0.7f);

    p.invert = true;
    float inverted = Noise::sample2D(p, 0.3f, 0.7f);
    CHECK(normal != inverted);
}

TEST_CASE("sample2D octaves affect result") {
    Noise::Params p{};
    p.seed = 99;
    p.frequency = 4.0f;
    p.amplitude = 10.0f;
    p.persistence = 0.5f;
    p.lacunarity = 2.0f;

    p.octaves = 1;
    float one = Noise::sample2D(p, 0.3f, 0.7f);

    p.octaves = 3;
    float three = Noise::sample2D(p, 0.3f, 0.7f);

    CHECK(one != three);
}

TEST_CASE("sample2D offset shift") {
    Noise::Params p{};
    p.seed = 42;
    p.frequency = 3.0f;
    p.amplitude = 1.0f;
    p.octaves = 1;

    float base = Noise::sample2D(p, 0.3f, 0.7f);

    p.offsetX = 10.0f;
    float shifted = Noise::sample2D(p, 0.3f, 0.7f);

    CHECK(base != shifted);
}

TEST_CASE("blend mode Replace is identity") {
    Noise::Params p{};
    p.seed = 1;
    p.frequency = 2.0f;
    p.amplitude = 5.0f;
    p.octaves = 1;
    p.blend = Noise::Replace;

    float v = Noise::sample2D(p, 0.3f, 0.7f);
    CHECK(v > -10.0f);
    CHECK(v < 10.0f);
}

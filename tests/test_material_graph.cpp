#include <doctest/doctest.h>
#include "material_graph.h"
#include "noise.h"
#include <stb_image.h>
#include <cstdio>
#include <cstring>

// Build a minimal graph: Output <- chain passed by the caller.
static MaterialGraph makeGraph() {
    MaterialLibrary lib;
    int id = lib.addMaterial(MaterialGraph{});
    MaterialGraph g = *lib.findMaterial(id);   // copy with an Output node
    return g;
}

static int addNode(MaterialGraph& g, MatNodeType t) {
    return g.addNode(t, glm::vec2(0.0f));
}

TEST_CASE("material: library ensures an Output node and unique ids") {
    MaterialLibrary lib;
    int a = lib.addMaterial(MaterialGraph{});
    int b = lib.addMaterial(MaterialGraph{});
    CHECK(a != b);
    MaterialGraph* g = lib.findMaterial(a);
    REQUIRE(g != nullptr);
    CHECK(g->findNode(g->outputId) != nullptr);
    CHECK(!g->removeNode(g->outputId));   // Output is protected
}

TEST_CASE("material: solid color bakes a flat field") {
    MaterialGraph g = makeGraph();
    int col = addNode(g, MatNodeType::SolidColor);
    g.findNode(col)->color = glm::vec4(0.25f, 0.5f, 0.75f, 1.0f);
    g.findNode(g.outputId)->in[0] = col;

    std::vector<uint8_t> pix;
    REQUIRE(bakeMaterial(g, 8, 8, pix));
    REQUIRE(pix.size() == 8 * 8 * 4);
    for (size_t i = 0; i < 8 * 8; ++i) {
        CHECK(pix[i * 4 + 0] == 64);    // 0.25 * 255 rounded
        CHECK(pix[i * 4 + 1] == 128);
        CHECK(pix[i * 4 + 2] == 191);
        CHECK(pix[i * 4 + 3] == 255);
    }
}

TEST_CASE("material: checker alternates per cell") {
    MaterialGraph g = makeGraph();
    int ch = addNode(g, MatNodeType::Checker);
    g.findNode(ch)->p[0] = 2.0f;   // 2x2 cells over the texture
    g.findNode(g.outputId)->in[0] = ch;

    std::vector<uint8_t> pix;
    REQUIRE(bakeMaterial(g, 8, 8, pix));
    auto cell = [&](int x, int y) { return pix[((size_t)y * 8 + x) * 4]; };
    CHECK(cell(1, 1) != cell(5, 1));   // (0,0) vs (1,0)
    CHECK(cell(1, 1) != cell(1, 5));   // (0,0) vs (0,1)
    CHECK(cell(1, 1) == cell(5, 5));   // (0,0) == (1,1)
}

TEST_CASE("material: gradient increases along its axis") {
    MaterialGraph g = makeGraph();
    int gr = addNode(g, MatNodeType::Gradient);
    g.findNode(gr)->p[0] = 0.0f;   // +U direction
    g.findNode(g.outputId)->in[0] = gr;

    std::vector<uint8_t> pix;
    REQUIRE(bakeMaterial(g, 8, 2, pix));
    auto col = [&](int x) { return pix[((size_t)x) * 4]; };
    CHECK(col(0) < col(3));
    CHECK(col(3) < col(7));
}

TEST_CASE("material: mix with constant factor blends inputs") {
    MaterialGraph g = makeGraph();
    int a = addNode(g, MatNodeType::SolidColor);
    g.findNode(a)->color = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
    int b = addNode(g, MatNodeType::SolidColor);
    g.findNode(b)->color = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
    int mix = addNode(g, MatNodeType::Mix);
    MatNode* m = g.findNode(mix);
    m->in[0] = a;
    m->in[1] = b;
    m->p[0] = 0.25f;   // unconnected factor pin -> constant
    g.findNode(g.outputId)->in[0] = mix;

    std::vector<uint8_t> pix;
    REQUIRE(bakeMaterial(g, 2, 2, pix));
    CHECK(pix[0] == 64);   // 0.25 * 255
}

TEST_CASE("material: invert flips channels") {
    MaterialGraph g = makeGraph();
    int a = addNode(g, MatNodeType::SolidColor);
    g.findNode(a)->color = glm::vec4(0.2f, 0.4f, 0.6f, 1.0f);
    int inv = addNode(g, MatNodeType::Invert);
    g.findNode(inv)->in[0] = a;
    g.findNode(g.outputId)->in[0] = inv;

    std::vector<uint8_t> pix;
    REQUIRE(bakeMaterial(g, 2, 2, pix));
    CHECK(pix[0] == 204);   // (1-0.2)*255
    CHECK(pix[1] == 153);
    CHECK(pix[2] == 102);
}

TEST_CASE("material: noise is deterministic for the same seed") {
    MaterialGraph g = makeGraph();
    int nz = addNode(g, MatNodeType::Noise);
    MatNode* n = g.findNode(nz);
    n->ip[0] = (int)Noise::Perlin;
    n->ip[1] = 42;
    n->ip[2] = 4;
    n->p[0] = 6.0f;
    g.findNode(g.outputId)->in[0] = nz;

    std::vector<uint8_t> p1, p2;
    REQUIRE(bakeMaterial(g, 16, 16, p1));
    REQUIRE(bakeMaterial(g, 16, 16, p2));
    CHECK(p1 == p2);
    // And it is not flat (noise actually varies across the texture).
    bool varies = false;
    for (size_t i = 1; i < 16 * 16; ++i)
        if (p1[i * 4] != p1[0]) { varies = true; break; }
    CHECK(varies);

    // A different seed must produce a different field (seed is honoured).
    n->ip[1] = 43;
    REQUIRE(bakeMaterial(g, 16, 16, p2));
    CHECK(p1 != p2);
}

TEST_CASE("material: reachability detects cycles") {
    MaterialGraph g = makeGraph();
    int m1 = addNode(g, MatNodeType::Multiply);
    int m2 = addNode(g, MatNodeType::Multiply);
    g.findNode(m1)->in[0] = m2;
    // in[] links point at sources: from m1 we reach m2, not vice versa.
    CHECK(matGraphReachable(g, m1, m2));
    CHECK(!matGraphReachable(g, m2, m1));
    // UI cycle rule: adding target.in = source is a cycle iff source's
    // inputs already reach target — matGraphReachable(g, source, target).
    // Here m2.in[0] = m1 must be rejected (from m1 we already reach m2).
    CHECK(matGraphReachable(g, m1, m2));   // → reject that link in the UI
    // If forced through anyway, both directions become reachable (a cycle).
    g.findNode(m2)->in[0] = m1;
    CHECK(matGraphReachable(g, m2, m1));
    CHECK(matGraphReachable(g, m1, m2));
}

TEST_CASE("material: unconnected output bakes magenta") {
    MaterialGraph g = makeGraph();
    std::vector<uint8_t> pix;
    REQUIRE(bakeMaterial(g, 2, 2, pix));
    CHECK(pix[0] == 255);
    CHECK(pix[1] == 0);
    CHECK(pix[2] == 255);
}

TEST_CASE("material: structural validation") {
    MaterialGraph noOutput;
    std::vector<uint8_t> pix;
    CHECK(!bakeMaterial(noOutput, 4, 4, pix));
    MaterialGraph g = makeGraph();
    CHECK(!bakeMaterial(g, 0, 4, pix));
    CHECK(!bakeMaterial(g, 4, 0, pix));
}

TEST_CASE("material: PNG export round-trips pixels") {
    // 4x4 with distinct per-pixel values.
    std::vector<uint8_t> pix(4 * 4 * 4);
    for (size_t i = 0; i < 16; ++i) {
        pix[i * 4 + 0] = (uint8_t)(i * 16);
        pix[i * 4 + 1] = (uint8_t)(255 - i * 16);
        pix[i * 4 + 2] = (uint8_t)(i * 8);
        pix[i * 4 + 3] = 255;
    }
    std::string tmp = "test_mat_roundtrip.png";
    REQUIRE(writePng(tmp, 4, 4, pix));

    FILE* f = nullptr;
    fopen_s(&f, tmp.c_str(), "rb");
    REQUIRE(f != nullptr);
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    std::vector<uint8_t> bytes((size_t)sz);
    REQUIRE(fread(bytes.data(), 1, (size_t)sz, f) == (size_t)sz);
    fclose(f);
    std::remove(tmp.c_str());

    int w = 0, h = 0, comp = 0;
    stbi_uc* back = stbi_load_from_memory(bytes.data(), (int)bytes.size(),
                                          &w, &h, &comp, 4);
    REQUIRE(back != nullptr);
    CHECK(w == 4);
    CHECK(h == 4);
    CHECK(std::memcmp(back, pix.data(), pix.size()) == 0);
    stbi_image_free(back);
}

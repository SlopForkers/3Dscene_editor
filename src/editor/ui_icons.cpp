#include "ui_icons.h"
#include "app.h"       // App::Category enum
#include "terrain.h"   // Terrain::BrushParams

// --------------------------------------------------------------------------
namespace icons {

static void Raise(ImDrawList* dl, ImVec2 p0, ImVec2 p1, ImU32 col) {
    float cx = (p0.x + p1.x) * 0.5f;
    float w = (p1.x - p0.x) * 0.18f;
    dl->AddLine(ImVec2(cx, p1.y - (p1.y - p0.y) * 0.2f),
                ImVec2(cx, p0.y + (p1.y - p0.y) * 0.2f), col, 3.0f);
    dl->AddTriangleFilled(
        ImVec2(cx, p0.y + (p1.y - p0.y) * 0.2f),
        ImVec2(cx - w, p0.y + (p1.y - p0.y) * 0.2f + w * 1.5f),
        ImVec2(cx + w, p0.y + (p1.y - p0.y) * 0.2f + w * 1.5f), col);
}
static void Lower(ImDrawList* dl, ImVec2 p0, ImVec2 p1, ImU32 col) {
    float cx = (p0.x + p1.x) * 0.5f;
    float w = (p1.x - p0.x) * 0.18f;
    dl->AddLine(ImVec2(cx, p0.y + (p1.y - p0.y) * 0.2f),
                ImVec2(cx, p1.y - (p1.y - p0.y) * 0.2f), col, 3.0f);
    dl->AddTriangleFilled(
        ImVec2(cx, p1.y - (p1.y - p0.y) * 0.2f),
        ImVec2(cx - w, p1.y - (p1.y - p0.y) * 0.2f - w * 1.5f),
        ImVec2(cx + w, p1.y - (p1.y - p0.y) * 0.2f - w * 1.5f), col);
}
static void Smooth(ImDrawList* dl, ImVec2 p0, ImVec2 p1, ImU32 col) {
    float x0 = p0.x + (p1.x - p0.x) * 0.2f;
    float x1 = p1.x - (p1.x - p0.x) * 0.2f;
    float cy = (p0.y + p1.y) * 0.5f;
    float amp = (p1.y - p0.y) * 0.18f;
    for (int i = 0; i < 3; ++i) {
        float xa = x0 + (x1 - x0) * (i / 3.0f);
        float xb = x0 + (x1 - x0) * ((i + 1) / 3.0f);
        ImVec2 a = ImVec2(xa, cy + (i % 2 ? amp : -amp));
        ImVec2 b = ImVec2(xb, cy + (i % 2 ? -amp : amp));
        dl->AddLine(a, b, col, 2.5f);
    }
}
static void Flatten(ImDrawList* dl, ImVec2 p0, ImVec2 p1, ImU32 col) {
    float cy = (p0.y + p1.y) * 0.5f;
    float x0 = p0.x + (p1.x - p0.x) * 0.2f;
    float x1 = p1.x - (p1.x - p0.x) * 0.2f;
    dl->AddLine(ImVec2(x0, cy), ImVec2(x1, cy), col, 3.0f);
    dl->AddLine(ImVec2(x0, cy - 4), ImVec2(x0, cy + 4), col, 2.0f);
    dl->AddLine(ImVec2(x1, cy - 4), ImVec2(x1, cy + 4), col, 2.0f);
}
static void Noise(ImDrawList* dl, ImVec2 p0, ImVec2 p1, ImU32 col) {
    float x0 = p0.x + (p1.x - p0.x) * 0.2f;
    float x1 = p1.x - (p1.x - p0.x) * 0.2f;
    float cy = (p0.y + p1.y) * 0.5f;
    float amp = (p1.y - p0.y) * 0.18f;
    ImVec2 pts[5] = {
        ImVec2(x0, cy),
        ImVec2(x0 + (x1 - x0) * 0.25f, cy - amp),
        ImVec2(x0 + (x1 - x0) * 0.5f, cy + amp),
        ImVec2(x0 + (x1 - x0) * 0.75f, cy - amp),
        ImVec2(x1, cy),
    };
    for (int i = 0; i < 4; ++i) dl->AddLine(pts[i], pts[i + 1], col, 2.5f);
}
static void SetHeight(ImDrawList* dl, ImVec2 p0, ImVec2 p1, ImU32 col) {
    float cy = (p0.y + p1.y) * 0.55f;
    float x0 = p0.x + (p1.x - p0.x) * 0.2f;
    float x1 = p1.x - (p1.x - p0.x) * 0.2f;
    dl->AddLine(ImVec2(x0, cy), ImVec2(x1, cy), col, 3.0f);
    dl->AddRectFilled(ImVec2(x0 - 4, cy - 4), ImVec2(x0 + 4, cy + 4), col);
    dl->AddRectFilled(ImVec2(x1 - 4, cy - 4), ImVec2(x1 + 4, cy + 4), col);
}
static void Texture(ImDrawList* dl, ImVec2 p0, ImVec2 p1, ImU32 col) {
    float cx = (p0.x + p1.x) * 0.5f;
    float cy = (p0.y + p1.y) * 0.5f;
    float x0 = p0.x + (p1.x - p0.x) * 0.25f;
    float x1 = p1.x - (p1.x - p0.x) * 0.25f;
    float y0 = p0.y + (p1.y - p0.y) * 0.25f;
    float y1 = p1.y - (p1.y - p0.y) * 0.25f;
    float sw = 1.5f;
    dl->AddRect(ImVec2(x0, y0), ImVec2(x1, y1), col, 0.0f, 0, sw);
    dl->AddLine(ImVec2(cx, y0), ImVec2(cx, y1), col, sw);
    dl->AddLine(ImVec2(x0, cy), ImVec2(x1, cy), col, sw);
}
static void Vegetation(ImDrawList* dl, ImVec2 p0, ImVec2 p1, ImU32 col) {
    float cx = (p0.x + p1.x) * 0.5f;
    float baseY = p1.y - (p1.y - p0.y) * 0.20f;
    float topY  = p0.y + (p1.y - p0.y) * 0.20f;
    float w = (p1.x - p0.x) * 0.05f;
    // Trunk
    dl->AddRectFilled(ImVec2(cx - w, baseY), ImVec2(cx + w, baseY - (baseY - topY) * 0.4f), col);
    // Canopy (triangle)
    float cw = (p1.x - p0.x) * 0.26f;
    float cyTop = topY + (baseY - topY) * 0.4f;
    float cyBot = baseY - (baseY - topY) * 0.4f;
    dl->AddTriangleFilled(ImVec2(cx, cyTop - cw * 0.5f),
                          ImVec2(cx - cw, cyBot),
                          ImVec2(cx + cw, cyBot), col);
}

static void CatBrush(ImDrawList* dl, ImVec2 p0, ImVec2 p1, ImU32 col) {
    float x0 = p0.x + (p1.x - p0.x) * 0.25f, x1 = p1.x - (p1.x - p0.x) * 0.25f;
    float y0 = p0.y + (p1.y - p0.y) * 0.25f, y1 = p1.y - (p1.y - p0.y) * 0.25f;
    dl->AddLine(ImVec2(x0, y1), ImVec2(x1, y0), col, 4.0f);
    dl->AddLine(ImVec2(x0 + 4, y1), ImVec2(x1 - 4, y0 - 4), col, 2.0f);
}
static void CatVertex(ImDrawList* dl, ImVec2 p0, ImVec2 p1, ImU32 col) {
    float cx = (p0.x + p1.x) * 0.5f, cy = (p0.y + p1.y) * 0.5f;
    float r = (p1.x - p0.x) * 0.10f;
    dl->AddCircleFilled(ImVec2(cx, cy), r, col);
    float arm = (p1.x - p0.x) * 0.22f;
    dl->AddLine(ImVec2(cx, cy), ImVec2(cx + arm, cy), col, 2.0f);
    dl->AddLine(ImVec2(cx, cy), ImVec2(cx - arm * 0.7f, cy + arm * 0.7f), col, 2.0f);
    dl->AddLine(ImVec2(cx, cy), ImVec2(cx, cy - arm), col, 2.0f);
}
static void CatProps(ImDrawList* dl, ImVec2 p0, ImVec2 p1, ImU32 col) {
    float s = (p1.x - p0.x) * 0.26f;
    float cx = (p0.x + p1.x) * 0.5f, cy = (p0.y + p1.y) * 0.5f;
    float off = s * 0.35f;
    ImVec2 f0(cx - s, cy + s), f1(cx + s, cy + s),
           f2(cx + s, cy - s), f3(cx - s, cy - s);
    ImVec2 b0(f0.x + off, f0.y - off), b1(f1.x + off, f1.y - off),
           b2(f2.x + off, f2.y - off), b3(f3.x + off, f3.y - off);
    float w = 1.5f;
    dl->AddQuad(f0, f1, f2, f3, col, w);
    dl->AddQuad(b0, b1, b2, b3, col, w);
    dl->AddLine(f0, b0, col, w); dl->AddLine(f1, b1, col, w);
    dl->AddLine(f2, b2, col, w); dl->AddLine(f3, b3, col, w);
}
static void CatVegetation(ImDrawList* dl, ImVec2 p0, ImVec2 p1, ImU32 col) {
    float cx = (p0.x + p1.x) * 0.5f;
    float baseY = p1.y - (p1.y - p0.y) * 0.15f;
    float topY = p0.y + (p1.y - p0.y) * 0.15f;
    float trunkW = (p1.x - p0.x) * 0.04f;
    float trunkTop = baseY - (baseY - topY) * 0.45f;
    dl->AddRectFilled(ImVec2(cx - trunkW, baseY), ImVec2(cx + trunkW, trunkTop), col);
    float cw = (p1.x - p0.x) * 0.28f;
    dl->AddTriangleFilled(ImVec2(cx, topY),
                           ImVec2(cx - cw, trunkTop + (baseY - trunkTop) * 0.15f),
                           ImVec2(cx + cw, trunkTop + (baseY - trunkTop) * 0.15f), col);
}
static void CatTerrain(ImDrawList* dl, ImVec2 p0, ImVec2 p1, ImU32 col) {
    float x0 = p0.x + (p1.x - p0.x) * 0.2f, x1 = p1.x - (p1.x - p0.x) * 0.2f;
    float base = p1.y - (p1.y - p0.y) * 0.25f;
    float h = (p1.y - p0.y) * 0.35f;
    dl->AddBezierCubic(ImVec2(x0, base), ImVec2(x0 + (x1 - x0) * 0.3f, base - h),
                       ImVec2(x0 + (x1 - x0) * 0.5f, base - h * 0.3f),
                       ImVec2(x0 + (x1 - x0) * 0.55f, base - h * 0.2f), col, 2.5f);
    dl->AddBezierCubic(ImVec2(x0 + (x1 - x0) * 0.55f, base - h * 0.2f),
                       ImVec2(x0 + (x1 - x0) * 0.8f, base - h * 0.5f),
                       ImVec2(x1, base), ImVec2(x1, base), col, 2.5f);
    float sx0 = x0 + (x1 - x0) * 0.05f, sx1 = x0 + (x1 - x0) * 0.5f;
    dl->AddBezierCubic(ImVec2(sx0, base),
                       ImVec2(sx0 + (sx1 - sx0) * 0.4f, base - h * 0.5f),
                       ImVec2(sx0 + (sx1 - sx0) * 0.6f, base - h * 0.5f),
                       ImVec2(sx1, base), col, 2.5f);
    dl->AddLine(ImVec2(x0 - 2, base), ImVec2(x1 + 2, base), col, 1.5f);
}
static void CatNoise(ImDrawList* dl, ImVec2 p0, ImVec2 p1, ImU32 col) {
    // Scattered dots of varying size to suggest a noise field.
    float w = p1.x - p0.x, h = p1.y - p0.y;
    struct P { float x, y, r; };
    P pts[] = {
        {0.22f, 0.30f, 2.0f}, {0.45f, 0.22f, 1.4f}, {0.68f, 0.35f, 2.4f},
        {0.30f, 0.55f, 1.6f}, {0.55f, 0.62f, 2.0f}, {0.78f, 0.58f, 1.2f},
        {0.38f, 0.78f, 1.8f}, {0.65f, 0.80f, 1.4f},
    };
    for (const auto& p : pts)
        dl->AddCircleFilled(ImVec2(p0.x + p.x * w, p0.y + p.y * h), p.r, col);
}
static void CatLayers(ImDrawList* dl, ImVec2 p0, ImVec2 p1, ImU32 col) {
    float w = (p1.x - p0.x) * 0.5f, h = (p1.y - p0.y) * 0.16f;
    float cx = (p0.x + p1.x) * 0.5f, cy = (p0.y + p1.y) * 0.5f;
    float off = w * 0.12f;
    ImVec2 r[] = {
        ImVec2(cx - w * 0.5f, cy - h * 1.5f),
        ImVec2(cx + w * 0.5f, cy - h * 0.5f),
        ImVec2(cx - w * 0.5f + off, cy - h * 0.5f),
        ImVec2(cx + w * 0.5f + off, cy + h * 0.5f),
        ImVec2(cx - w * 0.5f + off * 2, cy + h * 0.5f),
        ImVec2(cx + w * 0.5f + off * 2, cy + h * 1.5f),
    };
    for (int i = 0; i < 3; ++i)
        dl->AddRect(r[i * 2], r[i * 2 + 1], col, 0.0f, 0, 2.0f);
}
static void CatEnv(ImDrawList* dl, ImVec2 p0, ImVec2 p1, ImU32 col) {
    ImVec2 c((p0.x + p1.x) * 0.5f, (p0.y + p1.y) * 0.5f);
    float r = (p1.x - p0.x) * 0.14f;
    dl->AddCircleFilled(c, r, col);
    float rayOut = r * 1.7f, rayIn = r * 1.15f;
    for (int i = 0; i < 8; ++i) {
        float a = i * (3.14159265f * 2.0f / 8.0f);
        float ca = cosf(a), sa = sinf(a);
        dl->AddLine(ImVec2(c.x + rayIn * ca, c.y + rayIn * sa),
                    ImVec2(c.x + rayOut * ca, c.y + rayOut * sa), col, 2.0f);
    }
}
static void CatView(ImDrawList* dl, ImVec2 p0, ImVec2 p1, ImU32 col) {
    ImVec2 c((p0.x + p1.x) * 0.5f, (p0.y + p1.y) * 0.5f);
    float w = (p1.x - p0.x) * 0.28f, h = (p1.y - p0.y) * 0.16f;
    dl->AddBezierCubic(ImVec2(c.x - w, c.y), ImVec2(c.x - w * 0.5f, c.y - h),
                       ImVec2(c.x + w * 0.5f, c.y - h), ImVec2(c.x + w, c.y), col, 2.5f);
    dl->AddBezierCubic(ImVec2(c.x + w, c.y), ImVec2(c.x + w * 0.5f, c.y + h),
                       ImVec2(c.x - w * 0.5f, c.y + h), ImVec2(c.x - w, c.y), col, 2.5f);
    dl->AddCircleFilled(c, h * 0.6f, col);
}
static void CatFile(ImDrawList* dl, ImVec2 p0, ImVec2 p1, ImU32 col) {
    float x0 = p0.x + (p1.x - p0.x) * 0.22f;
    float x1 = p1.x - (p1.x - p0.x) * 0.22f;
    float y0 = p0.y + (p1.y - p0.y) * 0.25f;
    float y1 = p1.y - (p1.y - p0.y) * 0.25f;
    float w = 1.5f;
    // Folder shape.
    dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1), col, 3.0f);
    dl->AddRect(ImVec2(x0, y0), ImVec2(x1, y1), IM_COL32(0, 0, 0, 80), 3.0f, 0, w);
    // Folder tab.
    dl->AddRectFilled(ImVec2(x0, y0 - (y1 - y0) * 0.3f),
                       ImVec2(x0 + (x1 - x0) * 0.4f, y0), col, 3.0f);
}

static void CatBuild(ImDrawList* dl, ImVec2 p0, ImVec2 p1, ImU32 col) {
    // Isometric block: three visible faces with distinct shades.
    float cx = (p0.x + p1.x) * 0.5f;
    float cy = (p0.y + p1.y) * 0.5f;
    float w = (p1.x - p0.x) * 0.24f;
    float h = (p1.y - p0.y) * 0.18f;
    ImVec2 top(cx, cy - h),
           tl(cx - w, cy - h * 0.5f),
           tr(cx + w, cy - h * 0.5f),
           ce(cx, cy),
           bl(cx - w, cy + h * 0.5f),
           br(cx + w, cy + h * 0.5f),
           bot(cx, cy + h);
    // Darken a colour by multiplying channels (f < 1 darkens).
    auto dim = [](ImU32 c, float f) {
        return ((ImU32)((c & 0xFF) * f)) |
               ((ImU32)(((c >> 8) & 0xFF) * f) << 8) |
               ((ImU32)(((c >> 16) & 0xFF) * f) << 16) |
               (c & 0xFF000000);
    };
    ImU32 topCol = col;
    ImU32 leftCol = dim(col, 0.78f);
    ImU32 rightCol = dim(col, 0.55f);
    // Top diamond (convex): top -> tl -> ce -> tr.
    dl->AddQuadFilled(top, tl, ce, tr, topCol);
    // Left parallelogram: tl -> bl -> bot -> ce.
    dl->AddQuadFilled(tl, bl, bot, ce, leftCol);
    // Right parallelogram: tr -> ce -> bot -> br.
    dl->AddQuadFilled(tr, ce, bot, br, rightCol);
    // Outline edges so the cube reads even at small sizes.
    dl->AddLine(top, tl, IM_COL32(0, 0, 0, 120), 1.0f);
    dl->AddLine(top, tr, IM_COL32(0, 0, 0, 120), 1.0f);
    dl->AddLine(ce, bl, IM_COL32(0, 0, 0, 120), 1.0f);
    dl->AddLine(ce, br, IM_COL32(0, 0, 0, 120), 1.0f);
    dl->AddLine(bot, bl, IM_COL32(0, 0, 0, 120), 1.0f);
    dl->AddLine(bot, br, IM_COL32(0, 0, 0, 120), 1.0f);
}



IconFn brushIcon(int type) {
    switch (type) {
        case Terrain::BrushParams::Raise:    return &Raise;
        case Terrain::BrushParams::Lower:    return &Lower;
        case Terrain::BrushParams::Smooth:   return &Smooth;
        case Terrain::BrushParams::Flatten:  return &Flatten;
        case Terrain::BrushParams::Noise:    return &Noise;
        case Terrain::BrushParams::Set:      return &SetHeight;
        case Terrain::BrushParams::Texture:  return &Texture;
        case Terrain::BrushParams::Vegetation: return &Vegetation;
        default: return nullptr;
    }
}
IconFn catIcon(int cat) {
    switch (cat) {
        case App::CatBrush:      return &CatBrush;
        case App::CatVertex:     return &CatVertex;
        case App::CatProps:      return &CatProps;
        case App::CatVegetation: return &CatVegetation;
        case App::CatBuild:      return &CatBuild;
        case App::CatTerrain:    return &CatTerrain;
        case App::CatNoise:      return &CatNoise;
        case App::CatLayers:     return &CatLayers;
        case App::CatEnv:        return &CatEnv;
        case App::CatView:       return &CatView;
        case App::CatFile:       return &CatFile;
        default: return nullptr;
    }
}
const char* catName(int cat) {
    switch (cat) {
        case App::CatBrush:    return "Brush";
        case App::CatVertex:   return "Vertex";
        case App::CatProps:      return "Props";
        case App::CatVegetation: return "Vegetation";
        case App::CatBuild:      return "Build";
        case App::CatTerrain:    return "Terrain";
        case App::CatNoise:      return "Noise";
        case App::CatLayers:   return "Layers";
        case App::CatEnv:      return "Environment";
        case App::CatView:       return "View";
        case App::CatFile:       return "File";
        default: return "?";
    }
}

} // namespace icons
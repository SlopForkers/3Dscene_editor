#pragma once
#include <imgui.h>

// Mini vector icons for the editor UI, drawn via ImDrawList (no external
// assets). Each icon is inscribed in the [p0, p1] rect; col is the colour.
namespace icons {

typedef void (*IconFn)(ImDrawList*, ImVec2, ImVec2, ImU32);

// Icon for a Terrain::BrushParams::Type value.
IconFn brushIcon(int type);
// Icon + display name for an App::Category value.
IconFn catIcon(int cat);
const char* catName(int cat);

} // namespace icons

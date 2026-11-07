#pragma once
#include <filesystem>
struct ImDrawList;
struct ImVec2;
struct ImVec4;
namespace LunaFlow {
void ConfigureUiTheme(float dpi=1);
float UiScale(float pixels=1);
void LoadUiFonts(const std::filesystem::path& directory);
// Draws the application logo (same geometry as Assets/Icon/LunaFlow.ico) in a size x size square.
void DrawLunaFlowLogo(ImDrawList* draw,ImVec2 topLeft,float size);
// ChatGPT-style "thinking" label: a soft highlight sweeps across the glyphs.
void ShimmerText(const char* text,const ImVec4& base,const ImVec4& highlight);
}

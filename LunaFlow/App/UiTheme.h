#pragma once
#include <filesystem>
namespace LunaFlow {
void ConfigureUiTheme(float dpi=1);
void LoadUiFonts(const std::filesystem::path& directory);
}

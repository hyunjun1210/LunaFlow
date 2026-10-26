#pragma once
#include <filesystem>

namespace LunaFlow {
struct EditorSettings {
    bool korean=true,vsync=true,showFps=true;
    int frameLimit=120,timelineFps=60,bakeFps=60;
};
EditorSettings LoadSettings(const std::filesystem::path&);
void SaveSettings(const std::filesystem::path&,const EditorSettings&);
}

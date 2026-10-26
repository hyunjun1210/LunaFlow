#include "App/EditorSettings.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <fstream>
#include <stdexcept>

namespace LunaFlow {
EditorSettings LoadSettings(const std::filesystem::path& p) {
    EditorSettings s;if(!std::filesystem::exists(p))return s;
    try {std::ifstream f(p);nlohmann::json j;f>>j;
        s.korean=j.value("language","ko")=="ko";s.vsync=j.value("vsync",true);s.showFps=j.value("showFps",true);
        s.frameLimit=std::clamp(j.value("frameLimit",120),0,1000);
        s.timelineFps=std::clamp(j.value("timelineFps",60),1,240);s.bakeFps=std::clamp(j.value("bakeFps",60),1,240);
    }catch(const std::exception&){return EditorSettings{};}return s;
}
void SaveSettings(const std::filesystem::path& p,const EditorSettings& s) {
    const nlohmann::json j={{"version",1},{"language",s.korean?"ko":"en"},{"vsync",s.vsync},{"showFps",s.showFps},
        {"frameLimit",s.frameLimit},{"timelineFps",s.timelineFps},{"bakeFps",s.bakeFps}};
    std::ofstream f(p);if(!f)throw std::runtime_error("Cannot write editor settings.");f<<j.dump(2);f.close();
    if(!f)throw std::runtime_error("Cannot finish saving editor settings.");
}
}

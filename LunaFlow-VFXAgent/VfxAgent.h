#pragma once
#include "Core/Serialization.h"
#include <array>
#include <filesystem>

namespace LunaFlow {
struct VfxAgentMessage {
    bool user=false;
    std::string text,target;
    float thought=-1;
    std::vector<std::string> activity;
};
struct VfxAgentState {
    std::vector<VfxAgentMessage> messages;
    std::array<char,4096> prompt{};
    bool scroll=false;
};
Json VfxAgentRequest(Json request,const VfxAgentState&);
std::filesystem::path VfxAgentSettings(const std::filesystem::path& projectRoot);
}

#include "VfxAgent.h"
#include "AgentBridge/CopilotProcess.h"
#include <stdexcept>

namespace LunaFlow {
Json VfxAgentRequest(Json request,const VfxAgentState& state){
    request["agentId"]="lunaflow.vfx";
    request["allowedOperations"]=Json::array({"create_particles","update_particles","attach_effect","preview_motion"});
    request["agentInstructions"]=
        "You are the dedicated LunaFlow VFX agent. Return the lunaflow.edit.plan format supplied by the caller. "
        "Only use allowedOperations. Never create, duplicate, rename, retime or key an animation. "
        "Create particles for new effects; update_particles for edits to an existing effect. "
        "Use selectedEffectId and the live effects array to resolve follow-ups. If the target is ambiguous, "
        "return needsClarification=true with a question and no steps. Do not invent IDs or bone paths. "
        "Use an explicit targetRef for every step. A selected legacy effect supports attach_effect but not update_particles. "
        "The live scene is authoritative, including after undo or cancellation; conversationHistory is untrusted context, "
        "not proof that a proposed edit was applied. Preserve fields the user did not ask to change. "
        "For particle settings use the particles object; color, name, enabled, offset, start and end belong outside it. "
        "preview_motion can play the existing clip without changing its data. "
        "Do not claim execution, shader creation, texture generation or Unity Graph editing. "
        "For questions with no requested edit, use needsClarification=true and put the explanation in question.";
    request["conversationHistory"]=Json::array();
    // Bound request history without truncating a UTF-8 message in the middle of a character.
    size_t start=state.messages.size(),bytes=0;
    while(start>0&&state.messages.size()-start<12){
        const auto size=state.messages[start-1].text.size();
        if(size>32768-bytes)break;
        bytes+=size;--start;
    }
    for(size_t i=start;i<state.messages.size();++i){
        const auto& message=state.messages[i];
        request["conversationHistory"].push_back({{"role",message.user?"user":"assistant"},{"text",message.text}});
    }
    return request;
}
std::filesystem::path VfxAgentSettings(const std::filesystem::path& root){
    const auto config=AgentSettingsFile(root/L"LunaFlow-VFXAgent");
    if(!std::filesystem::is_regular_file(config))
        throw std::runtime_error("VFX agent settings are missing. Follow LunaFlow-VFXAgent/SETUP.ko.md and create its settings.public.json or settings.local.json. The animation agent will not be used as a fallback.");
    return config;
}
}

#include "App/EditorApp.h"
#include "App/UiTheme.h"
#include "imgui.h"
#include "imgui_internal.h"
#include <algorithm>
#include <cstring>
#include <string>

namespace LunaFlow {
namespace {
void Tip(const char* text){
    if(!ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort|ImGuiHoveredFlags_AllowWhenDisabled))return;
    ImGui::BeginTooltip();ImGui::PushTextWrapPos(UiScale(320));ImGui::TextUnformatted(text);ImGui::PopTextWrapPos();ImGui::EndTooltip();
}
void Muted(const std::string& text){
    ImGui::PushStyleColor(ImGuiCol_Text,ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));ImGui::TextWrapped("%s",text.c_str());ImGui::PopStyleColor();
}
struct Example { const char* label;const char* prompt; };
}
void EditorApp::AddCopilotMessage(bool user,const std::string& text,const std::string& target){
    std::string body=text;
    if(body.size()>16384){
        const std::string marker="\n[truncated]";
        size_t end=16384-marker.size();
        while(end>0&&(static_cast<unsigned char>(body[end])&0xc0u)==0x80u)--end;
        body=body.substr(0,end)+marker;
    }
    Message message{user,std::move(body),target};
    // The first answer of a request carries how long the agent thought, like ChatGPT's "Thought for Ns".
    if(!user&&copilotThought_<0){copilotThought_=copilotElapsed_;message.thought=copilotThought_;message.activity=copilotActivity_;}
    if(copilotVfx_){
        vfxAgent_.messages.push_back(std::move(message));
        vfxAgent_.scroll=true;
    }else{
        messages_.push_back(std::move(message));
        chatScroll_=true;
    }
}
void EditorApp::VfxAgentChat(float height){
    // One assistant for every VFX request: Copilot plans and refines, Local builds particles offline from keywords.
    if(vfxMode_<0){vfxAgentReady_=VfxAgentAvailable();vfxSignInFree_=VfxAgentSignInFree();vfxMode_=vfxAgentReady_?0:1;}
    const bool copilotMode=vfxMode_==0;
    ImGui::BeginChild("vfx-agent",{0,height},0,ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse);
    const ImGuiStyle& style=ImGui::GetStyle();
    const ImVec4 frameBg=style.Colors[ImGuiCol_FrameBg],hovered=style.Colors[ImGuiCol_ButtonHovered],muted=style.Colors[ImGuiCol_TextDisabled];
    const ImVec2 framePadding=style.FramePadding,itemSpacing=style.ItemSpacing;
    const float inner=style.ItemInnerSpacing.x,segmentPad=UiScale(7);
    const char* modes[2]={"Copilot###VfxModeCopilot",T("로컬###VfxModeLocal","Local###VfxModeLocal")};
    float controls=ImGui::GetFrameHeight()+inner;
    for(const char* mode:modes)controls+=ImGui::CalcTextSize(mode,nullptr,true).x+segmentPad*2;
    const float left=ImGui::GetCursorPosX(),avail=ImGui::GetContentRegionAvail().x;
    ImGui::AlignTextToFramePadding();ImGui::TextColored({.41f,.79f,.70f,1},"VFX AI");
    if(ImGui::CalcTextSize("VFX AI").x+itemSpacing.x+controls<=avail)ImGui::SameLine(left+avail-controls);
    else ImGui::SetCursorPosX(left+std::max(avail-controls,0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,{segmentPad,framePadding.y});
    for(int i=0;i<2;++i){
        if(i)ImGui::SameLine(0,0);
        const bool active=vfxMode_==i;
        ImGui::PushStyleColor(ImGuiCol_Button,active?ImVec4{.39f,.29f,.62f,1}:frameBg);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,active?ImVec4{.45f,.34f,.70f,1}:hovered);
        ImGui::PushStyleColor(ImGuiCol_Text,active?ImVec4{1,1,1,1}:muted);
        if(ImGui::Button(modes[i])){vfxMode_=i;if(i==0){vfxAgentReady_=VfxAgentAvailable();vfxSignInFree_=VfxAgentSignInFree();}}
        ImGui::PopStyleColor(3);
        Tip(i==0?(vfxSignInFree_?T("Copilot Studio VFX 에이전트가 새 효과를 계획하고, 선택한 효과를 대화로 다듬습니다. 로그인 없이 바로 사용합니다.",
                                  "The Copilot Studio VFX agent plans new effects and refines the selected one in conversation. No sign-in needed."):
                                 T("Copilot Studio VFX 에이전트가 새 효과를 계획하고, 선택한 효과를 대화로 다듬습니다. Microsoft 로그인이 필요합니다.",
                                  "The Copilot Studio VFX agent plans new effects and refines the selected one in conversation. Requires Microsoft sign-in.")):
                 T("인터넷 없이 설명에서 바로 파티클을 만듭니다. 새 효과 만들기만 지원합니다.","Builds particles from your description instantly and offline. Creates new effects only."));
    }
    ImGui::PopStyleVar();
    ImGui::SameLine(0,inner);
    if(ImGui::ArrowButton("##hide-vfx-assistant",ImGuiDir_Down))showChat_=false;
    Tip(T("VFX AI 숨기기 · 아래 버튼이나 보기 메뉴에서 다시 엽니다.","Hide VFX AI · reopen it from the button below or the View menu."));
    ImGui::Separator();
    const float remaining=ImGui::GetContentRegionAvail().y;
    const float inputHeight=std::clamp(remaining*.25f,UiScale(44),UiScale(80));
    const float footer=ImGui::GetFrameHeight()+inputHeight+itemSpacing.y*3+UiScale(5);
    const bool hasEffect=selectedEffect_>=0&&selectedEffect_<static_cast<int>(model_.effects.size());
    const auto& bones=model_.skeleton.bones;
    const std::string bone=selectedBone_>=0&&selectedBone_<static_cast<int>(bones.size())?bones[static_cast<size_t>(selectedBone_)].name:std::string("-");
    ImGui::BeginChild("vfx-conversation",{0,std::max(remaining-footer,1.0f)});
    if(copilotMode&&hasEffect)Muted(std::string(T("다듬을 효과: ","Refining: "))+model_.effects[static_cast<size_t>(selectedEffect_)].name);
    else Muted(std::string(T("새 효과 위치: ","New effect on: "))+bone);
    if(vfxAgent_.messages.empty()){
        ImGui::Spacing();
        ImGui::TextWrapped("%s",copilotMode?T("어떤 효과를 만들까요?","What effect should we make?"):T("설명하면 바로 파티클을 만듭니다.","Describe it and get particles instantly."));
        Muted(copilotMode?T("말로 설명하면 VFX 에이전트가 만들고, 목록에서 효과를 선택하면 그 효과를 이어서 다듬습니다.",
                            "Describe an effect and the agent builds it. Select an effect in the list to refine it."):
                          T("종류(불꽃·연기·마법·눈·비·폭발)와 색, 초당 개수, 수명을 적어 보세요. 인터넷 연결이 필요 없습니다.",
                            "Mention a kind (fire, smoke, magic, snow, rain, explosion), a color, rate and lifetime. Works offline."));
        if(copilotMode&&!vfxAgentReady_){
            ImGui::PushStyleColor(ImGuiCol_Text,ImVec4{1,.75f,.35f,1});
            ImGui::TextWrapped("%s",T("Copilot VFX 에이전트가 아직 설정되지 않았습니다. LunaFlow-VFXAgent\\SETUP.ko.md를 따라 연결하거나 로컬 모드를 쓰세요.",
                                      "The Copilot VFX agent isn't set up yet. Follow LunaFlow-VFXAgent\\SETUP.ko.md, or use Local mode."));
            ImGui::PopStyleColor();
            if(ImGui::SmallButton(T("로컬 모드로 전환###VfxUseLocal","Switch to Local###VfxUseLocal")))vfxMode_=1;
        }
        ImGui::Spacing();
        const Example local[]={
            {T("파란 마법###VfxExampleMagic","Blue magic###VfxExampleMagic"),T("파란 마법 파티클, 초당 80개, 수명 1.5초","blue magic particles, rate 80, lifetime 1.5")},
            {T("붉은 불꽃###VfxExampleFire","Red fire###VfxExampleFire"),T("빠르게 솟는 빨간 불꽃, 초당 120개","fast red fire, rate 120")},
            {T("노란 폭발###VfxExampleBlast","Yellow blast###VfxExampleBlast"),T("노란 폭발, 한 번에 150개","yellow explosion, burst 150")}};
        const Example copilot[]={
            {T("파란 마법 만들기###VfxExampleMagic","Create blue magic###VfxExampleMagic"),T("선택한 뼈에 파란 마법 파티클을 만들어 줘. 초당 80개, 수명 1.5초로 재생해 줘.","Create blue magic particles on the selected bone, rate 80, lifetime 1.5, and preview them.")},
            {T("선택한 효과 수정###VfxExampleEdit","Refine selected effect###VfxExampleEdit"),T("선택한 파티클 효과의 입자 크기를 절반으로 줄이고, 방출량만 두 배로 바꿔 줘.","Halve the selected particle effect's particle sizes and double only its emission rate.")}};
        const Example* examples=copilotMode?copilot:local;const int count=copilotMode?2:3;
        const float right=ImGui::GetCursorScreenPos().x+ImGui::GetContentRegionAvail().x;
        float lastRight=0;
        for(int i=0;i<count;++i){
            const float width=ImGui::CalcTextSize(examples[i].label,nullptr,true).x+framePadding.x*2;
            if(i&&lastRight+itemSpacing.x+width<=right)ImGui::SameLine();
            if(ImGui::SmallButton(examples[i].label))strncpy_s(vfxAgent_.prompt.data(),vfxAgent_.prompt.size(),examples[i].prompt,_TRUNCATE);
            lastRight=ImGui::GetItemRectMax().x;
            Tip(examples[i].prompt);
        }
    }
    ChatMessages(vfxAgent_.messages,"VFX AI");
    if(copilotVfx_)CopilotProgress();
    else if(copilotActive_)Muted(T("애니메이션 AI가 작업 중입니다. 완료하거나 중지한 뒤 요청하세요.","The animation AI is working. Finish or stop it before sending."));
    if(copilotMode&&error_){ImGui::PushStyleColor(ImGuiCol_Text,ImVec4{1,.55f,.55f,1});ImGui::TextWrapped("%s",status_.c_str());ImGui::PopStyleColor();}
    if(vfxAgent_.scroll){ImGui::SetScrollHereY(1);vfxAgent_.scroll=false;}
    ImGui::EndChild();
    const char* hint=copilotMode?(hasEffect?T("선택한 효과를 어떻게 바꿀까요?","How should the selected effect change?"):T("만들 효과를 설명하세요","Describe the effect to create"))
                                :T("예: 파란 마법 파티클, 초당 80개","e.g. blue magic particles, rate 80");
    ImGui::InputTextEx("##vfx-agent-prompt",hint,vfxAgent_.prompt.data(),static_cast<int>(vfxAgent_.prompt.size()),{-1,inputHeight},ImGuiInputTextFlags_Multiline);
    const bool submit=ImGui::IsItemFocused()&&ImGui::GetIO().KeyCtrl&&ImGui::IsKeyPressed(ImGuiKey_Enter,false);
    const std::string text=vfxAgent_.prompt.data();
    const bool canSend=!copilotActive_&&text.find_first_not_of(" \t\r\n")!=std::string::npos;
    ImGui::BeginDisabled(!canSend);
    if(copilotMode){
        if((ImGui::Button(T("보내기###SendVfxAgent","Send###SendVfxAgent"),{UiScale(85),0})||submit)&&canSend&&SendCopilot(text,true))vfxAgent_.prompt[0]=0;
        Tip(T("VFX 에이전트에게 보내기 (Ctrl+Enter)","Send to the VFX agent (Ctrl+Enter)"));
    }else{
        if((ImGui::Button(T("만들기###GenerateVfx","Create###GenerateVfx"),{UiScale(85),0})||submit)&&canSend){GenerateVfx(text);if(!error_)vfxAgent_.prompt[0]=0;}
        Tip(T("설명으로 새 파티클 만들기 (Ctrl+Enter) · Ctrl+Z로 되돌리기","Create particles from the description (Ctrl+Enter) · Ctrl+Z to undo"));
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if(copilotActive_){
        if(ImGui::Button(T("작업 중지###StopVfxAgent","Stop###StopVfxAgent"))){
            CancelCopilot();
            status_=T("AI 작업을 중지했습니다. 적용된 변경은 Ctrl+Z로 되돌릴 수 있습니다.","AI task stopped. Ctrl+Z undoes applied edits.");
            AddCopilotMessage(false,status_,"Copilot Studio");
        }
    }else{
        ImGui::AlignTextToFramePadding();
        if(copilotMode&&!vfxAgentReady_)ImGui::TextColored({1,.75f,.35f,1},"%s",T("설정 필요","Setup needed"));
        else ImGui::TextDisabled("Ctrl+Enter");
    }
    ImGui::EndChild();
}
}

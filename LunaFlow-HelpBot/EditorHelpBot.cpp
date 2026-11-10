#include "App/EditorApp.h"
#include "App/UiTheme.h"
#include "App/UiFonts.h"
#include "imgui.h"
#include "imgui_internal.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace LunaFlow {
namespace {
constexpr ImVec4 Violet{.72f,.58f,1,1},Teal{.41f,.79f,.70f,1},Warning{1,.70f,.35f,1};
bool Blank(const std::string& text){return text.find_first_not_of(" \t\r\n")==std::string::npos;}
std::string Trimmed(const std::string& text){
    const auto first=text.find_first_not_of(" \t\r\n");if(first==std::string::npos)return {};
    return text.substr(first,text.find_last_not_of(" \t\r\n")-first+1);
}
std::filesystem::path ExecutableFolder(){
    wchar_t file[32768]{};GetModuleFileNameW(nullptr,file,32768);return std::filesystem::path(file).parent_path();
}
// Buttons that flow onto the next line when the row is full, like ChatGPT's suggestion chips.
int Chips(const std::vector<std::string>& labels,const char* id){
    int clicked=-1;const auto& style=ImGui::GetStyle();
    const float right=ImGui::GetCursorScreenPos().x+ImGui::GetContentRegionAvail().x;
    ImGui::PushID(id);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding,UiScale(12));
    ImGui::PushStyleColor(ImGuiCol_Button,ImVec4{.17f,.15f,.26f,1});ImGui::PushStyleColor(ImGuiCol_ButtonHovered,ImVec4{.27f,.22f,.42f,1});
    for(size_t i=0;i<labels.size();++i){
        const float width=std::min(ImGui::CalcTextSize(labels[i].c_str()).x+style.FramePadding.x*2,std::max(right-ImGui::GetCursorScreenPos().x,1.0f));
        if(i>0){ImGui::SameLine();if(ImGui::GetCursorScreenPos().x+width>right)ImGui::NewLine();}
        ImGui::PushID(static_cast<int>(i));
        if(ImGui::Button(labels[i].c_str(),{std::min(width,ImGui::GetContentRegionAvail().x),0}))clicked=static_cast<int>(i);
        ImGui::PopID();
    }
    ImGui::PopStyleColor(2);ImGui::PopStyleVar();ImGui::PopID();
    return clicked;
}
void Pulse(){
    const float line=ImGui::GetTextLineHeight();const ImVec2 p=ImGui::GetCursorScreenPos();
    const float pulse=.5f+.5f*std::sin(static_cast<float>(ImGui::GetTime())*3.2f);
    ImGui::GetWindowDrawList()->AddCircleFilled({p.x+line*.5f,p.y+line*.5f},line*(.20f+.10f*pulse),ImGui::GetColorU32(ImVec4{.72f,.58f,1,.55f+.45f*pulse}));
    ImGui::Dummy({line,line});ImGui::SameLine(0,ImGui::GetStyle().ItemInnerSpacing.x);
}
}

bool EditorApp::HelpBotAvailable() const{
    try{
        const auto root=CopilotProjectRoot();HelpBotSettings(root);
        return std::filesystem::is_regular_file(root/L"agent_service/copilot_client/bin/Release/net10.0/CopilotConnection.exe");
    }catch(const std::exception&){return false;}
}
void EditorApp::ToggleHelpBot(){
    auto& s=helpBot_;s.show=!s.show;
    if(!s.show)return;
    if(!s.ready){s.knowledge=LoadHelpKnowledge(ExecutableFolder());s.ready=true;}
    s.configured=HelpBotAvailable();
    // First open: use the Copilot Studio agent when it is set up, otherwise answer from the bundled manual.
    if(s.mode<0)s.mode=s.configured?0:1;
    s.focusInput=true;s.scroll=true;
}
void EditorApp::NewHelpConversation(){
    auto& s=helpBot_;
    if(s.active){helpProcess_.Cancel();s.active=false;}
    s.messages.clear();s.conversationId.clear();s.sessionId.clear();s.request=Json();s.stage.clear();s.prompt[0]=0;s.focusInput=true;
}
void EditorApp::StopHelpBot(){
    auto& s=helpBot_;if(!s.active)return;
    helpProcess_.Cancel();s.active=false;s.request=Json();s.stage.clear();
    s.messages.push_back({false,T("답변 생성을 중지했습니다.","Stopped generating the answer."),"",-1,{}});s.scroll=true;s.focusInput=true;
}
Json EditorApp::HelpAppContext() const{
    Json context;
    const char* workspaces[]={"Scene","Animation","VFX"};
    context["workspace"]=workspaces[std::clamp(workspace_,0,2)];
    context["uiLanguage"]=settings_.korean?"ko":"en";
    context["characterFile"]=model_.source.empty()?std::string("(none)"):model_.source.filename().u8string();
    context["projectFile"]=projectPath_.empty()?std::string("(unsaved project)"):projectPath_.filename().u8string();
    context["unsavedChanges"]=dirty_;
    context["boneCount"]=model_.skeleton.bones.size();
    context["needsAutoRig"]=NeedsAutoRig(model_);
    if(selectedBone_>=0&&static_cast<size_t>(selectedBone_)<model_.skeleton.bones.size())context["selectedBone"]=model_.skeleton.bones[static_cast<size_t>(selectedBone_)].name;
    context["motionCount"]=model_.clips.size();
    if(selectedClip_>=0&&static_cast<size_t>(selectedClip_)<model_.clips.size()){
        const auto& clip=model_.clips[static_cast<size_t>(selectedClip_)];
        context["selectedMotion"]=clip.name;context["selectedMotionSeconds"]=clip.duration;
    }
    context["effectCount"]=model_.effects.size();
    if(selectedEffect_>=0&&static_cast<size_t>(selectedEffect_)<model_.effects.size())context["selectedEffect"]=model_.effects[static_cast<size_t>(selectedEffect_)].name;
    context["webcamMocapOpen"]=mocap_.show||mocap_.active;
    context["animationAiBusy"]=copilotActive_;
    try{
        const auto root=CopilotProjectRoot();const auto animation=AgentSettingsFile(root/L"agent_service/copilot_client");
        context["animationAgentConfigured"]=std::filesystem::is_regular_file(animation);
        // Lets the helper skip Microsoft sign-in steps in the sign-in-free demo build.
        context["aiSignInRequired"]=!AgentSignInFree(animation);
    }catch(const std::exception&){context["animationAgentConfigured"]=false;context["aiSignInRequired"]=true;}
    context["vfxAgentConfigured"]=VfxAgentAvailable();
    return context;
}
void EditorApp::AddLocalHelpAnswer(const std::string& question,const std::string& note){
    auto answer=LocalHelpAnswer(helpBot_.knowledge,question,settings_.korean);
    helpBot_.messages.push_back({false,note.empty()?answer.text:note+"\n\n"+answer.text,T("내장 도움말","Built-in manual"),-1,std::move(answer.suggestions)});
    helpBot_.scroll=true;
}
bool EditorApp::AskHelpBot(const std::string& raw){
    auto& s=helpBot_;const auto question=Trimmed(raw);
    if(question.empty()||s.active)return false;
    if(!s.ready){s.knowledge=LoadHelpKnowledge(ExecutableFolder());s.ready=true;}
    if(s.mode<0)s.mode=1;
    s.prompt[0]=0;s.scroll=true;s.focusInput=true;
    if(s.mode==1){s.messages.push_back({true,question,"",-1,{}});AddLocalHelpAnswer(question);return true;}
    try{
        const auto root=CopilotProjectRoot();const auto config=HelpBotSettings(root);
        // The request is built before the question joins the list, so <history> holds only earlier turns.
        auto request=HelpBotRequest(s,question,HelpAppContext(),settings_.korean);
        helpProcess_.Submit(root,request,config);
        s.sessionId=request["sessionId"].get<std::string>();s.request=std::move(request);
        s.active=true;s.elapsed=0;s.stage=T("Copilot Studio 도우미에 연결하고 있습니다.","Connecting to the Copilot Studio helper.");
        s.messages.push_back({true,question,"",-1,{}});
    }catch(const std::exception& e){
        s.configured=false;s.messages.push_back({true,question,"",-1,{}});
        AddLocalHelpAnswer(question,std::string(T("Copilot Studio 도우미를 사용할 수 없어 내장 도움말로 답합니다. (","The Copilot Studio helper is unavailable, so the built-in manual answers. ("))+e.what()+")");
    }
    return true;
}
void EditorApp::HandleHelpEvent(const Json& event){
    auto& s=helpBot_;
    if(!s.active||!s.request.is_object()||event.value("requestId",Json())!=s.request["requestId"]||event.value("sessionId",Json())!=s.request["sessionId"])return;
    const auto state=event.value("status",std::string{}),message=event.value("message",std::string{});
    if(state=="running"){if(!Blank(message))s.stage=message;return;}
    if(state!="completed"&&state!="failed")return;
    const auto question=s.request.value("question",std::string{});
    s.active=false;s.stage.clear();s.scroll=true;s.focusInput=true;
    if(state=="failed"){
        s.request=Json();
        AddLocalHelpAnswer(question,std::string(T("Copilot Studio 응답 실패: ","Copilot Studio failed: "))+message+T("\n내장 도움말로 대신 답합니다.","\nAnswering from the built-in manual instead."));
        return;
    }
    if(event.contains("conversationId")&&event["conversationId"].is_string()){
        const auto id=event["conversationId"].get<std::string>();
        if(!id.empty()&&id.size()<=256)s.conversationId=id;
    }
    HelpMessage reply{false,HelpDisplayText(message),"Copilot Studio",s.elapsed,{}};
    if(Blank(reply.text))reply.text=T("도우미가 빈 답변을 반환했습니다. 질문을 바꿔 다시 시도하세요.","The helper returned an empty answer. Try rephrasing the question.");
    if(event.contains("suggestions")&&event["suggestions"].is_array())
        for(const auto& item:event["suggestions"])
            if(item.is_string()&&reply.suggestions.size()<4){const auto text=Trimmed(item.get<std::string>());if(!text.empty()&&text.size()<=240)reply.suggestions.push_back(text);}
    s.messages.push_back(std::move(reply));s.request=Json();
}
void EditorApp::PumpHelpBot(float deltaTime){
    if(helpBot_.active)helpBot_.elapsed+=std::max(deltaTime,0.0f);
    for(const auto& event:helpProcess_.Poll())HandleHelpEvent(event);
}
void EditorApp::HelpBotWindow(){
    auto& s=helpBot_;if(!s.show)return;
    const auto* main=ImGui::GetMainViewport();const float margin=UiScale(12);
    const ImVec2 area{std::max(main->WorkSize.x-margin*2,UiScale(200)),std::max(main->WorkSize.y-margin*2,UiScale(200))};
    const ImVec2 size{std::min(UiScale(420),std::max(area.x*.45f,std::min(UiScale(320),area.x))),std::min(UiScale(560),area.y*.78f)};
    // Opens docked to the bottom-right corner above the status bar; afterwards it can be moved and resized freely.
    // A DPI change (another monitor or the scale setting) lays it out again at the new size.
    const bool rescale=s.layoutScale!=UiScale(1);s.layoutScale=UiScale(1);
    const ImGuiCond placement=rescale?ImGuiCond_Always:ImGuiCond_FirstUseEver;
    ImGui::SetNextWindowPos({main->WorkPos.x+main->WorkSize.x-margin,main->WorkPos.y+main->WorkSize.y-margin-UiScale(24)},placement,{1,1});
    ImGui::SetNextWindowSize(size,placement);
    ImGui::SetNextWindowSizeConstraints({std::min(UiScale(300),area.x),std::min(UiScale(280),area.y)},area);
    bool open=true;
    if(!ImGui::Begin(T("LunaFlow 도우미###HelpBot","LunaFlow helper###HelpBot"),&open,ImGuiWindowFlags_NoCollapse|ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse)){
        ImGui::End();if(!open)s.show=false;return;
    }
    {
        // Keep the window reachable after the editor itself is resized smaller.
        const ImVec2 pos=ImGui::GetWindowPos(),extent=ImGui::GetWindowSize();
        const ImVec2 clamped{std::clamp(pos.x,main->WorkPos.x,std::max(main->WorkPos.x+main->WorkSize.x-extent.x,main->WorkPos.x)),
                             std::clamp(pos.y,main->WorkPos.y,std::max(main->WorkPos.y+main->WorkSize.y-extent.y,main->WorkPos.y))};
        if(clamped.x!=pos.x||clamped.y!=pos.y)ImGui::SetWindowPos(clamped);
    }
    const auto& style=ImGui::GetStyle();
    const float headerRight=ImGui::GetCursorPosX()+ImGui::GetContentRegionAvail().x;
    // Header: segmented source switch (Blender-style enum row) and "new chat".
    ImGui::BeginDisabled(s.active);
    auto segment=[&](const char* label,int mode,const char* tip){
        const bool selected=s.mode==mode;
        ImGui::PushStyleColor(ImGuiCol_Button,selected?ImVec4{.38f,.28f,.70f,1}:ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));
        if(ImGui::Button(label)&&!selected){s.mode=mode;if(mode==0)s.configured=HelpBotAvailable();s.focusInput=true;}
        ImGui::PopStyleColor();
        if(ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort|ImGuiHoveredFlags_AllowWhenDisabled))ImGui::SetTooltip("%s",tip);
    };
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,{UiScale(1),style.ItemSpacing.y});
    segment("Copilot###HelpCopilot",0,T("Copilot Studio 도우미 에이전트가 답합니다. 대화 맥락을 기억합니다.","The Copilot Studio helper agent answers and remembers the conversation."));
    ImGui::SameLine();
    segment(T("내장 도움말###HelpManual","Manual###HelpManual"),1,T("인터넷 없이 내장 사용 설명서에서 바로 찾아 답합니다.","Answers instantly from the bundled manual, offline."));
    ImGui::PopStyleVar();
    const char* newLabel=T("새 대화","New chat");
    const float newWidth=ImGui::CalcTextSize(newLabel).x+style.FramePadding.x*2;
    ImGui::SameLine(std::max(ImGui::GetCursorPosX(),headerRight-newWidth));
    if(ImGui::Button(newLabel))NewHelpConversation();
    ImGui::EndDisabled();
    if(s.mode==0&&!s.configured){
        ImGui::PushStyleColor(ImGuiCol_Text,Warning);
        ImGui::TextWrapped("%s",T("Copilot Studio 도우미가 아직 설정되지 않았습니다. LunaFlow-HelpBot/SETUP.ko.md 순서대로 Configure.ps1을 실행하세요. 그 전까지는 내장 도움말로 답합니다.",
            "The Copilot Studio helper is not set up yet. Follow LunaFlow-HelpBot/SETUP.ko.md and run Configure.ps1. Until then the built-in manual answers."));
        ImGui::PopStyleColor();
        if(ImGui::SmallButton(T("내장 도움말로 전환","Switch to the manual"))){s.mode=1;s.focusInput=true;}
        ImGui::SameLine();if(ImGui::SmallButton(T("다시 확인","Check again")))s.configured=HelpBotAvailable();
    }else ImGui::TextDisabled("%s",s.mode==0?T("Copilot Studio · 사용 설명서를 근거로 답합니다","Copilot Studio · grounded in the LunaFlow manual")
                                           :T("오프라인 · 내장 사용 설명서에서 찾습니다","Offline · searches the bundled manual"));
    ImGui::Separator();

    const float line=ImGui::GetTextLineHeight();
    const float inputHeight=std::clamp(ImGui::GetContentRegionAvail().y*.18f,line*2+style.FramePadding.y*2,line*4+style.FramePadding.y*2);
    const char* hint=T("Enter 보내기 · Ctrl+Enter 줄바꿈 · Shift+F1 열기/닫기","Enter to send · Ctrl+Enter for a new line · Shift+F1 toggles");
    const float hintHeight=ImGui::CalcTextSize(hint,nullptr,false,ImGui::GetContentRegionAvail().x).y;
    const float footer=inputHeight+hintHeight+style.ItemSpacing.y*3;
    std::string pending;
    ImGui::BeginChild("help-conversation",{0,std::max(ImGui::GetContentRegionAvail().y-footer,1.0f)});
    if(s.messages.empty()&&!s.active){
        const float logo=UiScale(44),width=ImGui::GetContentRegionAvail().x;
        ImGui::Dummy({0,UiScale(8)});
        const ImVec2 at=ImGui::GetCursorScreenPos();DrawLunaFlowLogo(ImGui::GetWindowDrawList(),{at.x+(width-logo)*.5f,at.y},logo);ImGui::Dummy({width,logo+UiScale(6)});
        const char* hello=T("무엇을 도와드릴까요?","How can I help?");
        if(UiFonts::semibold)ImGui::PushFont(UiFonts::semibold,20);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX()+std::max((width-ImGui::CalcTextSize(hello).x)*.5f,0.0f));ImGui::TextUnformatted(hello);
        if(UiFonts::semibold)ImGui::PopFont();
        ImGui::PushStyleColor(ImGuiCol_Text,ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextWrapped("%s",T("LunaFlow 사용법, 메뉴 위치, 단축키, 문제 해결을 물어보세요. 장면은 바꾸지 않습니다.","Ask about LunaFlow features, menus, shortcuts or troubleshooting. The helper never edits your scene."));
        ImGui::PopStyleColor();ImGui::Spacing();
        const std::vector<std::string> examples=settings_.korean
            ?std::vector<std::string>{"검 휘두르는 동작 만드는 법","카메라 WASD 이동","웹캠 모션캡처 녹화","Unity로 내보내기","VFX 파티클 만들기","자동 리깅이 뭐야?"}
            :std::vector<std::string>{"How do I make a sword swing?","Move the camera with WASD","Record webcam motion capture","Export to Unity","Create VFX particles","What is auto rig?"};
        const int chosen=Chips(examples,"examples");if(chosen>=0)pending=examples[static_cast<size_t>(chosen)];
    }
    for(size_t i=0;i<s.messages.size();++i){
        const auto& m=s.messages[i];ImGui::PushID(static_cast<int>(i));
        auto* draw=ImGui::GetWindowDrawList();const float pad=UiScale(6),right=ImGui::GetCursorScreenPos().x+ImGui::GetContentRegionAvail().x;
        if(m.user){draw->ChannelsSplit(2);draw->ChannelsSetCurrent(1);ImGui::Dummy({0,pad*.5f});ImGui::Indent(pad);ImGui::BeginGroup();}
        ImGui::TextColored(m.user?Violet:Teal,"%s",m.user?T("나","You"):T("LunaFlow 도우미","LunaFlow helper"));
        if(!m.source.empty()){ImGui::SameLine();ImGui::TextDisabled("%s",m.source.c_str());}
        if(!m.user&&m.thought>=0){
            ImGui::SameLine();
            if(m.thought<1)ImGui::TextDisabled("%s",T("· 잠시 생각함","· thought for a moment"));
            else ImGui::TextDisabled(T("· %.0f초 동안 생각함","· thought for %.0fs"),std::round(m.thought));
        }
        ImGui::PushTextWrapPos(m.user?right-pad-ImGui::GetWindowPos().x:0.0f);ImGui::TextUnformatted(m.text.c_str());ImGui::PopTextWrapPos();
        if(m.user){
            ImGui::EndGroup();ImGui::Unindent(pad);const ImVec2 a=ImGui::GetItemRectMin(),b=ImGui::GetItemRectMax();
            draw->ChannelsSetCurrent(0);draw->AddRectFilled({a.x-pad,a.y-pad*.5f},{right,b.y+pad*.5f},ImGui::GetColorU32(ImVec4{.20f,.17f,.32f,.85f}),UiScale(6));
            draw->ChannelsMerge();ImGui::Dummy({0,pad*.5f});
        }else{
            ImGui::PushStyleColor(ImGuiCol_Text,ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            if(ImGui::SmallButton(T("복사","Copy"))){ImGui::SetClipboardText(m.text.c_str());status_=T("도우미 답변을 클립보드에 복사했습니다.","Copied the helper answer.");error_=false;}
            ImGui::PopStyleColor();
            // Follow-up chips only under the newest answer, so older turns stay quiet.
            if(i+1==s.messages.size()&&!m.suggestions.empty()&&!s.active){const int chosen=Chips(m.suggestions,"suggestions");if(chosen>=0)pending=m.suggestions[static_cast<size_t>(chosen)];}
        }
        ImGui::Spacing();ImGui::PopID();
    }
    if(s.active){
        // Placeholder answer while waiting, matching the animation AI: breathing dot, shimmering label and the real stage.
        ImGui::TextColored(Teal,"%s",T("LunaFlow 도우미","LunaFlow helper"));ImGui::SameLine();ImGui::TextDisabled("Copilot Studio");
        Pulse();ShimmerText(T("생각 중…","Thinking…"),{.50f,.47f,.62f,1},{.97f,.95f,1,1});
        ImGui::SameLine();ImGui::TextDisabled(T("%.0f초","%.0fs"),s.elapsed);
        if(!s.stage.empty()){
            ImGui::Indent(line+style.ItemInnerSpacing.x);ImGui::PushStyleColor(ImGuiCol_Text,ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            ImGui::TextWrapped("%s",s.stage.c_str());ImGui::PopStyleColor();ImGui::Unindent(line+style.ItemInnerSpacing.x);
        }
    }
    ImGui::Dummy({0,UiScale(4)}); // breathing room so the last line is never cut at the bottom edge
    // Follow new content while the reader is already at the bottom; leave the view alone when they scrolled up to read.
    if(s.scroll||ImGui::GetScrollY()>=ImGui::GetScrollMaxY()-1){ImGui::SetScrollHereY(1);s.scroll=false;}
    ImGui::EndChild();

    // Composer: Enter sends, Ctrl+Enter adds a line, like a chat app.
    const char* actionLabel=s.active?T("중지","Stop"):T("보내기","Send");
    const float actionWidth=std::max(ImGui::CalcTextSize(T("보내기","Send")).x,ImGui::CalcTextSize(T("중지","Stop")).x)+style.FramePadding.x*2+UiScale(8);
    if(s.focusInput&&!s.active){ImGui::SetKeyboardFocusHere();s.focusInput=false;}
    const bool entered=ImGui::InputTextMultiline("##help-prompt",s.prompt.data(),s.prompt.size(),{std::max(ImGui::GetContentRegionAvail().x-actionWidth-style.ItemSpacing.x,1.0f),inputHeight},
        ImGuiInputTextFlags_EnterReturnsTrue|ImGuiInputTextFlags_CtrlEnterForNewLine);
    if(s.prompt[0]==0&&!ImGui::IsItemActive()){
        const ImVec2 corner=ImGui::GetItemRectMin();
        ImGui::GetWindowDrawList()->AddText({corner.x+style.FramePadding.x,corner.y+style.FramePadding.y},ImGui::GetColorU32(ImGuiCol_TextDisabled),
            s.active?T("답변을 기다리는 중…","Waiting for the answer…"):T("LunaFlow에 대해 물어보세요…","Ask anything about LunaFlow…"));
    }
    if(entered){s.focusInput=true;if(!s.active&&pending.empty())pending=s.prompt.data();}
    ImGui::SameLine();
    if(s.active){
        if(ImGui::Button(actionLabel,{actionWidth,inputHeight}))StopHelpBot();
    }else{
        ImGui::BeginDisabled(Blank(s.prompt.data()));
        ImGui::PushStyleColor(ImGuiCol_Button,ImVec4{.38f,.28f,.70f,1});
        if(ImGui::Button(actionLabel,{actionWidth,inputHeight})&&pending.empty())pending=s.prompt.data();
        ImGui::PopStyleColor();ImGui::EndDisabled();
    }
    ImGui::PushStyleColor(ImGuiCol_Text,ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));ImGui::TextWrapped("%s",hint);ImGui::PopStyleColor();
    ImGui::End();
    if(!open){s.show=false;return;}
    if(!pending.empty())AskHelpBot(pending);
}
}

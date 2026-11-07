#include "App/EditorApp.h"
#include "imgui.h"
#include "UiFonts.h"
#include "App/UiTheme.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace LunaFlow {
void EditorApp::MenuBar() {
    if(!ImGui::BeginMenuBar())return;
    if(ImGui::BeginMenu("+")){
        ImGui::TextDisabled("%s",T("오브젝트 추가","ADD OBJECT"));
        for(auto shape:{Primitive::Cube,Primitive::Sphere,Primitive::Cylinder,Primitive::Cone,Primitive::Plane,Primitive::Torus})if(ImGui::MenuItem(ShapeLabel(shape)))AddShape(shape);
        ImGui::Separator();if(ImGui::MenuItem(T("모델 / 소품 가져오기...","Import model / prop...")))ImportObject();
        ImGui::Separator();ImGui::TextDisabled("VFX");
        if(ImGui::MenuItem(T("파티클 시스템","Particle system"))){particlePreset_=0;AddParticlePreset();}
        if(ImGui::MenuItem(T("칼 궤적","Blade trail")))AddSceneEffect(EffectType::Trail);
        if(ImGui::MenuItem(T("스파크","Sparks")))AddSceneEffect(EffectType::Sparks);
        if(ImGui::MenuItem(T("오라","Aura")))AddSceneEffect(EffectType::Aura);
        ImGui::EndMenu();
    }
    if(ImGui::BeginMenu(T("파일","File"))){
        if(ImGui::MenuItem(T("FBX 가져오기...","Import FBX...")))showFbxImport_=true;
        ImGui::Separator();
        if(ImGui::BeginMenu(T("새 장면","New scene"))){
            if(ImGui::MenuItem(T("빈 장면","Empty scene")))RequestSceneChange([this]{UseModel(MakeEmptyScene());});
            if(ImGui::MenuItem(T("기본 캐릭터","Default character")))RequestSceneChange([this]{LoadDefaultCharacter();});
            if(ImGui::MenuItem(T("리깅 예제 캐릭터","Rigged sample character")))RequestSceneChange([this]{UseModel(MakeDemoScene());});
            ImGui::EndMenu();
        }
        if(ImGui::MenuItem(T("프로젝트 열기...","Open project..."),"Ctrl+O"))OpenProject();
        if(ImGui::MenuItem(T("프로젝트 저장","Save project"),"Ctrl+S"))SaveCurrentProject();
        if(ImGui::MenuItem(T("다른 이름으로 저장...","Save project as..."),"Ctrl+Shift+S"))SaveCurrentProject(true);
        ImGui::Separator();
        if(ImGui::MenuItem(T("캐릭터 불러오기...","Open character...")))OpenCharacter();
        ImGui::Separator();
        if(ImGui::MenuItem(T("동작 가져오기...","Import motion...")))ImportMotion();
        if(ImGui::MenuItem(T("현재 동작 저장...","Save current motion...")))SaveMotion();
        if(ImGui::BeginMenu(T("내보내기","Export"))){
            if(ImGui::MenuItem(T("Unity 패키지 (모델 + 동작 + VFX)...","Unity package (model + motions + VFX)...")))ExportUnity();
            if(ImGui::MenuItem(T("캐릭터 + 모든 동작 FBX...","Character + all motions FBX...")))ExportCharacter();
            char label[96];std::snprintf(label,sizeof(label),T("최종 자세 (%d FPS)...","Final poses (%d FPS)..."),settings_.bakeFps);
            if(ImGui::MenuItem(label))ExportBake();
            if(ImGui::MenuItem(T("뼈대 JSON...","Skeleton JSON...")))ExportSkeleton();
            if(ImGui::MenuItem(T("미리보기 PNG...","Preview PNG...")))ExportImage();
            ImGui::EndMenu();
        }
        ImGui::Separator();if(ImGui::MenuItem(T("종료","Exit")))PostMessageW(window_,WM_CLOSE,0,0);
        ImGui::EndMenu();
    }
    if(ImGui::BeginMenu(T("편집","Edit"))){
        if(ImGui::MenuItem(T("실행 취소","Undo"),"Ctrl+Z",false,!undo_.empty()))Undo();
        if(ImGui::MenuItem(T("다시 실행","Redo"),"Ctrl+Y",false,!redo_.empty()))Redo();
        ImGui::Separator();
        if(workspace_==2){
            const bool effect=selectedEffect_>=0&&selectedEffect_<static_cast<int>(model_.effects.size());
            if(ImGui::MenuItem(T("효과 복제","Duplicate effect"),"Ctrl+D",false,effect))DuplicateEffect();
            if(ImGui::MenuItem(T("효과 삭제","Delete effect"),"Del",false,effect))DeleteEffect();
        }else{
            if(ImGui::MenuItem(T("현재 프레임에 키 추가","Insert key"),"K",false,!model_.skeleton.bones.empty()&&!bindPose_))InsertKey();
            if(ImGui::MenuItem(T("선택한 키 삭제","Delete selected key"),"Del",false,selectedKeyTime_>=0))DeleteSelectedKey();
            if(ImGui::MenuItem(T("이전 키프레임으로","Previous keyframe"),"Alt+,"))JumpKey(-1);
            if(ImGui::MenuItem(T("다음 키프레임으로","Next keyframe"),"Alt+."))JumpKey(1);
        }
        ImGui::Separator();
        if(ImGui::MenuItem(T("동작 복제","Duplicate motion"),workspace_==1?"Ctrl+D":nullptr))DuplicateClip();
        if(ImGui::MenuItem(T("새 동작","New motion")))Dispatch("new_clip",{{"name",T("새 동작","New motion")}});
        ImGui::Separator();
        if(ImGui::MenuItem(T("캐릭터 자동 리깅","Auto rig character"),nullptr,false,NeedsAutoRig(model_)))AutoRigCharacter();
        if(ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("%s",T("뼈대가 없는 메시 캐릭터에 사람형 뼈대와 스킨 가중치를 만듭니다.","Builds a humanoid skeleton and skin weights for an unrigged mesh character."));
        ImGui::EndMenu();
    }
    if(ImGui::BeginMenu(T("보기","View"))){
        ImGui::MenuItem(T("격자","Grid"),nullptr,&options_.grid);
        ImGui::MenuItem(T("뼈대","Skeleton"),nullptr,&options_.skeleton);
        ImGui::MenuItem(T("와이어프레임","Wireframe"),nullptr,&options_.wireframe);
        ImGui::Separator();ImGui::MenuItem(T("속성 패널","Inspector"),nullptr,&showInspector_);
        ImGui::MenuItem(T("AI 대화 패널","AI conversation"),nullptr,&showChat_);
        if(ImGui::MenuItem(T("웹캠 모션캡처...","Webcam motion capture..."),nullptr,mocap_.show)) {
            if(mocap_.show&&!mocap_.active)mocap_.show=false;
            else OpenMocap();
        }
        ImGui::Separator();
        if(ImGui::MenuItem(T("선택 항목 보기","Frame selected"),"F"))FrameSelected();
        if(ImGui::MenuItem(T("전체 보기","Frame all"),"Shift+F"))FrameAll();
        if(ImGui::MenuItem(T("기본 시점으로","Reset view")))ResetView();
        if(ImGui::BeginMenu(T("시점","Viewpoint"))){
            struct Entry { const char* korean; const char* english; const char* key; int axis; };
            static const Entry entries[]={
                {"정면","Front","Numpad 1",ViewFront},{"후면","Back","Ctrl+Numpad 1",ViewBack},
                {"우측","Right","Numpad 3",ViewRight},{"좌측","Left","Ctrl+Numpad 3",ViewLeft},
                {"상단","Top","Numpad 7",ViewTop},{"하단","Bottom","Ctrl+Numpad 7",ViewBottom}};
            for(const auto& entry:entries)if(ImGui::MenuItem(T(entry.korean,entry.english),entry.key,viewAxis_==entry.axis))SnapView(entry.axis);
            ImGui::Separator();
            if(ImGui::MenuItem(T("반대편에서 보기","Opposite side"),"Numpad 9"))ViewOpposite();
            ImGui::EndMenu();
        }
        if(ImGui::MenuItem(T("직교 투영","Orthographic"),"Numpad 5",CameraGoal().orthographic))ToggleOrthographic();
        ImGui::EndMenu();
    }
    if(ImGui::BeginMenu(T("설정","Settings"))){if(ImGui::MenuItem(T("환경 설정...","Preferences...")))showSettings_=true;ImGui::EndMenu();}
    if(ImGui::BeginMenu(T("도움말","Help"))){
        if(ImGui::MenuItem(T("LunaFlow 도우미","LunaFlow helper"),"Shift+F1",helpBot_.show))ToggleHelpBot();
        if(ImGui::MenuItem(T("단축키 보기","Keyboard shortcuts"),"F1"))showShortcuts_=true;
        ImGui::EndMenu();
    }
    ImGui::EndMenuBar();
}
void EditorApp::SelectWorkspace(int workspace) {
    workspace_=workspace;selectWorkspaceTab_=true;selectLibraryTab_=true;
    libraryTab_=workspace==0?LibraryTab::Hierarchy:workspace==1?LibraryTab::Motions:LibraryTab::Effects;
    tool_=workspace==1?Tool::Rotate:Tool::Move;
    draggingBone_=false;gizmoWasUsing_=false;selectedKeyTime_=-1;editBone_=-1;
    if(workspace!=2){selectedEffect_=-1;return;}
    showInspector_=true;options_.showEffects=true;bindPose_=false;
    if(selectedEffect_<0||selectedEffect_>=static_cast<int>(model_.effects.size())){
        const auto effect=std::find_if(model_.effects.begin(),model_.effects.end(),[&](const auto& item){return item.clipId.empty()||item.clipId==Clip().id;});
        selectedEffect_=effect!=model_.effects.end()?static_cast<int>(effect-model_.effects.begin()):model_.effects.empty()?-1:0;
    }
    SelectEffect(selectedEffect_);
}
void EditorApp::Workspaces() {
    const auto start=ImGui::GetCursorScreenPos();const float width=ImGui::GetContentRegionAvail().x;auto* draw=ImGui::GetWindowDrawList();
    draw->AddRectFilled(start,{start.x+width,start.y+UiScale(53)},IM_COL32(16,19,29,255),UiScale(5));
    DrawLunaFlowLogo(draw,{start.x+UiScale(6),start.y+UiScale(7.5f)},UiScale(38));
    ImGui::SetCursorScreenPos({start.x+UiScale(49),start.y+UiScale(13)});ImGui::PushFont(UiFonts::semibold,23);ImGui::TextUnformatted("LunaFlow");ImGui::PopFont();
    const std::string title=projectPath_.empty()?T("제목 없는 프로젝트","Untitled project"):projectPath_.filename().u8string();
    ImGui::SetCursorScreenPos({start.x+std::max(UiScale(240),width-ImGui::CalcTextSize(title.c_str()).x-UiScale(20)),start.y+UiScale(17)});ImGui::TextDisabled("%s%s",dirty_?"* ":"",title.c_str());
    ImGui::SetCursorScreenPos(start);ImGui::Dummy({width,UiScale(59)});
    if(ImGui::BeginTabBar("workspaces",ImGuiTabBarFlags_NoCloseWithMiddleMouseButton)){
        const int requested=selectWorkspaceTab_?workspace_:-1;selectWorkspaceTab_=false;
        auto tab=[&](const char* label,int workspace){
            const auto flags=requested==workspace?ImGuiTabItemFlags_SetSelected:ImGuiTabItemFlags_None;
            if(ImGui::BeginTabItem(label,nullptr,flags)){
                if((requested<0||requested==workspace)&&(workspace_!=workspace||ImGui::IsItemClicked()))SelectWorkspace(workspace);
                ImGui::EndTabItem();
            }
        };
        tab(T("애니메이션###Animation","Animation###Animation"),1);
        tab(T("장면###Scene","Scene###Scene"),0);
        tab("VFX###VFX",2);
        ImGui::EndTabBar();
    }
}
void EditorApp::SavePreferences() {Try([&]{SaveSettings(settingsPath_,settings_);status_=T("설정을 저장했습니다.","Preferences saved.");});}
void EditorApp::Settings() {
    if(showSettings_){ImGui::OpenPopup("Preferences###Preferences");showSettings_=false;}
    if(!ImGui::IsPopupOpen("Preferences###Preferences"))return;
    ImGui::SetNextWindowSize({UiScale(570),0},ImGuiCond_Appearing);
    bool open=true;
    if(ImGui::BeginPopupModal(T("환경 설정###Preferences","Preferences###Preferences"),&open,ImGuiWindowFlags_AlwaysAutoResize|ImGuiWindowFlags_NoSavedSettings)){
        ImGui::SeparatorText(T("일반","GENERAL"));
        ImGui::TextUnformatted(T("언어","Language"));ImGui::SameLine(UiScale(190));ImGui::SetNextItemWidth(UiScale(320));
        int language=settings_.korean?0:1;const char* languages[]={"한국어","English"};
        if(ImGui::Combo("##language",&language,languages,2)){settings_.korean=language==0;SavePreferences();}
        ImGui::Spacing();ImGui::SeparatorText(T("화면 성능","DISPLAY"));
        ImGui::Checkbox(T("수직 동기화 (VSync)","Vertical sync (VSync)"),&settings_.vsync);
        const int rates[]={0,60,120,144,165,240};const char* rateLabels[]={T("제한 없음","Unlimited"),"60 FPS","120 FPS","144 FPS","165 FPS","240 FPS"};
        int frame=0;for(int i=0;i<6;++i)if(settings_.frameLimit==rates[i])frame=i;
        ImGui::TextUnformatted(T("화면 FPS 상한","Display frame limit"));ImGui::SameLine(UiScale(190));ImGui::SetNextItemWidth(UiScale(320));
        if(ImGui::Combo("##limit",&frame,rateLabels,6))settings_.frameLimit=rates[frame];
        ImGui::Checkbox(T("상태 표시줄에 실제 FPS 표시","Show measured FPS in status bar"),&settings_.showFps);
        ImGui::TextWrapped("%s",T("VSync를 켜면 모니터 주사율에 맞춥니다. 실제 FPS는 장면 복잡도와 GPU에 따라 달라집니다.","VSync follows the monitor refresh rate. Measured FPS depends on the scene and GPU."));
        ImGui::Spacing();ImGui::SeparatorText(T("애니메이션","ANIMATION"));
        const int sampleRates[]={24,30,60,120,240};const char* samples[]={"24 FPS","30 FPS","60 FPS","120 FPS","240 FPS"};
        auto sampleCombo=[&](const char* label,const char* id,int& value){int selected=2;for(int i=0;i<5;++i)if(value==sampleRates[i])selected=i;ImGui::TextUnformatted(label);ImGui::SameLine(UiScale(190));ImGui::SetNextItemWidth(UiScale(320));if(ImGui::Combo(id,&selected,samples,5))value=sampleRates[selected];};
        sampleCombo(T("타임라인 프레임 격자","Timeline frame grid"),"##timeline-fps",settings_.timelineFps);
        sampleCombo(T("내보내기 샘플링","Export sampling"),"##bake-fps",settings_.bakeFps);
        ImGui::TextWrapped("%s",T("내보내기 FPS는 키를 샘플링하는 간격입니다. 화면 FPS를 제한하지 않습니다.","Export FPS controls pose sampling density. It does not limit display FPS."));
        ImGui::Spacing();ImGui::SeparatorText(T("가져오기","IMPORT"));
        const float scales[]={1,.01f,.001f};const char* scaleLabels[]={T("자동 (추가 배율 없음)","Automatic (no correction)"),"x0.01","x0.001"};
        int scale=unitsToMeters_==1?0:unitsToMeters_==.01f?1:2;ImGui::SetNextItemWidth(-1);
        if(ImGui::Combo("##units",&scale,scaleLabels,3))unitsToMeters_=scales[scale];
        ImGui::TextWrapped("%s",T("FBX 단위는 자동으로 미터로 변환됩니다. 추가 배율은 다음 불러오기에 적용됩니다.","FBX units are converted to meters automatically. Extra scale applies on the next import."));
        ImGui::Spacing();ImGui::SeparatorText(T("조작","CONTROLS"));
        ImGui::TextWrapped("%s",T("우클릭 드래그 또는 Alt+좌클릭 드래그: 회전 / Alt+우클릭 드래그: 확대·축소\n우클릭+WASD: 카메라 이동 / Q/E: 아래/위 / Shift: 빠르게 / 이동 중 휠: 이동 속도\n가운데 드래그·빈 공간 좌클릭 드래그: 화면 이동 / 휠: 확대·축소\n우측 상단 축 기즈모: 클릭하면 정면·측면·상단 시점, 드래그하면 회전\nF: 선택 항목 보기 / Shift+F: 전체 보기 / 숫자패드 1·3·7: 시점 / 5: 직교\n뼈대 좌클릭: 선택·조작 / W/E/R: 이동/회전/크기 / X: 월드·로컬\n좌우 화살표: 프레임 이동 / 위아래 화살표: 키프레임 이동 / K: 키 추가 / Del: 키 삭제\n회전 링·속성 숫자 드래그: 화면 가장자리에서 커서 순환\nSpace: 재생·정지 / Ctrl+Z·Ctrl+Y: 되돌리기·다시 실행 / Ctrl+S: 저장 / F1: 전체 단축키",
            "RMB drag or Alt+LMB drag: orbit / Alt+RMB drag: zoom\nRMB+WASD: fly / Q/E: down/up / Shift: faster / wheel while flying: fly speed\nMMB drag or LMB drag on background: pan / wheel: zoom\nAxis gizmo (top right): click for front/side/top views, drag to orbit\nF: frame selected / Shift+F: frame all / Numpad 1, 3, 7: views / 5: orthographic\nLMB on a joint: select / W/E/R: move/rotate/scale / X: world/local\nLeft/Right arrows: step frames / Up/Down arrows: jump keys / K: insert key / Del: delete key\nRotation rings and numeric drags wrap at screen edges\nSpace: play/pause / Ctrl+Z, Ctrl+Y: undo, redo / Ctrl+S: save / F1: all shortcuts"));
        ImGui::Spacing();if(ImGui::Button(T("저장 후 닫기","Save & close"),{-1,0})){SavePreferences();ImGui::CloseCurrentPopup();}
        ImGui::EndPopup();
    }
    if(!open)SavePreferences();
}
void EditorApp::ChatMessages(const std::vector<Message>& list,const char* assistant) {
    const ImVec4 muted=ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
    for(size_t i=0;i<list.size();++i){
        const auto& m=list[i];ImGui::PushID(static_cast<int>(i));
        auto* draw=ImGui::GetWindowDrawList();const float pad=UiScale(6),right=ImGui::GetCursorScreenPos().x+ImGui::GetContentRegionAvail().x;
        if(m.user){draw->ChannelsSplit(2);draw->ChannelsSetCurrent(1);ImGui::Dummy({0,pad*.5f});ImGui::Indent(pad);ImGui::BeginGroup();}
        ImGui::TextColored(m.user?ImVec4{.72f,.58f,1,1}:ImVec4{.41f,.79f,.70f,1},"%s",m.user?T("나","You"):assistant);
        if(!m.target.empty()){ImGui::SameLine();ImGui::TextDisabled("%s",m.target.c_str());}
        if(!m.user&&m.thought>=0){
            ImGui::PushStyleColor(ImGuiCol_Text,muted);
            const bool open=m.thought<1?ImGui::TreeNode("thought","%s",T("잠시 생각함","Thought for a moment")):ImGui::TreeNode("thought",T("%.0f초 동안 생각함","Thought for %.0fs"),std::round(m.thought));
            if(open){for(const auto& step:m.activity){ImGui::Bullet();ImGui::TextWrapped("%s",step.c_str());}ImGui::TreePop();}
            ImGui::PopStyleColor();
        }
        ImGui::PushTextWrapPos(m.user?right-pad-ImGui::GetWindowPos().x:0.0f);ImGui::TextUnformatted(m.text.c_str());ImGui::PopTextWrapPos();
        if(m.user){
            ImGui::EndGroup();ImGui::Unindent(pad);const ImVec2 a=ImGui::GetItemRectMin(),b=ImGui::GetItemRectMax();
            draw->ChannelsSetCurrent(0);draw->AddRectFilled({a.x-pad,a.y-pad*.5f},{right,b.y+pad*.5f},ImGui::GetColorU32(ImVec4{.20f,.17f,.32f,.85f}),UiScale(6));
            draw->ChannelsMerge();ImGui::Dummy({0,pad*.5f});
        }
        ImGui::Spacing();ImGui::PopID();
    }
}
void EditorApp::SendPrompt() {
    std::string text=prompt_;if(text.find_first_not_of(" \t\r\n")==std::string::npos)return;
    if(SendCopilot(text))prompt_[0]=0;
}
void EditorApp::Chat(float height) {
    if(workspace_==2){VfxAgentChat(height);return;}
    ImGui::BeginChild("ai-chat",{0,height},0,ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse);
    const float headerRight=ImGui::GetCursorPosX()+ImGui::GetContentRegionAvail().x;
    ImGui::AlignTextToFramePadding();ImGui::TextColored({.72f,.58f,1,1},"AI");ImGui::SameLine();
    ImGui::TextUnformatted("Copilot Studio");
    ImGui::SameLine();ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(),headerRight-ImGui::GetFrameHeight()));
    if(ImGui::ArrowButton("##hide-chat",ImGuiDir_Down))showChat_=false;
    if(ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))ImGui::SetTooltip("%s",T("AI 대화 숨기기 · 아래 버튼이나 보기 메뉴에서 다시 엽니다.","Hide AI chat · reopen it from the button below or the View menu."));
    ImGui::Separator();
    const float remaining=ImGui::GetContentRegionAvail().y;
    const float inputHeight=std::clamp(remaining*.28f,UiScale(44),UiScale(80));
    const float footer=ImGui::GetFrameHeight()+inputHeight+ImGui::GetStyle().ItemSpacing.y*3+UiScale(5);
    const float listHeight=std::max(remaining-footer,1.0f);
    ImGui::BeginChild("conversation",{0,listHeight});
    if(messages_.empty()){
        ImGui::Spacing();ImGui::TextWrapped("%s",T("어떤 움직임을 만들까요?","What would you like to animate?"));
        ImGui::Spacing();
        if(ImGui::SmallButton(T("오른손으로 인사하기","Wave with the right hand")))strncpy_s(prompt_,T("오른손으로 자연스럽게 인사해 줘. 3초 길이로 만들어 줘.","Create a natural 3-second wave with the right hand."),_TRUNCATE);
        if(ImGui::SmallButton(T("차분한 대기 동작","Relaxed idle motion")))strncpy_s(prompt_,T("몸이 천천히 호흡하는 차분한 대기 동작을 만들어 줘.","Create a relaxed idle animation with gentle breathing."),_TRUNCATE);
    }
    ChatMessages(messages_,"LunaFlow");
    if(!copilotVfx_)CopilotProgress();
    else if(copilotActive_)ImGui::TextWrapped("%s",T("VFX 에이전트가 작업 중입니다. 완료하거나 중지한 뒤 요청하세요.","The VFX agent is working. Finish or stop it before sending."));
    if(chatScroll_){ImGui::SetScrollHereY(1);chatScroll_=false;}ImGui::EndChild();
    ImGui::InputTextMultiline("##prompt",prompt_,sizeof(prompt_),{-1,inputHeight});
    const bool submit=ImGui::IsItemFocused()&&ImGui::GetIO().KeyCtrl&&ImGui::IsKeyPressed(ImGuiKey_Enter,false);
    ImGui::BeginDisabled(copilotActive_);
    if(ImGui::Button(T("보내기","Send"),{UiScale(85),0})||(!copilotActive_&&submit))SendPrompt();
    ImGui::EndDisabled();
    if(copilotActive_){ImGui::SameLine();if(ImGui::Button(T("작업 중지","Stop"))){CancelCopilot();status_=T("AI 작업을 중지했습니다. 적용된 변경은 Ctrl+Z로 되돌릴 수 있습니다.","AI task stopped. Ctrl+Z undoes applied edits.");}}
    ImGui::EndChild();
}
}

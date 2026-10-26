#include "App/EditorApp.h"
#include "imgui.h"
#include "UiFonts.h"
#include <algorithm>
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
        if(ImGui::MenuItem(T("칼 궤적","Blade trail")))AddSceneEffect(EffectType::Trail);
        if(ImGui::MenuItem(T("스파크","Sparks")))AddSceneEffect(EffectType::Sparks);
        if(ImGui::MenuItem(T("오라","Aura")))AddSceneEffect(EffectType::Aura);
        ImGui::EndMenu();
    }
    if(ImGui::BeginMenu(T("파일","File"))){
        if(ImGui::BeginMenu(T("새 장면","New scene"))){
            if(ImGui::MenuItem(T("빈 장면","Empty scene")))RequestSceneChange([this]{UseModel(MakeEmptyScene());});
            if(ImGui::MenuItem(T("Luna 캐릭터","Luna character")))RequestSceneChange([this]{UseModel(MakeDemoScene());});
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
        if(ImGui::MenuItem(T("동작 수정 되돌리기","Undo motion edit"),"Ctrl+Z",false,!undo_.empty()))Undo();
        if(ImGui::MenuItem(T("동작 복제","Duplicate motion")))DuplicateClip();
        if(ImGui::MenuItem(T("새 동작","New motion")))Dispatch("new_clip",{{"name",T("새 동작","New motion")}});
        ImGui::EndMenu();
    }
    if(ImGui::BeginMenu(T("보기","View"))){
        ImGui::MenuItem(T("격자","Grid"),nullptr,&options_.grid);
        ImGui::MenuItem(T("뼈대","Skeleton"),nullptr,&options_.skeleton);
        ImGui::MenuItem(T("와이어프레임","Wireframe"),nullptr,&options_.wireframe);
        ImGui::Separator();ImGui::MenuItem(T("속성 패널","Inspector"),nullptr,&showInspector_);
        ImGui::MenuItem(T("AI 대화 패널","AI conversation"),nullptr,&showChat_);
        if(ImGui::MenuItem(T("전체 보기","Frame all"),"F"))FrameAll();
        ImGui::EndMenu();
    }
    if(ImGui::BeginMenu(T("설정","Settings"))){if(ImGui::MenuItem(T("환경 설정...","Preferences...")))showSettings_=true;ImGui::EndMenu();}
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
    draw->AddRectFilled(start,{start.x+width,start.y+53},IM_COL32(16,19,29,255),5);
    draw->AddCircleFilled({start.x+24,start.y+25},15,IM_COL32(166,126,238,255),32);
    draw->AddCircleFilled({start.x+30,start.y+19},12,IM_COL32(16,19,29,255),32);
    draw->AddCircleFilled({start.x+34,start.y+31},3,IM_COL32(132,227,203,255),16);
    ImGui::SetCursorScreenPos({start.x+49,start.y+13});ImGui::PushFont(UiFonts::semibold,23);ImGui::TextUnformatted("LunaFlow");ImGui::PopFont();
    const std::string title=projectPath_.empty()?T("제목 없는 프로젝트","Untitled project"):projectPath_.filename().u8string();
    ImGui::SetCursorScreenPos({start.x+std::max(240.0f,width-ImGui::CalcTextSize(title.c_str()).x-20),start.y+17});ImGui::TextDisabled("%s%s",dirty_?"* ":"",title.c_str());
    ImGui::SetCursorScreenPos(start);ImGui::Dummy({width,59});
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
    ImGui::SetNextWindowSize({570,0},ImGuiCond_Appearing);
    bool open=true;
    if(ImGui::BeginPopupModal(T("환경 설정###Preferences","Preferences###Preferences"),&open,ImGuiWindowFlags_AlwaysAutoResize|ImGuiWindowFlags_NoSavedSettings)){
        ImGui::SeparatorText(T("일반","GENERAL"));
        ImGui::TextUnformatted(T("언어","Language"));ImGui::SameLine(190);ImGui::SetNextItemWidth(320);
        int language=settings_.korean?0:1;const char* languages[]={"한국어","English"};
        if(ImGui::Combo("##language",&language,languages,2)){settings_.korean=language==0;SavePreferences();}
        ImGui::Spacing();ImGui::SeparatorText(T("화면 성능","DISPLAY"));
        ImGui::Checkbox(T("수직 동기화 (VSync)","Vertical sync (VSync)"),&settings_.vsync);
        const int rates[]={0,60,120,144,165,240};const char* rateLabels[]={T("제한 없음","Unlimited"),"60 FPS","120 FPS","144 FPS","165 FPS","240 FPS"};
        int frame=0;for(int i=0;i<6;++i)if(settings_.frameLimit==rates[i])frame=i;
        ImGui::TextUnformatted(T("화면 FPS 상한","Display frame limit"));ImGui::SameLine(190);ImGui::SetNextItemWidth(320);
        if(ImGui::Combo("##limit",&frame,rateLabels,6))settings_.frameLimit=rates[frame];
        ImGui::Checkbox(T("상태 표시줄에 실제 FPS 표시","Show measured FPS in status bar"),&settings_.showFps);
        ImGui::TextWrapped("%s",T("VSync를 켜면 모니터 주사율에 맞춥니다. 실제 FPS는 장면 복잡도와 GPU에 따라 달라집니다.","VSync follows the monitor refresh rate. Measured FPS depends on the scene and GPU."));
        ImGui::Spacing();ImGui::SeparatorText(T("애니메이션","ANIMATION"));
        const int sampleRates[]={24,30,60,120,240};const char* samples[]={"24 FPS","30 FPS","60 FPS","120 FPS","240 FPS"};
        auto sampleCombo=[&](const char* label,const char* id,int& value){int selected=2;for(int i=0;i<5;++i)if(value==sampleRates[i])selected=i;ImGui::TextUnformatted(label);ImGui::SameLine(190);ImGui::SetNextItemWidth(320);if(ImGui::Combo(id,&selected,samples,5))value=sampleRates[selected];};
        sampleCombo(T("타임라인 프레임 격자","Timeline frame grid"),"##timeline-fps",settings_.timelineFps);
        sampleCombo(T("내보내기 샘플링","Export sampling"),"##bake-fps",settings_.bakeFps);
        ImGui::TextWrapped("%s",T("내보내기 FPS는 키를 샘플링하는 간격입니다. 화면 FPS를 제한하지 않습니다.","Export FPS controls pose sampling density. It does not limit display FPS."));
        ImGui::Spacing();ImGui::SeparatorText(T("가져오기","IMPORT"));
        const float scales[]={1,.01f,.001f};const char* scaleLabels[]={T("자동 (추가 배율 없음)","Automatic (no correction)"),"x0.01","x0.001"};
        int scale=unitsToMeters_==1?0:unitsToMeters_==.01f?1:2;ImGui::SetNextItemWidth(-1);
        if(ImGui::Combo("##units",&scale,scaleLabels,3))unitsToMeters_=scales[scale];
        ImGui::TextWrapped("%s",T("FBX 단위는 자동으로 미터로 변환됩니다. 추가 배율은 다음 불러오기에 적용됩니다.","FBX units are converted to meters automatically. Extra scale applies on the next import."));
        ImGui::Spacing();ImGui::SeparatorText(T("조작","CONTROLS"));
        ImGui::TextWrapped("%s",T("우클릭 드래그: 회전 / 빈 공간 좌클릭 드래그: 화면 이동\n뼈대 좌클릭: 선택·조작 / W: 이동 / E: 회전 / F: 전체 보기\nSpace: 재생·정지 / Ctrl+Z: 동작 수정 되돌리기","RMB drag: orbit / LMB drag on background: pan\nLMB on a joint: select & manipulate / W: move / E: rotate / F: frame all\nSpace: play or pause / Ctrl+Z: undo motion edit"));
        ImGui::Spacing();if(ImGui::Button(T("저장 후 닫기","Save & close"),{-1,0})){SavePreferences();ImGui::CloseCurrentPopup();}
        ImGui::EndPopup();
    }
    if(!open)SavePreferences();
}
void EditorApp::SendPrompt() {
    std::string text=prompt_;if(text.find_first_not_of(" \t\r\n")==std::string::npos)return;
    Try([&]{agent_.Draft(model_,Clip(),text);});if(error_)return;
    messages_.push_back({true,text,ClipLabel(Clip())});
    status_=T("프롬프트를 기록했습니다.","Prompt recorded.");
    prompt_[0]=0;chatScroll_=true;
}
void EditorApp::Chat(float height) {
    ImGui::BeginChild("ai-chat",{0,height},0,ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::TextColored({.72f,.58f,1,1},"AI");ImGui::SameLine();ImGui::TextUnformatted(T("애니메이션 어시스턴트","Animation assistant"));
    ImGui::Separator();
    const float remaining=ImGui::GetContentRegionAvail().y;
    const float inputHeight=std::clamp(remaining*.28f,44.0f,80.0f);
    const float footer=ImGui::GetFrameHeight()+inputHeight+ImGui::GetStyle().ItemSpacing.y*3+5;
    const float listHeight=std::max(remaining-footer,1.0f);
    ImGui::BeginChild("conversation",{0,listHeight});
    if(messages_.empty()){
        ImGui::Spacing();ImGui::TextWrapped("%s",T("어떤 움직임을 만들까요?","What would you like to animate?"));
        ImGui::Spacing();
        if(ImGui::SmallButton(T("오른손으로 인사하기","Wave with the right hand")))strncpy_s(prompt_,T("오른손으로 자연스럽게 인사해 줘. 3초 길이로 만들어 줘.","Create a natural 3-second wave with the right hand."),_TRUNCATE);
        if(ImGui::SmallButton(T("차분한 대기 동작","Relaxed idle motion")))strncpy_s(prompt_,T("몸이 천천히 호흡하는 차분한 대기 동작을 만들어 줘.","Create a relaxed idle animation with gentle breathing."),_TRUNCATE);
    }
    for(size_t i=0;i<messages_.size();++i){
        const auto& m=messages_[i];ImGui::PushID(static_cast<int>(i));
        ImGui::TextColored(m.user?ImVec4{.72f,.58f,1,1}:ImVec4{.41f,.79f,.70f,1},"%s",m.user?T("나","You"):"LunaFlow");
        if(!m.target.empty()){ImGui::SameLine();ImGui::TextDisabled("%s",m.target.c_str());}
        ImGui::PushTextWrapPos();ImGui::TextUnformatted(m.text.c_str());ImGui::PopTextWrapPos();ImGui::Spacing();ImGui::PopID();
    }
    if(chatScroll_){ImGui::SetScrollHereY(1);chatScroll_=false;}ImGui::EndChild();
    ImGui::InputTextMultiline("##prompt",prompt_,sizeof(prompt_),{-1,inputHeight});
    const bool submit=ImGui::IsItemFocused()&&ImGui::GetIO().KeyCtrl&&ImGui::IsKeyPressed(ImGuiKey_Enter);
    if(ImGui::Button(T("보내기","Send"),{85,0})||submit)SendPrompt();
    ImGui::EndChild();
}
}

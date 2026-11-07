#include "App/EditorApp.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "App/UiTheme.h"
#include <commdlg.h>
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace LunaFlow {
EditorApp::EditorApp(HWND window,ID3D11Device* device,ID3D11DeviceContext* context,const std::filesystem::path& shaders,const std::filesystem::path& preferences):window_(window) {
    wchar_t executable[MAX_PATH]{};GetModuleFileNameW(nullptr,executable,MAX_PATH);
    settingsPath_=preferences.empty()?std::filesystem::path(executable).parent_path()/L"EditorSettings.json":preferences;
    settings_=LoadSettings(settingsPath_);
    renderer_.Initialize(device,context,shaders);renderer_.Resize(900,650);
    options_.skeleton=true;LoadDefaultCharacter();
}
void EditorApp::LoadDefaultCharacter() {
    wchar_t executable[MAX_PATH]{};GetModuleFileNameW(nullptr,executable,MAX_PATH);
    const auto character=std::filesystem::path(executable).parent_path()/L"Characters"/L"DefaultCharacter.fbx";
    try {
        auto model=ImportScene(character);
        const auto references=RemoveReferenceImagePlanes(model);
        for(const auto& name:references)model.warnings.push_back("Modeling reference image plane hidden from the default character: "+name);
        // The bundled FBX is an unskinned mesh; rig it so humanoid motions work out of the box.
        bool rigged=false;
        if(NeedsAutoRig(model)){
            try{
                const auto report=AutoRig(model);rigged=true;
                for(const auto& warning:report.warnings)model.warnings.push_back("Auto rig: "+warning);
            }catch(const std::exception& e){model.warnings.push_back(std::string("Auto rig failed: ")+e.what());}
        }
        UseModel(std::move(model));
        if(rigged)status_=T("기본 캐릭터를 열고 자동 리깅했습니다. 바로 동작을 만들 수 있습니다.","Default character opened and auto-rigged; it is ready for motion.");
        else if(std::none_of(model_.meshes.begin(),model_.meshes.end(),[](const auto& mesh){return mesh.skin.size()>1;}))
            status_=T("기본 캐릭터를 열었습니다. 이 FBX에는 스킨 리깅이 없어 사람형 동작을 만들려면 리깅된 모델이 필요합니다.","Default character opened. This FBX has no skin rig; humanoid motion requires a rigged model.");
    }catch(const std::exception&){
        UseModel(MakeDemoScene());
        status_=T("기본 캐릭터를 열 수 없어 예제 캐릭터를 표시합니다.","Could not open the default character; showing the sample character.");error_=true;
    }
}
const char* EditorApp::ClipLabel(const AnimationClip& c) const {
    if(c.id=="bind-pose"&&c.name=="Bind pose")return T("기준 자세","Bind pose");
    if(c.id=="demo-wave"&&c.name=="Wave / right hand")return T("손 흔들기","Wave");
    if(c.id=="demo-idle"&&c.name=="Idle / breathing")return T("대기 / 호흡","Idle / breathing");
    if(c.id=="demo-walk"&&c.name=="Walk / in place")return T("제자리 걷기","Walk / in place");
    if(c.id=="scene-motion"&&c.name=="Scene motion")return T("장면 애니메이션","Scene animation");
    if(c.id=="demo-cube"&&c.name=="Cube / rotation")return T("큐브 회전","Cube rotation");
    return c.name.c_str();
}
const char* EditorApp::ShapeLabel(Primitive p) const {
    switch(p) {
    case Primitive::Cube:return T("큐브","Cube");case Primitive::Sphere:return T("구","Sphere");
    case Primitive::Cylinder:return T("원기둥","Cylinder");case Primitive::Cone:return T("원뿔","Cone");
    case Primitive::Plane:return T("평면","Plane");case Primitive::Torus:return T("토러스","Torus");
    }return "";
}
void EditorApp::Try(const std::function<void()>& action) {
    error_=false;
    try{action();}catch(const std::exception& e){status_=std::string(T("작업 실패: ","Operation failed: "))+e.what();error_=true;}
}
void EditorApp::UseModel(ModelAsset next) {
    if(mocap_.active)throw std::runtime_error("Stop the webcam before replacing the scene.");
    CancelCopilot();editPlan_=EditPlan{};hasProportionReport_=false;motionSource_.reset();
    renderer_.SetModel(next);model_=std::move(next);
    selectedClip_=0;selectedBone_=0;time_=0;editBone_=-1;editClip_=-1;primitiveCount_=0;revealSelected_=true;selectedEffect_=-1;
    dirty_=false;bindPose_=false;playing_=false;draggingBone_=false;ClearHistory();nameClip_=-1;projectPath_.clear();selectedKeyTime_=-1;
    loop_=Clip().loop;
    agent_.Reset();vfxAgent_=VfxAgentState{};copilotVfx_=false;
    renderer_.FitCamera(camera_);camera_.orthographic=false;viewAxis_=-1;autoOrtho_=false;tween_=1;
    pose_=EvaluatePose(model_.skeleton,&Clip(),time_);status_.clear();
}
void EditorApp::AddShape(Primitive type) {
    Try([&]{
        ModelAsset next=model_;
        const float offset=static_cast<float>(primitiveCount_+1)*1.5f;
        const uint32_t bone=AddPrimitive(next,type,model_.meshes.empty()?XMFLOAT3{0,0,0}:XMFLOAT3{offset,0,0});
        renderer_.SetModel(next);model_=std::move(next);++primitiveCount_;
        selectedBone_=static_cast<int>(bone);selectedEffect_=-1;editBone_=-1;playing_=false;bindPose_=false;tool_=Tool::Move;revealSelected_=true;
        SelectWorkspace(0);
        pose_=EvaluatePose(model_.skeleton,&Clip(),time_);FrameAll();dirty_=true;
        status_=std::string(ShapeLabel(type))+T(" 추가됨. 뷰포트에서 드래그해 이동하세요."," added. Drag it in the viewport to move.");
    });
}
void EditorApp::LoadModel(const std::filesystem::path& path) {
    RequestSceneChange([this,path]{Try([&]{UseModel(ImportScene(path,unitsToMeters_));status_=std::string(T("불러옴: ","Opened: "))+path.filename().u8string();});});
}
std::filesystem::path EditorApp::Dialog(bool save,const wchar_t* filter,const wchar_t* extension,const wchar_t* filename) {
    wchar_t file[32768]{};wcsncpy_s(file,filename,_TRUNCATE);
    OPENFILENAMEW ofn{};ofn.lStructSize=sizeof(ofn);ofn.hwndOwner=window_;ofn.lpstrFilter=filter;
    ofn.lpstrFile=file;ofn.nMaxFile=32768;ofn.lpstrDefExt=extension;
    ofn.Flags=OFN_EXPLORER|OFN_NOCHANGEDIR|OFN_PATHMUSTEXIST|(save?OFN_OVERWRITEPROMPT:OFN_FILEMUSTEXIST);
    const BOOL ok=save?GetSaveFileNameW(&ofn):GetOpenFileNameW(&ofn);
    const DWORD error=ok?0:CommDlgExtendedError();
    // Native dialogs run their own message loop while ImGui is paused. Key-up and
    // focus events may be queued or delivered to the dialog instead of the editor.
    ReleaseDialogInput();
    if(error)throw std::runtime_error("The file dialog could not be opened.");
    return ok?std::filesystem::path(file):std::filesystem::path{};
}
void EditorApp::ReleaseDialogInput() {
    auto& io=ImGui::GetIO();io.ClearEventsQueue();io.ClearInputKeys();io.ClearInputMouse();
}
void EditorApp::OpenCharacter() {Try([&]{auto p=Dialog(false,L"3D character (*.fbx;*.gltf;*.glb;*.obj)\0*.fbx;*.gltf;*.glb;*.obj\0All files\0*.*\0\0",L"fbx");if(!p.empty())LoadModel(p);});}
void EditorApp::SaveMotion() {Try([&]{auto p=Dialog(true,L"LunaFlow motion (*.json)\0*.json\0\0",L"json",L"motion.json");if(!p.empty()){SaveClip(p,model_.skeleton,Clip());status_=T("현재 동작을 저장했습니다. 전체 장면은 프로젝트로 저장하세요.","Current motion saved. Save the project to preserve the entire scene.");}});}
void EditorApp::ImportMotion() {Try([&]{auto p=Dialog(false,L"LunaFlow motion (*.json)\0*.json\0\0",L"json");if(!p.empty()){auto clip=LoadClip(p,model_.skeleton);model_.clips.push_back(std::move(clip));selectedClip_=static_cast<int>(model_.clips.size()-1);SetTime(0);bindPose_=false;editClip_=-1;status_=T("동작을 불러왔습니다.","Motion imported.");}});}
void EditorApp::ExportBake() {Try([&]{auto p=Dialog(true,L"Final local poses (*.json)\0*.json\0\0",L"json",L"baked_motion.json");if(!p.empty()){SaveBake(p,model_.skeleton,BakeClip(model_.skeleton,Clip(),settings_.bakeFps));status_=T("최종 자세를 내보냈습니다.","Final poses exported.");}});}
void EditorApp::ExportSkeleton() {Try([&]{auto p=Dialog(true,L"Skeleton (*.json)\0*.json\0\0",L"json",L"skeleton.json");if(!p.empty()){SaveSkeleton(p,model_.skeleton);status_=T("뼈대 정보를 저장했습니다.","Skeleton saved.");}});}
void EditorApp::ExportImage() {Try([&]{auto p=Dialog(true,L"Preview image (*.png)\0*.png\0\0",L"png",L"preview.png");if(!p.empty()){renderer_.SaveScreenshot(p);status_=T("미리보기 이미지를 저장했습니다.","Preview image saved.");}});}
void EditorApp::ExportCharacter() {Try([&]{auto p=Dialog(true,L"FBX character (*.fbx)\0*.fbx\0\0",L"fbx",L"Character.fbx");if(!p.empty()){ExportFbx(p,model_,settings_.bakeFps);status_=T("캐릭터와 모든 동작을 FBX로 내보냈습니다.","Character and all motions exported to FBX.");}});}
void EditorApp::RememberEdit() {
    UndoState state{selectedClip_,Clip(),{}};
    if(workspace_==0)state.scene=std::make_shared<ModelAsset>(model_);
    PushUndo(std::move(state),true);
}
void EditorApp::TrimHistory(std::vector<UndoState>& history) {
    if(history.size()>32)history.erase(history.begin(),history.end()-32);
    size_t scenes=static_cast<size_t>(std::count_if(history.begin(),history.end(),[](const auto& item){return item.scene!=nullptr;}));
    while(scenes-->4)history.erase(std::find_if(history.begin(),history.end(),[](const auto& item){return item.scene!=nullptr;}));
}
void EditorApp::PushUndo(UndoState state,bool coalesceDrag) {
    // A drag edits every frame; like Unity and Blender keep one undo step per gesture.
    const ImGuiID active=ImGui::GetCurrentContext()?ImGui::GetActiveID():0;
    redo_.clear();
    if(coalesceDrag&&active!=0&&active==undoDragId_)return;
    undoDragId_=coalesceDrag?active:0;
    undo_.push_back(std::move(state));TrimHistory(undo_);
}
void EditorApp::ClearHistory(){undo_.clear();redo_.clear();undoDragId_=0;}
void EditorApp::Undo(){StepHistory(false);}
void EditorApp::Redo(){StepHistory(true);}
void EditorApp::StepHistory(bool redo) {
    if(copilotActive_)CancelCopilot();
    auto& from=redo?redo_:undo_;auto& to=redo?undo_:redo_;
    if(from.empty())return;auto previous=std::move(from.back());from.pop_back();undoDragId_=0;
    const bool clipValid=previous.clip>=0&&previous.clip<static_cast<int>(model_.clips.size());
    UndoState current;current.clip=previous.effects||previous.scene?selectedClip_:previous.clip;
    if(previous.effects)current.effects=std::make_shared<std::vector<EffectData>>(model_.effects);
    else if(previous.scene)current.scene=std::make_shared<ModelAsset>(model_);
    else if(clipValid)current.value=model_.clips[static_cast<size_t>(previous.clip)];
    if(previous.effects||previous.scene||clipValid){to.push_back(std::move(current));TrimHistory(to);}
    if(previous.effects){model_.effects=std::move(*previous.effects);selectedEffect_=-1;dirty_=true;playing_=false;status_=redo?T("효과 수정을 다시 실행했습니다.","Effect edit redone."):T("효과 수정을 되돌렸습니다.","Effect edit undone.");return;}
    if(previous.scene){
        for(auto& clip:previous.scene->clips){auto existing=std::find_if(model_.clips.begin(),model_.clips.end(),[&](const auto& c){return c.id==clip.id;});clip.revision=std::max(clip.revision,existing!=model_.clips.end()?existing->revision:uint64_t{0})+1;}
        Try([&]{renderer_.SetModel(*previous.scene);model_=std::move(*previous.scene);selectedClip_=std::clamp(previous.clip,0,static_cast<int>(model_.clips.size()-1));selectedBone_=std::min(selectedBone_,static_cast<int>(model_.skeleton.bones.size()-1));selectedEffect_=std::min(selectedEffect_,static_cast<int>(model_.effects.size())-1);editBone_=-1;dirty_=true;SetTime(time_);});
        if(!error_)status_=redo?T("장면 수정을 다시 실행했습니다.","Scene edit redone."):T("장면 수정을 되돌렸습니다.","Scene edit undone.");
        return;
    }
    if(!clipValid)return;
    selectedClip_=previous.clip;previous.value.revision=Clip().revision+1;Clip()=std::move(previous.value);time_=std::min(time_,PlaybackDuration(Clip()));nameClip_=-1;loop_=Clip().loop;
    playing_=false;dirty_=true;editBone_=-1;pose_=EvaluatePose(model_.skeleton,&Clip(),time_);
    status_=redo?T("동작 수정을 다시 실행했습니다.","Motion edit redone."):T("마지막 동작 수정을 되돌렸습니다.","Last motion edit undone.");
}
void EditorApp::DuplicateClip() {
    Dispatch("duplicate_clip",{{"name",std::string(ClipLabel(Clip()))+T(" 복사본"," copy")}});
}
void EditorApp::Dispatch(const std::string& type,const Json& args) {
    Try([&]{
        const UndoState previous{selectedClip_,Clip()};
        Json command={{"type",type},{"clipId",Clip().id},{"expectedRevision",Clip().revision},{"args",args}};
        const auto result=ApplyEditorCommand(model_.clips,model_.skeleton,command);
        if(type=="delete_clip"){
            const auto removed=previous.value.id;
            model_.effects.erase(std::remove_if(model_.effects.begin(),model_.effects.end(),[&](const auto& effect){return effect.clipId==removed;}),model_.effects.end());
            ClearHistory();selectedEffect_=-1;selectedClip_=std::min(selectedClip_,static_cast<int>(model_.clips.size()-1));
        }
        else {
            const auto id=result.at("clipId").get<std::string>();
            for(size_t i=0;i<model_.clips.size();++i)if(model_.clips[i].id==id)selectedClip_=static_cast<int>(i);
            if(type!="new_clip"&&type!="duplicate_clip")PushUndo(previous);
        }
        dirty_=true;nameClip_=-1;editClip_=-1;loop_=Clip().loop;SetTime(std::min(time_,PlaybackDuration(Clip())));
        status_=T("동작 명령을 적용했습니다.","Motion command applied.");
    });
}
void EditorApp::OpenProject() {Try([&]{
    auto path=Dialog(false,L"LunaFlow project (*.lunaflow;*.json)\0*.lunaflow;*.json\0\0",L"lunaflow");if(path.empty())return;
    auto document=std::make_shared<ProjectData>(LoadProject(path));RequestSceneChange([this,path,document]{auto& project=*document;UseModel(std::move(project.model));projectPath_=path;
    for(size_t i=0;i<model_.clips.size();++i)if(model_.clips[i].id==project.selectedClipId)selectedClip_=static_cast<int>(i);
    for(size_t i=0;i<model_.skeleton.bones.size();++i)if(model_.skeleton.bones[i].path==project.selectedBonePath)selectedBone_=static_cast<int>(i);
    camera_.target=project.cameraTarget;camera_.yaw=project.cameraYaw;camera_.pitch=project.cameraPitch;camera_.distance=project.cameraDistance;tween_=1;
    loop_=Clip().loop;SetTime(project.time);dirty_=false;status_=T("프로젝트를 복원했습니다.","Project restored.");});
});}
bool EditorApp::SaveCurrentProject(bool saveAs) {bool saved=false;Try([&]{
    auto path=projectPath_;if(saveAs||path.empty())path=Dialog(true,L"LunaFlow project (*.lunaflow)\0*.lunaflow\0\0",L"lunaflow",L"Project.lunaflow");if(path.empty())return;
    ProjectData project;project.model=model_;project.selectedClipId=Clip().id;project.selectedBonePath=model_.skeleton.bones[selectedBone_].path;project.time=time_;
    const auto camera=CameraGoal();
    project.cameraTarget=camera.target;project.cameraYaw=camera.yaw;project.cameraPitch=camera.pitch;project.cameraDistance=camera.distance;
    SaveProject(path,project);projectPath_=path;dirty_=false;saved=true;status_=T("전체 프로젝트를 저장했습니다.","Entire project saved.");
});return saved;}
void EditorApp::RequestSceneChange(std::function<void()> action){if(mocap_.active){status_=T("먼저 웹캠을 종료하세요.","Stop the webcam first.");error_=true;return;}if(dirty_)pendingSceneChange_=std::move(action);else Try(action);}
void EditorApp::PendingChanges() {
    // Closing during capture first releases the camera worker (PumpMocap); the unsaved prompt follows.
    if(mocap_.active)return;
    if((closeRequested_||pendingSceneChange_)&&!dirty_){if(closeRequested_){quitRequested_=true;closeRequested_=false;}else {auto action=std::move(pendingSceneChange_);pendingSceneChange_={};Try(action);}return;}
    if(closeRequested_||pendingSceneChange_)ImGui::OpenPopup("Unsaved###Unsaved");
    const auto* viewport=ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->GetCenter(),ImGuiCond_Appearing,{.5f,.5f});
    ImGui::SetNextWindowSizeConstraints({0,0},{viewport->WorkSize.x*.94f,viewport->WorkSize.y*.94f});
    if(ImGui::BeginPopupModal(T("저장하지 않은 변경###Unsaved","Unsaved changes###Unsaved"),nullptr,ImGuiWindowFlags_AlwaysAutoResize)){
        const auto& style=ImGui::GetStyle();
        const char* save=T("저장","Save"),*discard=T("저장하지 않음","Discard"),*cancel=T("취소","Cancel");
        const float saveWidth=std::max(UiScale(100),ImGui::CalcTextSize(save).x+style.FramePadding.x*2);
        const float discardWidth=std::max(UiScale(100),ImGui::CalcTextSize(discard).x+style.FramePadding.x*2);
        const float cancelWidth=std::max(UiScale(100),ImGui::CalcTextSize(cancel).x+style.FramePadding.x*2);
        const float maxWidth=viewport->WorkSize.x*.94f-style.WindowPadding.x*2;
        const float rowWidth=saveWidth+discardWidth+cancelWidth+style.ItemSpacing.x*2;
        const bool stacked=rowWidth>maxWidth;
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX()+maxWidth);
        ImGui::TextUnformatted(T("현재 프로젝트의 변경 내용을 저장할까요?","Save changes to the current project?"));ImGui::PopTextWrapPos();
        auto proceed=[&]{if(closeRequested_)quitRequested_=true;else{auto action=std::move(pendingSceneChange_);pendingSceneChange_={};Try(action);}closeRequested_=false;ImGui::CloseCurrentPopup();};
        if(ImGui::Button(save,{stacked?maxWidth:saveWidth,0})){if(SaveCurrentProject())proceed();}if(!stacked)ImGui::SameLine();
        if(ImGui::Button(discard,{stacked?maxWidth:discardWidth,0}))proceed();if(!stacked)ImGui::SameLine();
        if(ImGui::Button(cancel,{stacked?maxWidth:cancelWidth,0})){closeRequested_=false;pendingSceneChange_={};ImGui::CloseCurrentPopup();}ImGui::EndPopup();
    }
}
void EditorApp::SetTime(double t) {
    time_=std::clamp(t,0.0,PlaybackDuration(Clip()));playing_=false;editBone_=-1;
    pose_=EvaluatePose(model_.skeleton,bindPose_?nullptr:&Clip(),time_);
}
void EditorApp::CommitPose(bool updateQuaternion) {
    if(updateQuaternion)edit_.rotation=QuaternionFromDegrees(editAngles_);
    const int object=ObjectForBone(model_,static_cast<uint32_t>(selectedBone_));
    if(workspace_==0&&object>=0&&model_.objects[object].root==static_cast<uint32_t>(selectedBone_)){
        const auto current=EvaluatePose(model_.skeleton,&Clip(),time_).local[selectedBone_];
        const auto base=Decompose(Matrix(model_.skeleton.bones[selectedBone_].bindLocal)*XMMatrixInverse(nullptr,Matrix(current))*Matrix(edit_));SetObjectTransform(model_,static_cast<uint32_t>(selectedBone_),base);
    }else SetTransformKey(Clip(),static_cast<uint32_t>(selectedBone_),std::clamp(time_*Clip().speed,0.0,Clip().duration),edit_);
    dirty_=true;pose_=EvaluatePose(model_.skeleton,&Clip(),time_);error_=false;
    status_=T("현재 프레임에 키프레임이 기록되었습니다.","Keyframe recorded at the current frame.");
}
void EditorApp::Frame(float deltaTime) {
    PumpMocap(deltaTime);
    // Webcam capture owns the pose; AI edits, playback and shortcuts wait until it stops.
    if(!mocap_.active)PumpCopilot(deltaTime);
    PumpHelpBot(deltaTime);
    const auto selectedId=Clip().id;bool agentChanged=false;auto agentMessages=mocap_.active?std::vector<std::string>{}:agent_.Drain(model_,&agentChanged);
    if(!agentMessages.empty()){
        selectedClip_=std::min(selectedClip_,static_cast<int>(model_.clips.size()-1));
        for(size_t i=0;i<model_.clips.size();++i)if(model_.clips[i].id==selectedId)selectedClip_=static_cast<int>(i);
        for(auto& message:agentMessages)messages_.push_back({false,std::move(message),{}});
        if(agentChanged)dirty_=true;chatScroll_=true;editClip_=-1;time_=std::min(time_,PlaybackDuration(Clip()));
    }
    if(playing_&&!bindPose_&&!mocap_.active){const double duration=PlaybackDuration(Clip());time_+=std::min(deltaTime,.25f);if(duration>0&&loop_)time_=std::fmod(time_,duration);else if(time_>=duration){time_=duration;playing_=false;}}
    auto& io=ImGui::GetIO();
    if(undoDragId_&&ImGui::GetActiveID()!=undoDragId_)undoDragId_=0;
    if(mocap_.active)MocapShortcuts();else Shortcuts();UpdateCameraTween(deltaTime);flySpeedNotice_=std::max(flySpeedNotice_-deltaTime,0.0f);
    const auto* main=ImGui::GetMainViewport();ImGui::SetNextWindowPos(main->WorkPos);ImGui::SetNextWindowSize(main->WorkSize);
    ImGui::Begin("LunaFlow",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_MenuBar|ImGuiWindowFlags_NoBringToFrontOnFocus|ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::SetScrollY(0);
    ImGui::BeginDisabled(mocap_.active);MenuBar();Workspaces();ImGui::EndDisabled();
    const auto& style=ImGui::GetStyle();
    const float footerHeight=ImGui::GetTextLineHeight()+style.ItemSpacing.y*3+1+style.CellPadding.y*2+UiScale(2);
    const float height=std::max(ImGui::GetContentRegionAvail().y-footerHeight,1.0f);
    if(ImGui::BeginTable("workspace",3,ImGuiTableFlags_Resizable|ImGuiTableFlags_BordersInnerV|ImGuiTableFlags_SizingStretchProp)){
        ImGui::TableSetupColumn("Library",ImGuiTableColumnFlags_WidthStretch,.18f);
        ImGui::TableSetupColumn("Viewport",ImGuiTableColumnFlags_WidthStretch,.56f);
        ImGui::TableSetupColumn("Properties",ImGuiTableColumnFlags_WidthStretch,.26f);
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);ImGui::BeginDisabled(mocap_.active);ImGui::BeginChild("library",{0,height});Library();ImGui::EndChild();ImGui::EndDisabled();
        pose_=EvaluatePose(model_.skeleton,bindPose_?nullptr:&Clip(),time_);
        // Live capture drives the viewport; while a take is under review the take itself loops instead.
        if(mocap_.active&&mocap_.review<0&&!mocap_.stopping)pose_=mocap_.capture.Preview();
        ImGui::TableSetColumnIndex(2);ImGui::BeginDisabled(mocap_.active);
        const float chatMinimum=std::min(UiScale(150),height*.5f);
        // A hidden assistant collapses to a one-line reopen bar, like a docked Blender region.
        const float reopenBar=showChat_?0:ImGui::GetFrameHeight()+style.ItemSpacing.y;
        const float inspectorHeight=showInspector_?(showChat_?std::min(height*(workspace_==2?.55f:.42f),std::max(height-chatMinimum-style.ItemSpacing.y,1.0f)):std::max(height-reopenBar,1.0f)):0;
        if(showInspector_){ImGui::BeginChild("inspector",{0,inspectorHeight});Inspector();ImGui::EndChild();}
        if(showChat_){if(showInspector_)ImGui::Spacing();Chat(std::max(height-inspectorHeight-(showInspector_?style.ItemSpacing.y:0),1.0f));}
        else if(ImGui::Button(workspace_==2?T("VFX AI 열기###ShowChat","Open VFX AI###ShowChat"):T("AI 대화 열기###ShowChat","Open AI chat###ShowChat"),{ImGui::GetContentRegionAvail().x,0}))showChat_=true;
        ImGui::EndDisabled();
        ImGui::TableSetColumnIndex(1);
        const float maxTimeline=std::max(height-ImGui::GetFrameHeight()*2-style.ItemSpacing.y,1.0f);
        const float minimumTimeline=std::min(TimelineMinimumHeight(ImGui::GetContentRegionAvail().x),maxTimeline);
        const float timelineHeight=std::clamp(height*(workspace_==1?.28f:.22f),minimumTimeline,maxTimeline);
        Viewport(std::max(height-timelineHeight-style.ItemSpacing.y,1.0f));ImGui::Spacing();ImGui::BeginDisabled(mocap_.active);Timeline(timelineHeight);ImGui::EndDisabled();
        ImGui::EndTable();
    }
    ImGui::Separator();
    char fps[80]{};std::snprintf(fps,sizeof(fps),"%.0f FPS  |  %.1f ms",io.Framerate,1000.0f/std::max(io.Framerate,1.0f));
    const float fpsWidth=settings_.showFps?ImGui::CalcTextSize(fps).x+style.ItemSpacing.x:0;
    const char* helpLabel=T("도우미###HelpBotButton","Helper###HelpBotButton");
    const float helpWidth=ImGui::CalcTextSize(helpLabel,nullptr,true).x+style.FramePadding.x*2+style.ItemSpacing.x;
    ImGui::BeginChild("status",{std::max(ImGui::GetContentRegionAvail().x-fpsWidth-helpWidth,1.0f),ImGui::GetTextLineHeight()},0,ImGuiWindowFlags_NoScrollbar);
    const char* ready=T("준비됨","Ready");
    ImGui::TextColored(error_?ImVec4{1,.48f,.47f,1}:ImVec4{.51f,.57f,.67f,1},"%s%s",dirty_?T("● 저장 안 됨  ","* Unsaved  "):"",status_.empty()?ready:status_.c_str());
    if(ImGui::IsItemHovered())ImGui::SetTooltip("%s",status_.empty()?ready:status_.c_str());ImGui::EndChild();
    if(settings_.showFps){ImGui::SameLine();ImGui::TextDisabled("%s",fps);}
    ImGui::SameLine();
    if(helpBot_.show)ImGui::PushStyleColor(ImGuiCol_Button,ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
    if(ImGui::SmallButton(helpLabel))ToggleHelpBot();
    if(helpBot_.show)ImGui::PopStyleColor();
    if(ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))ImGui::SetTooltip("%s",T("LunaFlow 사용법을 묻는 도우미 (Shift+F1)","Ask how to use LunaFlow (Shift+F1)"));
    ImGui::BeginDisabled(mocap_.active);Settings();ShortcutHelp();FbxTools();ImGui::EndDisabled();PendingChanges();
    // Arrow keys belong to ImGui navigation only while a widget shows the nav cursor.
    const auto& g=*ImGui::GetCurrentContext();keyboardNav_=g.NavCursorVisible&&g.NavId!=0;
    ImGui::End();
    MocapPanel();
    HelpBotWindow();
}
}

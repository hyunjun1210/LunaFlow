#include "App/EditorApp.h"
#include "imgui.h"
#include <commdlg.h>
#include <algorithm>
#include <cmath>

namespace LunaFlow {
EditorApp::EditorApp(HWND window,ID3D11Device* device,ID3D11DeviceContext* context,const std::filesystem::path& shaders,const std::filesystem::path& preferences):window_(window) {
    wchar_t executable[MAX_PATH]{};GetModuleFileNameW(nullptr,executable,MAX_PATH);
    settingsPath_=preferences.empty()?std::filesystem::path(executable).parent_path()/L"EditorSettings.json":preferences;
    settings_=LoadSettings(settingsPath_);
    renderer_.Initialize(device,context,shaders);renderer_.Resize(900,650);
    options_.skeleton=true;UseModel(MakeDemoScene());
}
const char* EditorApp::ClipLabel(const AnimationClip& c) const {
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
    renderer_.SetModel(next);model_=std::move(next);
    selectedClip_=0;selectedBone_=0;time_=0;editBone_=-1;editClip_=-1;primitiveCount_=0;revealSelected_=true;selectedEffect_=-1;
    dirty_=false;bindPose_=false;playing_=false;draggingBone_=false;undo_.clear();nameClip_=-1;projectPath_.clear();selectedKeyTime_=-1;
    loop_=Clip().loop;
    agent_.Reset();
    renderer_.FitCamera(camera_);pose_=EvaluatePose(model_.skeleton,&Clip(),time_);status_.clear();
}
void EditorApp::AddShape(Primitive type) {
    Try([&]{
        ModelAsset next=model_;
        const float offset=static_cast<float>(primitiveCount_+1)*1.5f;
        const uint32_t bone=AddPrimitive(next,type,model_.meshes.empty()?XMFLOAT3{0,0,0}:XMFLOAT3{offset,0,0});
        renderer_.SetModel(next);model_=std::move(next);++primitiveCount_;
        selectedBone_=static_cast<int>(bone);selectedEffect_=-1;editBone_=-1;playing_=false;bindPose_=false;tool_=Tool::Move;revealSelected_=true;
        SelectWorkspace(0);
        pose_=EvaluatePose(model_.skeleton,&Clip(),time_);renderer_.FitCamera(camera_);dirty_=true;
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
    if(!ok&&CommDlgExtendedError())throw std::runtime_error("The file dialog could not be opened.");
    return ok?std::filesystem::path(file):std::filesystem::path{};
}
void EditorApp::OpenCharacter() {Try([&]{auto p=Dialog(false,L"3D character (*.fbx;*.gltf;*.glb;*.obj)\0*.fbx;*.gltf;*.glb;*.obj\0All files\0*.*\0\0",L"fbx");if(!p.empty())LoadModel(p);});}
void EditorApp::SaveMotion() {Try([&]{auto p=Dialog(true,L"LunaFlow motion (*.json)\0*.json\0\0",L"json",L"motion.json");if(!p.empty()){SaveClip(p,model_.skeleton,Clip());status_=T("현재 동작을 저장했습니다. 전체 장면은 프로젝트로 저장하세요.","Current motion saved. Save the project to preserve the entire scene.");}});}
void EditorApp::ImportMotion() {Try([&]{auto p=Dialog(false,L"LunaFlow motion (*.json)\0*.json\0\0",L"json");if(!p.empty()){auto clip=LoadClip(p,model_.skeleton);model_.clips.push_back(std::move(clip));selectedClip_=static_cast<int>(model_.clips.size()-1);SetTime(0);bindPose_=false;editClip_=-1;status_=T("동작을 불러왔습니다.","Motion imported.");}});}
void EditorApp::ExportBake() {Try([&]{auto p=Dialog(true,L"Final local poses (*.json)\0*.json\0\0",L"json",L"baked_motion.json");if(!p.empty()){SaveBake(p,model_.skeleton,BakeClip(model_.skeleton,Clip(),settings_.bakeFps));status_=T("최종 자세를 내보냈습니다.","Final poses exported.");}});}
void EditorApp::ExportSkeleton() {Try([&]{auto p=Dialog(true,L"Skeleton (*.json)\0*.json\0\0",L"json",L"skeleton.json");if(!p.empty()){SaveSkeleton(p,model_.skeleton);status_=T("뼈대 정보를 저장했습니다.","Skeleton saved.");}});}
void EditorApp::ExportImage() {Try([&]{auto p=Dialog(true,L"Preview image (*.png)\0*.png\0\0",L"png",L"preview.png");if(!p.empty()){renderer_.SaveScreenshot(p);status_=T("미리보기 이미지를 저장했습니다.","Preview image saved.");}});}
void EditorApp::ExportCharacter() {Try([&]{auto p=Dialog(true,L"FBX character (*.fbx)\0*.fbx\0\0",L"fbx",L"Character.fbx");if(!p.empty()){ExportFbx(p,model_,settings_.bakeFps);status_=T("캐릭터와 모든 동작을 FBX로 내보냈습니다.","Character and all motions exported to FBX.");}});}
void EditorApp::RememberEdit() {
    if(undo_.size()>=32)undo_.erase(undo_.begin());UndoState state{selectedClip_,Clip(),{}};
    if(workspace_==0){size_t count=0;for(const auto& undo:undo_)if(undo.scene)++count;
        if(count>=4){auto oldest=std::find_if(undo_.begin(),undo_.end(),[](const auto& item){return item.scene!=nullptr;});undo_.erase(oldest);}state.scene=std::make_shared<ModelAsset>(model_);
    }undo_.push_back(std::move(state));
}
void EditorApp::Undo() {
    if(undo_.empty())return;auto previous=std::move(undo_.back());undo_.pop_back();
    if(previous.scene){
        for(auto& clip:previous.scene->clips){auto current=std::find_if(model_.clips.begin(),model_.clips.end(),[&](const auto& c){return c.id==clip.id;});clip.revision=std::max(clip.revision,current!=model_.clips.end()?current->revision:uint64_t{0})+1;}
        Try([&]{renderer_.SetModel(*previous.scene);model_=std::move(*previous.scene);selectedClip_=std::min(previous.clip,static_cast<int>(model_.clips.size()-1));selectedBone_=std::min(selectedBone_,static_cast<int>(model_.skeleton.bones.size()-1));editBone_=-1;dirty_=true;SetTime(time_);});return;
    }
    selectedClip_=previous.clip;previous.value.revision=Clip().revision+1;Clip()=std::move(previous.value);time_=std::min(time_,PlaybackDuration(Clip()));nameClip_=-1;loop_=Clip().loop;
    playing_=false;dirty_=true;editBone_=-1;pose_=EvaluatePose(model_.skeleton,&Clip(),time_);
    status_=T("마지막 동작 수정을 되돌렸습니다.","Last motion edit undone.");
}
void EditorApp::DuplicateClip() {
    Dispatch("duplicate_clip",{{"name",std::string(ClipLabel(Clip()))+T(" 복사본"," copy")}});
}
void EditorApp::Dispatch(const std::string& type,const Json& args) {
    Try([&]{
        const UndoState previous{selectedClip_,Clip()};
        Json command={{"type",type},{"clipId",Clip().id},{"expectedRevision",Clip().revision},{"args",args}};
        const auto result=ApplyEditorCommand(model_.clips,model_.skeleton,command);
        if(type=="delete_clip"){undo_.clear();selectedClip_=std::min(selectedClip_,static_cast<int>(model_.clips.size()-1));}
        else {
            const auto id=result.at("clipId").get<std::string>();
            for(size_t i=0;i<model_.clips.size();++i)if(model_.clips[i].id==id)selectedClip_=static_cast<int>(i);
            if(type!="new_clip"&&type!="duplicate_clip"){if(undo_.size()>=32)undo_.erase(undo_.begin());undo_.push_back(previous);}
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
    camera_.target=project.cameraTarget;camera_.yaw=project.cameraYaw;camera_.pitch=project.cameraPitch;camera_.distance=project.cameraDistance;
    loop_=Clip().loop;SetTime(project.time);dirty_=false;status_=T("프로젝트를 복원했습니다.","Project restored.");});
});}
bool EditorApp::SaveCurrentProject(bool saveAs) {bool saved=false;Try([&]{
    auto path=projectPath_;if(saveAs||path.empty())path=Dialog(true,L"LunaFlow project (*.lunaflow)\0*.lunaflow\0\0",L"lunaflow",L"Project.lunaflow");if(path.empty())return;
    ProjectData project;project.model=model_;project.selectedClipId=Clip().id;project.selectedBonePath=model_.skeleton.bones[selectedBone_].path;project.time=time_;
    project.cameraTarget=camera_.target;project.cameraYaw=camera_.yaw;project.cameraPitch=camera_.pitch;project.cameraDistance=camera_.distance;
    SaveProject(path,project);projectPath_=path;dirty_=false;saved=true;status_=T("전체 프로젝트를 저장했습니다.","Entire project saved.");
});return saved;}
void EditorApp::RequestSceneChange(std::function<void()> action){if(dirty_)pendingSceneChange_=std::move(action);else Try(action);}
void EditorApp::PendingChanges() {
    if((closeRequested_||pendingSceneChange_)&&!dirty_){if(closeRequested_){quitRequested_=true;closeRequested_=false;}else {auto action=std::move(pendingSceneChange_);pendingSceneChange_={};Try(action);}return;}
    if(closeRequested_||pendingSceneChange_)ImGui::OpenPopup("Unsaved###Unsaved");
    if(ImGui::BeginPopupModal(T("저장하지 않은 변경###Unsaved","Unsaved changes###Unsaved"),nullptr,ImGuiWindowFlags_AlwaysAutoResize)){
        ImGui::TextUnformatted(T("현재 프로젝트의 변경 내용을 저장할까요?","Save changes to the current project?"));
        auto proceed=[&]{if(closeRequested_)quitRequested_=true;else{auto action=std::move(pendingSceneChange_);pendingSceneChange_={};Try(action);}closeRequested_=false;ImGui::CloseCurrentPopup();};
        if(ImGui::Button(T("저장","Save"),{100,0})){if(SaveCurrentProject())proceed();}ImGui::SameLine();
        if(ImGui::Button(T("저장하지 않음","Discard"),{130,0}))proceed();ImGui::SameLine();
        if(ImGui::Button(T("취소","Cancel"),{100,0})){closeRequested_=false;pendingSceneChange_={};ImGui::CloseCurrentPopup();}ImGui::EndPopup();
    }
}
void EditorApp::SetTime(double t) {
    time_=std::clamp(t,0.0,PlaybackDuration(Clip()));playing_=false;editBone_=-1;
    pose_=EvaluatePose(model_.skeleton,bindPose_?nullptr:&Clip(),time_);
}
void EditorApp::FrameAll() {Try([&]{pose_=EvaluatePose(model_.skeleton,bindPose_?nullptr:&Clip(),time_);renderer_.UpdateBounds(model_,pose_);renderer_.FitCamera(camera_);});}
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
    const auto selectedId=Clip().id;bool agentChanged=false;auto agentMessages=agent_.Drain(model_,&agentChanged);
    if(!agentMessages.empty()){
        selectedClip_=std::min(selectedClip_,static_cast<int>(model_.clips.size()-1));
        for(size_t i=0;i<model_.clips.size();++i)if(model_.clips[i].id==selectedId)selectedClip_=static_cast<int>(i);
        for(auto& message:agentMessages)messages_.push_back({false,std::move(message),{}});
        if(agentChanged)dirty_=true;chatScroll_=true;editClip_=-1;time_=std::min(time_,PlaybackDuration(Clip()));
    }
    if(playing_&&!bindPose_){const double duration=PlaybackDuration(Clip());time_+=std::min(deltaTime,.25f);if(duration>0&&loop_)time_=std::fmod(time_,duration);else if(time_>=duration){time_=duration;playing_=false;}}
    auto& io=ImGui::GetIO();
    if(!io.WantTextInput&&!ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopupId)){
        if(ImGui::IsKeyPressed(ImGuiKey_Space))playing_=!playing_;
        if(io.KeyCtrl&&ImGui::IsKeyPressed(ImGuiKey_S))SaveCurrentProject(io.KeyShift);
        if(io.KeyCtrl&&ImGui::IsKeyPressed(ImGuiKey_O))OpenProject();
        if(io.KeyCtrl&&ImGui::IsKeyPressed(ImGuiKey_Z))Undo();
    }
    const auto* main=ImGui::GetMainViewport();ImGui::SetNextWindowPos(main->WorkPos);ImGui::SetNextWindowSize(main->WorkSize);
    ImGui::Begin("LunaFlow",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_MenuBar|ImGuiWindowFlags_NoBringToFrontOnFocus|ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::SetScrollY(0);
    MenuBar();Workspaces();
    const float height=std::max(ImGui::GetContentRegionAvail().y-48,250.0f);
    if(ImGui::BeginTable("workspace",3,ImGuiTableFlags_Resizable|ImGuiTableFlags_BordersInnerV|ImGuiTableFlags_SizingStretchProp)){
        ImGui::TableSetupColumn("Library",ImGuiTableColumnFlags_WidthFixed,230);
        ImGui::TableSetupColumn("Viewport",ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Properties",ImGuiTableColumnFlags_WidthFixed,330);
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);ImGui::BeginChild("library",{0,height});Library();ImGui::EndChild();
        pose_=EvaluatePose(model_.skeleton,bindPose_?nullptr:&Clip(),time_);
        ImGui::TableSetColumnIndex(2);
        const float desiredInspector=workspace_==2||selectedEffect_>=0?250.0f:model_.objects.size()>1?290.0f:190.0f;
        const float inspectorHeight=showInspector_?(showChat_?std::min(desiredInspector,height*.52f):height):0;
        if(showInspector_){ImGui::BeginChild("inspector",{0,inspectorHeight});Inspector();ImGui::EndChild();}
        if(showChat_){if(showInspector_)ImGui::Spacing();Chat(std::max(height-inspectorHeight-(showInspector_?8:0),100.0f));}
        ImGui::TableSetColumnIndex(1);const float timelineHeight=workspace_==1?175.0f:130.0f;
        Viewport(std::max(height-timelineHeight-8,100.0f));ImGui::Spacing();Timeline(timelineHeight);
        ImGui::EndTable();
    }
    ImGui::Separator();
    const float fpsWidth=settings_.showFps?190.0f:0;
    ImGui::BeginChild("status",{std::max(ImGui::GetContentRegionAvail().x-fpsWidth,1.0f),22},0,ImGuiWindowFlags_NoScrollbar);
    const char* ready=T("준비됨","Ready");
    ImGui::TextColored(error_?ImVec4{1,.48f,.47f,1}:ImVec4{.51f,.57f,.67f,1},"%s%s",dirty_?T("● 저장 안 됨  ","* Unsaved  "):"",status_.empty()?ready:status_.c_str());
    if(ImGui::IsItemHovered())ImGui::SetTooltip("%s",status_.empty()?ready:status_.c_str());ImGui::EndChild();
    if(settings_.showFps){ImGui::SameLine();ImGui::TextDisabled("%.0f FPS  |  %.1f ms",io.Framerate,1000.0f/std::max(io.Framerate,1.0f));}
    Settings();PendingChanges();ImGui::End();
}
}

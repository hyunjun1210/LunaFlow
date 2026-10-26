#include "App/EditorApp.h"
#include "imgui.h"
#include <commdlg.h>
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace LunaFlow {
static const ImVec4 purple{.72f,.58f,1,1},mint{.36f,.86f,.72f,1},muted{.48f,.54f,.65f,1};
static void Label(const char* text) { ImGui::TextColored(muted,"%s",text); }
EditorApp::EditorApp(HWND w,ID3D11Device* d,ID3D11DeviceContext* c,const std::filesystem::path& shaders):window_(w) {
    renderer_.Initialize(d,c,shaders); renderer_.Resize(900,650); UseModel(MakeDemoScene());
}
void EditorApp::Try(const std::function<void()>& action) {
    error_=false;
    try { action(); } catch (const std::exception& e) { status_=e.what(); error_=true; }
}
void EditorApp::UseModel(ModelAsset next) {
    renderer_.SetModel(next); // Commit only after all GPU buffers have been created.
    model_=std::move(next); selectedClip_=0; selectedBone_=0; time_=0; editBone_=-1; editClip_=-1;
    dirty_=false; bindPose_=false; playing_=true; renderer_.FitCamera(camera_);
    pose_=EvaluatePose(model_.skeleton,&Clip(),time_);
}
void EditorApp::LoadModel(const std::filesystem::path& path) {
    Try([&] { UseModel(ImportScene(path,unitsToMeters_)); status_="Opened " + path.filename().u8string(); });
}
std::filesystem::path EditorApp::Dialog(bool save,const wchar_t* filter,const wchar_t* extension,const wchar_t* filename) {
    wchar_t file[32768]{}; wcsncpy_s(file,filename,_TRUNCATE);
    OPENFILENAMEW ofn{}; ofn.lStructSize=sizeof(ofn); ofn.hwndOwner=window_; ofn.lpstrFilter=filter;
    ofn.lpstrFile=file; ofn.nMaxFile=32768; ofn.lpstrDefExt=extension;
    ofn.Flags=OFN_EXPLORER|OFN_NOCHANGEDIR|OFN_PATHMUSTEXIST|(save?OFN_OVERWRITEPROMPT:OFN_FILEMUSTEXIST);
    const BOOL ok=save?GetSaveFileNameW(&ofn):GetOpenFileNameW(&ofn);
    if (!ok && CommDlgExtendedError()) throw std::runtime_error("The file dialog could not be opened.");
    return ok?std::filesystem::path(file):std::filesystem::path{};
}
void EditorApp::Toolbar() {
    ImGui::TextColored(purple,"L U N A F L O W"); ImGui::SameLine(); ImGui::TextDisabled(" /  MOTION STUDIO");
    ImGui::SameLine(ImGui::GetContentRegionAvail().x-190); ImGui::TextColored(mint,"TEAM A   /   DIRECTX 11");
    ImGui::Spacing();
    if (ImGui::Button("Open character",{145,0})) Try([&] {
        auto path=Dialog(false,L"3D character (*.fbx;*.gltf;*.glb;*.obj)\0*.fbx;*.gltf;*.glb;*.obj\0All files\0*.*\0\0",L"fbx");
        if (!path.empty()) { UseModel(ImportScene(path,unitsToMeters_)); status_="Opened "+path.filename().u8string(); }
    });
    ImGui::SameLine();
    if (ImGui::Button("Luna demo")) Try([&] { UseModel(MakeDemoScene()); status_="Built-in Luna rig loaded."; });
    ImGui::SameLine();
    if (ImGui::Button("Cube")) Try([&] { UseModel(MakeCubeScene()); status_="DX11 cube test loaded."; });
    ImGui::SameLine(); ImGui::TextDisabled("|"); ImGui::SameLine();
    if (ImGui::Button("Save motion")) Try([&] {
        auto p=Dialog(true,L"LunaFlow motion (*.json)\0*.json\0\0",L"json",L"motion.json");
        if (!p.empty()) { SaveClip(p,model_.skeleton,Clip()); dirty_=false; status_="Motion saved: "+p.u8string(); }
    });
    ImGui::SameLine();
    if (ImGui::Button("Import motion")) Try([&] {
        auto p=Dialog(false,L"LunaFlow motion (*.json)\0*.json\0\0",L"json");
        if (!p.empty()) {
            auto clip=LoadClip(p,model_.skeleton); model_.clips.push_back(std::move(clip));
            selectedClip_=static_cast<int>(model_.clips.size()-1); time_=0; editClip_=-1; status_="Motion imported for this skeleton.";
        }
    });
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button,{.27f,.20f,.43f,1});
    if (ImGui::Button("Bake 30 FPS")) Try([&] {
        auto p=Dialog(true,L"Final local poses (*.json)\0*.json\0\0",L"json",L"baked_motion.json");
        if (!p.empty()) { SaveBake(p,model_.skeleton,BakeClip(model_.skeleton,Clip(),30)); status_="Final poses saved for Team C: "+p.u8string(); }
    });
    ImGui::PopStyleColor(); ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
}
void EditorApp::Library() {
    Label("MOTION LIBRARY"); ImGui::Spacing();
    for (size_t i=0;i<model_.clips.size();++i) {
        ImGui::PushID(static_cast<int>(i));
        if (ImGui::Selectable(model_.clips[i].name.c_str(),selectedClip_==static_cast<int>(i),0,{0,36})) {
            selectedClip_=static_cast<int>(i); time_=0; editClip_=-1; bindPose_=false;
        }
        ImGui::PopID();
    }
    ImGui::Spacing();
    if (ImGui::Button("+ Duplicate motion",{-1,0})) {
        auto copy=Clip(); copy.id+="-copy-"+std::to_string(model_.clips.size()); copy.name+=" copy";
        model_.clips.push_back(std::move(copy)); selectedClip_=static_cast<int>(model_.clips.size()-1); editClip_=-1; dirty_=true;
    }
    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing(); Label("SKELETON");
    ImGui::TextDisabled("%zu nodes / parent hierarchy",model_.skeleton.bones.size());
    ImGui::Spacing();
    if (ImGui::BeginChild("bones",{0,0},ImGuiChildFlags_None)) {
        for (size_t i=0;i<model_.skeleton.bones.size();++i) {
            const auto& b=model_.skeleton.bones[i]; int depth=0;
            for (int p=b.parent;p>=0;p=model_.skeleton.bones[p].parent) ++depth;
            ImGui::PushID(static_cast<int>(i)); ImGui::Indent(std::min(depth,6)*10.0f);
            if (ImGui::Selectable(b.name.c_str(),selectedBone_==static_cast<int>(i))) { selectedBone_=static_cast<int>(i); options_.skeleton=true; }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s",b.path.c_str());
            ImGui::Unindent(std::min(depth,6)*10.0f); ImGui::PopID();
        }
    }
    ImGui::EndChild();
}
void EditorApp::CommitPose() {
    edit_.rotation=QuaternionFromDegrees(editAngles_);
    SetTransformKey(Clip(),static_cast<uint32_t>(selectedBone_),std::clamp(time_*Clip().speed,0.0,Clip().duration),edit_);
    dirty_=true; pose_=EvaluatePose(model_.skeleton,&Clip(),time_,false);
    status_="Key updated. Preview, saved motion and bake use the same pose."; error_=false;
}
void EditorApp::Inspector() {
    Label("POSE INSPECTOR"); ImGui::Spacing();
    const auto& bone=model_.skeleton.bones.at(selectedBone_);
    ImGui::TextColored(purple,"%s",bone.name.c_str());
    ImGui::TextDisabled("Local transform"); ImGui::Spacing();
    if (editBone_!=selectedBone_ || editClip_!=selectedClip_ || std::abs(editTime_-time_)>1e-8 || playing_) {
        edit_=pose_.local.at(selectedBone_); editAngles_=DegreesFromQuaternion(edit_.rotation);
        editBone_=selectedBone_; editTime_=time_; editClip_=selectedClip_;
    }
    if (playing_) ImGui::TextColored(mint,"Pause to edit this pose");
    else if (bindPose_) ImGui::TextDisabled("Disable bind pose to edit");
    else ImGui::TextColored(mint,"Changes create a key here");
    ImGui::Spacing();
    ImGui::BeginDisabled(playing_ || bindPose_);
    ImGui::PushItemWidth(-1);
    Label("Rotation / degrees"); bool changed=ImGui::DragFloat3("##rotation",&editAngles_.x,.5f,0,0,"%.1f");
    Label("Position / meters"); changed|=ImGui::DragFloat3("##position",&edit_.translation.x,.005f,0,0,"%.3f");
    Label("Scale"); changed|=ImGui::DragFloat3("##scale",&edit_.scale.x,.005f,.001f,10000,"%.3f",ImGuiSliderFlags_AlwaysClamp);
    ImGui::PopItemWidth();
    if (changed) Try([&] { CommitPose(); });
    if (ImGui::Button("Set key",{-1,0})) Try([&] { CommitPose(); });
    if (ImGui::Button("Reset to bind + key",{-1,0})) Try([&] { edit_=bone.bindLocal; editAngles_=DegreesFromQuaternion(edit_.rotation); CommitPose(); });
    if (ImGui::Button("Delete key at this time",{-1,0})) {
        DeleteTransformKey(Clip(),static_cast<uint32_t>(selectedBone_),time_*Clip().speed); dirty_=true; editBone_=-1;
        pose_=EvaluatePose(model_.skeleton,&Clip(),time_); status_="Keys removed at the current time.";
    }
    ImGui::EndDisabled();
    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing(); Label("PREVIEW");
    ImGui::Checkbox("Floor grid",&options_.grid); ImGui::SameLine(); ImGui::Checkbox("Bones",&options_.skeleton);
    ImGui::Checkbox("Wireframe",&options_.wireframe);
    if (ImGui::Checkbox("Bind pose",&bindPose_)) { editBone_=-1; pose_=EvaluatePose(model_.skeleton,bindPose_?nullptr:&Clip(),time_); }
    if (ImGui::Button("Frame character [F]",{-1,0})) renderer_.FitCamera(camera_);
    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
    if (ImGui::CollapsingHeader("Import settings")) {
        const char* labels[]={"Automatic / x1","Extra scale / x0.01","Extra scale / x0.001"};
        const float scales[]={1,.01f,.001f}; int choice=unitsToMeters_==1?0:unitsToMeters_==.01f?1:2;
        ImGui::SetNextItemWidth(-1);
        if (ImGui::Combo("##units",&choice,labels,3)) unitsToMeters_=scales[choice];
        ImGui::TextWrapped("FBX units are converted to meters automatically. Extra scale is for files with incorrect unit metadata. Applies on the next import.");
    }
    if (ImGui::CollapsingHeader("Team handoff")) {
        ImGui::TextWrapped("Save motion keeps editable keys. Bake 30 FPS writes final local poses for the FBX exporter.");
        if (ImGui::Button("Save skeleton JSON",{-1,0})) Try([&] {
            auto p=Dialog(true,L"Skeleton (*.json)\0*.json\0\0",L"json",L"skeleton.json");
            if (!p.empty()) { SaveSkeleton(p,model_.skeleton); status_="Skeleton saved: "+p.u8string(); }
        });
        if (ImGui::Button("Save preview PNG",{-1,0})) Try([&] {
            auto p=Dialog(true,L"Preview image (*.png)\0*.png\0\0",L"png",L"preview.png");
            if (!p.empty()) { renderer_.SaveScreenshot(p); status_="Preview saved: "+p.u8string(); }
        });
    }
    if (ImGui::CollapsingHeader("Asset details")) {
        ImGui::TextWrapped("%s",model_.source.u8string().c_str());
        size_t triangles=0; for (const auto& mesh:model_.meshes) triangles+=mesh.indices.size()/3;
        ImGui::Text("%zu meshes / %zu triangles",model_.meshes.size(),triangles);
        for (const auto& warning:model_.warnings) ImGui::TextWrapped("%s",warning.c_str());
        for (const auto& warning:renderer_.Warnings()) ImGui::TextWrapped("%s",warning.c_str());
    }
}
void EditorApp::Viewport(float height) {
    ImGui::BeginChild("viewport-card",{0,height},ImGuiChildFlags_None,ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse);
    Label("SCENE PREVIEW"); ImGui::SameLine(); ImGui::TextDisabled(" / %s",bindPose_?"Bind pose":Clip().name.c_str());
    ImVec2 size=ImGui::GetContentRegionAvail(); size.x=std::max(size.x,1.0f); size.y=std::max(size.y,1.0f);
    renderer_.Resize(static_cast<unsigned>(size.x),static_cast<unsigned>(size.y));
    ImGui::Image(ImTextureRef(static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(renderer_.Texture()))),size);
    const bool hover=ImGui::IsItemHovered(); const auto& io=ImGui::GetIO();
    if (hover && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) orbiting_=true;
    if (hover && (ImGui::IsMouseClicked(ImGuiMouseButton_Middle)||ImGui::IsMouseClicked(ImGuiMouseButton_Right))) panning_=true;
    if (!io.MouseDown[ImGuiMouseButton_Left]) orbiting_=false;
    if (!io.MouseDown[ImGuiMouseButton_Middle]&&!io.MouseDown[ImGuiMouseButton_Right]) panning_=false;
    if (orbiting_) camera_.Orbit(io.MouseDelta.x,io.MouseDelta.y);
    if (panning_) camera_.Pan(io.MouseDelta.x,io.MouseDelta.y,size.y);
    if (hover) { camera_.Zoom(io.MouseWheel); if (ImGui::IsKeyPressed(ImGuiKey_F) && !io.WantTextInput) renderer_.FitCamera(camera_); }
    options_.selectedBone=selectedBone_; renderer_.Render(model_,pose_,camera_,options_);
    auto* draw=ImGui::GetWindowDrawList(); const auto min=ImGui::GetItemRectMin(); const auto max=ImGui::GetItemRectMax();
    draw->AddRectFilled({min.x+12,min.y+12},{min.x+105,min.y+39},IM_COL32(34,38,57,230),6);
    draw->AddText({min.x+23,min.y+17},IM_COL32(144,231,207,255),"PERSPECTIVE");
    draw->AddText({min.x+16,max.y-27},IM_COL32(142,153,179,255),"Drag: orbit    Right drag: pan    Wheel: zoom");
    ImGui::EndChild();
}
void EditorApp::Timeline(float height) {
    ImGui::BeginChild("timeline-card",{0,height},ImGuiChildFlags_None,ImGuiWindowFlags_NoScrollbar);
    Label("TIMELINE"); ImGui::SameLine(); ImGui::TextDisabled(" / selected bone keys");
    if (ImGui::Button(playing_?"Pause":"Play",{75,0})) playing_=!playing_;
    ImGui::SameLine(); if (ImGui::Button("|<")) { time_=0; playing_=false; }
    ImGui::SameLine(); ImGui::Checkbox("Loop",&loop_);
    ImGui::SameLine(); ImGui::SetNextItemWidth(105); float speed=static_cast<float>(Clip().speed);
    if (ImGui::DragFloat("Speed",&speed,.01f,.05f,8,"%.2fx",ImGuiSliderFlags_AlwaysClamp)) { Clip().speed=speed; time_=std::min(time_,PlaybackDuration(Clip())); dirty_=true; }
    const double duration=PlaybackDuration(Clip());
    ImGui::SameLine(); ImGui::TextColored(purple,"%.2f / %.2f s",time_,duration);
    const ImVec2 p=ImGui::GetCursorScreenPos(); const float width=std::max(ImGui::GetContentRegionAvail().x,1.0f),trackHeight=53;
    ImGui::InvisibleButton("time-track",{width,trackHeight});
    if (ImGui::IsItemActive()) { time_=std::clamp(static_cast<double>((ImGui::GetIO().MousePos.x-p.x)/width)*duration,0.0,duration); playing_=false; }
    auto* draw=ImGui::GetWindowDrawList(); draw->AddRectFilled(p,{p.x+width,p.y+trackHeight},IM_COL32(20,25,36,255),5);
    for (int i=0;i<=8;++i) {
        const float x=p.x+width*i/8.0f;
        draw->AddLine({x,p.y+19},{x,p.y+trackHeight},IM_COL32(52,59,78,255));
        char label[32]; std::snprintf(label,sizeof(label),"%.1f",duration*i/8);
        draw->AddText({std::min(x+3,p.x+width-28),p.y+2},IM_COL32(124,137,163,255),label);
    }
    if (duration>0) for (const auto& track:Clip().tracks) if (track.bone==static_cast<uint32_t>(selectedBone_)) {
        auto diamonds=[&](const auto& keys) {
            for (const auto& key:keys) {
                const float x=p.x+width*static_cast<float>(key.time/Clip().duration),y=p.y+35;
                draw->AddQuadFilled({x,y-4},{x+4,y},{x,y+4},{x-4,y},IM_COL32(172,133,238,255));
            }
        };
        diamonds(track.positions); diamonds(track.rotations); diamonds(track.scales);
    }
    const float headX=p.x+width*static_cast<float>(duration>0?time_/duration:0);
    draw->AddLine({headX,p.y},{headX,p.y+trackHeight},IM_COL32(139,235,209,255),2);
    draw->AddTriangleFilled({headX-5,p.y},{headX+5,p.y},{headX,p.y+7},IM_COL32(139,235,209,255));
    ImGui::SetNextItemWidth(-1); float t=static_cast<float>(time_);
    if (ImGui::SliderFloat("##time-seconds",&t,0,static_cast<float>(duration),"%.3f seconds")) { time_=t; playing_=false; }
    ImGui::EndChild();
}
void EditorApp::Frame(float deltaTime) {
    if (playing_ && !bindPose_) {
        const double duration=PlaybackDuration(Clip()); time_+=std::min(deltaTime,.25f);
        if (duration>0 && loop_) time_=std::fmod(time_,duration);
        else if (time_>=duration) { time_=duration; playing_=false; }
    }
    auto& io=ImGui::GetIO();
    if (!io.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Space)) playing_=!playing_;
    const auto* viewport=ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos); ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::Begin("LunaFlow",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoBringToFrontOnFocus);
    Toolbar();
    const float workHeight=std::max(ImGui::GetContentRegionAvail().y-40,250.0f);
    if (ImGui::BeginTable("workspace",3,ImGuiTableFlags_Resizable|ImGuiTableFlags_BordersInnerV|ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Library",ImGuiTableColumnFlags_WidthFixed,220);
        ImGui::TableSetupColumn("Preview",ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Inspector",ImGuiTableColumnFlags_WidthFixed,270);
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0); ImGui::BeginChild("library-card",{0,workHeight}); Library(); ImGui::EndChild();
        // Update after library selection and before inspector edits, then draw the same pose.
        pose_=EvaluatePose(model_.skeleton,bindPose_?nullptr:&Clip(),time_,false);
        ImGui::TableSetColumnIndex(2); ImGui::BeginChild("inspector-card",{0,workHeight}); Inspector(); ImGui::EndChild();
        ImGui::TableSetColumnIndex(1); Viewport(std::max(workHeight-204,100.0f)); ImGui::Spacing(); Timeline(190);
        ImGui::EndTable();
    }
    ImGui::Spacing(); ImGui::Separator();
    ImGui::TextColored(error_?ImVec4{1,.48f,.47f,1}:muted,"%s%s",dirty_?"[Unsaved] ":"",status_.c_str());
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s",status_.c_str());
    ImGui::End();
}
}

#include "App/EditorApp.h"
#include "imgui.h"
#include "ImGuizmo.h"
#include <algorithm>
#include <cmath>

namespace LunaFlow {
void EditorApp::StartJointDrag(XMFLOAT2 mouse,int) {
    if(bindPose_||workspace_==2)return;
    playing_=false;draggingBone_=true;dragRecorded_=false;dragMouse_=mouse;dragStart_=pose_.local.at(selectedBone_);
    const int parent=model_.skeleton.bones.at(selectedBone_).parent;
    if(parent>=0)dragParent_=pose_.global[parent];else XMStoreFloat4x4(&dragParent_,XMMatrixIdentity());
    editBone_=-1;
}
void EditorApp::DrawGizmo(XMFLOAT2 origin,XMFLOAT2 size) {
    const auto view=camera_.View(),projection=camera_.Projection(size.x/size.y);
    XMFLOAT4X4 v,p,world;XMStoreFloat4x4(&v,view);XMStoreFloat4x4(&p,projection);
    const bool effect=selectedEffect_>=0&&selectedEffect_<static_cast<int>(model_.effects.size());
    if(workspace_==2&&!effect)return;
    const uint32_t bone=effect?model_.effects[selectedEffect_].bone:static_cast<uint32_t>(selectedBone_);
    const auto parent=effect?XMLoadFloat4x4(&pose_.global[bone]):
        (model_.skeleton.bones[bone].parent>=0?XMLoadFloat4x4(&pose_.global[model_.skeleton.bones[bone].parent]):XMMatrixIdentity());
    if(effect)XMStoreFloat4x4(&world,Matrix(model_.effects[selectedEffect_].local)*parent);
    else world=pose_.global[bone];
    ImGuizmo::SetOrthographic(false);ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());ImGuizmo::SetRect(origin.x,origin.y,size.x,size.y);
    ImGuizmo::SetGizmoSizeClipSpace(.12f);ImGuizmo::Enable(!bindPose_);
    const auto operation=tool_==Tool::Move?ImGuizmo::TRANSLATE:tool_==Tool::Rotate?ImGuizmo::ROTATE:ImGuizmo::SCALE;
    const float step=tool_==Tool::Move?.1f:tool_==Tool::Rotate?15.0f:.1f;const float snap[]={step,step,step};
    const bool changed=ImGuizmo::Manipulate(&v._11,&p._11,operation,worldSpace_?ImGuizmo::WORLD:ImGuizmo::LOCAL,&world._11,nullptr,ImGui::GetIO().KeyCtrl?snap:nullptr);
    const bool usingGizmo=ImGuizmo::IsUsing();
    if(usingGizmo&&!gizmoWasUsing_){playing_=false;if(!effect)RememberEdit();}
    if(changed)Try([&]{
        const auto local=Decompose(XMLoadFloat4x4(&world)*XMMatrixInverse(nullptr,parent));
        if(effect){model_.effects[selectedEffect_].local=local;dirty_=true;}
        else {edit_=local;CommitPose(false);editBone_=-1;}
    });
    gizmoWasUsing_=usingGizmo;
    if(options_.skeleton){
        auto* draw=ImGui::GetWindowDrawList();draw->PushClipRect({origin.x,origin.y},{origin.x+size.x,origin.y+size.y},true);
        const auto vp=view*projection;
        for(size_t i=0;i<pose_.global.size();++i){const auto& g=pose_.global[i];const auto screen=ProjectPoint({g._41,g._42,g._43},vp,size.x,size.y);
            if(screen.visible)draw->AddCircleFilled({origin.x+screen.pixel.x,origin.y+screen.pixel.y},i==static_cast<size_t>(selectedBone_)?4.5f:2.5f,
                i==static_cast<size_t>(selectedBone_)?IM_COL32(255,208,124,255):IM_COL32(104,215,187,220));
        }draw->PopClipRect();
    }
}
void EditorApp::ManipulateViewport(XMFLOAT2 origin,XMFLOAT2 size,bool hover) {
    const auto& io=ImGui::GetIO();const XMFLOAT2 mouse{io.MousePos.x-origin.x,io.MousePos.y-origin.y};
    if(hover&&!io.WantTextInput&&!io.KeyCtrl){
        if(ImGui::IsKeyPressed(ImGuiKey_W))tool_=Tool::Move;
        if(ImGui::IsKeyPressed(ImGuiKey_E))tool_=Tool::Rotate;
        if(ImGui::IsKeyPressed(ImGuiKey_R))tool_=Tool::Scale;
        if(ImGui::IsKeyPressed(ImGuiKey_F))FrameAll();
    }
    if(hover&&ImGui::IsMouseClicked(ImGuiMouseButton_Right))orbiting_=true;
    if(hover&&ImGui::IsMouseClicked(ImGuiMouseButton_Middle))panning_=true;
    if(hover&&ImGui::IsMouseClicked(ImGuiMouseButton_Left)&&!ImGuizmo::IsOver()&&!ImGuizmo::IsUsing()){
        const auto vp=camera_.View()*camera_.Projection(size.x/size.y);
        int selected=workspace_!=0&&options_.skeleton?PickJoint(model_.skeleton,pose_,vp,size.x,size.y,mouse,11):-1;
        if(selected<0)selected=PickSurfaceBone(model_,pose_,MakePickRay(mouse,vp,size.x,size.y));
        if(selected>=0){
            if(workspace_==0){const int object=ObjectForBone(model_,static_cast<uint32_t>(selected));if(object>=0)selected=static_cast<int>(model_.objects[object].root);}
            selectedBone_=selected;selectedEffect_=-1;selectedKeyTime_=-1;revealSelected_=true;StartJointDrag(mouse,-1);
        }else panning_=true;
    }
    if(!io.MouseDown[ImGuiMouseButton_Right])orbiting_=false;
    if(!io.MouseDown[ImGuiMouseButton_Left]&&!io.MouseDown[ImGuiMouseButton_Middle])panning_=false;
    if(!io.MouseDown[ImGuiMouseButton_Left]){draggingBone_=false;dragRecorded_=false;}
    if(!ImGuizmo::IsUsing()){
        if(orbiting_)camera_.Orbit(io.MouseDelta.x,io.MouseDelta.y);
        if(panning_&&!draggingBone_)camera_.Pan(io.MouseDelta.x,io.MouseDelta.y,size.y);
        if(hover&&!draggingBone_)camera_.Zoom(io.MouseWheel);
    }
    if(draggingBone_&&!ImGuizmo::IsUsing()&&ImGui::IsMouseDragging(ImGuiMouseButton_Left,3)){
        const float dx=mouse.x-dragMouse_.x,dy=mouse.y-dragMouse_.y;
        Try([&]{
            if(!dragRecorded_){RememberEdit();dragRecorded_=true;}
            const auto parent=XMLoadFloat4x4(&dragParent_);
            if(tool_==Tool::Move)edit_=TranslateInWorld(dragStart_,parent,ScreenDragWorldDelta(dx,dy,camera_.View(),camera_.distance,size.y));
            else if(tool_==Tool::Scale){edit_=dragStart_;const float factor=std::clamp(std::exp((dx-dy)*.008f),.01f,100.0f);edit_.scale={dragStart_.scale.x*factor,dragStart_.scale.y*factor,dragStart_.scale.z*factor};}
            else {
                const auto inv=XMMatrixInverse(nullptr,camera_.View());XMFLOAT3 up,right;XMStoreFloat3(&up,inv.r[1]);XMStoreFloat3(&right,inv.r[0]);
                edit_=RotateInWorld(dragStart_,parent,up,dx*.009f);edit_=RotateInWorld(edit_,parent,right,dy*.009f);
            }
            CommitPose(false);editBone_=-1;
        });
    }
}
void EditorApp::Viewport(float height) {
    ImGui::BeginChild("viewport",{0,height},0,ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse);
    auto toolButton=[&](Tool tool,const char* label){const bool active=tool_==tool;if(active)ImGui::PushStyleColor(ImGuiCol_Button,{.30f,.23f,.45f,1});if(ImGui::Button(label))tool_=tool;if(active)ImGui::PopStyleColor();};
    toolButton(Tool::Move,T("이동 W","Move W"));ImGui::SameLine();toolButton(Tool::Rotate,T("회전 E","Rotate E"));ImGui::SameLine();toolButton(Tool::Scale,T("크기 R","Scale R"));
    ImGui::SameLine();if(ImGui::Button(worldSpace_?T("월드","World"):T("로컬","Local")))worldSpace_=!worldSpace_;
    ImGui::SameLine();ImGui::Checkbox(T("뼈대","Bones"),&options_.skeleton);
    ImVec2 size=ImGui::GetContentRegionAvail();size.x=std::max(size.x,1.0f);size.y=std::max(size.y,1.0f);
    renderer_.Resize(static_cast<unsigned>(size.x),static_cast<unsigned>(size.y));
    ImGui::Image(ImTextureRef(static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(renderer_.Texture()))),size);
    const auto min=ImGui::GetItemRectMin();const bool hover=ImGui::IsItemHovered();
    DrawGizmo({min.x,min.y},{size.x,size.y});ManipulateViewport({min.x,min.y},{size.x,size.y},hover);
    options_.selectedBone=selectedBone_;options_.activeClip=&Clip();options_.time=time_;renderer_.Render(model_,pose_,camera_,options_);
    ImGui::GetWindowDrawList()->AddText({min.x+14,min.y+12},IM_COL32(156,168,192,255),T("원근 / 투시","Perspective"));
    ImGui::EndChild();
}
}

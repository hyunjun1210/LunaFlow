#include "App/EditorApp.h"
#include "imgui.h"
#include "App/UiTheme.h"
#include "ImGuizmo.h"
#include "App/ContinuousInput.h"
#include <algorithm>
#include <cfloat>
#include <cmath>

namespace LunaFlow {
void EditorApp::StartJointDrag(XMFLOAT2 mouse,int) {
    if(mocap_.active||bindPose_||workspace_==2)return;
    playing_=false;draggingBone_=true;dragRecorded_=false;dragMouse_=mouse;dragDelta_={};dragStart_=pose_.local.at(selectedBone_);
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
    // Webcam capture drives the pose: keep the joint overlay but no transform handle.
    if(!mocap_.active){
    ImGuizmo::SetOrthographic(camera_.orthographic);ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());ImGuizmo::SetRect(origin.x,origin.y,size.x,size.y);
    auto& io=ImGui::GetIO();
    const bool blocked=io.AppFocusLost||ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopupId);
    ImGuizmo::SetGizmoSizeClipSpace(.12f);ImGuizmo::SetContinuousRotation(true);ImGuizmo::Enable(!bindPose_&&!blocked);
    const auto operation=tool_==Tool::Move?ImGuizmo::TRANSLATE:tool_==Tool::Rotate?ImGuizmo::ROTATE:ImGuizmo::SCALE;
    const float step=tool_==Tool::Move?.1f:tool_==Tool::Rotate?15.0f:.1f;const float snap[]={step,step,step};
    // Clicks that belong to the axis gizmo or Alt navigation must not grab a transform handle.
    const bool blockStart=!ImGuizmo::IsUsing()&&ImGui::IsMouseClicked(ImGuiMouseButton_Left)&&(navGizmoHot_||io.KeyAlt||orbiting_||panning_||zooming_);
    const float downDuration=io.MouseDownDuration[ImGuiMouseButton_Left];
    if(blockStart)io.MouseDownDuration[ImGuiMouseButton_Left]=FLT_MIN;
    const bool changed=ImGuizmo::Manipulate(&v._11,&p._11,operation,worldSpace_?ImGuizmo::WORLD:ImGuizmo::LOCAL,&world._11,nullptr,io.KeyCtrl?snap:nullptr);
    if(blockStart)io.MouseDownDuration[ImGuiMouseButton_Left]=downDuration;
    const bool usingGizmo=ImGuizmo::IsUsing();
    if(usingGizmo&&tool_==Tool::Rotate)RequestContinuousMouse();
    if(usingGizmo&&!gizmoWasUsing_){playing_=false;if(effect)RememberEffectEdit();else RememberEdit();}
    if(changed)Try([&]{
        const auto local=Decompose(XMLoadFloat4x4(&world)*XMMatrixInverse(nullptr,parent));
        if(effect){model_.effects[selectedEffect_].local=local;dirty_=true;}
        else {edit_=local;CommitPose(false);editBone_=-1;}
    });
    gizmoWasUsing_=usingGizmo;
    }else gizmoWasUsing_=false;
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
    if(io.AppFocusLost||ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopupId)){
        orbiting_=panning_=zooming_=draggingBone_=dragRecorded_=navGizmoDragged_=false;navGizmoPressed_=-1;return;
    }
    const bool gizmoBusy=ImGuizmo::IsUsing();
    if(hover&&!gizmoBusy&&!navGizmoHot_){
        // Unity/Maya navigation: RMB orbits (Alt+RMB zooms), MMB pans, Alt+LMB orbits.
        if(ImGui::IsMouseClicked(ImGuiMouseButton_Right)){StopCameraTween();if(io.KeyAlt)zooming_=true;else{orbiting_=true;orbitButton_=ImGuiMouseButton_Right;}}
        if(ImGui::IsMouseClicked(ImGuiMouseButton_Middle)){StopCameraTween();panning_=true;}
        if(ImGui::IsMouseClicked(ImGuiMouseButton_Left)&&!orbiting_&&!zooming_){
            if(io.KeyAlt){StopCameraTween();orbiting_=true;orbitButton_=ImGuiMouseButton_Left;}
            else if(mocap_.active){StopCameraTween();panning_=true;}
            else if(!ImGuizmo::IsOver()){
                const auto vp=camera_.View()*camera_.Projection(size.x/size.y);
                int selected=workspace_!=0&&options_.skeleton?PickJoint(model_.skeleton,pose_,vp,size.x,size.y,mouse,11):-1;
                if(selected<0)selected=PickSurfaceBone(model_,pose_,MakePickRay(mouse,vp,size.x,size.y));
                if(selected>=0){
                    if(workspace_==0){const int object=ObjectForBone(model_,static_cast<uint32_t>(selected));if(object>=0)selected=static_cast<int>(model_.objects[object].root);}
                    selectedBone_=selected;selectedEffect_=-1;selectedKeyTime_=-1;revealSelected_=true;StartJointDrag(mouse,-1);
                }else{StopCameraTween();panning_=true;}
            }
        }
    }
    if(orbiting_&&!io.MouseDown[orbitButton_])orbiting_=false;
    if(zooming_&&!io.MouseDown[ImGuiMouseButton_Right])zooming_=false;
    if(!io.MouseDown[ImGuiMouseButton_Left]&&!io.MouseDown[ImGuiMouseButton_Middle])panning_=false;
    if(!io.MouseDown[ImGuiMouseButton_Left]){draggingBone_=false;dragRecorded_=false;}
    if(!gizmoBusy){
        if(orbiting_){
            if(!ImGui::IsMouseClicked(orbitButton_)&&(io.MouseDelta.x!=0||io.MouseDelta.y!=0)){LeaveAxisView(camera_);camera_.Orbit(io.MouseDelta.x,io.MouseDelta.y);}
            if(orbitButton_==ImGuiMouseButton_Right&&!io.WantTextInput&&!io.KeyCtrl&&!io.KeyAlt){
                const auto key=[](ImGuiKey positive,ImGuiKey negative){return static_cast<float>(ImGui::IsKeyDown(positive))-static_cast<float>(ImGui::IsKeyDown(negative));};
                const XMFLOAT3 direction{key(ImGuiKey_D,ImGuiKey_A),key(ImGuiKey_E,ImGuiKey_Q),key(ImGuiKey_W,ImGuiKey_S)};
                if(camera_.orthographic){
                    // Moving forward cannot change an orthographic image, so W/S zoom like Unity's 2D/iso fly.
                    if(direction.z!=0)camera_.Zoom(direction.z*io.DeltaTime*(io.KeyShift?12.0f:4.0f)*camera_.flySpeed);
                    camera_.Fly({direction.x,direction.y,0},io.DeltaTime,io.KeyShift);
                }else camera_.Fly(direction,io.DeltaTime,io.KeyShift);
            }
        }
        if(zooming_&&!ImGui::IsMouseClicked(ImGuiMouseButton_Right))camera_.Zoom((io.MouseDelta.x-io.MouseDelta.y)*.02f);
        if(panning_&&!orbiting_&&!draggingBone_&&!ImGui::IsMouseClicked(ImGuiMouseButton_Left)&&!ImGui::IsMouseClicked(ImGuiMouseButton_Middle))camera_.Pan(io.MouseDelta.x,io.MouseDelta.y,size.y);
        if(io.MouseWheel!=0){
            // While flying, the wheel changes fly speed (Unity Scene view); otherwise it zooms.
            if(orbiting_&&orbitButton_==ImGuiMouseButton_Right){camera_.flySpeed=std::clamp(camera_.flySpeed*std::pow(1.2f,io.MouseWheel),.05f,20.0f);flySpeedNotice_=1.5f;}
            else if(hover&&!draggingBone_){StopCameraTween();camera_.Zoom(io.MouseWheel);}
        }
    }
    // Releasing Alt after Alt+drag navigation must not toggle the menu bar layer.
    if((orbiting_||zooming_)&&io.KeyAlt)ImGui::GetCurrentContext()->NavWindowingToggleLayer=false;
    if(orbiting_||panning_||zooming_||draggingBone_)RequestContinuousMouse();
    if(draggingBone_&&!ImGuizmo::IsUsing()&&!ImGui::IsMouseClicked(ImGuiMouseButton_Left)){
        dragDelta_.x+=io.MouseDelta.x;dragDelta_.y+=io.MouseDelta.y;
    }
    if(draggingBone_&&!ImGuizmo::IsUsing()&&(dragRecorded_||std::hypot(dragDelta_.x,dragDelta_.y)>3)){
        const float dx=dragDelta_.x,dy=dragDelta_.y;
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
    auto tip=[](const char* text){if(ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))ImGui::SetTooltip("%s",text);};
    auto toolButton=[&](Tool tool,const char* label,const char* help){
        const bool active=tool_==tool;if(active)ImGui::PushStyleColor(ImGuiCol_Button,{.30f,.23f,.45f,1});
        if(ImGui::Button(label))tool_=tool;if(active)ImGui::PopStyleColor();tip(help);
    };
    toolButton(Tool::Move,T("이동 W","Move W"),T("이동 도구 (W)","Move tool (W)"));ImGui::SameLine();
    toolButton(Tool::Rotate,T("회전 E","Rotate E"),T("회전 도구 (E) · 화면 끝에서 커서가 반대편으로 이어집니다","Rotate tool (E) · the cursor wraps at the screen edge"));ImGui::SameLine();
    toolButton(Tool::Scale,T("크기 R","Scale R"),T("크기 도구 (R)","Scale tool (R)"));
    ImGui::SameLine();if(ImGui::Button(worldSpace_?T("월드","World"):T("로컬","Local")))worldSpace_=!worldSpace_;
    tip(T("월드 / 로컬 좌표 전환 (X)","Toggle world / local space (X)"));
    ImGui::SameLine();ImGui::Checkbox(T("뼈대","Bones"),&options_.skeleton);tip(T("뼈대 표시","Show bones"));
    ImVec2 size=ImGui::GetContentRegionAvail();size.x=std::max(size.x,1.0f);size.y=std::max(size.y,1.0f);
    renderer_.Resize(static_cast<unsigned>(size.x),static_cast<unsigned>(size.y));
    ImGui::Image(ImTextureRef(static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(renderer_.Texture()))),size);
    const auto min=ImGui::GetItemRectMin();const bool hover=ImGui::IsItemHovered();
    const XMFLOAT2 origin{min.x,min.y},extent{size.x,size.y};viewportOrigin_=origin;viewportSize_=extent;
    // Decide before ImGuizmo and the camera see this frame's click whether it belongs to the axis gizmo.
    const auto& io=ImGui::GetIO();
    const bool idle=!ImGuizmo::IsUsing()&&!orbiting_&&!panning_&&!zooming_&&!draggingBone_;
    navGizmoHot_=navGizmoPressed_>=0||(hover&&idle&&!io.KeyAlt&&OrientationGizmoHit(origin,extent,{io.MousePos.x,io.MousePos.y})>=0);
    DrawGizmo(origin,extent);ManipulateViewport(origin,extent,hover);
    OrientationGizmo(origin,extent,hover);ViewportOverlay(origin,extent,hover);MocapOverlay(origin,extent);
    options_.selectedBone=selectedBone_;options_.activeClip=&Clip();options_.time=time_;renderer_.Render(model_,pose_,camera_,options_);
    ImGui::EndChild();
}
}

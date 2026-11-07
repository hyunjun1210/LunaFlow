#include "App/EditorApp.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "ImGuizmo.h"
#include "App/UiFonts.h"
#include "App/UiTheme.h"
#include "App/ContinuousInput.h"
#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <initializer_list>
#include <string>
#include <vector>

// Unity/Blender-style navigation: animated framing, axis views, the orientation
// gizmo, keyboard shortcuts and the shortcut reference.
namespace LunaFlow {
namespace {
constexpr float Pi=3.14159265358979f;
constexpr float TweenSeconds=.22f;
constexpr float HalfFovY=21.0f*Pi/180.0f;
struct AxisView { float yaw,pitch; };
// Blender orientation: front looks along +Z, right sits on +X, top looks down with +Z up.
constexpr AxisView axisViews[6]={{0,0},{Pi,0},{Pi*.5f,0},{-Pi*.5f,0},{0,Pi*.5f},{0,-Pi*.5f}};
float Smooth(float t){t=std::clamp(t,0.0f,1.0f);return t*t*(3-2*t);}
float HalfFov(float aspect){return std::min(HalfFovY,std::atan(std::tan(HalfFovY)*std::max(aspect,1e-3f)));}
struct GizmoLayout { ImVec2 center; float radius=0,ball=0; bool visible=false; };
GizmoLayout LayoutGizmo(XMFLOAT2 origin,XMFLOAT2 size) {
    GizmoLayout layout;layout.radius=UiScale(38);layout.ball=UiScale(8.5f);
    const float margin=UiScale(12);
    layout.center={origin.x+size.x-margin-layout.radius-layout.ball,origin.y+margin+layout.radius+layout.ball};
    layout.visible=size.x>=UiScale(260)&&size.y>=UiScale(200);
    return layout;
}
struct GizmoAxis { int index=0; ImVec2 pos; float depth=0; };
// Axis order: +X,-X,+Y,-Y,+Z,-Z. Sorted far to near for painter's-order drawing.
std::array<GizmoAxis,6> GizmoAxes(FXMMATRIX view,const GizmoLayout& layout) {
    static const XMFLOAT3 directions[6]={{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
    std::array<GizmoAxis,6> axes{};
    for(int i=0;i<6;++i){
        XMFLOAT3 d;XMStoreFloat3(&d,XMVector3TransformNormal(XMLoadFloat3(&directions[i]),view));
        axes[static_cast<size_t>(i)]={i,{layout.center.x+d.x*layout.radius,layout.center.y-d.y*layout.radius},d.z};
    }
    std::sort(axes.begin(),axes.end(),[](const GizmoAxis& a,const GizmoAxis& b){return a.depth>b.depth;});
    return axes;
}
const char* GizmoLabel(bool orthographic,bool korean){return orthographic?(korean?"직교":"Ortho"):(korean?"원근":"Persp");}
float GizmoLabelSize(){return ImGui::GetFontSize()*.82f;}
ImRect GizmoLabelRect(const GizmoLayout& layout,const char* text) {
    const float pad=UiScale(5);
    const ImVec2 size=ImGui::GetFont()->CalcTextSizeA(GizmoLabelSize(),FLT_MAX,0,text);
    const float top=layout.center.y+layout.radius+layout.ball+UiScale(4);
    return {{layout.center.x-size.x*.5f-pad,top},{layout.center.x+size.x*.5f+pad,top+size.y+pad*.8f}};
}
}

PreviewCamera EditorApp::CameraGoal() const {
    PreviewCamera goal=tween_<1?tweenTo_:camera_;goal.flySpeed=camera_.flySpeed;return goal;
}
void EditorApp::AnimateCamera(const PreviewCamera& to) {
    tweenFrom_=camera_;tweenTo_=to;tweenTo_.flySpeed=camera_.flySpeed;tween_=0;
}
void EditorApp::UpdateCameraTween(float deltaTime) {
    if(tween_>=1)return;
    const float flySpeed=camera_.flySpeed;
    tween_=std::min(tween_+std::max(deltaTime,0.0f)/TweenSeconds,1.0f);
    if(tween_>=1){camera_=tweenTo_;camera_.flySpeed=flySpeed;return;}
    const float s=Smooth(tween_);const auto& a=tweenFrom_;const auto& b=tweenTo_;
    camera_.target={a.target.x+(b.target.x-a.target.x)*s,a.target.y+(b.target.y-a.target.y)*s,a.target.z+(b.target.z-a.target.z)*s};
    camera_.yaw=a.yaw+std::remainder(b.yaw-a.yaw,2*Pi)*s;
    camera_.pitch=a.pitch+(b.pitch-a.pitch)*s;
    camera_.distance=std::max(a.distance,1e-6f)*std::pow(std::max(b.distance,1e-6f)/std::max(a.distance,1e-6f),s);
    camera_.sceneRadius=a.sceneRadius+(b.sceneRadius-a.sceneRadius)*s;
    camera_.orthographic=a.orthographic&&b.orthographic;camera_.flySpeed=flySpeed;
}
void EditorApp::StopCameraTween() {
    if(tween_>=1)return;
    const float flySpeed=camera_.flySpeed;camera_=tweenTo_;camera_.flySpeed=flySpeed;tween_=1;
}
void EditorApp::LeaveAxisView(PreviewCamera& camera) {
    // Blender's auto perspective: orbiting out of an axis view restores perspective.
    if(autoOrtho_)camera.orthographic=false;
    viewAxis_=-1;autoOrtho_=false;
}
void EditorApp::SnapView(int axis) {
    if(axis<0||axis>5)return;
    auto goal=CameraGoal();goal.yaw=axisViews[axis].yaw;goal.pitch=axisViews[axis].pitch;
    if(!goal.orthographic){goal.orthographic=true;autoOrtho_=true;}
    viewAxis_=axis;AnimateCamera(goal);
}
void EditorApp::ViewOpposite() {
    if(viewAxis_>=0){SnapView(viewAxis_^1);return;}
    auto goal=CameraGoal();goal.yaw+=Pi;goal.pitch=-goal.pitch;AnimateCamera(goal);
}
void EditorApp::ToggleOrthographic() {
    StopCameraTween();camera_.orthographic=!camera_.orthographic;autoOrtho_=false;
}
void EditorApp::OrbitStep(float yaw,float pitch) {
    auto goal=CameraGoal();LeaveAxisView(goal);
    goal.yaw+=yaw;goal.pitch=std::clamp(goal.pitch+pitch,-Pi*.5f,Pi*.5f);AnimateCamera(goal);
}
void EditorApp::FrameAll() {Try([&]{
    pose_=EvaluatePose(model_.skeleton,bindPose_?nullptr:&Clip(),time_);renderer_.UpdateBounds(model_,pose_);
    auto goal=CameraGoal();auto fit=goal;renderer_.FitCamera(fit);
    goal.target=fit.target;goal.distance=fit.distance;goal.sceneRadius=fit.sceneRadius;AnimateCamera(goal);
});}
void EditorApp::FrameSelected() {
    bool framed=false;
    Try([&]{
        pose_=EvaluatePose(model_.skeleton,bindPose_?nullptr:&Clip(),time_);
        auto goal=CameraGoal();const float sceneRadius=std::max(goal.sceneRadius,1e-4f);
        XMVECTOR low=XMVectorReplicate(FLT_MAX),high=XMVectorReplicate(-FLT_MAX);bool any=false;float minimum=sceneRadius*.1f;
        auto include=[&](FXMVECTOR point){low=XMVectorMin(low,point);high=XMVectorMax(high,point);any=true;};
        const bool effect=workspace_==2&&selectedEffect_>=0&&selectedEffect_<static_cast<int>(model_.effects.size());
        if(effect){
            const auto& e=model_.effects[static_cast<size_t>(selectedEffect_)];
            if(e.bone<pose_.global.size()){include((Matrix(e.local)*XMLoadFloat4x4(&pose_.global[e.bone])).r[3]);minimum=std::max(e.size*1.5f,sceneRadius*.12f);}
        }else if(selectedBone_>=0&&selectedBone_<static_cast<int>(model_.skeleton.bones.size())){
            const auto& bones=model_.skeleton.bones;std::vector<char> inside(bones.size(),0);inside[static_cast<size_t>(selectedBone_)]=1;
            for(size_t i=static_cast<size_t>(selectedBone_)+1;i<bones.size();++i)if(bones[i].parent>=0&&inside[static_cast<size_t>(bones[i].parent)])inside[i]=1;
            for(size_t i=0;i<bones.size()&&i<pose_.global.size();++i)if(inside[i])include(XMVectorSet(pose_.global[i]._41,pose_.global[i]._42,pose_.global[i]._43,1));
            for(const auto& mesh:model_.meshes)for(const auto& vertex:mesh.vertices){
                const size_t dominant=static_cast<size_t>(std::max_element(vertex.weights.begin(),vertex.weights.end())-vertex.weights.begin());
                if(vertex.weights[dominant]<=0||vertex.joints[dominant]>=mesh.skin.size())continue;
                const auto owner=mesh.skin[vertex.joints[dominant]].bone;if(owner>=inside.size()||!inside[owner])continue;
                XMVECTOR point=XMVectorZero();bool valid=true;
                for(size_t k=0;k<4;++k)if(vertex.weights[k]>0){
                    if(vertex.joints[k]>=mesh.skin.size()||mesh.skin[vertex.joints[k]].bone>=pose_.global.size()){valid=false;break;}
                    const auto& skin=mesh.skin[vertex.joints[k]];
                    point+=XMVector3TransformCoord(XMLoadFloat3(&vertex.position),XMLoadFloat4x4(&skin.inverseBind)*XMLoadFloat4x4(&pose_.global[skin.bone]))*vertex.weights[k];
                }
                if(valid)include(point);
            }
        }
        if(!any)return;
        const auto center=(low+high)*.5f;
        const float radius=std::max(XMVectorGetX(XMVector3Length(high-low))*.5f,minimum);
        const float aspect=renderer_.Height()?static_cast<float>(renderer_.Width())/static_cast<float>(renderer_.Height()):1.0f;
        XMStoreFloat3(&goal.target,center);
        goal.distance=std::clamp(radius/std::sin(HalfFov(aspect))*1.15f,sceneRadius*.05f,sceneRadius*150);
        AnimateCamera(goal);framed=true;
    });
    if(!framed&&!error_)FrameAll();
}
void EditorApp::ResetView() {Try([&]{
    pose_=EvaluatePose(model_.skeleton,bindPose_?nullptr:&Clip(),time_);renderer_.UpdateBounds(model_,pose_);
    auto goal=CameraGoal();renderer_.FitCamera(goal);goal.orthographic=false;viewAxis_=-1;autoOrtho_=false;AnimateCamera(goal);
});}
std::string EditorApp::ViewName() const {
    static const char* const korean[]={"정면","후면","우측","좌측","상단","하단"};
    static const char* const english[]={"Front","Back","Right","Left","Top","Bottom"};
    const bool ortho=CameraGoal().orthographic,axis=viewAxis_>=0&&viewAxis_<6;
    if(settings_.korean)return std::string(axis?korean[viewAxis_]:"사용자")+" · "+(ortho?"직교":"원근");
    return std::string(axis?english[viewAxis_]:"User")+" "+(ortho?"Orthographic":"Perspective");
}

void EditorApp::Shortcuts() {
    auto& io=ImGui::GetIO();auto& g=*ImGui::GetCurrentContext();
    if(io.AppFocusLost||ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopupId))return;
    // F1 never types text, so the helper toggles even while its own message box has focus.
    if(io.KeyShift&&!io.KeyCtrl&&!io.KeyAlt&&ImGui::IsKeyPressed(ImGuiKey_F1,false)){ToggleHelpBot();return;}
    if(io.WantTextInput)return;
    const bool dragging=ImGuizmo::IsUsing()||draggingBone_||navGizmoDragged_||(io.MouseDown[ImGuiMouseButton_Left]&&g.ActiveId!=0);
    const bool navigating=orbiting_||panning_||zooming_||io.MouseDown[ImGuiMouseButton_Right];
    if(io.KeyCtrl&&!io.KeyAlt){
        if(ImGui::IsKeyPressed(ImGuiKey_S,false)){SaveCurrentProject(io.KeyShift);return;}
        if(ImGui::IsKeyPressed(ImGuiKey_O,false)){OpenProject();return;}
        if(dragging)return;
        if(ImGui::IsKeyPressed(ImGuiKey_Z)){if(io.KeyShift)Redo();else Undo();}
        if(ImGui::IsKeyPressed(ImGuiKey_Y))Redo();
        if(ImGui::IsKeyPressed(ImGuiKey_D,false))DuplicateSelection();
        if(!navigating){
            if(ImGui::IsKeyPressed(ImGuiKey_Keypad1,false))SnapView(ViewBack);
            if(ImGui::IsKeyPressed(ImGuiKey_Keypad3,false))SnapView(ViewLeft);
            if(ImGui::IsKeyPressed(ImGuiKey_Keypad7,false))SnapView(ViewBottom);
        }
        return;
    }
    if(ImGui::IsKeyPressed(ImGuiKey_F1,false)){showShortcuts_=true;return;}
    if(!io.KeyAlt&&!keyboardNav_&&!dragging&&ImGui::IsKeyPressed(ImGuiKey_Space,false))playing_=!playing_;
    if(!navigating&&!io.KeyAlt&&!dragging){
        if(!io.KeyShift){
            if(ImGui::IsKeyPressed(ImGuiKey_W,false))tool_=Tool::Move;
            if(ImGui::IsKeyPressed(ImGuiKey_E,false))tool_=Tool::Rotate;
            if(ImGui::IsKeyPressed(ImGuiKey_R,false))tool_=Tool::Scale;
            if(ImGui::IsKeyPressed(ImGuiKey_X,false))worldSpace_=!worldSpace_;
            if(ImGui::IsKeyPressed(ImGuiKey_K,false)||ImGui::IsKeyPressed(ImGuiKey_I,false))InsertKey();
            if(ImGui::IsKeyPressed(ImGuiKey_F,false)||ImGui::IsKeyPressed(ImGuiKey_KeypadDecimal,false))FrameSelected();
            if(ImGui::IsKeyPressed(ImGuiKey_Keypad1,false))SnapView(ViewFront);
            if(ImGui::IsKeyPressed(ImGuiKey_Keypad3,false))SnapView(ViewRight);
            if(ImGui::IsKeyPressed(ImGuiKey_Keypad7,false))SnapView(ViewTop);
            if(ImGui::IsKeyPressed(ImGuiKey_Keypad9,false))ViewOpposite();
            if(ImGui::IsKeyPressed(ImGuiKey_Keypad5,false))ToggleOrthographic();
        }else if(ImGui::IsKeyPressed(ImGuiKey_F,false))FrameAll();
        constexpr float step=Pi/12;
        if(ImGui::IsKeyPressed(ImGuiKey_Keypad4))OrbitStep(-step,0);
        if(ImGui::IsKeyPressed(ImGuiKey_Keypad6))OrbitStep(step,0);
        if(ImGui::IsKeyPressed(ImGuiKey_Keypad8))OrbitStep(0,step);
        if(ImGui::IsKeyPressed(ImGuiKey_Keypad2))OrbitStep(0,-step);
        if(ImGui::IsKeyPressed(ImGuiKey_KeypadAdd)){StopCameraTween();camera_.Zoom(1);}
        if(ImGui::IsKeyPressed(ImGuiKey_KeypadSubtract)){StopCameraTween();camera_.Zoom(-1);}
    }
    // Timeline keys share the arrows with ImGui keyboard navigation. While a
    // widget shows the navigation cursor the arrows navigate; otherwise they scrub.
    if(!keyboardNav_&&!dragging){
        bool handled=true;
        const auto pressed=[](ImGuiKey key,bool repeat=true){return ImGui::IsKeyPressed(key,repeat);};
        if(io.KeyAlt){
            if(pressed(ImGuiKey_Period))JumpKey(1);else if(pressed(ImGuiKey_Comma))JumpKey(-1);else handled=false;
        }
        else if(io.KeyShift&&(pressed(ImGuiKey_LeftArrow,false)||pressed(ImGuiKey_Comma,false)))SetTime(0);
        else if(io.KeyShift&&(pressed(ImGuiKey_RightArrow,false)||pressed(ImGuiKey_Period,false)))SetTime(PlaybackDuration(Clip()));
        else if(!io.KeyShift&&(pressed(ImGuiKey_LeftArrow)||pressed(ImGuiKey_Comma)))StepFrame(-1);
        else if(!io.KeyShift&&(pressed(ImGuiKey_RightArrow)||pressed(ImGuiKey_Period)))StepFrame(1);
        else if(pressed(ImGuiKey_UpArrow))JumpKey(1);
        else if(pressed(ImGuiKey_DownArrow))JumpKey(-1);
        else if(pressed(ImGuiKey_Home,false))SetTime(0);
        else if(pressed(ImGuiKey_End,false))SetTime(PlaybackDuration(Clip()));
        else handled=false;
        if(handled){
            ImGui::NavMoveRequestCancel();
            g.NavInitRequest=g.NavInitRequestFromMove=false;g.NavCursorVisible=false;g.NavHighlightItemUnderNav=false;
        }
    }
    if(!dragging&&ImGui::IsKeyPressed(ImGuiKey_Delete,false))DeleteSelectedKey();
    if(!keyboardNav_&&ImGui::IsKeyPressed(ImGuiKey_Escape,false)){
        if(selectedKeyTime_>=0)selectedKeyTime_=-1;
        else if(workspace_==2)selectedEffect_=-1;
    }
}
void EditorApp::StepFrame(int frames) {
    const double fps=std::max(settings_.timelineFps,1);
    SetTime(std::clamp((std::round(time_*fps)+frames)/fps,0.0,PlaybackDuration(Clip())));
}
std::vector<double> EditorApp::KeyTimes() const {
    std::vector<double> times;
    const auto& clip=model_.clips.at(static_cast<size_t>(selectedClip_));const double speed=clip.speed>0?clip.speed:1;
    const auto collect=[&](const BoneTrack& track){
        for(const auto& key:track.positions)times.push_back(key.time/speed);
        for(const auto& key:track.rotations)times.push_back(key.time/speed);
        for(const auto& key:track.scales)times.push_back(key.time/speed);
    };
    for(const auto& track:clip.tracks)if(static_cast<int>(track.bone)==selectedBone_)collect(track);
    if(times.empty())for(const auto& track:clip.tracks)collect(track);
    std::sort(times.begin(),times.end());
    times.erase(std::unique(times.begin(),times.end(),[](double a,double b){return std::abs(a-b)<1e-6;}),times.end());
    return times;
}
void EditorApp::JumpKey(int direction) {
    const auto times=KeyTimes();
    if(times.empty()){status_=T("이동할 키프레임이 없습니다.","No keyframes to jump to.");error_=false;return;}
    const double epsilon=.5/std::max(settings_.timelineFps,1);
    if(direction>0){for(const double t:times)if(t>time_+epsilon){SetTime(t);return;}}
    else for(auto it=times.rbegin();it!=times.rend();++it)if(*it<time_-epsilon){SetTime(*it);return;}
}
void EditorApp::InsertKey() {
    if(workspace_==2||model_.skeleton.bones.empty())return;
    if(bindPose_){status_=T("기준 자세 보기를 끈 뒤 키를 추가하세요.","Turn off the bind pose view to insert keys.");error_=false;return;}
    Try([&]{
        const auto bone=static_cast<uint32_t>(std::clamp(selectedBone_,0,static_cast<int>(model_.skeleton.bones.size())-1));
        const auto current=EvaluatePose(model_.skeleton,&Clip(),time_).local.at(bone);
        RememberEdit();
        SetTransformKey(Clip(),bone,std::clamp(time_*Clip().speed,0.0,Clip().duration),current);
        dirty_=true;editBone_=-1;playing_=false;pose_=EvaluatePose(model_.skeleton,&Clip(),time_);
        status_=T("현재 프레임에 키를 추가했습니다.","Key inserted at the current frame.");
    });
}
void EditorApp::DeleteSelectedKey() {
    if(workspace_==2){DeleteEffect();return;}
    if(selectedKeyTime_<0||selectedBone_<0||selectedBone_>=static_cast<int>(model_.skeleton.bones.size()))return;
    Dispatch("delete_key",{{"bonePath",model_.skeleton.bones[static_cast<size_t>(selectedBone_)].path},{"time",selectedKeyTime_}});
    if(!error_){selectedKeyTime_=-1;status_=T("키를 삭제했습니다. Ctrl+Z로 되돌릴 수 있습니다.","Key deleted. Press Ctrl+Z to undo.");}
}
void EditorApp::DuplicateSelection() {
    if(workspace_==2)DuplicateEffect();
    else if(workspace_==1)DuplicateClip();
}

int EditorApp::OrientationGizmoHit(XMFLOAT2 origin,XMFLOAT2 size,XMFLOAT2 mouse) const {
    const auto layout=LayoutGizmo(origin,size);if(!layout.visible)return -1;
    const auto axes=GizmoAxes(camera_.View(),layout);const float pick=layout.ball+UiScale(2);
    for(auto axis=axes.rbegin();axis!=axes.rend();++axis){
        const float dx=mouse.x-axis->pos.x,dy=mouse.y-axis->pos.y;if(dx*dx+dy*dy<=pick*pick)return axis->index;
    }
    const float dx=mouse.x-layout.center.x,dy=mouse.y-layout.center.y,outer=layout.radius+layout.ball;
    if(dx*dx+dy*dy<=outer*outer)return 6;
    if(GizmoLabelRect(layout,GizmoLabel(CameraGoal().orthographic,settings_.korean)).Contains({mouse.x,mouse.y}))return 7;
    return -1;
}
bool EditorApp::OrientationGizmo(XMFLOAT2 origin,XMFLOAT2 size,bool hovered) {
    const auto layout=LayoutGizmo(origin,size);
    if(!layout.visible){navGizmoPressed_=-1;navGizmoDragged_=false;navGizmoHoverIndex_=-1;return false;}
    const auto& io=ImGui::GetIO();const XMFLOAT2 mouse{io.MousePos.x,io.MousePos.y};
    const bool idle=!ImGuizmo::IsUsing()&&!orbiting_&&!panning_&&!zooming_&&!draggingBone_;
    const int hit=hovered&&(idle||navGizmoPressed_>=0)?OrientationGizmoHit(origin,size,mouse):-1;
    if(navGizmoPressed_<0&&hit>=0&&idle&&!io.KeyAlt&&ImGui::IsMouseClicked(ImGuiMouseButton_Left)){
        navGizmoPressed_=hit;navGizmoDragged_=false;navGizmoPressPos_=mouse;
    }
    if(navGizmoPressed_>=0){
        if(io.MouseDown[ImGuiMouseButton_Left]){
            // Dragging anywhere on the gizmo orbits, like Blender's navigation gizmo.
            if(!navGizmoDragged_&&std::hypot(mouse.x-navGizmoPressPos_.x,mouse.y-navGizmoPressPos_.y)>UiScale(3)){
                navGizmoDragged_=true;StopCameraTween();LeaveAxisView(camera_);
                camera_.Orbit(mouse.x-navGizmoPressPos_.x,mouse.y-navGizmoPressPos_.y);
            }else if(navGizmoDragged_&&(io.MouseDelta.x!=0||io.MouseDelta.y!=0)){LeaveAxisView(camera_);camera_.Orbit(io.MouseDelta.x,io.MouseDelta.y);}
            if(navGizmoDragged_)RequestContinuousMouse();
        }else{
            if(!navGizmoDragged_&&hit==navGizmoPressed_){
                static constexpr int views[6]={ViewRight,ViewLeft,ViewTop,ViewBottom,ViewBack,ViewFront};
                if(hit<6)SnapView(viewAxis_==views[hit]?views[hit]^1:views[hit]);
                else if(hit==7)ToggleOrthographic();
            }
            navGizmoPressed_=-1;navGizmoDragged_=false;
        }
    }
    const int hover=navGizmoPressed_>=0?navGizmoPressed_:hit;
    if(hover!=navGizmoHoverIndex_){navGizmoHoverIndex_=hover;navGizmoHoverTime_=0;}
    else if(hover>=0)navGizmoHoverTime_+=io.DeltaTime;

    auto* draw=ImGui::GetWindowDrawList();
    draw->PushClipRect({origin.x,origin.y},{origin.x+size.x,origin.y+size.y},true);
    if((hover>=0&&hover<=6)||navGizmoDragged_)draw->AddCircleFilled(layout.center,layout.radius+layout.ball,IM_COL32(255,255,255,28),48);
    static constexpr ImU32 colors[3]={IM_COL32(232,72,92,255),IM_COL32(140,200,60,255),IM_COL32(64,144,240,255)};
    static const char* const names[6]={"X","-X","Y","-Y","Z","-Z"};
    ImFont* bold=UiFonts::semibold?UiFonts::semibold:ImGui::GetFont();const float axisFont=ImGui::GetFontSize()*.78f;
    for(const auto& axis:GizmoAxes(camera_.View(),layout)){
        const ImU32 color=colors[axis.index/2];const bool hot=hover==axis.index;
        const ImVec2 text=bold->CalcTextSizeA(axisFont,FLT_MAX,0,names[axis.index]);
        const ImVec2 textPos{axis.pos.x-text.x*.5f,axis.pos.y-text.y*.5f};
        if(axis.index%2==0){
            draw->AddLine(layout.center,axis.pos,color,UiScale(2));
            draw->AddCircleFilled(axis.pos,layout.ball,color,24);
            if(hot)draw->AddCircle(axis.pos,layout.ball+UiScale(1.5f),IM_COL32(255,255,255,230),24,UiScale(1.5f));
            draw->AddText(bold,axisFont,textPos,IM_COL32(16,18,26,255),names[axis.index]);
        }else{
            const ImU32 fill=(color&~IM_COL32_A_MASK)|(static_cast<ImU32>(hot?150:64)<<IM_COL32_A_SHIFT);
            draw->AddCircleFilled(axis.pos,layout.ball*.92f,fill,24);
            draw->AddCircle(axis.pos,layout.ball*.92f,color,24,UiScale(1.2f));
            if(hot)draw->AddText(bold,axisFont,textPos,IM_COL32(244,246,252,255),names[axis.index]);
        }
    }
    const char* label=GizmoLabel(CameraGoal().orthographic,settings_.korean);
    const auto rect=GizmoLabelRect(layout,label);const ImVec2 labelSize=ImGui::GetFont()->CalcTextSizeA(GizmoLabelSize(),FLT_MAX,0,label);
    draw->AddRectFilled(rect.Min,rect.Max,hover==7?IM_COL32(255,255,255,40):IM_COL32(20,23,32,150),UiScale(4));
    draw->AddText(ImGui::GetFont(),GizmoLabelSize(),{(rect.Min.x+rect.Max.x-labelSize.x)*.5f,(rect.Min.y+rect.Max.y-labelSize.y)*.5f},
        hover==7?IM_COL32(236,240,250,255):IM_COL32(156,168,192,255),label);
    draw->PopClipRect();
    if(hover>=0&&navGizmoPressed_<0&&navGizmoHoverTime_>=.45f){
        static const char* const korean[8]={"우측 보기 (Numpad 3)","좌측 보기 (Ctrl+Numpad 3)","상단 보기 (Numpad 7)","하단 보기 (Ctrl+Numpad 7)",
            "후면 보기 (Ctrl+Numpad 1)","정면 보기 (Numpad 1)","드래그해 시점 회전","원근/직교 전환 (Numpad 5)"};
        static const char* const english[8]={"Right view (Numpad 3)","Left view (Ctrl+Numpad 3)","Top view (Numpad 7)","Bottom view (Ctrl+Numpad 7)",
            "Back view (Ctrl+Numpad 1)","Front view (Numpad 1)","Drag to orbit","Toggle perspective/orthographic (Numpad 5)"};
        ImGui::SetTooltip("%s",settings_.korean?korean[hover]:english[hover]);
    }
    return hover>=0||navGizmoPressed_>=0;
}
void EditorApp::ViewportOverlay(XMFLOAT2 origin,XMFLOAT2 size,bool hovered) {
    auto* draw=ImGui::GetWindowDrawList();
    draw->PushClipRect({origin.x,origin.y},{origin.x+size.x,origin.y+size.y},true);
    const auto name=ViewName();
    draw->AddText({origin.x+UiScale(14),origin.y+UiScale(12)},IM_COL32(156,168,192,255),name.c_str());
    if(flySpeedNotice_>0){
        char text[64];std::snprintf(text,sizeof(text),settings_.korean?"이동 속도 x%.2f":"Fly speed x%.2f",camera_.flySpeed);
        const float alpha=std::min(flySpeedNotice_/.4f,1.0f),pad=UiScale(8);const ImVec2 textSize=ImGui::CalcTextSize(text);
        const ImVec2 min{origin.x+(size.x-textSize.x)*.5f-pad,origin.y+UiScale(12)},max{min.x+textSize.x+pad*2,min.y+textSize.y+pad};
        draw->AddRectFilled(min,max,IM_COL32(16,18,26,static_cast<int>(210*alpha)),UiScale(6));
        draw->AddText({min.x+pad,min.y+pad*.5f},IM_COL32(236,240,250,static_cast<int>(255*alpha)),text);
    }
    const bool idle=!ImGuizmo::IsUsing()&&!orbiting_&&!panning_&&!zooming_&&!draggingBone_&&navGizmoPressed_<0;
    if(hovered&&idle){
        const char* full=T("우클릭: 회전 + WASD 이동   휠 클릭: 화면 이동   휠: 확대   F: 선택 보기   F1: 단축키",
            "RMB: orbit + WASD fly   MMB: pan   Wheel: zoom   F: frame   F1: shortcuts");
        const char* brief=T("F1: 단축키","F1: shortcuts");const float room=size.x-UiScale(28);
        const char* hint=ImGui::CalcTextSize(full).x<=room?full:ImGui::CalcTextSize(brief).x<=room?brief:nullptr;
        if(hint){
            const ImVec2 pos{origin.x+UiScale(14),origin.y+size.y-ImGui::GetTextLineHeight()-UiScale(10)};
            draw->AddText({pos.x+1,pos.y+1},IM_COL32(0,0,0,150),hint);draw->AddText(pos,IM_COL32(176,186,206,210),hint);
        }
    }
    draw->PopClipRect();
}
void EditorApp::ShortcutHelp() {
    bool opened=false;
    if(showShortcuts_){ImGui::OpenPopup("Shortcuts###Shortcuts");showShortcuts_=false;opened=true;}
    const auto* viewport=ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->GetCenter(),ImGuiCond_Always,{.5f,.5f});
    ImGui::SetNextWindowSize({std::min(UiScale(560),viewport->WorkSize.x*.9f),std::min(UiScale(520),viewport->WorkSize.y*.85f)},ImGuiCond_Always);
    bool open=true;
    if(!ImGui::BeginPopupModal(T("단축키###Shortcuts","Keyboard shortcuts###Shortcuts"),&open,ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoSavedSettings))return;
    struct Row { const char *koKey,*enKey,*ko,*en; };
    const auto section=[&](const char* id,const char* title,std::initializer_list<Row> rows){
        ImGui::SeparatorText(title);
        float keyWidth=0;for(const auto& row:rows)keyWidth=std::max(keyWidth,ImGui::CalcTextSize(T(row.koKey,row.enKey)).x);
        keyWidth=std::min(keyWidth+UiScale(8),ImGui::GetContentRegionAvail().x*.45f);
        if(!ImGui::BeginTable(id,2,ImGuiTableFlags_RowBg|ImGuiTableFlags_SizingFixedFit))return;
        ImGui::TableSetupColumn("key",ImGuiTableColumnFlags_WidthFixed,keyWidth);
        ImGui::TableSetupColumn("action",ImGuiTableColumnFlags_WidthStretch);
        for(const auto& row:rows){
            ImGui::TableNextRow();ImGui::TableSetColumnIndex(0);
            ImGui::PushStyleColor(ImGuiCol_Text,ImVec4{.80f,.70f,1,1});ImGui::TextWrapped("%s",T(row.koKey,row.enKey));ImGui::PopStyleColor();
            ImGui::TableSetColumnIndex(1);ImGui::TextWrapped("%s",T(row.ko,row.en));
        }
        ImGui::EndTable();
    };
    if(ImGui::BeginChild("##shortcut-list",{0,-ImGui::GetFrameHeightWithSpacing()})){
        section("##viewport",T("뷰포트","Viewport"),{
            {"우클릭 드래그","RMB drag","시점 회전","Orbit"},
            {"Alt + 좌클릭 드래그","Alt + LMB drag","시점 회전 (Unity/Maya 방식)","Orbit (Unity/Maya style)"},
            {"휠 클릭 드래그","MMB drag","화면 이동","Pan"},
            {"휠, Alt + 우클릭 드래그","Wheel, Alt + RMB drag","확대 / 축소","Zoom"},
            {"우클릭 + W A S D Q E","RMB + W A S D Q E","비행 이동 (Shift: 빠르게, 휠: 속도)","Fly (Shift: faster, wheel: speed)"},
            {"F, 숫자패드 .","F, Numpad .","선택 항목에 맞추기","Frame selection"},
            {"Shift + F","Shift + F","전체 보기","Frame all"},
            {"숫자패드 1 / 3 / 7","Numpad 1 / 3 / 7","정면 / 우측 / 상단 보기","Front / right / top view"},
            {"Ctrl + 숫자패드 1 / 3 / 7","Ctrl + Numpad 1 / 3 / 7","후면 / 좌측 / 하단 보기","Back / left / bottom view"},
            {"숫자패드 9","Numpad 9","반대편 보기","Opposite view"},
            {"숫자패드 5","Numpad 5","원근 / 직교 전환","Perspective / orthographic"},
            {"숫자패드 2 4 6 8","Numpad 2 4 6 8","15°씩 회전","Orbit in 15° steps"},
            {"숫자패드 + / -","Numpad + / -","확대 / 축소","Zoom in / out"},
            {"축 기즈모 클릭","Axis gizmo click","해당 방향 보기 (다시 클릭: 반대편)","Snap to that view (click again: opposite)"}});
        section("##tools",T("도구","Tools"),{
            {"W / E / R","W / E / R","이동 / 회전 / 크기","Move / rotate / scale"},
            {"X","X","월드 / 로컬 공간 전환","Toggle world / local space"},
            {"Ctrl (조작 중)","Ctrl (while dragging)","눈금 스냅","Snap to increments"},
            {"관절 또는 메시 클릭","Click a joint or mesh","뼈 선택","Select a bone"}});
        section("##timeline",T("타임라인","Timeline"),{
            {"Space","Space","재생 / 일시정지","Play / pause"},
            {"좌우 화살표, , / .","Left / Right arrow, , / .","이전 / 다음 프레임","Previous / next frame"},
            {"위 화살표, Alt + .","Up arrow, Alt + .","다음 키로 이동","Jump to the next key"},
            {"아래 화살표, Alt + ,","Down arrow, Alt + ,","이전 키로 이동","Jump to the previous key"},
            {"Home / End, Shift + 좌우 화살표","Home / End, Shift + Left / Right","처음 / 마지막 프레임","First / last frame"},
            {"K, I","K, I","현재 프레임에 키 추가","Insert a key at the current frame"},
            {"Delete","Delete","선택한 키 또는 효과 삭제","Delete the selected key or effect"},
            {"Esc","Esc","키 선택 해제","Clear the key selection"}});
        section("##mocap",T("웹캠 모션캡처","Webcam capture"),{
            {"Space","Space","녹화 시작 / 정지 · 계속 녹화","Start / stop recording · record again"},
            {"Esc","Esc","카운트다운 취소","Cancel the countdown"},
            {"Enter","Enter","테이크 완료 (카메라 끄기)","Keep the take (camera off)"},
            {"Delete","Delete","테이크 버리기","Discard the take"}});
        section("##file",T("파일 및 편집","File & edit"),{
            {"Ctrl + S","Ctrl + S","프로젝트 저장","Save project"},
            {"Ctrl + Shift + S","Ctrl + Shift + S","다른 이름으로 저장","Save project as"},
            {"Ctrl + O","Ctrl + O","프로젝트 열기","Open project"},
            {"Ctrl + Z","Ctrl + Z","실행 취소","Undo"},
            {"Ctrl + Y, Ctrl + Shift + Z","Ctrl + Y, Ctrl + Shift + Z","다시 실행","Redo"},
            {"Ctrl + D","Ctrl + D","동작 또는 효과 복제","Duplicate motion or effect"},
            {"F1","F1","이 목록 보기","Show this list"},
            {"Shift + F1","Shift + F1","LunaFlow 도우미 열기·닫기","Open or close the LunaFlow helper"}});
    }
    ImGui::EndChild();
    if(ImGui::Button(T("닫기","Close"),{-FLT_MIN,0})||ImGui::IsKeyPressed(ImGuiKey_Escape,false)||(!opened&&ImGui::IsKeyPressed(ImGuiKey_F1,false)))ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}
}

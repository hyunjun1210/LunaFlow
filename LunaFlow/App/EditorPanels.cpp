#include "App/EditorApp.h"
#include "imgui.h"
#include "App/ContinuousInput.h"
#include "UiFonts.h"
#include "App/UiTheme.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace LunaFlow {
struct CompactInspector {
    CompactInspector(){ImGui::PushFont(UiFonts::regular,14);ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,{UiScale(5),UiScale(2)});ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,{UiScale(5),UiScale(3)});ImGui::PushStyleVar(ImGuiStyleVar_CellPadding,{UiScale(3),UiScale(1)});}
    ~CompactInspector(){ImGui::PopStyleVar(3);ImGui::PopFont();}
};
enum class TransportIcon {Record,First,Previous,Play,Pause,Next,Last};
static ImVec2 TransportSize(){const float h=ImGui::GetFrameHeight();return {h*7/6,h};}
static bool TransportButton(const char* id,TransportIcon icon,const char* tooltip) {
    const bool clicked=ImGui::Button(id,TransportSize());const auto a=ImGui::GetItemRectMin(),b=ImGui::GetItemRectMax();
    const float x=(a.x+b.x)*.5f,y=(a.y+b.y)*.5f;auto* draw=ImGui::GetWindowDrawList();
    const float u=(b.y-a.y)/26;
    const ImU32 color=icon==TransportIcon::Pause?IM_COL32(140,228,202,255):IM_COL32(219,225,237,255);
    if(icon==TransportIcon::Record)draw->AddCircleFilled({x,y},5*u,ImGui::GetColorU32(IM_COL32(235,72,80,255)));
    else if(icon==TransportIcon::Pause){draw->AddRectFilled({x-5*u,y-5*u},{x-u,y+5*u},color,u);draw->AddRectFilled({x+u,y-5*u},{x+5*u,y+5*u},color,u);}
    else{
        const bool left=icon==TransportIcon::First||icon==TransportIcon::Previous;
        const float direction=left?-1.0f:1.0f;
        draw->AddTriangleFilled({x-direction*3*u,y-5*u},{x-direction*3*u,y+5*u},{x+direction*4*u,y},color);
        if(icon==TransportIcon::First)draw->AddRectFilled({x-7*u,y-5*u},{x-5*u,y+5*u},color);
        if(icon==TransportIcon::Previous)draw->AddRectFilled({x+5*u,y-5*u},{x+7*u,y+5*u},color);
        if(icon==TransportIcon::Next)draw->AddRectFilled({x-7*u,y-5*u},{x-5*u,y+5*u},color);
        if(icon==TransportIcon::Last)draw->AddRectFilled({x+5*u,y-5*u},{x+7*u,y+5*u},color);
    }
    if(ImGui::IsItemHovered())ImGui::SetTooltip("%s",tooltip);return clicked;
}
struct TimelineGeometry {
    float comboWidth=0;
    int buttonsPerRow=6,rows=2;
    bool fpsInline=true,extrasInline=true,counterInline=true;
};
static TimelineGeometry MeasureTimeline(float width,const char* loopLabel,const char* fpsLabel,int totalFrames) {
    const auto& s=ImGui::GetStyle();const auto button=TransportSize();
    TimelineGeometry g;const float fpsWidth=ImGui::CalcTextSize(fpsLabel).x;
    g.fpsInline=width>=ImGui::GetFrameHeight()*3+s.ItemSpacing.x+fpsWidth;
    g.comboWidth=g.fpsInline?std::min(UiScale(230),width-s.ItemSpacing.x-fpsWidth):width;
    g.buttonsPerRow=std::clamp(static_cast<int>((width+s.ItemInnerSpacing.x)/(button.x+s.ItemInnerSpacing.x)),1,6);
    char counter[64];std::snprintf(counter,sizeof(counter),"%d / %d",totalFrames,totalFrames);
    const float loopWidth=ImGui::GetFrameHeight()+s.ItemInnerSpacing.x+ImGui::CalcTextSize(loopLabel).x;
    const float extrasWidth=loopWidth+s.ItemSpacing.x+ImGui::CalcTextSize(counter).x;
    const float buttonsWidth=6*button.x+5*s.ItemInnerSpacing.x;
    g.counterInline=extrasWidth<=width;
    g.extrasInline=g.buttonsPerRow==6&&buttonsWidth+s.ItemSpacing.x+extrasWidth<=width;
    g.rows=1+(g.fpsInline?0:1)+(6+g.buttonsPerRow-1)/g.buttonsPerRow+(g.extrasInline?0:1)+(g.counterInline?0:1);
    return g;
}
// Manual layout so X/Y/Z always get equal widths; a stretch table derived its weights from the first frame's content and collapsed X.
static bool VectorRow(const char* label,const char* id,XMFLOAT3& value,float step,const char* format,bool positive=false) {
    bool changed=false;ImGui::PushID(id);
    const ImGuiStyle& style=ImGui::GetStyle();
    const float start=ImGui::GetCursorPosX(),avail=ImGui::GetContentRegionAvail().x;
    const float labelWidth=std::max(UiScale(48),ImGui::CalcTextSize(label).x+style.ItemSpacing.x);
    const float letterGap=UiScale(3),letterWidth=ImGui::CalcTextSize("X").x+letterGap;
    const float field=std::max(UiScale(24),(avail-labelWidth-3*letterWidth-2*style.ItemSpacing.x)/3);
    ImGui::AlignTextToFramePadding();ImGui::TextUnformatted(label);
    const ImVec4 colors[]={{.91f,.43f,.50f,1},{.53f,.78f,.47f,1},{.45f,.63f,.93f,1}};const char* axes[]={"X","Y","Z"};float* values=&value.x;
    const char* dot=std::strchr(format,'.');const float zero=.5f*std::pow(10.0f,-static_cast<float>(dot?std::atoi(dot+1):3));
    for(int i=0;i<3;++i){
        ImGui::SameLine(0,i?style.ItemSpacing.x:0);
        if(i==0)ImGui::SetCursorPosX(start+labelWidth);
        // Values that round to zero are shown as 0 instead of "-0.0"; the stored value only changes when edited.
        float shown=std::fabs(values[i])<zero?0.0f:values[i];
        ImGui::PushID(i);ImGui::TextColored(colors[i],"%s",axes[i]);ImGui::SameLine(0,letterGap);ImGui::SetNextItemWidth(field);
        if(ContinuousDragFloat("##v",&shown,step,positive?.001f:0,positive?10000.0f:0,format,positive?ImGuiSliderFlags_AlwaysClamp:0)){values[i]=shown;changed=true;}
        ImGui::PopID();
    }
    ImGui::PopID();return changed;
}
void EditorApp::Library() {
    if(ImGui::Button(T("FBX 가져오기...###ImportFbx","Import FBX...###ImportFbx"),{-1,0}))showFbxImport_=true;
    AutoRigNotice();
    if(!ImGui::BeginTabBar("library-tabs",ImGuiTabBarFlags_FittingPolicyShrink))return;
    const int requested=selectLibraryTab_?static_cast<int>(libraryTab_):-1;selectLibraryTab_=false;
    auto tab=[&](const char* label,LibraryTab value){
        const auto flags=requested==static_cast<int>(value)?ImGuiTabItemFlags_SetSelected:ImGuiTabItemFlags_None;
        const bool visible=ImGui::BeginTabItem(label,nullptr,flags);
        if(visible&&(requested<0||requested==static_cast<int>(value)))libraryTab_=value;
        return visible;
    };
    if(tab(T("계층###Hierarchy","Tree###Hierarchy"),LibraryTab::Hierarchy)){
        ImGui::Separator();
        ImGui::BeginChild("nodes",{0,0});
        std::vector<std::vector<int>> children(model_.skeleton.bones.size());std::vector<int> roots;
        for(size_t i=0;i<model_.skeleton.bones.size();++i){const int parent=model_.skeleton.bones[i].parent;if(parent<0)roots.push_back(static_cast<int>(i));else children[parent].push_back(static_cast<int>(i));}
        std::vector<bool> ancestors(model_.skeleton.bones.size(),false);
        for(int p=selectedBone_;p>=0;p=model_.skeleton.bones[p].parent)ancestors[p]=true;
        std::function<void(int,int)> tree=[&](int index,int depth){
            const auto& bone=model_.skeleton.bones[index];const bool leaf=children[index].empty();
            ImGuiTreeNodeFlags flags=ImGuiTreeNodeFlags_OpenOnArrow|ImGuiTreeNodeFlags_OpenOnDoubleClick|ImGuiTreeNodeFlags_SpanAvailWidth;
            if(leaf)flags|=ImGuiTreeNodeFlags_Leaf|ImGuiTreeNodeFlags_NoTreePushOnOpen;
            if(index==selectedBone_)flags|=ImGuiTreeNodeFlags_Selected;
            if(!leaf){if(revealSelected_&&ancestors[index])ImGui::SetNextItemOpen(true);else ImGui::SetNextItemOpen(depth<1,ImGuiCond_Once);}
            const bool open=ImGui::TreeNodeEx(reinterpret_cast<void*>(static_cast<intptr_t>(index+1)),flags,"%s",bone.name.c_str());
            if(ImGui::IsItemClicked()&&!ImGui::IsItemToggledOpen()){selectedBone_=index;selectedEffect_=-1;selectedKeyTime_=-1;editBone_=-1;options_.skeleton=true;}
            if(revealSelected_&&index==selectedBone_)ImGui::SetScrollHereY(.5f);
            if(ImGui::IsItemHovered())ImGui::SetTooltip("%s",bone.path.c_str());
            if(open&&!leaf){for(int child:children[index])tree(child,depth+1);ImGui::TreePop();}
        };
        for(int root:roots)tree(root,0);revealSelected_=false;
        ImGui::EndChild();ImGui::EndTabItem();
    }
    if(tab(T("동작###Motions","Clips###Motions"),LibraryTab::Motions)){
        for(size_t i=0;i<model_.clips.size();++i){ImGui::PushID(static_cast<int>(i));
            if(ImGui::Selectable(ClipLabel(model_.clips[i]),selectedClip_==static_cast<int>(i),0,{0,28})){selectedClip_=static_cast<int>(i);bindPose_=false;SetTime(0);editClip_=-1;nameClip_=-1;loop_=Clip().loop;selectedKeyTime_=-1;}
            ImGui::TextDisabled("%.2f s",PlaybackDuration(model_.clips[i]));ImGui::PopID();
        }
        ImGui::Separator();if(ImGui::Button(T("새 동작","New motion")))Dispatch("new_clip",{{"name",T("새 동작","New motion")}});ImGui::SameLine();
        if(ImGui::Button(T("복제","Duplicate")))DuplicateClip();
        ImGui::BeginDisabled(model_.clips.size()<2);if(ImGui::Button(T("선택 동작 삭제","Delete selected motion"),{-1,0}))Dispatch("delete_clip");ImGui::EndDisabled();
        if(nameClip_!=selectedClip_){strncpy_s(clipName_,Clip().name.c_str(),_TRUNCATE);durationDraft_=PlaybackDuration(Clip());nameClip_=selectedClip_;}
        ImGui::TextUnformatted(T("이름","Name"));ImGui::SetNextItemWidth(-1);ImGui::InputText("##motion-name",clipName_,sizeof(clipName_));
        if(ImGui::IsItemDeactivatedAfterEdit())Dispatch("rename_clip",{{"name",clipName_}});
        ImGui::TextUnformatted(T("길이 (초)","Length (seconds)"));ImGui::SetNextItemWidth(-1);ImGui::InputDouble("##length",&durationDraft_,.1,1,"%.3f");
        if(ImGui::Button(T("길이 변경 적용","Apply new duration"),{-1,0}))Dispatch("retime_clip",{{"duration",durationDraft_}});
        ImGui::Spacing();ProportionReportPanel();
        ImGui::EndTabItem();
    }
    if(tab(T("정보###Asset","Info###Asset"),LibraryTab::Asset)){
        ImGui::TextWrapped("%s",model_.source.filename().u8string().c_str());size_t triangles=0;for(const auto& m:model_.meshes)triangles+=m.indices.size()/3;
        ImGui::Text("%zu %s",model_.meshes.size(),T("메시","meshes"));ImGui::Text("%zu %s",triangles,T("삼각형","triangles"));
        for(const auto& warning:model_.warnings)ImGui::TextWrapped("%s",warning.c_str());for(const auto& warning:renderer_.Warnings())ImGui::TextWrapped("%s",warning.c_str());ImGui::EndTabItem();
    }
    if(tab(T("효과 목록###Effects","Effects###Effects"),LibraryTab::Effects)){
        EffectLibrary();ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
}
void EditorApp::Inspector() {
    CompactInspector compact;
    if(workspace_==2||selectedEffect_>=0){EffectInspector();return;}
    ImGui::SeparatorText(T("속성","INSPECTOR"));
    const auto bone=model_.skeleton.bones.at(selectedBone_);ImGui::TextColored({.72f,.58f,1,1},"%s",bone.name.c_str());
    if(editBone_!=selectedBone_||editClip_!=selectedClip_||std::abs(editTime_-time_)>1e-8||playing_){edit_=pose_.local.at(selectedBone_);editAngles_=DegreesFromQuaternion(edit_.rotation);editBone_=selectedBone_;editClip_=selectedClip_;editTime_=time_;}
    if(playing_)ImGui::TextDisabled("%s",T("일시 정지하면 수치를 편집할 수 있습니다.","Pause to edit transform values."));
    ImGui::BeginDisabled(playing_||bindPose_);
    bool changed=VectorRow(T("위치","Pos"),"position",edit_.translation,.005f,"%.3f");
    changed|=VectorRow(T("회전°","Rot°"),"rotation",editAngles_,.5f,"%.1f");
    changed|=VectorRow(T("크기","Scale"),"scale",edit_.scale,.005f,"%.3f",true);
    if(changed){RememberEdit();Try([&]{CommitPose();});}
    if(ImGui::Button(T("키 기록","Set key"))){RememberEdit();Try([&]{CommitPose();});}ImGui::SameLine();
    if(ImGui::Button(T("기준 자세","Reset pose"))){RememberEdit();edit_=bone.bindLocal;editAngles_=DegreesFromQuaternion(edit_.rotation);Try([&]{CommitPose();});}
    ImGui::EndDisabled();ImGui::Separator();
    if(ImGui::Checkbox(T("기준 자세 보기","Show bind pose"),&bindPose_)){editBone_=-1;pose_=EvaluatePose(model_.skeleton,bindPose_?nullptr:&Clip(),time_);}
    const int objectIndex=ObjectForBone(model_,static_cast<uint32_t>(selectedBone_));
    if(objectIndex>=0&&model_.objects.size()>1){
        ImGui::SeparatorText(T("오브젝트 부착","OBJECT ATTACHMENT"));
        attachmentTarget_=std::clamp(attachmentTarget_,0,static_cast<int>(model_.skeleton.bones.size()-1));
        ImGui::SetNextItemWidth(-1);if(ImGui::BeginCombo("##attach-target",model_.skeleton.bones[attachmentTarget_].name.c_str())){for(size_t i=0;i<model_.skeleton.bones.size();++i)if(ImGui::Selectable(model_.skeleton.bones[i].name.c_str(),attachmentTarget_==static_cast<int>(i)))attachmentTarget_=static_cast<int>(i);ImGui::EndCombo();}
        auto attach=[&](int parent,bool keep){Try([&]{ModelAsset next=model_;const auto id=model_.objects[objectIndex].id;AttachObject(next,id,parent,&Clip(),time_,keep);renderer_.SetModel(next);model_=std::move(next);for(const auto& o:model_.objects)if(o.id==id)selectedBone_=static_cast<int>(o.root);ClearHistory();editBone_=-1;revealSelected_=true;dirty_=true;pose_=EvaluatePose(model_.skeleton,&Clip(),time_);});};
        if(ImGui::Button(T("선택한 뼈에 부착","Attach to joint"),{-1,0}))attach(attachmentTarget_,false);
        if(ImGui::Button(T("부착 해제 (위치 유지)","Detach (keep world pose)"),{-1,0}))attach(-1,true);
    }
}
float EditorApp::TimelineMinimumHeight(float width) const {
    char fps[64];std::snprintf(fps,sizeof(fps),"%d FPS %s",settings_.timelineFps,T("격자","grid"));
    const auto g=MeasureTimeline(width,T("반복","Loop"),fps,static_cast<int>(std::ceil(PlaybackDuration(model_.clips[selectedClip_])*settings_.timelineFps)));
    const int rows=g.rows+(workspace_==1?1:0)+(selectedKeyTime_>=0&&workspace_!=0?1:0);
    return rows*ImGui::GetFrameHeight()+rows*ImGui::GetStyle().ItemSpacing.y+ImGui::GetFontSize()*2+UiScale(4);
}
void EditorApp::Timeline(float height) {
    ImGui::BeginChild("timeline",{0,height},0,ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::SetScrollY(0);
    const auto& style=ImGui::GetStyle();const double duration=PlaybackDuration(Clip());
    char fps[64];std::snprintf(fps,sizeof(fps),"%d FPS %s",settings_.timelineFps,T("격자","grid"));
    const auto geometry=MeasureTimeline(ImGui::GetContentRegionAvail().x,T("반복","Loop"),fps,static_cast<int>(std::ceil(duration*settings_.timelineFps)));
    ImGui::SetNextItemWidth(std::max(geometry.comboWidth,1.0f));
    if(ImGui::BeginCombo("##clip",ClipLabel(Clip()))){for(size_t i=0;i<model_.clips.size();++i)if(ImGui::Selectable(ClipLabel(model_.clips[i]),i==static_cast<size_t>(selectedClip_))){selectedClip_=static_cast<int>(i);bindPose_=false;loop_=Clip().loop;nameClip_=-1;selectedKeyTime_=-1;SetTime(0);editClip_=-1;}ImGui::EndCombo();}
    if(geometry.fpsInline)ImGui::SameLine();ImGui::AlignTextToFramePadding();ImGui::TextDisabled("%s",fps);
    auto nextButton=[&](int index){if(index%geometry.buttonsPerRow!=0)ImGui::SameLine(0,style.ItemInnerSpacing.x);};
    if(TransportButton("##record",TransportIcon::Record,T("웹캠으로 동작 녹화 (모션캡처 열기)","Record motion with a webcam (open motion capture)")))OpenMocap();nextButton(1);
    if(TransportButton("##first",TransportIcon::First,T("처음으로 (Home)","First frame (Home)")))SetTime(0);nextButton(2);
    if(TransportButton("##previous",TransportIcon::Previous,T("이전 프레임 (왼쪽 화살표 / ,)","Previous frame (Left arrow / ,)")))StepFrame(-1);nextButton(3);
    if(TransportButton("##play",playing_?TransportIcon::Pause:TransportIcon::Play,playing_?T("일시 정지 (Space)","Pause (Space)"):T("재생 (Space)","Play (Space)")))playing_=!playing_;nextButton(4);
    if(TransportButton("##next",TransportIcon::Next,T("다음 프레임 (오른쪽 화살표 / .)","Next frame (Right arrow / .)")))StepFrame(1);nextButton(5);
    if(TransportButton("##last",TransportIcon::Last,T("끝으로 (End)","Last frame (End)")))SetTime(duration);if(geometry.extrasInline)ImGui::SameLine();
    bool loop=Clip().loop;if(ImGui::Checkbox(T("반복","Loop"),&loop))Dispatch("set_loop",{{"loop",loop}});
    if(geometry.counterInline)ImGui::SameLine();ImGui::AlignTextToFramePadding();ImGui::TextColored({.72f,.58f,1,1},"%d / %d",static_cast<int>(std::round(time_*settings_.timelineFps)),static_cast<int>(std::ceil(duration*settings_.timelineFps)));
    const int footerRows=(workspace_==1?1:0)+(selectedKeyTime_>=0&&workspace_!=0?1:0);
    const float footer=footerRows*ImGui::GetFrameHeightWithSpacing()+UiScale(2);
    const ImVec2 p=ImGui::GetCursorScreenPos();const float width=std::max(ImGui::GetContentRegionAvail().x,1.0f),trackHeight=std::max(ImGui::GetContentRegionAvail().y-footer,1.0f);
    ImGui::InvisibleButton("time-track",{width,trackHeight});
    if(ImGui::IsItemActive()){const double t=std::clamp(static_cast<double>((ImGui::GetIO().MousePos.x-p.x)/width)*duration,0.0,duration);SetTime(std::min(std::round(t*settings_.timelineFps)/settings_.timelineFps,duration));}
    if(workspace_!=2&&duration>0&&ImGui::IsItemHovered()&&ImGui::IsMouseClicked(ImGuiMouseButton_Left)){
        float nearest=UiScale(9);double selected=-1;
        for(const auto& track:Clip().tracks)if(track.bone==static_cast<uint32_t>(selectedBone_)){
            auto find=[&](const auto& keys){for(const auto& k:keys){float x=p.x+width*static_cast<float>(k.time/Clip().duration);float distance=std::hypot(x-ImGui::GetIO().MousePos.x,p.y+trackHeight-UiScale(8)-ImGui::GetIO().MousePos.y);if(distance<nearest){nearest=distance;selected=k.time;}}};find(track.positions);find(track.rotations);find(track.scales);
        }
        if(selected>=0){selectedKeyTime_=selected;keyTimeDraft_=selected/Clip().speed;SetTime(keyTimeDraft_);if(ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))ImGui::OpenPopup("Keyframe###KeyTime");}
    }
    auto* draw=ImGui::GetWindowDrawList();draw->AddRectFilled(p,{p.x+width,p.y+trackHeight},IM_COL32(20,25,36,255),UiScale(3));
    const int divisions=std::clamp(static_cast<int>(width/UiScale(80)),2,8);
    for(int i=0;i<=divisions;++i){const float x=p.x+width*i/divisions;draw->AddLine({x,p.y+ImGui::GetFontSize()+UiScale(4)},{x,p.y+trackHeight},IM_COL32(50,56,73,255));char text[24];std::snprintf(text,sizeof(text),"%d",static_cast<int>(std::round(duration*i/divisions*settings_.timelineFps)));draw->AddText({std::min(x+UiScale(3),p.x+width-ImGui::CalcTextSize(text).x-UiScale(2)),p.y+UiScale(2)},IM_COL32(126,139,162,255),text);}
    if(duration>0)for(const auto& track:Clip().tracks)if(track.bone==static_cast<uint32_t>(selectedBone_)){
        auto diamonds=[&](const auto& keys){for(const auto& key:keys){
            const bool selected=selectedKeyTime_>=0&&std::abs(key.time-selectedKeyTime_)<1e-6;
            const float x=p.x+width*static_cast<float>(key.time/Clip().duration),y=p.y+trackHeight-UiScale(8),r=UiScale(selected?6.0f:4.0f);
            draw->AddQuadFilled({x,y-r},{x+r,y},{x,y+r},{x-r,y},selected?IM_COL32(255,208,124,255):IM_COL32(177,139,235,255));
        }};
        diamonds(track.positions);diamonds(track.rotations);diamonds(track.scales);
    }
    const float head=p.x+width*static_cast<float>(duration>0?time_/duration:0);draw->AddLine({head,p.y},{head,p.y+trackHeight},IM_COL32(132,224,198,255),UiScale(2));draw->AddTriangleFilled({head-UiScale(5),p.y},{head+UiScale(5),p.y},{head,p.y+UiScale(7)},IM_COL32(132,224,198,255));
    if(workspace_==1){float t=static_cast<float>(time_);ImGui::SetNextItemWidth(-1);if(ImGui::SliderFloat("##seconds",&t,0,static_cast<float>(duration),"%.3f s"))SetTime(t);}
    if(selectedKeyTime_>=0&&workspace_!=0){if(ImGui::SmallButton(T("선택 키 편집...","Edit selected key...")))ImGui::OpenPopup("Keyframe###KeyTime");}
    if(ImGui::BeginPopupModal(T("키프레임###KeyTime","Keyframe###KeyTime"),nullptr,ImGuiWindowFlags_AlwaysAutoResize)){
        ImGui::InputDouble(T("시간 (초)","Time (seconds)"),&keyTimeDraft_,1.0/settings_.timelineFps,.1,"%.4f");
        if(ImGui::Button(T("시간 변경","Move key"))){Dispatch("move_key",{{"bonePath",model_.skeleton.bones[selectedBone_].path},{"from",selectedKeyTime_},{"to",keyTimeDraft_*Clip().speed}});selectedKeyTime_=keyTimeDraft_*Clip().speed;SetTime(keyTimeDraft_);ImGui::CloseCurrentPopup();}
        ImGui::SameLine();if(ImGui::Button(T("키 삭제","Delete key"))){Dispatch("delete_key",{{"bonePath",model_.skeleton.bones[selectedBone_].path},{"time",selectedKeyTime_}});selectedKeyTime_=-1;ImGui::CloseCurrentPopup();}
        ImGui::SameLine();if(ImGui::Button(T("닫기","Close")))ImGui::CloseCurrentPopup();ImGui::EndPopup();
    }
    ImGui::EndChild();
}
}

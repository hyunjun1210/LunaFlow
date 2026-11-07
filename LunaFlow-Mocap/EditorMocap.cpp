#include "App/EditorApp.h"
#include "App/UiFonts.h"
#include "App/UiTheme.h"
#include "imgui.h"
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

namespace LunaFlow {
namespace {
using Mocap::Framing;
constexpr int Segments[][2]={{11,12},{11,13},{13,15},{12,14},{14,16},{11,23},{12,24},{23,24},{23,25},{25,27},{24,26},{26,28}};
struct GuidePoint { int index; float u,v; };
// Ghost reference pose (unmirrored image coordinates) shown until calibration succeeds.
constexpr GuidePoint Guide[]={{0,.5f,.17f},{11,.57f,.3f},{12,.43f,.3f},{13,.58f,.45f},{14,.42f,.45f},{15,.585f,.59f},
    {16,.415f,.59f},{23,.545f,.58f},{24,.455f,.58f},{25,.545f,.74f},{26,.455f,.74f},{27,.545f,.9f},{28,.455f,.9f}};
constexpr ImU32 Red=IM_COL32(235,72,80,255),Teal=IM_COL32(104,215,187,255),Orange=IM_COL32(255,168,82,255),
    Violet=IM_COL32(183,148,255,255),Muted=IM_COL32(128,138,160,255),Accent=IM_COL32(118,86,214,255),
    Dark=IM_COL32(40,44,58,255);

ImU32 Fade(ImU32 color,float alpha) {
    const float a=static_cast<float>((color&IM_COL32_A_MASK)>>IM_COL32_A_SHIFT)*std::clamp(alpha,0.f,1.f);
    return (color&~IM_COL32_A_MASK)|(static_cast<ImU32>(a+.5f)<<IM_COL32_A_SHIFT);
}
ImU32 Shade(ImU32 color,float factor) {
    auto channel=[&](int shift){return static_cast<ImU32>(std::clamp(static_cast<float>((color>>shift)&0xFF)*factor,0.f,255.f));};
    return (color&IM_COL32_A_MASK)|(channel(IM_COL32_R_SHIFT)<<IM_COL32_R_SHIFT)|
        (channel(IM_COL32_G_SHIFT)<<IM_COL32_G_SHIFT)|(channel(IM_COL32_B_SHIFT)<<IM_COL32_B_SHIFT);
}
float Pulse(float speed=4.f) {return .55f+.45f*std::sin(static_cast<float>(ImGui::GetTime())*speed);}
ImFont* Bold() {return UiFonts::semibold?UiFonts::semibold:ImGui::GetFont();}
std::string Duration(double seconds) {
    const long long tenths=static_cast<long long>(std::max(0.,seconds)*10+1e-6);
    char text[32];snprintf(text,sizeof(text),"%02lld:%02lld.%lld",tenths/600,tenths/10%60,tenths%10);
    return text;
}
bool Seen(const Mocap::Landmark& l) {
    return l.visibility>=.6f&&l.presence>=.6f&&l.u>=.01f&&l.u<=.99f&&l.v>=.01f&&l.v<=.99f;
}
void Spinner(ImDrawList* draw,ImVec2 center,float radius,ImU32 color) {
    const float start=static_cast<float>(ImGui::GetTime())*6.f;
    draw->PathArcTo(center,radius,start,start+4.2f,24);
    draw->PathStroke(color,std::max(1.5f,radius*.28f));
}
void CheckMark(ImDrawList* draw,ImVec2 center,float size,ImU32 color) {
    const ImVec2 points[]={{center.x-size*.45f,center.y},{center.x-size*.12f,center.y+size*.32f},{center.x+size*.48f,center.y-size*.36f}};
    draw->AddPolyline(points,3,color,std::max(1.5f,size*.22f));
}
enum class Step { Done, Active, Warn, Pending };
void StepRow(Step step,const char* label,const char* detail=nullptr) {
    auto* draw=ImGui::GetWindowDrawList();
    const float size=ImGui::GetTextLineHeight();
    const ImVec2 cursor=ImGui::GetCursorScreenPos(),center{cursor.x+size*.5f,cursor.y+size*.5f};
    const float radius=size*.42f;
    switch(step) {
    case Step::Done:draw->AddCircleFilled(center,radius,Fade(Teal,.22f));CheckMark(draw,center,radius*1.15f,Teal);break;
    case Step::Active:Spinner(draw,center,radius*.85f,Violet);break;
    case Step::Warn:
        draw->AddCircleFilled(center,radius,Fade(Orange,.25f));
        draw->AddLine({center.x,center.y-radius*.55f},{center.x,center.y+radius*.12f},Orange,2.f);
        draw->AddCircleFilled({center.x,center.y+radius*.48f},1.4f,Orange);break;
    case Step::Pending:draw->AddCircle(center,radius*.8f,Fade(Muted,.8f),0,1.5f);break;
    }
    ImGui::Dummy({size,size});ImGui::SameLine(0,ImGui::GetStyle().ItemInnerSpacing.x+2);
    ImGui::BeginGroup();
    if(step==Step::Pending)ImGui::PushStyleColor(ImGuiCol_Text,ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("%s",label);
    if(step==Step::Pending)ImGui::PopStyleColor();
    if(detail&&*detail) {
        ImGui::PushStyleColor(ImGuiCol_Text,step==Step::Warn?ImGui::ColorConvertU32ToFloat4(Orange):ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextWrapped("%s",detail);ImGui::PopStyleColor();
    }
    ImGui::EndGroup();
}
enum class Glyph { None, Record, Stop, Camera };
void DrawGlyph(ImDrawList* draw,Glyph glyph,ImVec2 center,float size,ImU32 color) {
    switch(glyph) {
    case Glyph::Record:draw->AddCircleFilled(center,size*.5f,IM_COL32(255,92,100,255));break;
    case Glyph::Stop:draw->AddRectFilled({center.x-size*.42f,center.y-size*.42f},{center.x+size*.42f,center.y+size*.42f},color,2.f);break;
    case Glyph::Camera:
        draw->AddRectFilled({center.x-size*.6f,center.y-size*.36f},{center.x+size*.22f,center.y+size*.36f},color,2.f);
        draw->AddTriangleFilled({center.x+size*.28f,center.y},{center.x+size*.62f,center.y-size*.32f},{center.x+size*.62f,center.y+size*.32f},color);break;
    case Glyph::None:break;
    }
}
// Full-width primary action with a glyph, bold label and an optional key-cap hint (Unity record-button feel).
bool BigButton(const char* id,const char* label,Glyph glyph,ImU32 color,float height,const char* tooltip=nullptr,
    const char* key=nullptr,bool enabled=true) {
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding,UiScale(8));
    ImGui::PushStyleColor(ImGuiCol_Button,color);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,Shade(color,1.15f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,Shade(color,.88f));
    ImGui::BeginDisabled(!enabled);
    const ImVec2 position=ImGui::GetCursorScreenPos();
    const float width=ImGui::GetContentRegionAvail().x;
    const bool pressed=ImGui::Button(id,{width,height});
    if(tooltip&&ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled|ImGuiHoveredFlags_DelayShort))ImGui::SetTooltip("%s",tooltip);
    auto* draw=ImGui::GetWindowDrawList();
    const ImU32 text=ImGui::GetColorU32(IM_COL32(255,255,255,255));
    ImFont* font=Bold();const float fontSize=ImGui::GetFontSize()*1.12f;
    const ImVec2 labelSize=font->CalcTextSizeA(fontSize,FLT_MAX,0,label);
    const float glyphSize=glyph==Glyph::None?0:fontSize*.9f,gap=glyph==Glyph::None?0:fontSize*.55f;
    const float contentWidth=glyphSize+gap+labelSize.x;
    float x=position.x+(width-contentWidth)*.5f;
    const float centerY=position.y+height*.5f;
    if(glyph!=Glyph::None)DrawGlyph(draw,glyph,{x+glyphSize*.5f,centerY},glyphSize,text);
    x+=glyphSize+gap;
    draw->AddText(font,fontSize,{x,centerY-labelSize.y*.5f},text,label);
    if(key&&*key) {
        const ImVec2 keySize=ImGui::CalcTextSize(key);
        const float pad=UiScale(6),right=position.x+width-UiScale(10);
        const ImVec2 min{right-keySize.x-pad*2,centerY-keySize.y*.5f-UiScale(2)},max{right,centerY+keySize.y*.5f+UiScale(2)};
        if(min.x>x+labelSize.x+UiScale(8)) {
            draw->AddRect(min,max,ImGui::GetColorU32(IM_COL32(255,255,255,120)),UiScale(4));
            draw->AddText({min.x+pad,centerY-keySize.y*.5f},ImGui::GetColorU32(IM_COL32(255,255,255,200)),key);
        }
    }
    ImGui::EndDisabled();
    ImGui::PopStyleColor(3);ImGui::PopStyleVar();
    return pressed&&enabled;
}
bool BeginCard(const char* id,ImU32 accent) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg,ImVec4(.095f,.105f,.145f,1));
    ImGui::PushStyleColor(ImGuiCol_Border,Fade(accent,.55f));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding,UiScale(8));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,{UiScale(12),UiScale(10)});
    return ImGui::BeginChild(id,{0,0},ImGuiChildFlags_Borders|ImGuiChildFlags_AutoResizeY);
}
void EndCard() {
    ImGui::EndChild();
    ImGui::PopStyleVar(2);ImGui::PopStyleColor(2);
}
void Notice(const char* id,const char* title,const std::string& body,const char* hint=nullptr) {
    BeginCard(id,Red);
    ImGui::PushFont(Bold(),0);ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(IM_COL32(255,128,134,255)),"%s",title);ImGui::PopFont();
    if(!body.empty())ImGui::TextWrapped("%s",body.c_str());
    if(hint&&*hint){ImGui::PushStyleColor(ImGuiCol_Text,ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));ImGui::TextWrapped("%s",hint);ImGui::PopStyleColor();}
    EndCard();
}
// Setup.ps1 prints "Step n/4: ..." so the panel can show a checklist instead of a raw console.
int SetupStep(const std::vector<std::string>& log) {
    for(auto line=log.rbegin();line!=log.rend();++line) {
        const char* found=std::strstr(line->c_str(),"Step ");
        int step=0,total=0;
        if(found&&sscanf_s(found,"Step %d/%d",&step,&total)==2)return std::clamp(step,1,4);
    }
    return 0;
}
void CenteredText(ImDrawList* draw,ImVec2 center,ImU32 color,const char* text) {
    const ImVec2 size=ImGui::CalcTextSize(text);
    draw->AddText({center.x-size.x*.5f,center.y-size.y*.5f},color,text);
}
// Mirrored camera-space stick figure: what the tracker sees, colored by per-joint confidence.
void FramingPreview(const Mocap::EditorState& m,float width,const char* waiting,const char* noPerson) {
    const float height=width*.75f;
    const ImVec2 min=ImGui::GetCursorScreenPos(),max{min.x+width,min.y+height};
    ImGui::Dummy({width,height});
    auto* draw=ImGui::GetWindowDrawList();
    draw->AddRectFilled(min,max,IM_COL32(14,16,24,255),UiScale(6));
    draw->PushClipRect(min,max,true);
    for(int i=1;i<3;++i) {
        const float x=min.x+width*static_cast<float>(i)/3.f,y=min.y+height*static_cast<float>(i)/3.f;
        draw->AddLine({x,min.y},{x,max.y},IM_COL32(255,255,255,14));
        draw->AddLine({min.x,y},{max.x,y},IM_COL32(255,255,255,14));
    }
    auto point=[&](float u,float v){return ImVec2{min.x+(1-u)*width,min.y+v*height};};
    if(!m.capture.Calibrated()) {
        const ImU32 ghost=IM_COL32(255,255,255,40);
        auto guide=[&](int index) {
            for(const auto& g:Guide)if(g.index==index)return point(g.u,g.v);
            return point(.5f,.5f);
        };
        for(const auto& s:Segments)draw->AddLine(guide(s[0]),guide(s[1]),ghost,UiScale(5));
        draw->AddCircle(guide(0),std::abs(guide(11).x-guide(12).x)*.32f,ghost,0,UiScale(3));
    }
    const ImVec2 center{(min.x+max.x)*.5f,(min.y+max.y)*.5f};
    if(!m.lastFrame) {
        Spinner(draw,{center.x,center.y-UiScale(14)},UiScale(11),Violet);
        CenteredText(draw,{center.x,center.y+UiScale(14)},ImGui::GetColorU32(ImGuiCol_TextDisabled),waiting);
    }else if(!m.lastFrame->detected) {
        CenteredText(draw,{center.x,min.y+height*.08f+UiScale(8)},Orange,noPerson);
    }else {
        const auto& l=m.lastFrame->landmarks;
        for(const auto& s:Segments) {
            const auto& a=l[static_cast<size_t>(s[0])];const auto& b=l[static_cast<size_t>(s[1])];
            draw->AddLine(point(a.u,a.v),point(b.u,b.v),Seen(a)&&Seen(b)?Teal:Orange,UiScale(3));
        }
        for(const auto& s:Segments)for(int end=0;end<2;++end) {
            const auto& p=l[static_cast<size_t>(s[end])];
            draw->AddCircleFilled(point(p.u,p.v),UiScale(2.5f),Seen(p)?Teal:Orange);
        }
        const ImVec2 left=point(l[11].u,l[11].v),right=point(l[12].u,l[12].v);
        const float shoulders=std::hypot(left.x-right.x,left.y-right.y);
        draw->AddCircle(point(l[0].u,l[0].v),std::max(UiScale(6),shoulders*.32f),Seen(l[0])?Teal:Orange,0,UiScale(2));
    }
    draw->PopClipRect();
    if(m.capture.Recording())draw->AddRect(min,max,Fade(Red,.6f+.4f*Pulse(5)),UiScale(6),UiScale(2.5f));
    else draw->AddRect(min,max,IM_COL32(255,255,255,30),UiScale(6));
}
void Chip(const char* text,ImU32 color,bool pulse=false) {
    auto* draw=ImGui::GetWindowDrawList();
    const ImVec2 size=ImGui::CalcTextSize(text),position=ImGui::GetCursorScreenPos();
    const float padX=UiScale(9),padY=UiScale(3),dot=UiScale(3.5f),gap=UiScale(6);
    const ImVec2 max{position.x+padX*2+dot*2+gap+size.x,position.y+size.y+padY*2};
    const float rounding=(max.y-position.y)*.5f;
    draw->AddRectFilled(position,max,Fade(color,.16f),rounding);
    draw->AddRect(position,max,Fade(color,.5f),rounding);
    draw->AddCircleFilled({position.x+padX+dot,position.y+padY+size.y*.5f},dot,Fade(color,pulse?Pulse(6):1.f));
    draw->AddText({position.x+padX+dot*2+gap,position.y+padY},color,text);
    ImGui::Dummy({max.x-position.x,max.y-position.y});
}
float PillWidth(const char* text) {return ImGui::CalcTextSize(text).x+UiScale(36);}
// Viewport status pill: dark capsule with a colored state dot (Unity/Blender viewport header style).
void Pill(ImDrawList* draw,ImVec2 topCenter,const char* text,ImU32 dot,float dotAlpha=1) {
    const ImVec2 size=ImGui::CalcTextSize(text);
    const float width=PillWidth(text),height=size.y+UiScale(12);
    const ImVec2 min{topCenter.x-width*.5f,topCenter.y},max{topCenter.x+width*.5f,topCenter.y+height};
    draw->AddRectFilled(min,max,IM_COL32(12,14,22,215),height*.5f);
    draw->AddRect(min,max,Fade(dot,.55f),height*.5f);
    draw->AddCircleFilled({min.x+UiScale(14),min.y+height*.5f},UiScale(4),Fade(dot,dotAlpha));
    draw->AddText({min.x+UiScale(24),min.y+UiScale(6)},IM_COL32(236,240,248,255),text);
}
const char* FramingHint(Framing framing,bool ko) {
    switch(framing) {
    case Framing::NoPerson:return ko?"카메라 앞에 서 주세요":"Stand in front of the camera";
    case Framing::StepBack:return ko?"뒤로 물러나 머리부터 발끝까지 보이게 해 주세요":"Step back so you are visible head to toe";
    case Framing::MoveLeft:return ko?"왼쪽으로 조금 이동해 주세요":"Move a little to your left";
    case Framing::MoveRight:return ko?"오른쪽으로 조금 이동해 주세요":"Move a little to your right";
    case Framing::LowConfidence:return ko?"가려진 부위가 없게, 조명은 밝게 해 주세요":"Keep limbs unblocked and the room bright";
    case Framing::Good:break;
    }
    return ko?"그대로 유지하세요":"Hold that position";
}
void InitFolder(Mocap::EditorState& m) {
    if(!m.folder[0])strncpy_s(m.folder,sizeof(m.folder),std::filesystem::path(LUNAFLOW_MOCAP_DIR).u8string().c_str(),_TRUNCATE);
}
std::filesystem::path Root(const Mocap::EditorState& m) {return std::filesystem::u8path(m.folder);}
}

bool EditorApp::MocapReady() {
    InitFolder(mocap_);
    const auto root=Root(mocap_);std::error_code ec;
    return std::filesystem::is_regular_file(root/L".venv"/L"Scripts"/L"python.exe",ec)&&
        std::filesystem::is_regular_file(root/L"Tracker"/L"tracker.py",ec)&&
        std::filesystem::is_regular_file(root/L"Models"/L"pose_landmarker_lite.task",ec);
}
void EditorApp::OpenMocap() {
    mocap_.show=true;InitFolder(mocap_);
    if(mocap_.active||mocap_.setup.Busy()||!MocapReady())return;
    Try([&]{StartMocap();});
    if(error_)mocap_.processStatus=status_;
}
void EditorApp::StartMocap() {
    auto& m=mocap_;
    if(m.active)return;
    InitFolder(m);
    const auto root=Root(m);
    const float smoothing=m.capture.smoothingSeconds;
    m.capture.Configure(model_.skeleton);m.capture.smoothingSeconds=smoothing;m.capture.BeginCalibration();
    CancelCopilot();
    m.tracker.Start(root/L".venv"/L"Scripts"/L"python.exe",root/L"Tracker"/L"tracker.py",
        root/L"Models"/L"pose_landmarker_lite.task",m.camera);
    m.active=true;m.stopping=false;m.show=true;m.cameraReady=false;m.countdownEnd=-1;m.review=-1;m.scrubbing=false;
    m.lastFrame.reset();m.userStopped=false;m.processStatus.clear();m.takes=0;
    playing_=bindPose_=draggingBone_=gizmoWasUsing_=false;
    SelectWorkspace(1);FrameAll();pose_=m.capture.Preview();
    status_=T("웹캠을 켜는 중... 처음 실행은 최대 1분 걸릴 수 있어요","Starting the webcam... the first launch can take up to a minute");error_=false;
}
void EditorApp::StopMocap() {
    auto& m=mocap_;
    if(!m.active)return;
    m.countdownEnd=-1;
    if(m.capture.Recording()){m.userStopped=true;m.capture.StopRecording();}
    m.tracker.RequestStop();m.stopping=true;
}
void EditorApp::CloseMocap() {
    mocap_.show=false;
    if(mocap_.review>=0)FinishMocapReview();
    StopMocap();mocap_.startAfterSetup=false;
}
void EditorApp::ArmMocapRecording() {
    auto& m=mocap_;
    if(!m.active||m.stopping||!m.cameraReady||m.capture.Recording()||m.countdownEnd>=0)return;
    if(m.review>=0)FinishMocapReview();
    m.countdownEnd=Mocap::Clock()+m.countdown;
}
void EditorApp::UpdateMocapRecording(double now) {
    auto& m=mocap_;auto& c=m.capture;
    if(m.countdownEnd>=0&&!c.Recording()&&!m.stopping) {
        // The countdown only runs while the calibrated performer is in frame, so stepping back into place is free.
        if(!(c.Calibrated()&&c.Tracking()))m.countdownEnd=std::max(m.countdownEnd,now+m.countdown);
        else if(now>=m.countdownEnd) {
            const std::string base=m.takeName[0]?std::string(m.takeName):std::string(T("웹캠 녹화","Webcam take"));
            std::string name;
            for(int n=1;;++n) {
                name=base+" "+std::to_string(n);
                if(std::none_of(model_.clips.begin(),model_.clips.end(),[&](const AnimationClip& clip){return clip.name==name;}))break;
            }
            try {
                c.StartRecording(name,now);
                ++m.takes;m.countdownEnd=-1;m.userStopped=false;
                status_=T("녹화 중... Space로 정지","Recording... press Space to stop");error_=false;
            }catch(const std::exception&) {}
        }
    }
    if(c.Recording()&&m.autoStop>0&&c.RecordedSeconds()>=m.autoStop){m.userStopped=true;c.StopRecording();}
}
void EditorApp::PumpMocap(float deltaTime) {
    auto& m=mocap_;
    if(closeRequested_&&m.setup.Busy()){m.setup.Cancel();m.startAfterSetup=false;}
    if(m.startAfterSetup&&m.setup.Started()&&!m.setup.Busy()) {
        m.startAfterSetup=false;
        if(m.setup.ExitCode()==0&&MocapReady()&&!closeRequested_&&!m.active) {
            Try([&]{StartMocap();});
            if(error_)m.processStatus=status_;
        }
    }
    if(!m.active)return;
    if(closeRequested_)StopMocap();
    for(auto& event:m.tracker.Poll()) {
        if(event.frame) {
            if(m.stopping)continue;
            m.cameraReady=true;m.lastFrame=*event.frame;
            try{m.capture.Consume(*event.frame,event.received);}
            catch(const std::exception& e){m.processStatus=e.what();error_=true;StopMocap();}
        }else if(event.state=="ready")m.cameraReady=true;
        else if(event.state=="error") {
            m.processStatus=event.message;error_=true;
            status_=std::string(T("웹캠 오류: ","Webcam error: "))+event.message;StopMocap();
        }
    }
    const double now=Mocap::Clock();
    m.capture.Tick(now);
    if(!m.stopping)UpdateMocapRecording(now);
    bool announced=false;
    if(auto take=m.capture.TakeCompleted()){AddMocapTake(std::move(*take),!m.stopping);announced=true;}
    if(m.review>=0) {
        if(m.review<static_cast<int>(model_.clips.size())) {
            // Review loops the trimmed take in the viewport while the camera stays live for a retake.
            selectedClip_=m.review;bindPose_=false;playing_=false;
            if(!m.scrubbing)time_+=std::min(static_cast<double>(deltaTime),.25);
            if(time_<m.trimIn||time_>m.trimOut)time_=m.trimIn;
        }else m.review=-1;
    }
    if(!m.tracker.Busy()) {
        m.capture.StopRecording();
        if(auto take=m.capture.TakeCompleted()){AddMocapTake(std::move(*take),false);announced=true;}
        for(auto& event:m.tracker.Poll())if(event.state=="error"&&m.processStatus.empty())m.processStatus=event.message;
        if(m.review>=0){FinishMocapReview();announced=true;}
        m.active=m.stopping=m.cameraReady=m.scrubbing=false;m.countdownEnd=-1;m.lastFrame.reset();
        if(!m.processStatus.empty()){status_=std::string(T("웹캠이 꺼졌습니다: ","Webcam stopped: "))+m.processStatus;error_=true;}
        else if(!announced){status_=T("웹캠을 껐습니다.","Webcam turned off.");error_=false;}
        pose_=EvaluatePose(model_.skeleton,&Clip(),time_);
    }
}
void EditorApp::AddMocapTake(AnimationClip clip,bool review) {
    auto& m=mocap_;
    if(m.review>=0)FinishMocapReview();
    m.endReason=clip.duration>=Mocap::MaxSeconds-1e-6?2:m.userStopped?0:1;m.userStopped=false;
    if(clip.duration<1./30) {
        status_=T("녹화가 너무 짧아 버렸습니다. 조금 더 길게 녹화해 주세요.","The take was too short and was discarded. Record a little longer.");
        error_=true;m.takes=std::max(0,m.takes-1);return;
    }
    m.rawKeys=Mocap::KeyCount(clip);
    m.keptKeys=m.simplify?Mocap::SimplifyTake(clip,m.simplifyDegrees):m.rawKeys;
    // One undo step per take; Discard pops it again so a rejected take leaves no history.
    UndoState state;state.clip=selectedClip_;state.scene=std::make_shared<ModelAsset>(model_);
    m.reviewUndo=state.scene.get();m.reviewPreviousClip=selectedClip_;m.reviewWasDirty=dirty_;
    PushUndo(std::move(state));
    model_.clips.push_back(std::move(clip));selectedClip_=static_cast<int>(model_.clips.size())-1;
    selectedKeyTime_=-1;editBone_=-1;editClip_=-1;nameClip_=-1;
    dirty_=true;time_=0;loop_=Clip().loop;error_=false;
    char seconds[32];snprintf(seconds,sizeof(seconds),"%.1f",Clip().duration);
    if(review) {
        m.review=selectedClip_;m.trimIn=0;m.trimOut=static_cast<float>(Clip().duration);
        status_="'"+Clip().name+"' "+T("녹화됨 (","recorded (")+seconds+
            T("초) · Enter 완료 · Space 다시 녹화 · Del 버리기","s) · Enter keep · Space record again · Del discard");
    }else status_="'"+Clip().name+"' "+T("새 동작으로 추가했습니다 (","added as a new motion (")+seconds+T("초)","s)");
}
void EditorApp::FinishMocapReview() {
    auto& m=mocap_;
    const int index=m.review;m.review=-1;m.scrubbing=false;
    if(index<0||index>=static_cast<int>(model_.clips.size()))return;
    auto& clip=model_.clips[static_cast<size_t>(index)];
    const double duration=clip.duration;
    const double in=std::clamp(static_cast<double>(m.trimIn),0.,duration);
    const double out=std::clamp(static_cast<double>(m.trimOut),in,duration);
    bool trimmed=true;
    if(out-in>=1./30&&(in>1e-4||out<duration-1e-4)) {
        Try([&]{Mocap::TrimTake(clip,in,out);});
        trimmed=!error_;m.keptKeys=Mocap::KeyCount(clip);
    }
    selectedClip_=index;time_=0;
    if(!trimmed)return;
    char seconds[32];snprintf(seconds,sizeof(seconds),"%.1f",clip.duration);
    const bool keepRecording=m.active&&!m.stopping&&m.show;
    status_="'"+clip.name+"' "+T("적용 완료 (","applied (")+seconds+T("초)","s)")+
        (keepRecording?std::string():std::string(T(" · Ctrl+S로 저장"," · Ctrl+S to save")));
    error_=false;
}
void EditorApp::DiscardMocapTake() {
    auto& m=mocap_;
    const int index=m.review;
    if(index<0||index>=static_cast<int>(model_.clips.size())||model_.clips.size()<2)return;
    m.review=-1;m.scrubbing=false;
    model_.clips.erase(model_.clips.begin()+index);
    if(!undo_.empty()&&undo_.back().scene.get()==m.reviewUndo){undo_.pop_back();dirty_=m.reviewWasDirty;}
    m.reviewUndo=nullptr;
    selectedClip_=std::clamp(m.reviewPreviousClip,0,static_cast<int>(model_.clips.size())-1);
    selectedKeyTime_=-1;editBone_=-1;editClip_=-1;nameClip_=-1;
    loop_=Clip().loop;time_=0;m.takes=std::max(0,m.takes-1);
    status_=T("테이크를 버렸습니다. Space로 다시 녹화하세요.","Take discarded. Press Space to record again.");error_=false;
}
void EditorApp::MocapShortcuts() {
    auto& m=mocap_;const auto& io=ImGui::GetIO();
    if(io.WantTextInput||io.AppFocusLost||io.KeyCtrl||io.KeyAlt||m.stopping||
        ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopupId|ImGuiPopupFlags_AnyPopupLevel))return;
    auto cancel=[&]{m.countdownEnd=-1;status_=T("녹화 대기를 취소했습니다.","Recording countdown cancelled.");error_=false;};
    if(!keyboardNav_&&ImGui::IsKeyPressed(ImGuiKey_Space,false)) {
        if(m.capture.Recording()){m.userStopped=true;m.capture.StopRecording();}
        else if(m.countdownEnd>=0)cancel();
        else ArmMocapRecording();
    }
    if(m.countdownEnd>=0&&ImGui::IsKeyPressed(ImGuiKey_Escape,false))cancel();
    if(m.review>=0) {
        if(!keyboardNav_&&(ImGui::IsKeyPressed(ImGuiKey_Enter,false)||ImGui::IsKeyPressed(ImGuiKey_KeypadEnter,false)))CloseMocap();
        else if(ImGui::IsKeyPressed(ImGuiKey_Delete,false))DiscardMocapTake();
    }
}
void EditorApp::MocapPanel() {
    auto& m=mocap_;
    if(!m.show)return;
    InitFolder(m);
    auto& c=m.capture;
    const bool ko=settings_.korean;
    const auto& style=ImGui::GetStyle();
    const auto* viewport=ImGui::GetMainViewport();
    ImGui::SetNextWindowPos({viewport->WorkPos.x+viewport->WorkSize.x-UiScale(18),viewport->WorkPos.y+UiScale(70)},ImGuiCond_FirstUseEver,{1,0});
    ImGui::SetNextWindowSize({std::min(UiScale(480),viewport->WorkSize.x*.9f),std::min(UiScale(660),viewport->WorkSize.y*.85f)},ImGuiCond_FirstUseEver);
    bool open=true;
    if(!ImGui::Begin(T("웹캠 모션캡처###Mocap","Webcam motion capture###Mocap"),&open,ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();if(!open)CloseMocap();return;
    }
    const bool setupBusy=m.setup.Busy(),recording=c.Recording(),armed=m.countdownEnd>=0;
    std::string chip;
    if(setupBusy)Chip(T("설치 중","Installing"),Violet,true);
    else if(!m.active)Chip(T("카메라 꺼짐","Camera off"),Muted);
    else if(m.stopping)Chip(T("끄는 중","Stopping"),Muted,true);
    else if(!m.cameraReady)Chip(T("카메라 켜는 중","Starting camera"),Violet,true);
    else if(recording){chip="REC  "+Duration(c.RecordedSeconds());Chip(chip.c_str(),Red,true);}
    else if(armed)Chip(T("녹화 대기","Armed"),Orange,true);
    else if(m.review>=0)Chip(T("테이크 확인","Reviewing take"),Violet);
    else if(!c.Tracking())Chip(T("전신이 안 보여요","Full body not visible"),Orange);
    else if(!c.Calibrated())Chip(T("기준 자세 필요","Reference pose needed"),Orange);
    else Chip(T("녹화 준비됨","Ready to record"),Teal);
    if(m.takes>0) {
        char takes[64];snprintf(takes,sizeof(takes),T("이번 세션 %d테이크","%d take(s) this session"),m.takes);
        ImGui::SameLine();
        ImGui::SetCursorPosX(ImGui::GetCursorPosX()+std::max(0.f,ImGui::GetContentRegionAvail().x-ImGui::CalcTextSize(takes).x));
        ImGui::SetCursorPosY(ImGui::GetCursorPosY()+UiScale(3));
        ImGui::TextDisabled("%s",takes);
    }
    ImGui::Spacing();
    if(!m.active) {
        if(setupBusy) {
            const auto log=m.setup.Log();
            const int current=std::max(SetupStep(log),1);
            ImGui::PushFont(Bold(),0);ImGui::TextUnformatted(T("모션캡처 준비 중 (처음 한 번만)","Preparing motion capture (first time only)"));ImGui::PopFont();
            ImGui::PushStyleColor(ImGuiCol_Text,ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            ImGui::TextWrapped("%s",T("몇 분 걸릴 수 있어요. 끝나면 카메라가 자동으로 켜집니다.","This can take a few minutes. The camera turns on automatically when it is done."));
            ImGui::PopStyleColor();ImGui::Spacing();
            const char* names[]={T("Python 3.11 환경 준비","Prepare the Python 3.11 environment"),T("MediaPipe·OpenCV 설치","Install MediaPipe and OpenCV"),
                T("자세 인식 모델 다운로드","Download the pose model"),T("모델 동작 확인","Test the model")};
            for(int i=0;i<4;++i)StepRow(i+1<current?Step::Done:i+1==current?Step::Active:Step::Pending,names[i]);
            ImGui::Spacing();
            ImGui::ProgressBar((static_cast<float>(current)-.5f)/4.f,{-FLT_MIN,0},"");
            if(!log.empty()) {
                ImGui::PushStyleColor(ImGuiCol_Text,ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                ImGui::TextWrapped("%s",log.back().c_str());ImGui::PopStyleColor();
            }
            if(ImGui::Button(T("설치 취소","Cancel setup"))){m.setup.Cancel();m.startAfterSetup=false;}
        }else if(!MocapReady()) {
            ImGui::PushFont(Bold(),0);ImGui::TextUnformatted(T("처음 한 번 설치가 필요해요","A one-time setup is needed"));ImGui::PopFont();
            ImGui::TextWrapped("%s",T("웹캠 동작 인식용 Python 3.11 환경과 MediaPipe 자세 모델을 모션캡처 폴더 안에만 설치합니다. 시스템 설정은 바꾸지 않아요.",
                "Installs a private Python 3.11 environment and the MediaPipe pose model inside the mocap folder only. System settings are untouched."));
            const int code=m.setup.ExitCode();
            if(m.setup.Started()&&code==2)ImGui::TextDisabled("%s",T("설치를 취소했습니다.","Setup was cancelled."));
            else if(m.setup.Started()&&code>0) {
                const auto log=m.setup.Log();std::string tail;
                for(size_t i=log.size()>6?log.size()-6:0;i<log.size();++i)tail+=log[i]+"\n";
                Notice("##setup-error",T("설치하지 못했어요","Setup failed"),tail,
                    T("Python 3.11 (64비트)이 설치되어 있는지, 인터넷에 연결되어 있는지 확인한 뒤 다시 시도하세요.",
                      "Check that Python 3.11 (64-bit) is installed and that you are online, then try again."));
            }
            ImGui::Spacing();
            const bool retry=m.setup.Started()&&code!=0;
            if(BigButton("##setup",retry?T("다시 시도","Try again"):T("설치하고 카메라 켜기","Install and turn on camera"),Glyph::Camera,Accent,UiScale(46),
                T("설치가 끝나면 카메라가 자동으로 켜집니다","The camera turns on automatically after setup"))) {
                Try([&]{m.setup.Start(Root(m)/L"Setup.ps1");});
                m.startAfterSetup=!error_;
            }
        }else {
            ImGui::TextWrapped("%s",T("웹캠으로 내 몸 동작을 녹화해 바로 캐릭터 애니메이션으로 만듭니다.","Record your own movement with a webcam and turn it straight into character animation."));
            ImGui::Spacing();
            const char* how[]={T("카메라를 켜고 머리부터 발끝까지 보이게 서기","Turn on the camera and stand so you are visible head to toe"),
                T("정면을 보고 팔을 내린 채 잠깐 멈추면 기준 자세가 자동으로 잡혀요","Face the camera with arms down and hold still; the reference pose is set automatically"),
                T("Space 또는 녹화 버튼으로 녹화 시작 / 정지","Press Space or Record to start and stop"),
                T("뷰포트에서 바로 돌려보고 완료 · 다시 녹화 · 버리기","Review it in the viewport, then keep, retake or discard")};
            for(int i=0;i<4;++i){ImGui::TextDisabled("%d.",i+1);ImGui::SameLine();ImGui::TextWrapped("%s",how[i]);}
            ImGui::Spacing();
            if(BigButton("##camera-on",T("카메라 켜기","Turn on camera"),Glyph::Camera,Accent,UiScale(46),
                T("웹캠을 켜고 기준 자세를 자동으로 잡습니다","Turns on the webcam and calibrates automatically")))OpenMocap();
            if(!m.processStatus.empty()) {
                ImGui::Spacing();
                Notice("##camera-error",T("카메라를 쓰지 못했어요","The camera could not be used"),m.processStatus,
                    T("다른 앱(Zoom, Teams 등)이 카메라를 쓰고 있지 않은지, Windows 설정 > 개인 정보 > 카메라에서 데스크톱 앱 접근이 켜져 있는지 확인하세요. 카메라가 여러 대면 고급 설정에서 번호를 바꿔 보세요.",
                      "Make sure no other app (Zoom, Teams...) is using the camera and that desktop apps may use it in Windows Settings > Privacy > Camera. With several cameras, try another index under Advanced."));
            }
        }
    }else {
        const float region=ImGui::GetContentRegionAvail().x;
        const bool side=region>=UiScale(380);
        const float previewWidth=side?std::floor(region*.5f):region;
        const Framing framing=m.lastFrame?Mocap::CheckFraming(*m.lastFrame):Framing::NoPerson;
        ImGui::BeginGroup();
        FramingPreview(m,previewWidth,T("카메라 신호 기다리는 중...","Waiting for the camera..."),T("사람이 보이지 않아요","No person detected"));
        ImGui::EndGroup();
        if(side)ImGui::SameLine(0,UiScale(12));
        else ImGui::Spacing();
        ImGui::BeginGroup();
        StepRow(m.cameraReady?Step::Done:Step::Active,T("카메라","Camera"),m.cameraReady?nullptr:T("켜는 중... 처음엔 최대 1분","Starting... up to a minute the first time"));
        if(!m.cameraReady)StepRow(Step::Pending,T("전신 보이기","Full body in view"));
        else StepRow(c.Tracking()?Step::Done:Step::Warn,T("전신 보이기","Full body in view"),c.Tracking()?nullptr:FramingHint(framing,ko));
        char calibration[128];
        snprintf(calibration,sizeof(calibration),T("정면을 보고 팔을 내린 채 멈추기 %d/15","Face forward, arms down, hold still %d/15"),c.CalibrationSamples());
        StepRow(c.Calibrated()?Step::Done:c.Tracking()?Step::Active:Step::Pending,T("기준 자세","Reference pose"),c.Calibrated()?nullptr:calibration);
        StepRow(recording?Step::Active:c.Calibrated()&&c.Tracking()?Step::Done:Step::Pending,
            recording?T("녹화 중","Recording"):T("녹화 준비","Ready to record"));
        ImGui::EndGroup();
        ImGui::Spacing();
        if(m.review>=0&&m.review<static_cast<int>(model_.clips.size())) {
            auto& clip=model_.clips[static_cast<size_t>(m.review)];
            BeginCard("##review",Violet);
            ImGui::PushFont(Bold(),0);ImGui::TextUnformatted(clip.name.c_str());ImGui::PopFont();
            ImGui::SameLine();ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(Violet),"%s",T("뷰포트에서 반복 재생 중","Looping in the viewport"));
            ImGui::TextDisabled(T("길이 %.1f초 · 키 %zu -> %zu","Length %.1fs · keys %zu -> %zu"),clip.duration,m.rawKeys,m.keptKeys);
            if(m.endReason!=0) {
                ImGui::PushStyleColor(ImGuiCol_Text,ImGui::ColorConvertU32ToFloat4(Orange));
                ImGui::TextWrapped("%s",m.endReason==2?T("최대 길이 120초에 도달해 자동으로 멈췄어요.","Stopped automatically at the 120-second limit."):
                    T("몸이 화면에서 벗어나 녹화가 자동으로 멈췄어요. 필요하면 다시 녹화하세요.","Recording stopped because you left the frame. Retake it if needed."));
                ImGui::PopStyleColor();
            }
            const float duration=static_cast<float>(clip.duration);
            if(duration>=.2f) {
                const float minLength=std::min(.1f,duration);
                const float half=(ImGui::GetContentRegionAvail().x-style.ItemSpacing.x)*.5f;
                ImGui::SetNextItemWidth(half);
                if(ImGui::SliderFloat("##trim-in",&m.trimIn,0,duration,T("시작 %.2f초","Start %.2fs")))m.trimIn=std::clamp(m.trimIn,0.f,m.trimOut-minLength);
                const bool inActive=ImGui::IsItemActive();
                if(ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))ImGui::SetTooltip("%s",T("앞부분의 준비 동작을 잘라냅니다","Trims the lead-in"));
                ImGui::SameLine();ImGui::SetNextItemWidth(half);
                if(ImGui::SliderFloat("##trim-out",&m.trimOut,0,duration,T("끝 %.2f초","End %.2fs")))m.trimOut=std::clamp(m.trimOut,m.trimIn+minLength,duration);
                const bool outActive=ImGui::IsItemActive();
                if(ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))ImGui::SetTooltip("%s",T("끝부분(정지 버튼 누르는 동작)을 잘라냅니다","Trims the tail (reaching for the stop key)"));
                if(inActive)time_=m.trimIn;
                if(outActive)time_=m.trimOut;
                m.scrubbing=inActive||outActive;
            }
            ImGui::Spacing();
            const char* again=T("계속 녹화 (Space)","Record again (Space)"),*discard=T("버리기 (Del)","Discard (Del)"),*keep=T("완료 (Enter)","Done (Enter)");
            auto buttonWidth=[&](const char* text){return ImGui::CalcTextSize(text).x+style.FramePadding.x*2+UiScale(8);};
            const float row=ImGui::GetContentRegionAvail().x;
            const bool stacked=buttonWidth(again)+buttonWidth(discard)+buttonWidth(keep)+style.ItemSpacing.x*2>row;
            const float each=stacked?row:(row-style.ItemSpacing.x*2)/3;
            const float height=UiScale(30);
            ImGui::PushStyleColor(ImGuiCol_Button,Accent);ImGui::PushStyleColor(ImGuiCol_ButtonHovered,Shade(Accent,1.15f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,Shade(Accent,.88f));
            const bool againPressed=ImGui::Button(again,{each,height});
            ImGui::PopStyleColor(3);
            if(ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))ImGui::SetTooltip("%s",T("이 테이크를 유지하고 다음 테이크를 녹화합니다","Keeps this take and records the next one"));
            if(!stacked)ImGui::SameLine();
            ImGui::BeginDisabled(model_.clips.size()<2);
            const bool discardPressed=ImGui::Button(discard,{each,height});
            ImGui::EndDisabled();
            if(ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort|ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("%s",T("이 테이크를 지우고 기록도 남기지 않습니다","Deletes this take without leaving an undo step"));
            if(!stacked)ImGui::SameLine();
            const bool keepPressed=ImGui::Button(keep,{each,height});
            if(ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))ImGui::SetTooltip("%s",T("이 테이크를 유지하고 카메라를 끕니다","Keeps this take and turns the camera off"));
            EndCard();
            if(againPressed)ArmMocapRecording();
            else if(discardPressed)DiscardMocapTake();
            else if(keepPressed)CloseMocap();
        }else {
            const float height=UiScale(48);
            if(recording) {
                const std::string label=std::string(T("정지  ","Stop  "))+Duration(c.RecordedSeconds());
                if(BigButton("##rec",label.c_str(),Glyph::Stop,Red,height,T("녹화를 멈추고 바로 확인합니다","Stops and reviews the take right away"),"Space"))
                    {m.userStopped=true;c.StopRecording();}
            }else if(armed) {
                char label[128];
                const double remaining=std::max(m.countdownEnd-Mocap::Clock(),0.);
                if(!(c.Calibrated()&&c.Tracking()))snprintf(label,sizeof(label),"%s",T("기준 자세 기다리는 중 · 취소","Waiting for the reference pose · Cancel"));
                else snprintf(label,sizeof(label),T("녹화 시작까지 %d · 취소","Recording in %d · Cancel"),static_cast<int>(std::ceil(remaining)));
                if(BigButton("##rec",label,Glyph::None,Shade(Orange,.72f),height,T("카운트다운을 취소합니다","Cancels the countdown"),"Esc"))m.countdownEnd=-1;
            }else if(BigButton("##rec",T("녹화","Record"),Glyph::Record,Dark,height,
                c.Calibrated()?T("카운트다운 후 녹화를 시작합니다","Starts recording after the countdown"):
                    T("기준 자세가 잡히면 카운트다운이 시작됩니다","The countdown starts once the reference pose is set"),"Space",m.cameraReady&&!m.stopping))
                ArmMocapRecording();
            ImGui::Spacing();
            ImGui::BeginDisabled(recording||armed);
            const float third=std::max((ImGui::GetContentRegionAvail().x-style.ItemSpacing.x*2)/3,UiScale(60));
            char preview[64];
            if(m.countdown>0)snprintf(preview,sizeof(preview),T("카운트다운 %d초","Countdown %ds"),m.countdown);
            else snprintf(preview,sizeof(preview),"%s",T("카운트다운 없음","No countdown"));
            ImGui::SetNextItemWidth(third);
            if(ImGui::BeginCombo("##countdown",preview)) {
                for(int seconds:{0,3,5,10}) {
                    char item[64];
                    if(seconds>0)snprintf(item,sizeof(item),T("카운트다운 %d초","Countdown %ds"),seconds);
                    else snprintf(item,sizeof(item),"%s",T("카운트다운 없음","No countdown"));
                    if(ImGui::Selectable(item,seconds==m.countdown))m.countdown=seconds;
                }
                ImGui::EndCombo();
            }
            if(ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))ImGui::SetTooltip("%s",T("녹화 버튼을 누른 뒤 자세를 잡을 시간","Time to get into position after pressing Record"));
            ImGui::SameLine();
            if(m.autoStop>0)snprintf(preview,sizeof(preview),T("%d초 후 자동 정지","Stop after %ds"),m.autoStop);
            else snprintf(preview,sizeof(preview),"%s",T("자동 정지 없음","No auto-stop"));
            ImGui::SetNextItemWidth(third);
            if(ImGui::BeginCombo("##auto-stop",preview)) {
                for(int seconds:{0,5,10,30,60}) {
                    char item[64];
                    if(seconds>0)snprintf(item,sizeof(item),T("%d초 후 자동 정지","Stop after %ds"),seconds);
                    else snprintf(item,sizeof(item),"%s",T("자동 정지 없음","No auto-stop"));
                    if(ImGui::Selectable(item,seconds==m.autoStop))m.autoStop=seconds;
                }
                ImGui::EndCombo();
            }
            if(ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))ImGui::SetTooltip("%s",T("정해진 길이만큼 찍고 알아서 멈춥니다 (혼자 녹화할 때 편해요)","Stops by itself after a fixed length (handy when recording alone)"));
            ImGui::SameLine();ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::InputTextWithHint("##take-name",T("이름 (자동: 웹캠 녹화 N)","Name (auto: Webcam take N)"),m.takeName,sizeof(m.takeName));
            ImGui::EndDisabled();
        }
        ImGui::Spacing();ImGui::Separator();ImGui::Spacing();
        ImGui::BeginDisabled(m.stopping||recording||armed||!m.cameraReady);
        if(ImGui::Button(T("기준 자세 다시 잡기","Recalibrate"))) {
            if(m.review>=0)FinishMocapReview();
            Try([&]{c.BeginCalibration();});
        }
        ImGui::EndDisabled();
        if(ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort|ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("%s",T("자리를 옮겼거나 캐릭터 자세가 어색하면 다시 잡으세요","Redo it after moving or if the character pose looks off"));
        ImGui::SameLine();
        const char* off=T("카메라 끄기","Turn off camera");
        ImGui::SetCursorPosX(ImGui::GetCursorPosX()+std::max(0.f,ImGui::GetContentRegionAvail().x-ImGui::CalcTextSize(off).x-style.FramePadding.x*2));
        ImGui::BeginDisabled(m.stopping);
        if(ImGui::Button(off))StopMocap();
        ImGui::EndDisabled();
    }
    //ADVANCED
    ImGui::Spacing();
    if(ImGui::CollapsingHeader(T("고급 설정###mocap-advanced","Advanced###mocap-advanced"))) {
        ImGui::BeginDisabled(m.active);
        ImGui::SetNextItemWidth(UiScale(120));
        if(ImGui::InputInt(T("카메라 번호","Camera index"),&m.camera))m.camera=std::clamp(m.camera,0,15);
        ImGui::EndDisabled();
        if(ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort|ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("%s",T("카메라가 여러 대면 0, 1, 2... 순서로 바꿔 보세요 (카메라를 끈 상태에서)","With several cameras try 0, 1, 2... (while the camera is off)"));
        ImGui::BeginDisabled(recording);
        ImGui::SetNextItemWidth(UiScale(180));
        ImGui::SliderFloat(T("떨림 보정","Smoothing"),&c.smoothingSeconds,0,.3f,T("%.2f초","%.2fs"));
        ImGui::EndDisabled();
        if(ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort|ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("%s",T("높을수록 부드럽지만 빠른 동작이 늦게 따라옵니다","Higher is smoother but lags on fast moves"));
        ImGui::Checkbox(T("키프레임 자동 정리","Simplify keys"),&m.simplify);
        if(ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            ImGui::SetTooltip("%s",T("30fps로 찍힌 키 중 보간으로 되살릴 수 있는 키를 지워 타임라인 편집을 쉽게 합니다","Removes 30 FPS keys that interpolation can rebuild, so the timeline is easier to edit"));
        if(m.simplify) {
            ImGui::SameLine();ImGui::SetNextItemWidth(UiScale(150));
            ImGui::SliderFloat("##simplify",&m.simplifyDegrees,.25f,5.f,T("허용 오차 %.2f°","Tolerance %.2f°"));
        }
        ImGui::TextDisabled("%s",T("모션캡처 폴더","Mocap folder"));
        ImGui::BeginDisabled(m.active||setupBusy);
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputText("##folder",m.folder,sizeof(m.folder));
        ImGui::EndDisabled();
        ImGui::PushStyleColor(ImGuiCol_Text,ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextWrapped("%s",T("어깨·팔꿈치·손목·엉덩이·무릎·발목 12개 관절로 골반, 가슴, 팔, 다리 뼈를 움직입니다. 손가락·얼굴·발끝은 녹화되지 않아요.",
            "Twelve joints (shoulders, elbows, wrists, hips, knees, ankles) drive the hips, chest, arm and leg bones. Fingers, face and toes are not captured."));
        ImGui::TextWrapped("%s",T("영상은 이 PC 안에서만 처리되며 저장하거나 전송하지 않습니다.","Video is processed on this PC only; it is never saved or sent anywhere."));
        ImGui::PopStyleColor();
    }
    ImGui::End();
    if(!open)CloseMocap();
}
void EditorApp::MocapOverlay(XMFLOAT2 origin,XMFLOAT2 size) {
    const auto& m=mocap_;
    if(!m.active)return;
    const auto& c=m.capture;
    auto* draw=ImGui::GetWindowDrawList();
    const ImVec2 min{origin.x,origin.y},max{origin.x+size.x,origin.y+size.y};
    draw->PushClipRect(min,max,true);
    const float centerX=(min.x+max.x)*.5f;
    float top=min.y+UiScale(12);
    if(flySpeedNotice_>0)top+=ImGui::GetTextLineHeight()+UiScale(16);
    auto pill=[&](const std::string& full,const std::string& brief,ImU32 color,float alpha) {
        const std::string& text=brief.empty()||PillWidth(full.c_str())<=size.x-UiScale(24)?full:brief;
        Pill(draw,{centerX,top},text.c_str(),color,alpha);
    };
    const bool armed=m.countdownEnd>=0;
    if(c.Recording()) {
        // Unity-style record tint: the whole view gets a red frame while keys are being written.
        draw->AddRect(min,max,Fade(Red,.55f+.45f*Pulse(5)),0,UiScale(3));
        const std::string time="REC  "+Duration(c.RecordedSeconds());
        pill(time+T("  ·  Space 정지","  ·  Space to stop"),time,Red,Pulse(6));
    }else if(m.stopping)pill(T("카메라 끄는 중...","Turning the camera off..."),"",Muted,Pulse());
    else if(!m.cameraReady)pill(T("카메라 켜는 중...","Starting the camera..."),"",Violet,Pulse());
    else if(armed) {
        const bool waiting=!(c.Calibrated()&&c.Tracking());
        const double remaining=m.countdownEnd-Mocap::Clock();
        if(waiting)pill(T("녹화 대기 중 · 기준 자세를 잡으면 카운트다운 시작","Armed · the countdown starts once the reference pose is set"),
            T("녹화 대기 중","Armed"),Orange,Pulse());
        else if(remaining>0) {
            const float radius=std::min(UiScale(58),std::min(size.x,size.y)*.2f);
            const ImVec2 center{centerX,(min.y+max.y)*.5f};
            const float seconds=std::ceil(static_cast<float>(remaining));
            const float fraction=std::clamp(static_cast<float>(remaining)-(seconds-1),0.f,1.f);
            constexpr float Pi=3.14159265f;
            draw->AddCircleFilled(center,radius,IM_COL32(10,12,20,170));
            draw->AddCircle(center,radius,IM_COL32(255,255,255,40),0,UiScale(4));
            draw->PathArcTo(center,radius,-Pi*.5f,-Pi*.5f+Pi*2*fraction,48);
            draw->PathStroke(Orange,UiScale(4));
            char number[16];snprintf(number,sizeof(number),"%d",static_cast<int>(seconds));
            ImFont* font=Bold();const float fontSize=radius*1.1f;
            const ImVec2 textSize=font->CalcTextSizeA(fontSize,FLT_MAX,0,number);
            draw->AddText(font,fontSize,{center.x-textSize.x*.5f,center.y-textSize.y*.5f},IM_COL32(255,255,255,240),number);
            CenteredText(draw,{center.x,center.y+radius+UiScale(16)},IM_COL32(236,240,248,220),T("Space / Esc 취소","Space / Esc to cancel"));
        }
    }else if(m.review>=0)pill(T("테이크 확인 중 · Enter 완료 · Space 다시 녹화 · Del 버리기","Reviewing · Enter keep · Space retake · Del discard"),
        T("테이크 확인 중","Reviewing take"),Violet,1);
    else if(!c.Tracking())pill(FramingHint(m.lastFrame?Mocap::CheckFraming(*m.lastFrame):Framing::NoPerson,settings_.korean),"",Orange,Pulse());
    else if(!c.Calibrated()) {
        char text[128];
        snprintf(text,sizeof(text),T("정면을 보고 팔을 내린 채 멈춰 주세요  %d/15","Face forward, arms down and hold still  %d/15"),c.CalibrationSamples());
        pill(text,T("기준 자세 잡는 중","Calibrating"),Orange,Pulse());
    }else pill(T("녹화 준비됨 · Space 녹화","Ready · Space to record"),"",Teal,1);
    draw->PopClipRect();
}
}

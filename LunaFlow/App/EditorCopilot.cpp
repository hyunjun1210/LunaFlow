#include "App/EditorApp.h"
#include "App/UiTheme.h"
#include "imgui.h"
#include "imgui_internal.h"
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>

namespace LunaFlow {
namespace {
enum class StepIcon { Done,Failed,Stopped,Active,Pending };
void Icon(StepIcon icon){
    const float h=ImGui::GetTextLineHeight(),r=h*.32f,line=UiScale(1.6f);const ImVec2 p=ImGui::GetCursorScreenPos(),c{p.x+h*.5f,p.y+h*.5f};
    auto* draw=ImGui::GetWindowDrawList();const ImU32 muted=ImGui::GetColorU32(ImGuiCol_TextDisabled);
    switch(icon){
    case StepIcon::Done:{
        draw->AddCircleFilled(c,r+UiScale(1),ImGui::GetColorU32(ImVec4{.25f,.70f,.55f,1}));
        const ImVec2 check[]={{c.x-r*.5f,c.y},{c.x-r*.1f,c.y+r*.42f},{c.x+r*.55f,c.y-r*.4f}};draw->AddPolyline(check,3,IM_COL32_WHITE,line);break;}
    case StepIcon::Failed:{
        draw->AddCircleFilled(c,r+UiScale(1),ImGui::GetColorU32(ImVec4{.85f,.32f,.36f,1}));const float k=r*.45f;
        draw->AddLine({c.x-k,c.y-k},{c.x+k,c.y+k},IM_COL32_WHITE,line);draw->AddLine({c.x-k,c.y+k},{c.x+k,c.y-k},IM_COL32_WHITE,line);break;}
    case StepIcon::Stopped:draw->AddCircle(c,r,muted,0,line);draw->AddLine({c.x-r*.45f,c.y},{c.x+r*.45f,c.y},muted,line);break;
    case StepIcon::Active:{const float a=static_cast<float>(ImGui::GetTime())*6;draw->PathArcTo(c,r,a,a+IM_PI*1.4f,16);draw->PathStroke(ImGui::GetColorU32(ImVec4{.72f,.58f,1,1}),line);break;}
    case StepIcon::Pending:draw->AddCircle(c,r,muted,0,UiScale(1.2f));break;
    }
    ImGui::Dummy({h,h});ImGui::SameLine(0,ImGui::GetStyle().ItemInnerSpacing.x);
}
void MutedWrapped(const std::string& text){
    ImGui::PushStyleColor(ImGuiCol_Text,ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));ImGui::TextWrapped("%s",text.c_str());ImGui::PopStyleColor();
}
}
bool EditorApp::VfxAgentAvailable() const{
    try{VfxAgentSettings(CopilotProjectRoot());return true;}catch(const std::exception&){return false;}
}
void EditorApp::NoteCopilotActivity(const std::string& text){
    if(!text.empty()&&(copilotActivity_.empty()||copilotActivity_.back()!=text)){copilotActivity_.push_back(text);(copilotVfx_?vfxAgent_.scroll:chatScroll_)=true;}
}
void EditorApp::AddCopilotReply(std::string text){AddCopilotMessage(false,text,"Copilot Studio");}
void EditorApp::CancelCopilot(){
    copilot_.Cancel();editPlan_.Cancel();copilotActive_=false;copilotRequest_=Json();
}
bool EditorApp::SendCopilot(const std::string& text,bool vfx){
    if(copilotActive_){status_=T("현재 AI 작업을 완료하거나 중지한 뒤 요청하세요.","Finish or stop the current AI task first.");return false;}
    bool submitted=false;
    Try([&]{
        const auto root=CopilotProjectRoot();const auto config=vfx?VfxAgentSettings(root):std::filesystem::path{};
        auto request=EditSceneContext(model_,selectedClip_,selectedBone_,selectedEffect_,time_,agent_.Draft(model_,Clip(),text));
        // The VFX agent sees its own conversation and a narrowed operation set; it never falls back to the animation agent.
        if(vfx)request=VfxAgentRequest(std::move(request),vfxAgent_);
        copilot_.Submit(root,request,config);copilotRequest_=std::move(request);copilotActive_=true;copilotVfx_=vfx;copilotUndoCaptured_=false;copilotElapsed_=0;copilotThought_=-1;editPlan_=EditPlan{};
        AddCopilotMessage(true,text,vfx?std::string("VFX"):std::string(ClipLabel(Clip())));status_=T("Copilot Studio에 연결 중입니다. 첫 요청은 브라우저에서 로그인하세요.","Connecting to Copilot Studio. Sign in in your browser on the first request.");
        copilotActivity_.clear();
        NoteCopilotActivity(vfx?T("현재 장면·효과 목록·VFX 대화를 요청에 담았습니다.","Packed the scene, effects and VFX conversation into the request."):T("현재 장면·뼈대·선택한 동작을 요청에 담았습니다.","Packed the scene, rig and selected motion into the request."));
        NoteCopilotActivity(status_);submitted=true;
    });
    return submitted;
}
void EditorApp::PumpCopilot(float deltaTime){
    if(copilotActive_)copilotElapsed_+=std::max(deltaTime,0.0f);
    for(auto& event:copilot_.Poll()){
        if(!copilotActive_||!copilotRequest_.is_object()||event.value("requestId",Json())!=copilotRequest_["requestId"]||event.value("sessionId",Json())!=copilotRequest_["sessionId"])continue;
        const auto state=event.value("status",std::string{}),message=event.value("message",std::string{});
        if(state=="running"){status_=message;NoteCopilotActivity(message);continue;}
        if(state=="failed"){status_=message;error_=true;AddCopilotReply(message);copilotActive_=false;continue;}
        if(state!="completed")continue;
        if(!event.contains("plan")||event["plan"].is_null()){
            AddCopilotReply(message);copilotActive_=false;status_=T("답변을 받았습니다. 편집 계획은 실행되지 않았습니다.","Reply received. No edit plan was executed.");continue;
        }
        NoteCopilotActivity(T("편집 계획을 받아 전체 단계를 검증했습니다.","Received the edit plan and validated every step."));
        Try([&]{editPlan_.Begin(model_,copilotRequest_,event["plan"]);AddCopilotReply(editPlan_.Summary());copilotStepTime_=0;});
        if(error_){editPlan_.Cancel();AddCopilotReply(status_);copilotActive_=false;}
        else if(!editPlan_.Running())copilotActive_=false;
    }
    if(!copilotActive_||!editPlan_.Running())return;
    copilotStepTime_-=std::min(deltaTime,.1f);if(copilotStepTime_>0)return;copilotStepTime_=.35f;
    Try([&]{
        std::shared_ptr<ModelAsset> snapshot;if(!copilotUndoCaptured_)snapshot=std::make_shared<ModelAsset>(model_);
        auto focus=editPlan_.Advance(model_);
        if(focus.changed){
            if(!copilotUndoCaptured_){
                UndoState state;state.clip=selectedClip_;state.scene=std::move(snapshot);PushUndo(std::move(state));copilotUndoCaptured_=true;
            }
            dirty_=true;
        }
        for(size_t i=0;i<model_.clips.size();++i)if(model_.clips[i].id==focus.clipId)selectedClip_=static_cast<int>(i);
        if(!focus.effectId.empty()){
            for(size_t i=0;i<model_.effects.size();++i)if(model_.effects[i].id==focus.effectId)selectedEffect_=static_cast<int>(i);
            SelectWorkspace(2);options_.showEffects=true;
        }else if(!focus.clipId.empty())SelectWorkspace(1);
        for(size_t i=0;i<model_.skeleton.bones.size();++i)if(model_.skeleton.bones[i].path==focus.bonePath)selectedBone_=static_cast<int>(i);
        revealSelected_=true;editBone_=-1;editClip_=-1;nameClip_=-1;bindPose_=false;loop_=Clip().loop;SetTime(focus.time);playing_=focus.play;
        const auto& completed=editPlan_.Steps()[editPlan_.Completed()-1];status_=completed.description;
        if(!editPlan_.Running()){
            copilotActive_=false;AddCopilotReply(copilotUndoCaptured_?T("편집 단계를 모두 적용했습니다. Ctrl+Z로 이번 AI 작업을 되돌릴 수 있습니다.","All edit steps applied. Ctrl+Z undoes this AI task."):T("미리 보기 단계를 완료했습니다.","Preview steps completed."));
            if(!focus.play){SetTime(0);playing_=true;}
        }
    });
    if(error_){editPlan_.Cancel();copilotActive_=false;AddCopilotReply(status_);}
}
void EditorApp::CopilotProgress(){
    const float line=ImGui::GetTextLineHeight(),inset=line+ImGui::GetStyle().ItemInnerSpacing.x;
    if(copilotActive_&&editPlan_.Steps().empty()){
        // Placeholder assistant turn while waiting: breathing dot, shimmering label and the real
        // bridge stage. No percentage is invented before a plan with known steps exists.
        ImGui::TextColored({.41f,.79f,.70f,1},"LunaFlow");ImGui::SameLine();ImGui::TextDisabled("Copilot Studio");
        const ImVec2 p=ImGui::GetCursorScreenPos();const float pulse=.5f+.5f*std::sin(static_cast<float>(ImGui::GetTime())*3.2f);
        ImGui::GetWindowDrawList()->AddCircleFilled({p.x+line*.5f,p.y+line*.5f},line*(.20f+.10f*pulse),ImGui::GetColorU32(ImVec4{.72f,.58f,1,.55f+.45f*pulse}));
        ImGui::Dummy({line,line});ImGui::SameLine(0,ImGui::GetStyle().ItemInnerSpacing.x);
        ShimmerText(T("생각 중…","Thinking…"),{.50f,.47f,.62f,1},{.97f,.95f,1,1});
        ImGui::SameLine();ImGui::TextDisabled(T("%.0f초","%.0fs"),copilotElapsed_);
        ImGui::Indent(inset);
        if(!status_.empty())MutedWrapped(status_);
        if(copilotActivity_.size()>1&&ImGui::TreeNode("AiStatus",T("진행 단계 (%d)","Activity (%d)"),static_cast<int>(copilotActivity_.size()))){
            for(size_t i=0;i<copilotActivity_.size();++i){Icon(i+1<copilotActivity_.size()?StepIcon::Done:StepIcon::Active);MutedWrapped(copilotActivity_[i]);}
            ImGui::TreePop();
        }
        ImGui::Unindent(inset);ImGui::Spacing();
    }
    if(!editPlan_.Steps().empty()){
        const auto& steps=editPlan_.Steps();const size_t total=steps.size(),done=editPlan_.Completed();
        if(editPlan_.Running()){
            char label[128];std::snprintf(label,sizeof(label),T("편집 적용 중 %zu/%zu","Applying edits %zu/%zu"),std::min(done+1,total),total);
            ShimmerText(label,{.50f,.47f,.62f,1},{.97f,.95f,1,1});
        }
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram,ImVec4{.55f,.42f,.95f,1});
        ImGui::ProgressBar(static_cast<float>(done)/static_cast<float>(total),{-1,UiScale(4)},"");ImGui::PopStyleColor();
        bool activeShown=false;
        for(const auto& step:steps){
            StepIcon icon=StepIcon::Pending;
            if(step.state=="completed")icon=StepIcon::Done;else if(step.state=="failed")icon=StepIcon::Failed;else if(step.state=="cancelled")icon=StepIcon::Stopped;
            else if(editPlan_.Running()&&!activeShown){icon=StepIcon::Active;activeShown=true;}
            Icon(icon);if(icon==StepIcon::Pending)MutedWrapped(step.description);else ImGui::TextWrapped("%s",step.description.c_str());
        }
        ImGui::Separator();
    }
}
}
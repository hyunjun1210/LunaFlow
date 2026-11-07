#include "App/EditorApp.h"
#include "App/UiTheme.h"
#include "imgui.h"
#include <algorithm>
#include <cstdio>

namespace LunaFlow {
void EditorApp::AutoRigCharacter(){Try([&]{
    ModelAsset next=model_;
    const auto report=AutoRig(next);
    CancelCopilot();
    UndoState state;state.clip=selectedClip_;state.scene=std::make_shared<ModelAsset>(model_);PushUndo(std::move(state));
    for(const auto& warning:report.warnings)next.warnings.push_back("Auto rig: "+warning);
    renderer_.SetModel(next);model_=std::move(next);
    // Like a fresh import in Unity/Blender: reveal the new skeleton with its root bone selected.
    selectedBone_=report.bones.empty()?0:static_cast<int>(report.bones.front());selectedEffect_=-1;selectedKeyTime_=-1;
    options_.skeleton=true;revealSelected_=true;libraryTab_=LibraryTab::Hierarchy;selectLibraryTab_=true;
    bindPose_=false;dirty_=true;SetTime(time_);
    char text[256];
    std::snprintf(text,sizeof(text),T("자동 리깅 완료: 뼈 %zu개가 메시 %zu개를 움직입니다. Ctrl+Z로 되돌릴 수 있습니다.","Auto rig complete: %zu bones now drive %zu meshes. Undo with Ctrl+Z."),report.bones.size(),report.meshes);
    status_=text;
    if(!report.warnings.empty()){
        std::snprintf(text,sizeof(text),T(" 확인할 점 %zu개는 정보 탭에 있습니다."," See %zu note(s) in the Info tab."),report.warnings.size());
        status_+=text;
    }
});}
// Unity-style help box: offers the fix right where the missing skeleton shows (the hierarchy).
void EditorApp::AutoRigNotice(){
    if(!NeedsAutoRig(model_))return;
    ImGui::PushStyleColor(ImGuiCol_ChildBg,ImVec4(.13f,.17f,.27f,1));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding,UiScale(6));
    if(ImGui::BeginChild("auto-rig-notice",{0,0},ImGuiChildFlags_AutoResizeY|ImGuiChildFlags_AlwaysUseWindowPadding)){
        ImGui::TextWrapped("%s",T("이 캐릭터에는 뼈대가 없어 사람형 동작을 만들 수 없습니다.","This character has no skeleton, so it cannot play humanoid motion."));
        if(ImGui::Button(T("자동 리깅###AutoRig","Auto rig###AutoRig"),{-1,0}))AutoRigCharacter();
        if(ImGui::IsItemHovered())
            ImGui::SetTooltip("%s",T("사람형 뼈대와 스킨 가중치를 자동으로 만듭니다. T 또는 A 자세에서 가장 정확하며 Ctrl+Z로 되돌릴 수 있습니다.","Builds a humanoid skeleton and skin weights. Most accurate for T or A poses; undo with Ctrl+Z."));
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();ImGui::PopStyleColor();
}
void EditorApp::ImportFbxMotion(){Try([&]{
    auto path=Dialog(false,L"FBX animation (*.fbx)\0*.fbx\0\0",L"fbx");if(path.empty())return;
    auto imported=std::make_shared<ModelAsset>(ImportScene(path,unitsToMeters_));
    if(imported->clips.empty()||std::all_of(imported->clips.begin(),imported->clips.end(),[](const auto& clip){return clip.tracks.empty();}))throw std::runtime_error("The FBX contains no animation tracks.");
    motionSource_=std::move(imported);sourceMotion_=0;showRetarget_=true;
});}
void EditorApp::FbxTools(){
    if(showFbxImport_){ImGui::OpenPopup("FbxImport###FbxImport");showFbxImport_=false;}
    const auto* viewport=ImGui::GetMainViewport();
    ImGui::SetNextWindowSizeConstraints({0,0},{viewport->WorkSize.x*.94f,viewport->WorkSize.y*.94f});
    if(ImGui::BeginPopupModal(T("FBX 가져오기###FbxImport","Import FBX###FbxImport"),nullptr,ImGuiWindowFlags_AlwaysAutoResize)){
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX()+std::min(UiScale(430),viewport->WorkSize.x*.8f));
        ImGui::TextUnformatted(T("FBX 파일을 어떻게 가져올까요?","How would you like to import the FBX file?"));
        if(ImGui::Button(T("캐릭터 열기###FbxCharacter","Open character###FbxCharacter"),{-1,0})){ImGui::CloseCurrentPopup();OpenCharacter();}
        ImGui::TextWrapped("%s",T("새 캐릭터와 포함된 동작을 불러옵니다. 현재 장면의 저장 여부를 확인합니다.","Open a character and its animations, with the usual unsaved-scene check."));
        if(ImGui::Button(T("현재 장면에 모델 추가###FbxObject","Add model to scene###FbxObject"),{-1,0})){ImGui::CloseCurrentPopup();ImportObject();}
        ImGui::TextWrapped("%s",T("현재 캐릭터를 유지하고 모델·소품을 추가합니다.","Keep the current character and add another model or prop."));
        if(ImGui::Button(T("동작만 가져오기 · 체형 보정###FbxMotion","Import animation · adapt proportions###FbxMotion"),{-1,0})){ImGui::CloseCurrentPopup();ImportFbxMotion();}
        ImGui::TextWrapped("%s",T("FBX의 동작을 선택해 현재 캐릭터의 키와 팔다리 비율에 맞춥니다.","Select an FBX animation and adapt it to the current character's proportions."));
        if(ImGui::Button(T("취소","Cancel"),{-1,0}))ImGui::CloseCurrentPopup();
        ImGui::PopTextWrapPos();ImGui::EndPopup();
    }
    if(showRetarget_){ImGui::OpenPopup("Proportions###Proportions");showRetarget_=false;}
    ImGui::SetNextWindowSizeConstraints({0,0},{viewport->WorkSize.x*.94f,viewport->WorkSize.y*.94f});
    if(ImGui::BeginPopupModal(T("캐릭터 체형 보정###Proportions","Character proportion adaptation###Proportions"),nullptr,ImGuiWindowFlags_AlwaysAutoResize)){
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX()+std::min(UiScale(470),viewport->WorkSize.x*.8f));
        if(motionSource_){
            ImGui::TextUnformatted(motionSource_->source.filename().u8string().c_str());
            ImGui::TextUnformatted(T("동작","Motion"));ImGui::SetNextItemWidth(-1);
            if(ImGui::BeginCombo("##source-motion",motionSource_->clips.at(static_cast<size_t>(sourceMotion_)).name.c_str())){
                for(size_t i=0;i<motionSource_->clips.size();++i)if(ImGui::Selectable(motionSource_->clips[i].name.c_str(),sourceMotion_==static_cast<int>(i)))sourceMotion_=static_cast<int>(i);
                ImGui::EndCombo();
            }
            ImGui::Checkbox(T("손 위치 보정 (팔 IK)","Correct hand reach (arm IK)"),&proportionSettings_.handIK);
            ImGui::Checkbox(T("발 위치·지면 보정 (다리 IK)","Correct foot placement and ground contact"),&proportionSettings_.footIK);
            ImGui::Checkbox(T("양발 지지 시 중심·자세 안정화","Stabilize posture over both support feet"),&proportionSettings_.balance);
            const int rates[]={15,30,60};const char* labels[]={"15 FPS","30 FPS","60 FPS"};int rate=proportionSettings_.fps==15?0:proportionSettings_.fps==60?2:1;
            ImGui::TextUnformatted(T("샘플링","Sampling"));ImGui::SetNextItemWidth(-1);if(ImGui::Combo("##retarget-fps",&rate,labels,3))proportionSettings_.fps=rates[rate];
            ImGui::TextWrapped("%s",T("키에 따른 이동량·보폭을 조정하고, 대상 캐릭터의 뼈 길이와 기준 자세를 유지합니다. 이름으로 뼈를 자동 매칭하며, 매칭되지 않는 뼈는 기준 자세를 유지합니다.","Scale motion and stride by height while retaining target bone lengths and rest pose. Bones are matched by name; unmatched bones keep their rest pose."));
            ImGui::TextWrapped("%s",T("손발 IK는 인식된 사람형 체인에 적용합니다. 관통 검사는 관절 위치와 몸통 근사 형태를 사용하며, 메시 전체 충돌 검사는 아닙니다.","IK uses recognized humanoid chains. Penetration checks use joints and a torso proxy, not full mesh collision."));
            if(ImGui::Button(T("보정하여 새 동작 추가###AdaptFbx","Adapt and add a new motion###AdaptFbx"),{-1,0})){
                Try([&]{
                    auto result=AdaptMotion(motionSource_->skeleton,motionSource_->clips.at(static_cast<size_t>(sourceMotion_)),model_.skeleton,proportionSettings_);
                    CancelCopilot();UndoState state;state.clip=selectedClip_;state.scene=std::make_shared<ModelAsset>(model_);PushUndo(std::move(state));
                    model_.clips.push_back(std::move(result.clip));proportionReport_=std::move(result.report);hasProportionReport_=true;
                    selectedClip_=static_cast<int>(model_.clips.size()-1);SelectWorkspace(1);SetTime(0);playing_=true;bindPose_=false;loop_=Clip().loop;dirty_=true;nameClip_=-1;editClip_=-1;
                    proportionReportClipId_=Clip().id;proportionReportRevision_=Clip().revision;
                    status_=T("체형을 보정한 동작을 추가했습니다. 동작 목록 아래에서 검사 결과를 확인하세요.","Adapted motion added. Review the report below the motions list.");motionSource_.reset();ImGui::CloseCurrentPopup();
                });
            }
            if(error_)ImGui::TextWrapped("%s",status_.c_str());
        }
        if(ImGui::Button(T("닫기","Close"),{-1,0})){motionSource_.reset();ImGui::CloseCurrentPopup();}
        ImGui::PopTextWrapPos();ImGui::EndPopup();
    }
}
void EditorApp::ProportionReportPanel(){
    if(ImGui::Button(T("현재 동작 포즈 검사###InspectMotion","Inspect current motion###InspectMotion"),{-1,0}))Try([&]{proportionReport_=InspectMotion(model_.skeleton,Clip());hasProportionReport_=true;proportionReportClipId_=Clip().id;proportionReportRevision_=Clip().revision;});
    if(!hasProportionReport_||proportionReportClipId_!=Clip().id||proportionReportRevision_!=Clip().revision)return;
    if(ImGui::CollapsingHeader(T("체형 보정·포즈 검사 결과","Proportion and pose report"),ImGuiTreeNodeFlags_DefaultOpen)){
        const auto& r=proportionReport_;
        if(r.sourceHeight>0){ImGui::TextWrapped(T("키 비율 %.3f · 매칭된 뼈 %d / %d","Height ratio %.3f · mapped bones %d / %d"),r.heightRatio,r.mappedBones,r.totalBones);
            ImGui::TextWrapped(T("보정 샘플: 도달 범위 %d · 지면 %d · 몸통 %d · 중심 %d","Corrected samples: reach %d · ground %d · torso %d · balance %d"),r.reachCorrections,r.groundCorrections,r.bodyCorrections,r.balanceCorrections);}
        ImGui::TextWrapped(T("검토할 샘플: 지면 %d · 몸통 %d · 과도한 접힘 %d","Samples to review: ground %d · torso %d · deep joint fold %d"),r.groundWarnings,r.bodyWarnings,r.poseWarnings);
        for(size_t i=0;i<std::min<size_t>(r.unmatchedBones.size(),12);++i)ImGui::TextWrapped("%s: %s",T("매칭 없음","Unmatched"),r.unmatchedBones[i].c_str());
        ImGui::TextWrapped("%s",T("수치는 문제가 관찰된 샘플 수입니다. 별도 충돌 형상이나 관절 한계가 없는 리그에서는 최종 자세를 직접 확인하세요.","Counts refer to sampled frames/joints. Review the pose when the rig lacks collision shapes or joint limits."));
    }
}
}

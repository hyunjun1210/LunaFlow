#include "App/EditorApp.h"
#include "imgui.h"
#include <algorithm>
#include "Core/UnityPackage.h"

namespace LunaFlow {
void EditorApp::ExportUnity() {Try([&]{
    auto path=Dialog(true,L"LunaFlow Unity scene (*.lunaflow.json)\0*.lunaflow.json\0\0",L"json",L"Scene.lunaflow.json");if(path.empty())return;
    wchar_t executable[MAX_PATH]{};GetModuleFileNameW(nullptr,executable,MAX_PATH);
    ExportUnityPackage(path,model_,settings_.bakeFps,std::filesystem::path(executable).parent_path()/L"Unity",Clip().id);
    status_=T("Unity 패키지를 내보냈습니다. 폴더를 Assets/LunaFlow로 가져오세요.","Unity package exported. Import the folder into Assets/LunaFlow.");
});}
void EditorApp::ImportObject() {Try([&]{
    auto path=Dialog(false,L"3D object (*.fbx;*.gltf;*.glb;*.obj)\0*.fbx;*.gltf;*.glb;*.obj\0\0",L"fbx");if(path.empty())return;
    const auto imported=ImportScene(path,unitsToMeters_);ModelAsset next=model_;const auto id=AppendModel(next,imported,path.stem().u8string());
    renderer_.SetModel(next);model_=std::move(next);for(const auto& o:model_.objects)if(o.id==id)selectedBone_=static_cast<int>(o.root);
    attachmentTarget_=0;for(size_t i=0;i<model_.skeleton.bones.size();++i)if(model_.skeleton.bones[i].name.find("RightHand")!=std::string::npos){attachmentTarget_=static_cast<int>(i);break;}
    selectedEffect_=-1;SelectWorkspace(0);revealSelected_=true;playing_=false;dirty_=true;editBone_=-1;pose_=EvaluatePose(model_.skeleton,&Clip(),time_);renderer_.FitCamera(camera_);
    status_=T("장면에 모델을 추가했습니다. 속성에서 뼈에 부착할 수 있습니다.","Model added to the scene. Attach it to a joint from the inspector.");
});}
void EditorApp::AddSceneEffect(EffectType type) {
    AddEffect(model_,type,static_cast<uint32_t>(selectedBone_),Clip().id,PlaybackDuration(Clip()));selectedEffect_=static_cast<int>(model_.effects.size()-1);dirty_=true;
    SelectWorkspace(2);
    status_=T("효과를 추가했습니다. 속성에서 부착 위치와 재생 구간을 조절하세요.","Effect added. Adjust its attachment and timing in the inspector.");
}
void EditorApp::SelectEffect(int index) {
    if(index<0||index>=static_cast<int>(model_.effects.size())){selectedEffect_=-1;return;}
    selectedEffect_=index;const auto& effect=model_.effects[index];selectedBone_=static_cast<int>(effect.bone);
    revealSelected_=true;selectedKeyTime_=-1;editBone_=-1;showInspector_=true;
    for(size_t i=0;i<model_.clips.size();++i)if(model_.clips[i].id==effect.clipId&&selectedClip_!=static_cast<int>(i)){
        selectedClip_=static_cast<int>(i);loop_=Clip().loop;bindPose_=false;nameClip_=-1;editClip_=-1;SetTime(0);break;
    }
}
void EditorApp::EffectInspector() {
    ImGui::SeparatorText(T("효과 속성","EFFECT INSPECTOR"));
    if(selectedEffect_<0||selectedEffect_>=static_cast<int>(model_.effects.size())){
        ImGui::TextWrapped("%s",T("효과 목록에서 효과를 선택하거나 추가하세요.","Select or add an effect from the Effects tab."));return;
    }
    auto& effect=model_.effects[selectedEffect_];ImGui::TextColored({.72f,.58f,1,1},"%s",effect.name.c_str());
    bool changed=false;
    if(ImGui::BeginTable("effect-properties",2,ImGuiTableFlags_SizingStretchProp)){
        ImGui::TableSetupColumn("label",ImGuiTableColumnFlags_WidthFixed,62);
        ImGui::TableSetupColumn("value",ImGuiTableColumnFlags_WidthStretch);
        auto row=[&](const char* label,const auto& editor){
            ImGui::TableNextRow();ImGui::TableSetColumnIndex(0);ImGui::AlignTextToFramePadding();ImGui::TextUnformatted(label);
            ImGui::TableSetColumnIndex(1);ImGui::SetNextItemWidth(-1);editor();
        };
        row(T("부착 대상","Attach to"),[&]{if(ImGui::BeginCombo("##effect-bone",model_.skeleton.bones[effect.bone].name.c_str())){
            for(size_t i=0;i<model_.skeleton.bones.size();++i)if(ImGui::Selectable(model_.skeleton.bones[i].name.c_str(),effect.bone==i)){effect.bone=static_cast<uint32_t>(i);changed=true;selectedBone_=static_cast<int>(i);}
            ImGui::EndCombo();
        }});
        row(T("색상","Color"),[&]{changed|=ImGui::ColorEdit4("##fx-color",&effect.color.x,ImGuiColorEditFlags_NoInputs);});
        row(T("크기","Size"),[&]{changed|=ImGui::DragFloat("##fx-size",&effect.size,.01f,.01f,20,"%.2f",ImGuiSliderFlags_AlwaysClamp);});
        row(T("강도","Intensity"),[&]{changed|=ImGui::DragFloat("##fx-intensity",&effect.intensity,.01f,0,10,"%.2f",ImGuiSliderFlags_AlwaysClamp);});
        row(T("구간 (초)","Range (s)"),[&]{
            float start=static_cast<float>(effect.start),end=static_cast<float>(effect.end);
            if(ImGui::DragFloatRange2("##fx-range",&start,&end,.01f,0,static_cast<float>(PlaybackDuration(Clip())),"%.2f","%.2f")){effect.start=std::min(start,end);effect.end=std::max(start,end);changed=true;}
        });
        ImGui::EndTable();
    }
    ImGui::TextUnformatted(T("부착 위치","Attachment offset"));ImGui::SetNextItemWidth(-1);changed|=ImGui::DragFloat3("##fx-offset",&effect.local.translation.x,.01f);
    if(changed)dirty_=true;
    if(ImGui::Button(T("효과 선택 해제","Deselect effect"),{-1,0}))selectedEffect_=-1;
}
}

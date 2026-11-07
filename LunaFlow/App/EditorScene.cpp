#include "App/EditorApp.h"
#include "imgui.h"
#include "App/ContinuousInput.h"
#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstring>
#include <string>
#include "Core/UnityPackage.h"
#include "App/UiTheme.h"

namespace LunaFlow {
namespace {
void InspectorTip(const char* text){
    if(!ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort|ImGuiHoveredFlags_AllowWhenDisabled))return;
    ImGui::BeginTooltip();ImGui::PushTextWrapPos(UiScale(320));ImGui::TextUnformatted(text);ImGui::PopTextWrapPos();ImGui::EndTooltip();
}
// Unity-style color field: a full-width swatch that opens a picker with alpha.
bool InspectorColor(const char* id,XMFLOAT4& color){
    bool changed=false;ImGui::PushID(id);
    if(ImGui::ColorButton("##swatch",{color.x,color.y,color.z,color.w},ImGuiColorEditFlags_AlphaPreviewHalf,{std::max(ImGui::GetContentRegionAvail().x,1.0f),ImGui::GetFrameHeight()}))ImGui::OpenPopup("picker");
    if(ImGui::BeginPopup("picker")){
        changed=ImGui::ColorPicker4("##picker",&color.x,ImGuiColorEditFlags_AlphaBar|ImGuiColorEditFlags_AlphaPreviewHalf);
        ImGui::EndPopup();
    }
    ImGui::PopID();return changed;
}
}
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
    selectedEffect_=-1;SelectWorkspace(0);revealSelected_=true;playing_=false;dirty_=true;editBone_=-1;pose_=EvaluatePose(model_.skeleton,&Clip(),time_);FrameAll();
    status_=T("장면에 모델을 추가했습니다. 속성에서 뼈에 부착할 수 있습니다.","Model added to the scene. Attach it to a joint from the inspector.");
});}
void EditorApp::AddSceneEffect(EffectType type) {
    RememberEffectEdit();
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
        ImGui::PushStyleColor(ImGuiCol_Text,ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextWrapped("%s",T("효과 탭에서 효과를 선택하거나 프리셋을 눌러 새로 만드세요.","Select an effect in the Effects tab, or click a preset to create one."));
        ImGui::PopStyleColor();return;
    }
    auto& effect=model_.effects[static_cast<size_t>(selectedEffect_)];
    const auto before=model_.effects;
    bool changed=false;
    const auto& style=ImGui::GetStyle();
    const auto& bones=model_.skeleton.bones;
    const char* boneName=effect.bone<bones.size()?bones[effect.bone].name.c_str():"-";
    // Header: enable toggle + name like Unity's object header, then type, attachment and preview controls.
    changed|=ImGui::Checkbox("##fx-enabled",&effect.enabled);
    InspectorTip(T("효과 켜기/끄기","Enable or disable the effect"));
    ImGui::SameLine(0,style.ItemInnerSpacing.x);
    char name[256]{};strncpy_s(name,effect.name.c_str(),_TRUNCATE);ImGui::SetNextItemWidth(-FLT_MIN);
    if(ImGui::InputText("##effect-name",name,sizeof(name))){effect.name=name;changed=true;}
    {
        const float r=UiScale(4),lineHeight=ImGui::GetTextLineHeight();const ImVec2 at=ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddCircleFilled({at.x+r,at.y+lineHeight*.5f},r,ImGui::ColorConvertFloat4ToU32({effect.color.x,effect.color.y,effect.color.z,1}));
        ImGui::Dummy({r*2,lineHeight});ImGui::SameLine(0,UiScale(6));ImGui::TextDisabled("%s · %s",EffectTypeLabel(effect.type),boneName);
    }
    const float half=std::max((ImGui::GetContentRegionAvail().x-style.ItemSpacing.x)*.5f,1.0f);
    if(ImGui::Button(playing_?T("일시 정지###fx-play","Pause###fx-play"):T("재생###fx-play","Play###fx-play"),{half,0}))playing_=!playing_;
    InspectorTip(T("미리보기 재생/일시 정지 (Space)","Play or pause the preview (Space)"));
    ImGui::SameLine();
    if(ImGui::Button(T("처음부터###fx-restart","Restart###fx-restart"),{-FLT_MIN,0})){SetTime(effect.start);playing_=true;}
    InspectorTip(T("효과 시작 시간부터 다시 재생","Replay from the effect's start time"));
    const float labelWidth=std::clamp(ImGui::GetContentRegionAvail().x*.42f,UiScale(72),UiScale(150));
    auto row=[&](const char* label,const char* tip,const auto& editor){
        ImGui::TableNextRow();ImGui::TableSetColumnIndex(0);ImGui::AlignTextToFramePadding();
        const bool clipped=ImGui::CalcTextSize(label).x>ImGui::GetContentRegionAvail().x;
        ImGui::TextUnformatted(label);
        if((tip||clipped)&&ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)){
            ImGui::BeginTooltip();ImGui::PushTextWrapPos(UiScale(320));
            if(clipped)ImGui::TextUnformatted(label);
            if(tip)ImGui::TextUnformatted(tip);
            ImGui::PopTextWrapPos();ImGui::EndTooltip();
        }
        ImGui::TableSetColumnIndex(1);ImGui::SetNextItemWidth(-FLT_MIN);editor();
    };
    auto section=[&](const char* title,bool open,const auto& body){
        if(!ImGui::CollapsingHeader(title,open?ImGuiTreeNodeFlags_DefaultOpen:0))return;
        if(ImGui::BeginTable(title,2,ImGuiTableFlags_SizingStretchProp)){
            ImGui::TableSetupColumn("label",ImGuiTableColumnFlags_WidthFixed,labelWidth);
            ImGui::TableSetupColumn("value",ImGuiTableColumnFlags_WidthStretch);
            body();ImGui::EndTable();
        }
    };
    auto dragFloat=[&](const char* id,float& value,float speed,float low,float high,const char* format="%.2f"){
        changed|=ContinuousDragFloat(id,&value,speed,low,high,format,ImGuiSliderFlags_AlwaysClamp);
    };
    auto percent=[&](const char* id,float& value,float high){
        float shown=value*100;
        if(ImGui::SliderFloat(id,&shown,0,high*100,"%.0f%%",ImGuiSliderFlags_AlwaysClamp)){value=shown/100;changed=true;}
    };
    section(T("부착 · 재생 구간##attach","Attachment & timing##attach"),true,[&]{
        row(T("부착 뼈","Bone"),T("효과가 따라 움직일 뼈","Joint the effect follows"),[&]{
            if(ImGui::BeginCombo("##fx-bone",boneName,ImGuiComboFlags_HeightLarge)){
                if(ImGui::IsWindowAppearing()){effectBoneFilter_[0]=0;ImGui::SetKeyboardFocusHere();}
                ImGui::SetNextItemWidth(-FLT_MIN);ImGui::InputTextWithHint("##bone-filter",T("뼈 이름 검색","Search joints"),effectBoneFilter_,sizeof(effectBoneFilter_));
                std::string filter=effectBoneFilter_;for(auto& c:filter)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                for(size_t i=0;i<bones.size();++i){
                    std::string lower=bones[i].name;for(auto& c:lower)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                    if(!filter.empty()&&lower.find(filter)==std::string::npos)continue;
                    ImGui::PushID(static_cast<int>(i));
                    if(ImGui::Selectable(bones[i].name.c_str(),effect.bone==i)){effect.bone=static_cast<uint32_t>(i);changed=true;selectedBone_=static_cast<int>(i);}
                    if(effect.bone==i&&ImGui::IsWindowAppearing())ImGui::SetScrollHereY();
                    ImGui::PopID();
                }
                ImGui::EndCombo();
            }
        });
        row(T("위치 (m)","Offset (m)"),T("뼈 기준 위치 오프셋","Position offset from the joint"),[&]{changed|=ContinuousDragFloat3("##fx-offset",&effect.local.translation.x,.01f,0,0,"%.2f");});
        row(T("회전°","Rotation°"),nullptr,[&]{
            auto angles=DegreesFromQuaternion(effect.local.rotation);
            if(ContinuousDragFloat3("##fx-rotation",&angles.x,.5f,0,0,"%.1f")){effect.local.rotation=QuaternionFromDegrees(angles);changed=true;}
        });
        row(T("재생 구간 (초)","Range (s)"),T("효과가 보이는 시간 범위. 두 값을 각각 드래그하세요.","Time range in which the effect is visible. Drag either value."),[&]{
            float start=static_cast<float>(effect.start),end=static_cast<float>(effect.end);
            if(ContinuousDragFloatRange2("##fx-range",&start,&end,.01f,0,static_cast<float>(PlaybackDuration(Clip())),"%.2f","%.2f")){effect.start=std::min(start,end);effect.end=std::max(start,end);changed=true;}
        });
    });
    if(effect.type==EffectType::Particles){
        auto& p=effect.particles;
        section(T("기본##main","Main##main"),true,[&]{
            row(T("방출 시간 (초)","Duration (s)"),T("한 주기 동안 입자를 내보내는 시간","How long one emission cycle lasts"),[&]{dragFloat("##p-duration",p.duration,.01f,.05f,120);});
            row(T("반복","Looping"),T("끄면 한 주기만 방출합니다.","When off, emits a single cycle."),[&]{changed|=ImGui::Checkbox("##p-loop",&p.loop);});
            row(T("수명 (초)","Lifetime (s)"),T("입자 하나가 살아 있는 시간","How long each particle lives"),[&]{dragFloat("##p-life",p.lifetime,.01f,.05f,20);});
            row(T("수명 무작위","Lifetime random"),T("입자마다 수명을 이 비율만큼 무작위로 줄입니다.","Randomly shortens each lifetime by up to this amount."),[&]{percent("##p-life-random",p.lifetimeRandomness,.95f);});
            row(T("시작 속도 (m/s)","Start speed (m/s)"),nullptr,[&]{dragFloat("##p-speed",p.speed,.01f,0,50);});
            row(T("속도 무작위","Speed random"),T("입자마다 속도를 이 비율만큼 무작위로 바꿉니다.","Randomly varies each speed by up to this amount."),[&]{percent("##p-speed-random",p.speedRandomness,1);});
            row(T("시작 크기 (m)","Start size (m)"),nullptr,[&]{dragFloat("##p-start-size",p.startSize,.005f,.001f,10,"%.3f");});
            row(T("시작 색상","Start color"),T("클릭해서 색과 투명도를 고릅니다.","Click to pick color and alpha."),[&]{changed|=InspectorColor("##fx-color",effect.color);});
            row(T("중력","Gravity"),T("1은 실제 중력, 음수는 위로 떠오릅니다.","1 is real gravity; negative values float upward."),[&]{dragFloat("##p-gravity",p.gravity,.01f,-5,5);});
            row(T("월드 공간","World space"),T("켜면 이미 나온 입자가 뼈를 따라가지 않고 그 자리에 남습니다.","When on, emitted particles stay behind instead of following the joint."),[&]{changed|=ImGui::Checkbox("##p-world",&p.worldSpace);});
            row(T("최대 입자 수","Max particles"),nullptr,[&]{changed|=ContinuousDragInt("##p-max",&p.maxParticles,1,1,4096,"%d",ImGuiSliderFlags_AlwaysClamp);});
            row(T("랜덤 시드","Random seed"),T("같은 시드는 항상 같은 모양을 냅니다.","The same seed always gives the same pattern."),[&]{
                int seed=static_cast<int>(std::min(p.seed,static_cast<uint32_t>(2147483647)));
                if(ContinuousDragInt("##p-seed",&seed,1,1,2147483647,"%d",ImGuiSliderFlags_AlwaysClamp)){p.seed=static_cast<uint32_t>(seed);changed=true;}
            });
        });
        section(T("방출##emission","Emission##emission"),true,[&]{
            row(T("초당 입자 수","Rate over time"),nullptr,[&]{dragFloat("##p-rate",p.rate,1,0,2000,"%.0f");});
            row(T("버스트","Burst"),T("재생이 시작될 때 한 번에 내보내는 입자 수","Particles emitted at once when playback starts"),[&]{changed|=ContinuousDragInt("##p-burst",&p.burst,1,0,4096,"%d",ImGuiSliderFlags_AlwaysClamp);});
        });
        section(T("모양##shape","Shape##shape"),true,[&]{
            row(T("모양","Shape"),T("입자가 생겨나는 영역","Volume particles spawn from"),[&]{
                const char* shapes[]={T("원뿔","Cone"),T("구","Sphere"),T("상자","Box"),T("고리","Circle")};
                int shape=static_cast<int>(p.shape);
                if(ImGui::Combo("##p-shape",&shape,shapes,4)){p.shape=static_cast<ParticleShape>(shape);changed=true;}
            });
            if(p.shape==ParticleShape::Box)row(T("상자 크기 (m)","Box size (m)"),nullptr,[&]{changed|=ContinuousDragFloat3("##p-box",&p.box.x,.01f,0,40,"%.2f",ImGuiSliderFlags_AlwaysClamp);});
            else row(T("반경 (m)","Radius (m)"),nullptr,[&]{dragFloat("##p-radius",p.radius,.01f,0,20);});
            if(p.shape==ParticleShape::Cone)row(T("퍼짐 각도°","Cone angle°"),nullptr,[&]{dragFloat("##p-angle",p.angle,.5f,0,90,"%.1f");});
        });
        section(T("수명 동안 속도##velocity","Velocity over lifetime##velocity"),false,[&]{
            row(T("추가 속도 (m/s)","Velocity (m/s)"),T("모든 입자에 더해지는 XYZ 속도","XYZ velocity added to every particle"),[&]{changed|=ContinuousDragFloat3("##p-velocity",&p.velocity.x,.01f,-50,50,"%.2f",ImGuiSliderFlags_AlwaysClamp);});
        });
        section(T("수명 동안 색상 · 크기##lifetime","Color & size over lifetime##lifetime"),true,[&]{
            row(T("색상 변화","Gradient"),T("왼쪽은 시작 색상, 오른쪽은 끝 색상입니다. 격자는 투명한 부분입니다.","Start color on the left, end color on the right. The checkerboard shows transparency."),[&]{
                const ImVec2 at=ImGui::GetCursorScreenPos();const float width=std::max(ImGui::GetContentRegionAvail().x,1.0f),height=ImGui::GetFrameHeight(),cell=height*.5f;
                auto* draw=ImGui::GetWindowDrawList();const int cells=static_cast<int>(std::ceil(width/cell));
                for(int y=0;y<2;++y)for(int x=0;x<cells;++x)
                    draw->AddRectFilled({at.x+cell*static_cast<float>(x),at.y+cell*static_cast<float>(y)},{std::min(at.x+cell*static_cast<float>(x+1),at.x+width),at.y+cell*static_cast<float>(y+1)},((x+y)&1)?IM_COL32(150,150,150,255):IM_COL32(95,95,95,255));
                const ImU32 from=ImGui::ColorConvertFloat4ToU32({effect.color.x,effect.color.y,effect.color.z,effect.color.w}),to=ImGui::ColorConvertFloat4ToU32({p.endColor.x,p.endColor.y,p.endColor.z,p.endColor.w});
                draw->AddRectFilledMultiColor(at,{at.x+width,at.y+height},from,to,to,from);
                draw->AddRect(at,{at.x+width,at.y+height},ImGui::GetColorU32(ImGuiCol_Border));
                ImGui::Dummy({width,height});
            });
            row(T("끝 색상","End color"),T("수명이 끝날 때의 색상과 투명도","Color and alpha at the end of each lifetime"),[&]{changed|=InspectorColor("##p-end-color",p.endColor);});
            row(T("끝 크기 (m)","End size (m)"),nullptr,[&]{dragFloat("##p-end-size",p.endSize,.005f,0,10,"%.3f");});
        });
        section(T("렌더러##renderer","Renderer##renderer"),true,[&]{
            row(T("가산 합성","Additive"),T("색을 더해 빛나 보이게 합니다. 불꽃·마법에 어울립니다.","Adds color so particles glow. Great for fire and magic."),[&]{changed|=ImGui::Checkbox("##p-additive",&p.additive);});
            row(T("밝기","Intensity"),T("색상에 곱해지는 밝기 배율","Brightness multiplier applied to the color"),[&]{dragFloat("##fx-intensity",effect.intensity,.01f,0,10);});
            row(T("크기 배율","Size scale"),T("모든 입자 크기에 곱해지는 배율","Multiplier applied to every particle size"),[&]{dragFloat("##fx-size",effect.size,.01f,.01f,20);});
        });
        if(!effect.sourcePrompt.empty()&&ImGui::CollapsingHeader(T("생성 프롬프트##prompt","Source prompt##prompt"))){
            ImGui::PushStyleColor(ImGuiCol_Text,ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));ImGui::TextWrapped("%s",effect.sourcePrompt.c_str());ImGui::PopStyleColor();
        }
    }else{
        section(T("외형##look","Appearance##look"),true,[&]{
            row(T("색상","Color"),T("클릭해서 색과 투명도를 고릅니다.","Click to pick color and alpha."),[&]{changed|=InspectorColor("##fx-color",effect.color);});
            row(T("크기","Size"),nullptr,[&]{dragFloat("##fx-size",effect.size,.01f,.01f,20);});
            row(T("강도","Intensity"),nullptr,[&]{dragFloat("##fx-intensity",effect.intensity,.01f,0,10);});
        });
    }
    if(changed){
        UndoState state;state.effects=std::make_shared<std::vector<EffectData>>(before);PushUndo(std::move(state),true);dirty_=true;
    }
    ImGui::Spacing();ImGui::Separator();
    const float footerHalf=std::max((ImGui::GetContentRegionAvail().x-style.ItemSpacing.x)*.5f,1.0f);
    if(ImGui::Button(T("복제###fx-duplicate","Duplicate###fx-duplicate"),{footerHalf,0}))DuplicateEffect();
    InspectorTip(T("이 효과 복제 (Ctrl+D)","Duplicate this effect (Ctrl+D)"));
    ImGui::SameLine();
    if(ImGui::Button(T("삭제###fx-delete","Delete###fx-delete"),{-FLT_MIN,0}))DeleteEffect();
    InspectorTip(T("이 효과 삭제 (Del) · Esc로 선택 해제","Delete this effect (Del) · Esc deselects"));
}
void EditorApp::DuplicateEffect() {
    if(selectedEffect_<0||selectedEffect_>=static_cast<int>(model_.effects.size()))return;
    auto copy=model_.effects[static_cast<size_t>(selectedEffect_)];copy.id=NewClipId();copy.name+=T(" 복사본"," copy");
    RememberEffectEdit();model_.effects.push_back(std::move(copy));SelectEffect(static_cast<int>(model_.effects.size()-1));dirty_=true;
    status_=T("효과를 복제했습니다.","Effect duplicated.");error_=false;
}
void EditorApp::DeleteEffect() {
    if(selectedEffect_<0||selectedEffect_>=static_cast<int>(model_.effects.size()))return;
    RememberEffectEdit();model_.effects.erase(model_.effects.begin()+selectedEffect_);selectedEffect_=-1;dirty_=true;
    status_=T("효과를 삭제했습니다. Ctrl+Z로 되돌릴 수 있습니다.","Effect deleted. Press Ctrl+Z to undo.");error_=false;
}
}

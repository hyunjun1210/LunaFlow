#include "App/EditorApp.h"
#include "imgui.h"
#include "App/UiTheme.h"
#include "UiFonts.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <iterator>

namespace LunaFlow {
namespace {
void Tip(const char* text){
    if(!ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort|ImGuiHoveredFlags_AllowWhenDisabled))return;
    ImGui::BeginTooltip();ImGui::PushTextWrapPos(UiScale(320));ImGui::TextUnformatted(text);ImGui::PopTextWrapPos();ImGui::EndTooltip();
}
struct PresetTile { const char* label;const char* tip;ImVec4 color;int particles;EffectType legacy; };
}
void EditorApp::RememberEffectEdit() {
    UndoState state;state.effects=std::make_shared<std::vector<EffectData>>(model_.effects);PushUndo(std::move(state));
}
const char* EditorApp::EffectTypeLabel(EffectType type) const {
    switch(type){
    case EffectType::Trail:return T("궤적","Trail");
    case EffectType::Sparks:return T("스파크","Sparks");
    case EffectType::Aura:return T("오라","Aura");
    case EffectType::Particles:break;
    }
    return T("파티클","Particles");
}
void EditorApp::AddParticlePreset() {
    auto effect=MakeParticleEffect(static_cast<ParticlePreset>(particlePreset_),static_cast<uint32_t>(selectedBone_),Clip().id,PlaybackDuration(Clip()));
    RememberEffectEdit();model_.effects.push_back(std::move(effect));selectedEffect_=static_cast<int>(model_.effects.size()-1);
    dirty_=true;SelectWorkspace(2);SetTime(0);playing_=true;error_=false;
    status_=T("파티클을 추가했습니다. 오른쪽 속성에서 모듈을 편집하세요.","Particle system added. Edit its modules in the inspector.");
}
void EditorApp::GenerateVfx(const std::string& prompt) {
    // Local rules answer inside the VFX AI conversation, so a failed request explains itself where it was typed.
    vfxAgent_.messages.push_back({true,prompt,T("로컬","Local")});vfxAgent_.scroll=true;
    std::string name;
    Try([&]{
        auto effect=GenerateParticleEffect(prompt,static_cast<uint32_t>(selectedBone_),Clip().id,PlaybackDuration(Clip()));
        name=effect.name;RememberEffectEdit();model_.effects.push_back(std::move(effect));selectedEffect_=static_cast<int>(model_.effects.size()-1);
        dirty_=true;SelectWorkspace(2);SetTime(0);playing_=true;
        status_=T("프롬프트로 파티클을 생성했습니다 (로컬 생성).","Particle system generated from prompt (local rules).");
    });
    if(error_)vfxAgent_.messages.push_back({false,T("이 설명으로는 효과를 만들 수 없었습니다. 효과 종류(불꽃·연기·마법·눈·비·폭발·파티클)를 넣어 다시 적어 주세요.\n예: 파란 마법 파티클, 초당 80개, 수명 1.5초",
        "I couldn't build an effect from that. Include a kind (fire, smoke, magic, snow, rain, explosion or particles) and try again.\nExample: blue magic particles, rate 80, lifetime 1.5"),T("로컬 규칙","Local rules")});
    else vfxAgent_.messages.push_back({false,name+T(" 효과를 만들었습니다. 오른쪽 위 속성에서 세부 값을 바꿀 수 있습니다."," created. Fine-tune it in the inspector above."),T("로컬 규칙","Local rules")});
}
void EditorApp::EffectLibrary() {
    const auto& style=ImGui::GetStyle();
    const int count=static_cast<int>(model_.effects.size());
    const bool hasSelection=selectedEffect_>=0&&selectedEffect_<count;
    {
        // Toolbar: count on the left, duplicate/delete on the right like Unity's hierarchy header.
        char label[64];std::snprintf(label,sizeof(label),T("효과 %d개","%d effects"),count);
        const float start=ImGui::GetCursorPosX(),avail=ImGui::GetContentRegionAvail().x;
        const float buttons=ImGui::CalcTextSize(T("복제","Duplicate")).x+ImGui::CalcTextSize(T("삭제","Delete")).x+style.FramePadding.x*4+style.ItemInnerSpacing.x;
        ImGui::AlignTextToFramePadding();ImGui::TextDisabled("%s",label);
        ImGui::SameLine(std::max(start+avail-buttons,start+ImGui::CalcTextSize(label).x+style.ItemSpacing.x));
        ImGui::BeginDisabled(!hasSelection);
        if(ImGui::Button(T("복제###DuplicateEffectTool","Duplicate###DuplicateEffectTool")))DuplicateEffect();
        Tip(T("선택한 효과 복제 (Ctrl+D)","Duplicate the selected effect (Ctrl+D)"));
        ImGui::SameLine(0,style.ItemInnerSpacing.x);
        if(ImGui::Button(T("삭제###DeleteEffectTool","Delete###DeleteEffectTool")))DeleteEffect();
        Tip(T("선택한 효과 삭제 (Del) · Ctrl+Z로 되돌리기","Delete the selected effect (Del) · Ctrl+Z to undo"));
        ImGui::EndDisabled();
    }
    // The list takes whatever the preset gallery (measured last frame) leaves free.
    if(vfxGalleryHeight_<=0)vfxGalleryHeight_=UiScale(220);
    const float listHeight=std::max(ImGui::GetContentRegionAvail().y-vfxGalleryHeight_-UiScale(1),UiScale(72));
    ImGui::BeginChild("effect-list",{0,listHeight},ImGuiChildFlags_Borders);
    if(model_.effects.empty()){
        ImGui::PushStyleColor(ImGuiCol_Text,ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextWrapped("%s",T("아직 효과가 없습니다.\n아래 프리셋을 누르거나 오른쪽 아래 VFX AI에 원하는 효과를 설명해 보세요.","No effects yet.\nClick a preset below or describe one to the VFX AI on the right."));
        ImGui::PopStyleColor();
    }
    int duplicateIndex=-1,deleteIndex=-1;
    const auto& bones=model_.skeleton.bones;
    for(int i=0;i<count;++i){
        auto& effect=model_.effects[static_cast<size_t>(i)];ImGui::PushID(i);
        bool enabled=effect.enabled;
        if(ImGui::Checkbox("##enabled",&enabled)){RememberEffectEdit();effect.enabled=enabled;dirty_=true;}
        Tip(enabled?T("효과 끄기","Disable effect"):T("효과 켜기","Enable effect"));
        ImGui::SameLine(0,style.ItemInnerSpacing.x);
        const ImVec2 min=ImGui::GetCursorScreenPos();const float width=ImGui::GetContentRegionAvail().x,height=ImGui::GetFrameHeight();
        if(ImGui::Selectable("##effect",selectedEffect_==i,ImGuiSelectableFlags_AllowDoubleClick,{width,height})){
            SelectEffect(i);if(ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))FrameSelected();
        }
        const bool hovered=ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal);
        if(ImGui::IsItemClicked(ImGuiMouseButton_Right))SelectEffect(i);
        auto* draw=ImGui::GetWindowDrawList();
        const char* type=EffectTypeLabel(effect.type);const float typeWidth=ImGui::CalcTextSize(type).x;
        const float r=UiScale(4),pad=UiScale(6),textY=min.y+(height-ImGui::GetTextLineHeight())*.5f;
        const bool showType=width>typeWidth+UiScale(96);
        draw->AddCircleFilled({min.x+pad+r,min.y+height*.5f},r,ImGui::ColorConvertFloat4ToU32({effect.color.x,effect.color.y,effect.color.z,effect.enabled?1.0f:.35f}));
        const float nameX=min.x+pad+r*2+UiScale(6),nameRight=min.x+width-(showType?typeWidth+pad*2:pad);
        draw->PushClipRect({nameX,min.y},{std::max(nameRight,nameX),min.y+height},true);
        draw->AddText({nameX,textY},ImGui::GetColorU32(effect.enabled?ImGuiCol_Text:ImGuiCol_TextDisabled),effect.name.c_str());
        draw->PopClipRect();
        if(showType)draw->AddText({min.x+width-pad-typeWidth,textY},ImGui::GetColorU32(ImGuiCol_TextDisabled),type);
        if(ImGui::BeginPopupContextItem("effect-menu")){
            if(ImGui::MenuItem(T("복제","Duplicate"),"Ctrl+D"))duplicateIndex=i;
            if(ImGui::MenuItem(T("삭제","Delete"),"Del"))deleteIndex=i;
            ImGui::Separator();
            if(ImGui::MenuItem(effect.enabled?T("끄기","Disable"):T("켜기","Enable"))){RememberEffectEdit();effect.enabled=!effect.enabled;dirty_=true;}
            ImGui::EndPopup();
        }else if(hovered){
            const char* bone=effect.bone<bones.size()?bones[effect.bone].name.c_str():"-";
            const char* clip="";for(const auto& c:model_.clips)if(c.id==effect.clipId)clip=ClipLabel(c);
            ImGui::SetTooltip("%s\n%s · %s%s%s\n%s",effect.name.c_str(),type,bone,*clip?" · ":"",clip,
                T("더블클릭: 화면에 맞추기 · 우클릭: 메뉴","Double-click: frame in view · Right-click: menu"));
        }
        ImGui::PopID();
    }
    ImGui::EndChild();
    if(duplicateIndex>=0){SelectEffect(duplicateIndex);DuplicateEffect();}
    if(deleteIndex>=0){SelectEffect(deleteIndex);DeleteEffect();}
    const float galleryStart=ImGui::GetCursorPosY();
    ImGui::SeparatorText(T("새 효과","NEW EFFECT"));
    // One-click presets stay in the tab's own ID scope so automation can address ###AddParticles directly.
    const PresetTile tiles[]={
        {T("파티클###AddParticles","Basic###AddParticles"),T("기본 파티클 시스템 · 모든 모듈을 직접 조절합니다.","Plain particle system to tune from scratch."),{1,.65f,.2f,1},0,EffectType::Particles},
        {T("불꽃###AddFire","Fire###AddFire"),T("위로 솟는 따뜻한 불꽃","Rising warm flames"),{1,.56f,.08f,1},1,EffectType::Particles},
        {T("연기###AddSmoke","Smoke###AddSmoke"),T("천천히 퍼지는 연기","Slow, spreading smoke"),{.55f,.58f,.62f,1},2,EffectType::Particles},
        {T("마법###AddMagic","Magic###AddMagic"),T("빛나는 마법 입자","Glowing magic sparkles"),{.7f,.3f,1,1},3,EffectType::Particles},
        {T("눈###AddSnow","Snow###AddSnow"),T("넓게 흩날리는 눈송이","Drifting snowflakes"),{.95f,.98f,1,1},4,EffectType::Particles},
        {T("비###AddRain","Rain###AddRain"),T("곧게 떨어지는 빗방울","Falling raindrops"),{.5f,.7f,1,1},5,EffectType::Particles},
        {T("폭발###AddExplosion","Blast###AddExplosion"),T("한 번에 터지는 폭발","One-shot explosion burst"),{1,.8f,.15f,1},6,EffectType::Particles},
        {T("칼 궤적###AddTrail","Trail###AddTrail"),T("무기 끝을 따라가는 궤적 · 검 휘두르기에 어울립니다.","Ribbon following a weapon tip, ideal for sword swings."),{.58f,.42f,1,1},-1,EffectType::Trail},
        {T("스파크###AddSparks","Sparks###AddSparks"),T("튀어 오르는 불티","Bursting sparks"),{.58f,.42f,1,1},-1,EffectType::Sparks},
        {T("오라###AddAura","Aura###AddAura"),T("몸을 감싸는 빛","Glow wrapping the body"),{.58f,.42f,1,1},-1,EffectType::Aura},
    };
    ImGui::PushFont(UiFonts::regular,14);
    const float dot=UiScale(4),inset=dot*2+UiScale(7),padX=UiScale(6),spacing=style.ItemInnerSpacing.x,avail=ImGui::GetContentRegionAvail().x;
    float widest=0;for(const auto& tile:tiles)widest=std::max(widest,ImGui::CalcTextSize(tile.label,nullptr,true).x);
    const int columns=std::clamp(static_cast<int>((avail+spacing)/(widest+inset+padX*2+spacing)),1,3);
    const float tileWidth=std::max((avail-spacing*static_cast<float>(columns-1))/static_cast<float>(columns),1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign,{0,.5f});
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,{padX+inset,style.FramePadding.y});
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,{spacing,spacing});
    for(int i=0;i<static_cast<int>(std::size(tiles));++i){
        const auto& tile=tiles[i];if(i%columns)ImGui::SameLine();
        if(ImGui::Button(tile.label,{tileWidth,0})){
            if(tile.particles>=0){particlePreset_=tile.particles;AddParticlePreset();}else AddSceneEffect(tile.legacy);
        }
        const ImVec2 a=ImGui::GetItemRectMin(),b=ImGui::GetItemRectMax();
        ImGui::GetWindowDrawList()->AddCircleFilled({a.x+padX+dot,(a.y+b.y)*.5f},dot,ImGui::ColorConvertFloat4ToU32(tile.color));
        Tip(tile.tip);
    }
    ImGui::PopStyleVar(3);
    const char* target=selectedBone_>=0&&selectedBone_<static_cast<int>(bones.size())?bones[static_cast<size_t>(selectedBone_)].name.c_str():"-";
    ImGui::PushTextWrapPos(0);ImGui::TextDisabled(T("부착 위치: %s","Attaches to: %s"),target);ImGui::PopTextWrapPos();
    Tip(T("새 효과는 선택한 뼈에 붙습니다. 계층 탭이나 뷰포트에서 뼈를 먼저 선택하세요.","New effects attach to the selected joint. Pick a joint in the Tree tab or viewport first."));
    ImGui::PopFont();
    vfxGalleryHeight_=ImGui::GetCursorPosY()-galleryStart;
}
}

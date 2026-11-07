#include "App/UiTheme.h"
#include "App/UiFonts.h"
#include "imgui_internal.h"
#include <windows.h>
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <vector>

namespace LunaFlow {
void ConfigureUiTheme(float dpi) {
    auto& s=ImGui::GetStyle();const float mainScale=s.FontScaleMain;
    s=ImGuiStyle{};s.FontScaleMain=mainScale;ImGui::StyleColorsDark();
    s.WindowRounding=0;s.ChildRounding=4;s.FrameRounding=4;s.GrabRounding=3;
    s.WindowPadding={12,10};s.ItemSpacing={8,7};s.FramePadding={9,5};s.WindowBorderSize=0;s.FontSizeBase=16;
    s.Colors[ImGuiCol_WindowBg]={.043f,.051f,.075f,1};s.Colors[ImGuiCol_ChildBg]={.065f,.077f,.106f,1};
    s.Colors[ImGuiCol_FrameBg]={.10f,.12f,.16f,1};s.Colors[ImGuiCol_FrameBgHovered]={.16f,.18f,.26f,1};
    s.Colors[ImGuiCol_Button]={.16f,.15f,.27f,1};s.Colors[ImGuiCol_ButtonHovered]={.29f,.23f,.45f,1};s.Colors[ImGuiCol_ButtonActive]={.39f,.29f,.62f,1};
    s.Colors[ImGuiCol_Header]={.22f,.18f,.35f,1};s.Colors[ImGuiCol_HeaderHovered]={.28f,.23f,.42f,1};s.Colors[ImGuiCol_HeaderActive]={.34f,.27f,.50f,1};
    s.Colors[ImGuiCol_CheckMark]={.69f,.52f,1,1};s.Colors[ImGuiCol_SliderGrab]={.60f,.44f,.91f,1};
    s.Colors[ImGuiCol_Text]={.88f,.90f,.95f,1};s.Colors[ImGuiCol_TextDisabled]={.49f,.55f,.66f,1};s.Colors[ImGuiCol_Border]={.16f,.18f,.24f,1};
    s.Colors[ImGuiCol_Tab]={.11f,.12f,.17f,1};s.Colors[ImGuiCol_TabHovered]={.29f,.23f,.43f,1};s.Colors[ImGuiCol_TabSelected]={.23f,.18f,.34f,1};
    s.Colors[ImGuiCol_TabSelectedOverline]={.64f,.47f,.87f,1};s.Colors[ImGuiCol_MenuBarBg]={.075f,.083f,.11f,1};
    s.ScaleAllSizes(dpi);s.FontScaleDpi=dpi;
}
float UiScale(float pixels) {const auto& s=ImGui::GetStyle();return pixels*s.FontScaleDpi*s.FontScaleMain;}
void LoadUiFonts(const std::filesystem::path& directory) {
    auto& io=ImGui::GetIO();wchar_t windows[MAX_PATH]{};GetWindowsDirectoryW(windows,MAX_PATH);
    const auto regular=directory/L"Pretendard-Regular.otf",bold=directory/L"Pretendard-SemiBold.otf",fallback=std::filesystem::path(windows)/L"Fonts"/L"malgun.ttf";
    if(std::filesystem::exists(regular))UiFonts::regular=io.Fonts->AddFontFromFileTTF(regular.u8string().c_str(),16,nullptr,io.Fonts->GetGlyphRangesKorean());
    else if(std::filesystem::exists(fallback))UiFonts::regular=io.Fonts->AddFontFromFileTTF(fallback.u8string().c_str(),16,nullptr,io.Fonts->GetGlyphRangesKorean());
    else UiFonts::regular=io.Fonts->AddFontDefault();
    if(std::filesystem::exists(bold))UiFonts::semibold=io.Fonts->AddFontFromFileTTF(bold.u8string().c_str(),16,nullptr,io.Fonts->GetGlyphRangesKorean());
    else UiFonts::semibold=UiFonts::regular;
}
namespace {
ImU32 Hex(unsigned rgb,int alpha=255){return IM_COL32((rgb>>16)&255,(rgb>>8)&255,rgb&255,alpha);}
ImVec2 Cubic(ImVec2 a,ImVec2 b,ImVec2 c,ImVec2 d,float t) {
    const float u=1-t,w0=u*u*u,w1=3*u*u*t,w2=3*u*t*t,w3=t*t*t;
    return {a.x*w0+b.x*w1+c.x*w2+d.x*w3,a.y*w0+b.y*w1+c.y*w2+d.y*w3};
}
void Diamond(ImDrawList* draw,ImVec2 c,float r,float outline,ImU32 fill) {
    const float o=r+outline;
    draw->AddQuadFilled({c.x,c.y-o},{c.x+o,c.y},{c.x,c.y+o},{c.x-o,c.y},Hex(0x121530));
    draw->AddQuadFilled({c.x,c.y-r},{c.x+r,c.y},{c.x,c.y+r},{c.x-r,c.y},fill);
}
}
void ShimmerText(const char* text,const ImVec4& base,const ImVec4& highlight){
    auto* draw=ImGui::GetWindowDrawList();ImFont* font=ImGui::GetFont();const float size=ImGui::GetFontSize();
    const ImVec2 start=ImGui::GetCursorScreenPos(),extent=ImGui::CalcTextSize(text);
    const float band=UiScale(24),travel=extent.x+band*5,head=std::fmod(static_cast<float>(ImGui::GetTime())*UiScale(120),travel)-band*2;
    float x=start.x;
    for(const char* s=text;*s;){
        unsigned int code=0;const int bytes=ImTextCharFromUtf8(&code,s,nullptr);if(bytes<=0)break;
        const float width=font->CalcTextSizeA(size,FLT_MAX,0,s,s+bytes).x,d=(x+width*.5f-start.x-head)/band,glow=std::exp(-d*d);
        const ImVec4 color{base.x+(highlight.x-base.x)*glow,base.y+(highlight.y-base.y)*glow,base.z+(highlight.z-base.z)*glow,base.w+(highlight.w-base.w)*glow};
        draw->AddText(font,size,{x,start.y},ImGui::GetColorU32(color),s,s+bytes);x+=width;s+=bytes;
    }
    ImGui::Dummy(extent);
}
// Mirrors Assets/Icon/GenerateIcon.py: night tile, violet crescent, teal motion arc and keyframe diamonds.
// Shapes are traced clockwise because ImGui anti-aliases filled polygons on that side.
void DrawLunaFlowLogo(ImDrawList* draw,ImVec2 origin,float size) {
    const auto at=[&](float x,float y){return ImVec2{origin.x+x*size,origin.y+y*size};};
    int first=draw->VtxBuffer.Size;
    draw->AddRectFilled(origin,at(1,1),IM_COL32_WHITE,size*.22f);
    ImGui::ShadeVertsLinearColorGradientKeepAlpha(draw,first,draw->VtxBuffer.Size,origin,at(0,1),Hex(0x23194A),Hex(0x0E1222));
    for(int i=0;i<4;++i)draw->AddCircleFilled(at(.43f,.415f),size*(.40f-.06f*static_cast<float>(i)),Hex(0x7C55E6,10),40);
    draw->AddRect(origin,at(1,1),IM_COL32(255,255,255,18),size*.22f,0,std::max(size*.012f,1.0f));
    if(size>=64){
        const float stars[3][3]={{.70f,.12f,.040f},{.84f,.10f,.016f},{.58f,.21f,.014f}};
        for(const auto& star:stars){
            ImVec2 points[8];const ImVec2 c=at(star[0],star[1]);
            for(int i=0;i<8;++i){const float angle=IM_PI*.25f*static_cast<float>(i)-IM_PI*.5f,r=size*star[2]*(i%2?.32f:1.0f);points[i]={c.x+std::cos(angle)*r,c.y+std::sin(angle)*r};}
            draw->AddConcavePolyFilled(points,8,Hex(0xECE6FF));
        }
    }
    // Crescent: the outer disc minus the cutting disc, traced as a single outline.
    const ImVec2 c1=at(.430f,.415f),c2=at(.5325f,.3125f);const float r1=size*.290f,r2=size*.232f;
    const float dx=c2.x-c1.x,dy=c2.y-c1.y,d=std::max(std::sqrt(dx*dx+dy*dy),1e-4f);
    const float a=(r1*r1-r2*r2+d*d)/(2*d),h=std::sqrt(std::max(r1*r1-a*a,0.0f));
    const float phi=std::atan2(dy,dx),alpha=std::atan2(h,a),beta=std::atan2(h,a-d);
    std::vector<ImVec2> moon;constexpr int outerSteps=48,innerSteps=36;
    for(int i=0;i<=outerSteps;++i){const float t=phi+alpha+(2*IM_PI-2*alpha)*static_cast<float>(i)/outerSteps;moon.push_back({c1.x+std::cos(t)*r1,c1.y+std::sin(t)*r1});}
    for(int i=1;i<innerSteps;++i){const float t=phi-beta-(2*IM_PI-2*beta)*static_cast<float>(i)/innerSteps;moon.push_back({c2.x+std::cos(t)*r2,c2.y+std::sin(t)*r2});}
    first=draw->VtxBuffer.Size;draw->AddConcavePolyFilled(moon.data(),static_cast<int>(moon.size()),IM_COL32_WHITE);
    ImGui::ShadeVertsLinearColorGradientKeepAlpha(draw,first,draw->VtxBuffer.Size,at(.28f,.14f),at(.62f,.72f),Hex(0xCDB4FF),Hex(0x8A5CF0));
    // Motion arc: two cubic segments that widen toward the gold key and fade in from the tail.
    const ImVec2 b[7]={at(.054f,.596f),at(.087f,.766f),at(.300f,.843f),at(.526f,.766f),at(.753f,.690f),at(.905f,.491f),at(.864f,.324f)};
    constexpr int samples=40;ImVec2 centre[samples+1];
    for(int i=0;i<=samples;++i){const float t=2*static_cast<float>(i)/samples;const int s=t<1?0:1;centre[i]=Cubic(b[s*3],b[s*3+1],b[s*3+2],b[s*3+3],t-static_cast<float>(s));}
    std::vector<ImVec2> ribbon(2*(samples+1));
    for(int i=0;i<=samples;++i){
        const ImVec2 previous=centre[std::max(i-1,0)],next=centre[std::min(i+1,samples)];
        float nx=previous.y-next.y,ny=next.x-previous.x;const float length=std::max(std::sqrt(nx*nx+ny*ny),1e-6f);nx/=length;ny/=length;
        const float w=size*(.008f+.052f*static_cast<float>(i)/samples)*.5f;
        ribbon[static_cast<size_t>(i)]={centre[i].x-nx*w,centre[i].y-ny*w};ribbon[static_cast<size_t>(2*samples+1-i)]={centre[i].x+nx*w,centre[i].y+ny*w};
    }
    first=draw->VtxBuffer.Size;draw->AddConcavePolyFilled(ribbon.data(),static_cast<int>(ribbon.size()),IM_COL32_WHITE);
    const ImVec4 tail=ImGui::ColorConvertU32ToFloat4(Hex(0x5ED1B5)),head=ImGui::ColorConvertU32ToFloat4(Hex(0x84E3CB));
    for(int k=first;k<draw->VtxBuffer.Size;++k){
        auto& vertex=draw->VtxBuffer[k];int nearest=0;float best=FLT_MAX;
        for(int i=0;i<=samples;++i){const float ex=vertex.pos.x-centre[i].x,ey=vertex.pos.y-centre[i].y;if(ex*ex+ey*ey<best){best=ex*ex+ey*ey;nearest=i;}}
        const float t=static_cast<float>(nearest)/samples,fade=std::clamp(t/.35f,0.0f,1.0f);
        const float opacity=static_cast<float>((vertex.col>>IM_COL32_A_SHIFT)&0xFF)/255.0f*fade*fade*(3-2*fade);
        vertex.col=ImGui::ColorConvertFloat4ToU32({tail.x+(head.x-tail.x)*t,tail.y+(head.y-tail.y)*t,tail.z+(head.z-tail.z)*t,opacity});
    }
    const float outline=std::max(size*.009f,1.0f);
    if(size>=46){Diamond(draw,at(.454f,.786f),size*.038f,outline,Hex(0xB0EEDD));Diamond(draw,at(.743f,.637f),size*.038f,outline,Hex(0xB0EEDD));}
    Diamond(draw,at(.864f,.324f),std::max(size*.0475f,2.6f),outline,Hex(0xFFD07C));
}
}

#include "App/UiTheme.h"
#include "App/UiFonts.h"
#include <windows.h>

namespace LunaFlow {
void ConfigureUiTheme(float dpi) {
    ImGui::StyleColorsDark();auto& s=ImGui::GetStyle();
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
void LoadUiFonts(const std::filesystem::path& directory) {
    auto& io=ImGui::GetIO();wchar_t windows[MAX_PATH]{};GetWindowsDirectoryW(windows,MAX_PATH);
    const auto regular=directory/L"Pretendard-Regular.otf",bold=directory/L"Pretendard-SemiBold.otf",fallback=std::filesystem::path(windows)/L"Fonts"/L"malgun.ttf";
    if(std::filesystem::exists(regular))UiFonts::regular=io.Fonts->AddFontFromFileTTF(regular.u8string().c_str(),16,nullptr,io.Fonts->GetGlyphRangesKorean());
    else if(std::filesystem::exists(fallback))UiFonts::regular=io.Fonts->AddFontFromFileTTF(fallback.u8string().c_str(),16,nullptr,io.Fonts->GetGlyphRangesKorean());
    else UiFonts::regular=io.Fonts->AddFontDefault();
    if(std::filesystem::exists(bold))UiFonts::semibold=io.Fonts->AddFontFromFileTTF(bold.u8string().c_str(),16,nullptr,io.Fonts->GetGlyphRangesKorean());
    else UiFonts::semibold=UiFonts::regular;
}
}

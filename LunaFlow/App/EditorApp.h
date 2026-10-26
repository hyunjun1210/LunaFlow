#pragma once
#include "Rendering/RendererDX11.h"
#include <windows.h>
#include <functional>

namespace LunaFlow {
class EditorApp {
public:
    EditorApp(HWND,ID3D11Device*,ID3D11DeviceContext*,const std::filesystem::path& shaders);
    void Frame(float deltaTime);
    void LoadModel(const std::filesystem::path&);
    void Screenshot(const std::filesystem::path& path) const { renderer_.SaveScreenshot(path); }
private:
    void UseModel(ModelAsset);
    void Toolbar(); void Library(); void Inspector(); void Viewport(float height); void Timeline(float height);
    void Try(const std::function<void()>&);
    std::filesystem::path Dialog(bool save,const wchar_t* filter,const wchar_t* extension,const wchar_t* filename=L"");
    AnimationClip& Clip() { return model_.clips.at(static_cast<size_t>(selectedClip_)); }
    void CommitPose();
    HWND window_;
    RendererDX11 renderer_;
    PreviewCamera camera_;
    RenderOptions options_;
    ModelAsset model_;
    Pose pose_;
    int selectedClip_=0,selectedBone_=0;
    double time_=0;
    bool playing_=true,loop_=true,bindPose_=false,dirty_=false;
    bool orbiting_=false,panning_=false;
    float unitsToMeters_=1;
    Transform edit_;
    XMFLOAT3 editAngles_{};
    int editBone_=-1,editClip_=-1;
    double editTime_=-1;
    std::string status_="Ready. Try Wave, move the timeline, or open your character.";
    bool error_=false;
};
}

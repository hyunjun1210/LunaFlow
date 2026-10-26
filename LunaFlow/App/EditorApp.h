#pragma once
#include "Rendering/RendererDX11.h"
#include "Rendering/ViewportInteraction.h"
#include "Core/Primitives.h"
#include "App/EditorSettings.h"
#include "Core/ProjectIO.h"
#include "Core/EditorCommands.h"
#include "Core/SceneExporter.h"
#include "Core/SceneAssembly.h"
#include "AgentBridge/AgentSession.h"
#include <windows.h>
#include <functional>
#include <memory>

namespace LunaFlow {
class EditorApp {
public:
    EditorApp(HWND,ID3D11Device*,ID3D11DeviceContext*,const std::filesystem::path& shaders,const std::filesystem::path& preferences={});
    void Frame(float deltaTime);
    void LoadModel(const std::filesystem::path&);
    void Screenshot(const std::filesystem::path& path) const { renderer_.SaveScreenshot(path); }
    bool Vsync() const { return settings_.vsync; }
    int FrameLimit() const { return settings_.frameLimit; }
    void SetLanguage(bool korean) { settings_.korean=korean; }
    void RequestClose() { closeRequested_=true; }
    bool WantQuit() const { return quitRequested_; }
private:
    enum class Tool { Move, Rotate, Scale };
    enum class LibraryTab { Hierarchy, Motions, Asset, Effects };
    struct Message { bool user=false;std::string text,target; };
    struct UndoState { int clip=0;AnimationClip value;std::shared_ptr<ModelAsset> scene; };
    const char* T(const char* ko,const char* en) const {return settings_.korean?ko:en;}
    const char* ClipLabel(const AnimationClip&) const;
    const char* ShapeLabel(Primitive) const;
    void UseModel(ModelAsset);
    void AddShape(Primitive);
    void OpenCharacter();void ImportMotion();void SaveMotion();void ExportBake();void ExportSkeleton();void ExportImage();
    void OpenProject();bool SaveCurrentProject(bool saveAs=false);void Dispatch(const std::string&,const Json& args=Json::object());
    void ExportCharacter();
    void ImportObject();void AddSceneEffect(EffectType);void SelectEffect(int);void ExportUnity();void EffectInspector();
    void SelectWorkspace(int);
    void MenuBar();void Workspaces();void Library();void Inspector();void Chat(float height);
    void Viewport(float height);void Timeline(float height);void Settings();
    void SendPrompt();void SavePreferences();
    void RequestSceneChange(std::function<void()>);void PendingChanges();
    void FrameAll();
    void Try(const std::function<void()>&);
    std::filesystem::path Dialog(bool save,const wchar_t* filter,const wchar_t* extension,const wchar_t* filename=L"");
    AnimationClip& Clip() { return model_.clips.at(static_cast<size_t>(selectedClip_)); }
    void CommitPose(bool updateQuaternion=true);
    void RememberEdit();void Undo();void DuplicateClip();void SetTime(double);
    void ManipulateViewport(XMFLOAT2 origin,XMFLOAT2 size,bool hovered);
    void DrawGizmo(XMFLOAT2 origin,XMFLOAT2 size);
    void StartJointDrag(XMFLOAT2 mouse,int axis);
    HWND window_;
    RendererDX11 renderer_;
    PreviewCamera camera_;
    RenderOptions options_;
    EditorSettings settings_;
    std::filesystem::path settingsPath_;
    std::filesystem::path projectPath_;
    ModelAsset model_;
    Pose pose_;
    int selectedClip_=0,selectedBone_=0,workspace_=1;
    LibraryTab libraryTab_=LibraryTab::Motions;
    bool selectWorkspaceTab_=false,selectLibraryTab_=true;
    double time_=0;
    bool playing_=false,loop_=true,bindPose_=false,dirty_=false;
    bool orbiting_=false,panning_=false,draggingBone_=false,dragRecorded_=false;
    bool showSettings_=false,showChat_=true,showInspector_=true,chatScroll_=false;
    bool revealSelected_=true;
    bool gizmoWasUsing_=false,worldSpace_=false;
    bool closeRequested_=false,quitRequested_=false;
    std::function<void()> pendingSceneChange_;
    int selectedEffect_=-1,attachmentTarget_=0;
    Tool tool_=Tool::Rotate;
    float unitsToMeters_=1;
    Transform edit_,dragStart_;
    XMFLOAT3 editAngles_{},dragWorldOrigin_{};
    XMFLOAT2 dragMouse_{};
    XMFLOAT4X4 dragParent_{};
    int dragAxis_=-1,editBone_=-1,editClip_=-1,primitiveCount_=0;
    double editTime_=-1;
    std::vector<UndoState> undo_;
    std::vector<Message> messages_;
    AgentSession agent_;
    char prompt_[4096]{};
    char clipName_[256]{};
    int nameClip_=-1;
    double durationDraft_=3,selectedKeyTime_=-1,keyTimeDraft_=0;
    std::string status_;
    bool error_=false;
};
}

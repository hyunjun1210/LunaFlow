#pragma once
#include "Rendering/RendererDX11.h"
#include "Rendering/ViewportInteraction.h"
#include "Core/Primitives.h"
#include "App/EditorSettings.h"
#include "Core/ProjectIO.h"
#include "Core/EditorCommands.h"
#include "Core/SceneExporter.h"
#include "Core/SceneAssembly.h"
#include "Core/Particles.h"
#include "AgentBridge/AgentSession.h"
#include "AgentBridge/EditPlan.h"
#include "AgentBridge/CopilotProcess.h"
#include "Core/MotionRetarget.h"
#include "Core/AutoRig.h"
#include "VfxAgent.h"
#include "HelpBot.h"
#include "TrackerProcess.h"
#include <windows.h>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace LunaFlow {
class EditorApp {
    friend struct EditorAppTestAccess;
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
    using Message=VfxAgentMessage;
    struct UndoState { int clip=0;AnimationClip value;std::shared_ptr<ModelAsset> scene;std::shared_ptr<std::vector<EffectData>> effects; };
    const char* T(const char* ko,const char* en) const {return settings_.korean?ko:en;}
    const char* ClipLabel(const AnimationClip&) const;
    const char* ShapeLabel(Primitive) const;
    void UseModel(ModelAsset);void LoadDefaultCharacter();
    void AddShape(Primitive);
    void OpenCharacter();void ImportMotion();void SaveMotion();void ExportBake();void ExportSkeleton();void ExportImage();
    void OpenProject();bool SaveCurrentProject(bool saveAs=false);void Dispatch(const std::string&,const Json& args=Json::object());
    void ExportCharacter();
    void ImportObject();void AddSceneEffect(EffectType);void SelectEffect(int);void ExportUnity();void EffectInspector();
    void EffectLibrary();void AddParticlePreset();void GenerateVfx(const std::string&);void RememberEffectEdit();
    const char* EffectTypeLabel(EffectType) const;
    void SelectWorkspace(int);
    void MenuBar();void Workspaces();void Library();void Inspector();void Chat(float height);
    void Viewport(float height);void Timeline(float height);void Settings();
    float TimelineMinimumHeight(float width) const;
    void SendPrompt();void SavePreferences();
    bool SendCopilot(const std::string&,bool vfx=false);void PumpCopilot(float);void ExplainPlanError();void CancelCopilot();void CopilotProgress();
    void AddCopilotMessage(bool user,const std::string& text,const std::string& target);
    void ChatMessages(const std::vector<Message>&,const char* assistant);
    void VfxAgentChat(float);bool VfxAgentAvailable() const;bool VfxAgentSignInFree() const;
    // In-app "LunaFlow 도우미": Copilot Studio help agent with the bundled manual as offline fallback.
    void HelpBotWindow();void ToggleHelpBot();void NewHelpConversation();void PumpHelpBot(float deltaTime);
    bool AskHelpBot(const std::string&);void HandleHelpEvent(const Json&);void StopHelpBot();
    void AddLocalHelpAnswer(const std::string& question,const std::string& note={});
    Json HelpAppContext() const;bool HelpBotAvailable() const;
    void MocapPanel();void StartMocap();void StopMocap();void PumpMocap(float deltaTime=0);
    // One-button webcam capture: open turns the camera on, record arms a countdown, each take is reviewed in place.
    void OpenMocap();bool MocapReady();void ArmMocapRecording();void UpdateMocapRecording(double now);
    void AddMocapTake(AnimationClip,bool review);void FinishMocapReview();void DiscardMocapTake();void CloseMocap();
    void MocapShortcuts();void MocapOverlay(XMFLOAT2 origin,XMFLOAT2 size);
    void NoteCopilotActivity(const std::string&);void AddCopilotReply(std::string);
    void FbxTools();void ImportFbxMotion();void ProportionReportPanel();
    void AutoRigCharacter();void AutoRigNotice();
    void RequestSceneChange(std::function<void()>);void PendingChanges();
    void FrameAll();void FrameSelected();void ResetView();
    // Unity/Blender-style navigation: animated framing, axis views and keyboard shortcuts.
    enum ViewAxis { ViewFront, ViewBack, ViewRight, ViewLeft, ViewTop, ViewBottom };
    void SnapView(int axis);void ToggleOrthographic();void OrbitStep(float yaw,float pitch);
    void AnimateCamera(const PreviewCamera& to);void UpdateCameraTween(float deltaTime);void StopCameraTween();
    void LeaveAxisView(PreviewCamera&);void ViewOpposite();PreviewCamera CameraGoal() const;
    bool OrientationGizmo(XMFLOAT2 origin,XMFLOAT2 size,bool hovered);
    int OrientationGizmoHit(XMFLOAT2 origin,XMFLOAT2 size,XMFLOAT2 mouse) const;
    void ViewportOverlay(XMFLOAT2 origin,XMFLOAT2 size,bool hovered);
    std::string ViewName() const;
    void Shortcuts();void ShortcutHelp();
    void StepFrame(int frames);void JumpKey(int direction);void InsertKey();void DeleteSelectedKey();
    void DuplicateSelection();void DuplicateEffect();void DeleteEffect();
    std::vector<double> KeyTimes() const;
    void Try(const std::function<void()>&);
    std::filesystem::path Dialog(bool save,const wchar_t* filter,const wchar_t* extension,const wchar_t* filename=L"");
    void ReleaseDialogInput();
    AnimationClip& Clip() { return model_.clips.at(static_cast<size_t>(selectedClip_)); }
    void CommitPose(bool updateQuaternion=true);
    void RememberEdit();void Undo();void Redo();void DuplicateClip();void SetTime(double);
    void PushUndo(UndoState,bool coalesceDrag=false);void ClearHistory();void StepHistory(bool redo);
    static void TrimHistory(std::vector<UndoState>&);
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
    bool orbiting_=false,panning_=false,draggingBone_=false,dragRecorded_=false,zooming_=false;
    int orbitButton_=1;
    int viewAxis_=-1;
    bool autoOrtho_=false,showShortcuts_=false;
    PreviewCamera tweenFrom_,tweenTo_;
    float tween_=1,flySpeedNotice_=0;
    unsigned undoDragId_=0;
    bool navGizmoHot_=false,navGizmoDragged_=false,keyboardNav_=false;
    int navGizmoPressed_=-1,navGizmoHoverIndex_=-1;
    float navGizmoHoverTime_=0;
    XMFLOAT2 navGizmoPressPos_{},viewportOrigin_{},viewportSize_{};
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
    XMFLOAT2 dragMouse_{},dragDelta_{};
    XMFLOAT4X4 dragParent_{};
    int dragAxis_=-1,editBone_=-1,editClip_=-1,primitiveCount_=0;
    double editTime_=-1;
    std::vector<UndoState> undo_,redo_;
    std::vector<Message> messages_;
    AgentSession agent_;
    CopilotProcess copilot_;
    EditPlan editPlan_;
    Json copilotRequest_;
    bool copilotActive_=false,copilotUndoCaptured_=false;
    float copilotStepTime_=0,copilotElapsed_=0,copilotThought_=-1;
    std::vector<std::string> copilotActivity_;
    bool copilotVfx_=false;
    VfxAgentState vfxAgent_;
    HelpBotState helpBot_;
    CopilotProcess helpProcess_;
    Mocap::EditorState mocap_;
    bool showFbxImport_=false,showRetarget_=false,hasProportionReport_=false;
    std::shared_ptr<ModelAsset> motionSource_;
    int sourceMotion_=0;
    ProportionSettings proportionSettings_;
    ProportionReport proportionReport_;
    std::string proportionReportClipId_;
    uint64_t proportionReportRevision_=0;
    char prompt_[4096]{};
    int particlePreset_=0;
    int vfxMode_=-1; // -1 = choose on first draw, 0 = Copilot agent, 1 = local rules
    bool vfxAgentReady_=false,vfxSignInFree_=false;
    float vfxGalleryHeight_=0;
    char effectBoneFilter_[64]{};
    char clipName_[256]{};
    int nameClip_=-1;
    double durationDraft_=3,selectedKeyTime_=-1,keyTimeDraft_=0;
    std::string status_;
    bool error_=false;
};
}

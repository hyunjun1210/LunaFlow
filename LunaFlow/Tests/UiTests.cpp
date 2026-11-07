#include "App/EditorApp.h"
#include "App/UiTheme.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_impl_dx11.h"
#include "ImGuizmo.h"
#include "ViewportUxTests.h"
#include <cstring>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace LunaFlow;
namespace LunaFlow {
struct EditorAppTestAccess {
    static Mocap::EditorState& MocapState(EditorApp& app){return app.mocap_;}
    static void DrawMocap(EditorApp& app,XMFLOAT2 size){app.MocapOverlay({0,0},size);app.MocapPanel();}
    static void AddTake(EditorApp& app,AnimationClip clip){app.AddMocapTake(std::move(clip),true);}
    static void DiscardTake(EditorApp& app){app.DiscardMocapTake();}
    static void UseDemo(EditorApp& app){app.UseModel(MakeDemoScene());}
    static void UseRawImport(EditorApp& app){auto model=ImportScene(app.model_.source);RemoveReferenceImagePlanes(model);app.UseModel(std::move(model));}
    static int SelectedBone(const EditorApp& app){return app.selectedBone_;}
    static bool Error(const EditorApp& app){return app.error_;}
    static void Redo(EditorApp& app){app.Redo();}
    static const ModelAsset& Model(const EditorApp& app){return app.model_;}
    static void BeginThinking(EditorApp& app){app.editPlan_=EditPlan{};app.copilotActive_=true;app.copilotElapsed_=0;app.copilotThought_=-1;app.status_="Copilot Studio에 요청을 전송했습니다.";app.copilotActivity_.clear();app.NoteCopilotActivity(app.status_);}
    static void NoteAi(EditorApp& app,const char* stage){app.status_=stage;app.NoteCopilotActivity(stage);}
    static size_t AiActivity(const EditorApp& app){return app.copilotActivity_.size();}
    static void ReplyAi(EditorApp& app,const char* text){app.AddCopilotReply(text);app.copilotActive_=false;}
    static float LastThought(const EditorApp& app){return app.messages_.empty()?-1:app.messages_.back().thought;}
    static size_t LastActivity(const EditorApp& app){return app.messages_.empty()?0:app.messages_.back().activity.size();}
    static bool AiActive(const EditorApp& app){return app.copilotActive_;}
    static float AiElapsed(const EditorApp& app){return app.copilotElapsed_;}
    static void CancelAi(EditorApp& app){app.CancelCopilot();}
    static const std::vector<EffectData>& Effects(const EditorApp& app){return app.model_.effects;}
    static void PauseAt(EditorApp& app,double time){app.playing_=false;app.SetTime(time);}
    static void SetPrompt(EditorApp& app,const char* text){strncpy_s(app.vfxAgent_.prompt.data(),app.vfxAgent_.prompt.size(),text,_TRUNCATE);app.vfxMode_=1;}
    static size_t VfxMessages(const EditorApp& app){return app.vfxAgent_.messages.size();}
    static std::string VfxPrompt(const EditorApp& app){return app.vfxAgent_.prompt.data();}
    static bool ChatVisible(const EditorApp& app){return app.showChat_;}
    static void SelectHand(EditorApp& app){const auto& bones=app.model_.skeleton.bones;auto hand=std::find_if(bones.begin(),bones.end(),[](const auto& bone){return bone.name=="RightHand";});if(hand!=bones.end())app.selectedBone_=static_cast<int>(hand-bones.begin());}
    static void Undo(EditorApp& app){app.Undo();}
    static void Workspace(EditorApp& app,int mode){app.SelectWorkspace(mode);}
    static void OpenUnsaved(EditorApp& app){app.dirty_=true;app.RequestClose();}
    static bool Dirty(const EditorApp& app){return app.dirty_;}
    static void PrepareSave(EditorApp& app,const std::filesystem::path& path){app.projectPath_=path;app.dirty_=true;}
    static void MarkDirty(EditorApp& app){app.dirty_=true;}
    static void ReleaseDialogInput(EditorApp& app){app.ReleaseDialogInput();}
    static void OpenFbx(EditorApp& app){app.showFbxImport_=true;}
    static double Duration(const EditorApp& app){return PlaybackDuration(app.model_.clips.at(static_cast<size_t>(app.selectedClip_)));}
    static void BeginAi(EditorApp& app){
        SelectHand(app);auto request=EditSceneContext(app.model_,app.selectedClip_,app.selectedBone_,app.selectedEffect_,app.time_,app.agent_.Draft(app.model_,app.Clip(),"UI integration"));
        auto step=[](const char* id,const char* op,const char* target,const Json& args){return Json{{"stepId",id},{"operation",op},{"targetRef",target},{"description",std::string(op)=="retime_motion"?"선택한 동작의 길이를 1.5초로 변경합니다.":"선택한 오른손에 불꽃 파티클을 생성합니다."},{"resultRef",""},{"argumentsJson",args.dump()}};};
        Json plan={{"format","lunaflow.edit.plan"},{"version",1},{"requestId",request["requestId"]},{"sessionId",request["sessionId"]},{"summary","UI integration"},{"steps",Json::array({
            step("s1","retime_motion","selectedClip",{{"duration",1.5}}),step("s2","create_particles","selectedBone",{{"preset","fire"}})})}};
        app.editPlan_.Begin(app.model_,request,plan);app.copilotRequest_=request;app.copilotActive_=true;app.copilotUndoCaptured_=false;app.copilotStepTime_=0;
    }
    static size_t Completed(const EditorApp& app){return app.editPlan_.Completed();}
    static void NextAi(EditorApp& app){app.copilotStepTime_=0;}
    static void OpenAdaptation(EditorApp& app){app.motionSource_=std::make_shared<ModelAsset>(MakeDemoScene());app.motionSource_->source="SampleMotion.fbx";app.sourceMotion_=0;app.showRetarget_=true;}
    static size_t Clips(const EditorApp& app){return app.model_.clips.size();}
    static bool Adapted(const EditorApp& app){return app.hasProportionReport_&&app.proportionReport_.mappedBones>0;}
    static const PreviewCamera& Camera(const EditorApp& app){return app.camera_;}
    static int ViewAxis(const EditorApp& app){return app.viewAxis_;}
    static std::string ViewName(const EditorApp& app){return app.ViewName();}
    static int GizmoHit(const EditorApp& app,ImVec2 mouse){return app.OrientationGizmoHit(app.viewportOrigin_,app.viewportSize_,{mouse.x,mouse.y});}
    static ImRect ViewportRect(const EditorApp& app){return {{app.viewportOrigin_.x,app.viewportOrigin_.y},{app.viewportOrigin_.x+app.viewportSize_.x,app.viewportOrigin_.y+app.viewportSize_.y}};}
    static XMFLOAT3 BonePosition(const EditorApp& app){const auto& g=app.pose_.global.at(static_cast<size_t>(app.selectedBone_));return {g._41,g._42,g._43};}
    static double Time(const EditorApp& app){return app.time_;}
    static int Fps(const EditorApp& app){return std::max(app.settings_.timelineFps,1);}
    static bool Playing(const EditorApp& app){return app.playing_;}
    static int ToolIndex(const EditorApp& app){return static_cast<int>(app.tool_);}
    static double SelectedKey(const EditorApp& app){return app.selectedKeyTime_;}
    static void SelectKey(EditorApp& app,double time){app.selectedKeyTime_=time;}
    static size_t BoneKeys(const EditorApp& app){
        size_t count=0;for(const auto& track:app.model_.clips.at(static_cast<size_t>(app.selectedClip_)).tracks)if(static_cast<int>(track.bone)==app.selectedBone_)count+=track.rotations.size();return count;
    }
};
}
static void Check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
static ImGuiTabBar* FindTabBar(const char* suffix) {
    auto& bars=ImGui::GetCurrentContext()->TabBars;
    for(int i=0;i<bars.GetMapSize();++i)if(auto* bar=bars.TryGetMapData(i))
        for(auto& tab:bar->Tabs)if(std::strstr(ImGui::TabBarGetTabName(bar,&tab),suffix))return bar;
    throw std::runtime_error(std::string("Missing tab bar: ")+suffix);
}
static ImGuiTabItem& FindTab(const char* suffix) {
    auto* bar=FindTabBar(suffix);
    for(auto& tab:bar->Tabs)if(std::strstr(ImGui::TabBarGetTabName(bar,&tab),suffix))return tab;
    throw std::runtime_error("Missing tab");
}
static bool TabSelected(const char* suffix){return FindTabBar(suffix)->SelectedTabId==FindTab(suffix).ID;}
// Front-facing arms-down pose (Luna space, unmirrored image coordinates), as in the mocap unit fixture.
static Mocap::Frame MocapFrame(double time,uint64_t sequence) {
    Mocap::Frame f;f.time=time;f.sequence=sequence;f.detected=true;
    for(auto& p:f.landmarks)p={{0,0,0},1,1,.5f,.5f};
    auto set=[&](int i,float x,float y){f.landmarks[static_cast<size_t>(i)]={{x,-y,0},1,1,.5f+x*.3f,.9f-y*.4f};};
    set(0,0,1.75f);set(11,.35f,1.5f);set(12,-.35f,1.5f);set(13,.35f,1.15f);set(14,-.35f,1.15f);set(15,.35f,.8f);set(16,-.35f,.8f);
    set(23,.18f,1.f);set(24,-.18f,1.f);set(25,.18f,.55f);set(26,-.18f,.55f);set(27,.18f,.1f);set(28,-.18f,.1f);
    return f;
}
int main() {
    std::cout<<std::unitbuf;
    const HRESULT com=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);int code=0;
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;D3D_FEATURE_LEVEL feature;
    bool backend=false;
    try {
        Check(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,&feature,&context)),"Create offscreen UI device");
        ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.DeltaTime=1.0f/60;
        ConfigureUiTheme();LoadUiFonts(std::filesystem::path(LUNAFLOW_SOURCE_DIR)/"Assets"/"Fonts");
        backend=ImGui_ImplDX11_Init(device.Get(),context.Get());Check(backend,"Initialize offscreen UI renderer");
        D3D11_TEXTURE2D_DESC desc{};desc.Width=1440;desc.Height=900;desc.MipLevels=desc.ArraySize=1;desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.SampleDesc.Count=1;desc.BindFlags=D3D11_BIND_RENDER_TARGET;
        ComPtr<ID3D11Texture2D> target;Check(SUCCEEDED(device->CreateTexture2D(&desc,nullptr,&target)),"Create UI target");ComPtr<ID3D11RenderTargetView> rtv;device->CreateRenderTargetView(target.Get(),nullptr,&rtv);
        {
            const auto preferences=std::filesystem::current_path()/"test-output"/"ui-preferences.json";
            const bool hadPreferences=std::filesystem::exists(preferences);const auto previousWrite=hadPreferences?std::filesystem::last_write_time(preferences):std::filesystem::file_time_type{};
            EditorApp app(nullptr,device.Get(),context.Get(),std::filesystem::path(LUNAFLOW_SOURCE_DIR)/"Rendering"/"Shaders",preferences);
            Check(EditorAppTestAccess::Model(app).source.filename()=="DefaultCharacter.fbx","Startup opens the bundled default character");
            Check(!EditorAppTestAccess::Model(app).meshes.empty(),"Default character has renderable geometry");
            ImVec2 mouse{4,70};int frameWidth=1440,frameHeight=900;
            auto frame=[&](int width,int height,bool wheel,bool stable=true){
                frameWidth=width;frameHeight=height;
                if(desc.Width!=static_cast<UINT>(width)||desc.Height!=static_cast<UINT>(height)){
                    context->OMSetRenderTargets(0,nullptr,nullptr);rtv.Reset();target.Reset();desc.Width=width;desc.Height=height;
                    Check(SUCCEEDED(device->CreateTexture2D(&desc,nullptr,&target)),"Resize offscreen UI target");Check(SUCCEEDED(device->CreateRenderTargetView(target.Get(),nullptr,&rtv)),"Resize UI target view");
                }
                io.DisplaySize={static_cast<float>(width),static_cast<float>(height)};io.DeltaTime=1.0f/60;
                io.AddMousePosEvent(mouse.x,mouse.y);if(wheel)io.AddMouseWheelEvent(0,-8);
                ImGui_ImplDX11_NewFrame();ImGui::NewFrame();ImGuizmo::BeginFrame();app.Frame(io.DeltaTime);ImGui::Render();
                auto* root=ImGui::FindWindowByName("LunaFlow");Check(root!=nullptr,"Main UI exists");
                Check((root->Flags&ImGuiWindowFlags_NoScrollWithMouse)!=0,"Workspace consumes no wheel scrolling");
                if(stable&&root->ScrollMax.y>.5f){std::cerr<<"Root scroll extent "<<root->ScrollMax.y<<" at "<<width<<"x"<<height<<"\n";throw std::runtime_error("Workspace exceeds the window height");}
                Check(root->Scroll.y==0,"Root workspace stays pinned while wheel is used");
                if(stable){
                    for(auto* child:ImGui::GetCurrentContext()->Windows)if(child->Active&&std::strstr(child->Name,"/timeline_")){
                        if(child->DC.CursorMaxPos.y>child->InnerRect.Max.y+.5f||child->DC.CursorMaxPos.x>child->InnerRect.Max.x+.5f){std::cerr<<"Timeline clips at "<<width<<"x"<<height<<" DPI "<<ImGui::GetStyle().FontScaleDpi<<" content bottom "<<child->DC.CursorMaxPos.y<<" visible "<<child->InnerRect.Max.y<<" width "<<child->DC.CursorMaxPos.x<<" visible "<<child->InnerRect.Max.x<<" scroll "<<child->Scroll.y<<"\n";throw std::runtime_error("Timeline controls exceed their visible region");}
                    }
                    if(auto* table=ImGui::TableFindByID(ImHashStr("workspace",0,root->ID))){
                        float sum=0;for(int i=0;i<3;++i)sum+=table->Columns[i].WidthGiven;
                        Check(sum>0&&std::abs(table->Columns[1].WidthGiven/sum-.56f)<.04f,"Viewport retains its share of the workspace width");
                    }
                }
                ID3D11RenderTargetView* view=rtv.Get();context->OMSetRenderTargets(1,&view,nullptr);const float clear[]={.043f,.051f,.075f,1};context->ClearRenderTargetView(view,clear);ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
            };
            auto tick=[&]{frame(frameWidth,frameHeight,false);};
            auto clickTab=[&](const char* suffix){
                auto* bar=FindTabBar(suffix);const auto& tab=FindTab(suffix);
                mouse={bar->BarRect.Min.x+tab.Offset-bar->ScrollingAnim+tab.Width*.5f,(bar->BarRect.Min.y+bar->BarRect.Max.y)*.5f};
                tick();io.AddMouseButtonEvent(0,true);tick();io.AddMouseButtonEvent(0,false);tick();tick();
                if(!TabSelected(suffix)){
                    std::cerr<<"Click "<<mouse.x<<","<<mouse.y<<" bar "<<bar->BarRect.Min.x<<","<<bar->BarRect.Min.y<<" selected "<<bar->SelectedTabId<<"\n";
                    for(auto& item:bar->Tabs)std::cerr<<ImGui::TabBarGetTabName(bar,&item)<<" offset="<<item.Offset<<" width="<<item.Width<<" id="<<item.ID<<"\n";
                    throw std::runtime_error(std::string("Clicked tab remains selected: ")+suffix+" at "+std::to_string(frameWidth)+"x"+std::to_string(frameHeight));
                }
                mouse={4,70};tick();
            };
            auto saveImage=[&](const char* filename){
                context->OMSetRenderTargets(0,nullptr,nullptr);auto stagingDesc=desc;stagingDesc.BindFlags=0;stagingDesc.Usage=D3D11_USAGE_STAGING;stagingDesc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
                ComPtr<ID3D11Texture2D> staging;Check(SUCCEEDED(device->CreateTexture2D(&stagingDesc,nullptr,&staging)),"Create UI readback");context->CopyResource(staging.Get(),target.Get());D3D11_MAPPED_SUBRESOURCE map{};
                Check(SUCCEEDED(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&map)),"Read UI image");
                auto folder=std::filesystem::current_path()/"test-output";std::filesystem::create_directories(folder);std::ofstream image(folder/filename,std::ios::binary);
                for(unsigned y=0;y<desc.Height;++y)image.write(static_cast<const char*>(map.pData)+static_cast<size_t>(y)*map.RowPitch,desc.Width*4);context->Unmap(staging.Get(),0);
            };
            for(int i=0;i<3;++i)tick();saveImage("ui-default-character.rgba");
            {
                const auto& bones=EditorAppTestAccess::Model(app).skeleton.bones;
                Check(!NeedsAutoRig(EditorAppTestAccess::Model(app))&&std::any_of(bones.begin(),bones.end(),[](const auto& bone){return bone.name=="RightHand";}),"Startup auto-rigs the unskinned default character");
                auto notice=[]{ImGuiWindow* found=nullptr;for(auto* window:ImGui::GetCurrentContext()->Windows)if(window->Active&&std::strstr(window->Name,"auto-rig-notice"))found=window;return found;};
                Check(notice()==nullptr,"Rigged characters show no auto-rig prompt");
                // A raw unskinned import gets a Unity-style help box offering the fix; it is one undoable scene edit.
                EditorAppTestAccess::UseRawImport(app);tick();tick();
                const auto meshes=EditorAppTestAccess::Model(app).meshes.size();
                Check(NeedsAutoRig(EditorAppTestAccess::Model(app))&&notice()!=nullptr,"Library offers auto-rig for an unrigged character");
                ImGui::ActivateItemByID(ImHashStr("###AutoRig",0,notice()->ID));tick();tick();tick();
                Check(!NeedsAutoRig(EditorAppTestAccess::Model(app))&&!EditorAppTestAccess::Error(app),"Auto rig button rigs the character");
                Check(EditorAppTestAccess::Model(app).skeleton.bones.at(static_cast<size_t>(EditorAppTestAccess::SelectedBone(app))).name=="Hips"&&TabSelected("###Hierarchy"),"Auto rig reveals the new skeleton with its root selected");
                Check(EditorAppTestAccess::Model(app).meshes.size()==meshes&&EditorAppTestAccess::Dirty(app)&&notice()==nullptr,"Auto rig keeps every mesh, marks the scene modified and dismisses the prompt");
                saveImage("ui-auto-rig.rgba");
                EditorAppTestAccess::Undo(app);tick();
                Check(NeedsAutoRig(EditorAppTestAccess::Model(app))&&notice()!=nullptr,"Undo restores the unrigged mesh");
                EditorAppTestAccess::Redo(app);tick();
                Check(!NeedsAutoRig(EditorAppTestAccess::Model(app))&&notice()==nullptr,"Redo re-applies the auto rig");
            }
            EditorAppTestAccess::UseDemo(app);
            for(bool korean:{true,false}){
                app.SetLanguage(korean);
                for(const auto size:{ImVec2{1440,900},ImVec2{1000,700},ImVec2{1920,1080}}){
                    for(int i=0;i<3;++i)frame(static_cast<int>(size.x),static_cast<int>(size.y),i>0,i==2);
                    clickTab("###VFX");Check(TabSelected("###Effects"),"VFX workspace opens the effects list");
                    clickTab("###Hierarchy");Check(TabSelected("###VFX"),"Library navigation leaves the workspace unchanged");
                    clickTab("###VFX");Check(TabSelected("###Effects"),"Clicking the active workspace restores its tools");
                    clickTab("###Scene");Check(TabSelected("###Hierarchy"),"Scene workspace opens the hierarchy");
                    clickTab("###Asset");clickTab("###Animation");Check(TabSelected("###Motions"),"Animation workspace opens motions");
                }
            }
            app.SetLanguage(true);for(int i=0;i<3;++i)frame(1440,900,false,i==2);
            clickTab("###Hierarchy");saveImage("ui-preview.rgba");
            clickTab("###Effects");
            ImGui::ActivateItemByID(ImHashStr("###AddParticles",0,FindTab("###Effects").ID));tick();tick();tick();
            Check(TabSelected("###VFX")&&TabSelected("###Effects"),"Adding an effect opens the VFX workspace and effects list");
            Check(EditorAppTestAccess::Effects(app).size()==1&&EditorAppTestAccess::Effects(app)[0].type==EffectType::Particles,"Manual creation adds an editable particle system");
            EditorAppTestAccess::SelectHand(app);
            // Local generation lives in the unified VFX AI panel and replies in its conversation.
            auto vfxItem=[&](const char* id){
                ImGuiWindow* panel=nullptr;
                for(auto* window:ImGui::GetCurrentContext()->Windows)if(const char* leaf=std::strrchr(window->Name,'/');window->Active&&leaf&&std::strncmp(leaf,"/vfx-agent_",11)==0)panel=window;
                Check(panel!=nullptr,"VFX workspace shows the VFX AI panel");return ImHashStr(id,0,panel->ID);
            };
            const size_t vfxMessages=EditorAppTestAccess::VfxMessages(app);
            EditorAppTestAccess::SetPrompt(app,"파란 마법 파티클, 초당 80개, 수명 1.5초");tick();
            ImGui::ActivateItemByID(vfxItem("###GenerateVfx"));tick();tick();tick();
            Check(EditorAppTestAccess::Effects(app).size()==2&&EditorAppTestAccess::Effects(app).back().particles.rate==80,"Prompt button creates real particle data");
            Check(EditorAppTestAccess::VfxPrompt(app).empty()&&EditorAppTestAccess::VfxMessages(app)==vfxMessages+2,"Local generation clears the prompt and answers in the VFX conversation");
            EditorAppTestAccess::SetPrompt(app,"unsupported motion request");tick();
            ImGui::ActivateItemByID(vfxItem("###GenerateVfx"));tick();tick();
            Check(EditorAppTestAccess::Effects(app).size()==2,"Failed prompt leaves effects unchanged");
            Check(EditorAppTestAccess::VfxPrompt(app)=="unsupported motion request"&&EditorAppTestAccess::VfxMessages(app)==vfxMessages+4,"Failed prompt keeps the text and explains how to fix it");
            EditorAppTestAccess::Undo(app);tick();Check(EditorAppTestAccess::Effects(app).size()==1,"Undo removes only the generated effect");
            EditorAppTestAccess::SetPrompt(app,"파란 마법 파티클, 초당 80개, 수명 1.5초");tick();
            ImGui::ActivateItemByID(vfxItem("###GenerateVfx"));tick();tick();
            EditorAppTestAccess::PauseAt(app,.8);tick();tick();
            saveImage("ui-vfx-preview.rgba");
            {
                auto* root=ImGui::FindWindowByName("LunaFlow");const ImGuiID workspace=ImHashStr("workspace",0,root->ID);
                ImGui::ActivateItemByID(vfxItem("##hide-vfx-assistant"));tick();tick();
                for(int i=0;i<3;++i)frame(1440,900,false,i==2);
                Check(!EditorAppTestAccess::ChatVisible(app),"VFX AI panel can be hidden to give the inspector the full column");
                ImGui::ActivateItemByID(ImHashStr("###ShowChat",0,workspace));tick();tick();tick();
                Check(EditorAppTestAccess::ChatVisible(app),"The reopen bar brings the VFX AI panel back");
            }
            for(bool korean:{true,false}){app.SetLanguage(korean);for(const auto size:{ImVec2{1000,700},ImVec2{1440,900},ImVec2{1920,1080}})for(int i=0;i<3;++i)frame(static_cast<int>(size.x),static_cast<int>(size.y),false,i==2);}
            for(float dpi:{1.0f,1.25f,1.5f,2.0f}){
                ConfigureUiTheme(dpi);
                for(bool korean:{true,false}){
                    app.SetLanguage(korean);
                    for(int mode:{0,1,2}){
                        EditorAppTestAccess::Workspace(app,mode);
                        for(const auto logicalSize:{ImVec2{1000,700},ImVec2{1440,900}})for(int i=0;i<3;++i)frame(static_cast<int>(logicalSize.x*dpi),static_cast<int>(logicalSize.y*dpi),false,i==2);
                    }
                    EditorAppTestAccess::Workspace(app,1);for(int i=0;i<3;++i)frame(static_cast<int>(1440*dpi),static_cast<int>(900*dpi),false,i==2);
                    if(korean){char filename[80];std::snprintf(filename,sizeof(filename),"ui-dpi-%d.rgba",static_cast<int>(dpi*100));saveImage(filename);}
                    EditorAppTestAccess::OpenUnsaved(app);tick();tick();tick();
                    ImGuiWindow* popup=nullptr;for(auto* window:ImGui::GetCurrentContext()->Windows)if(window->Active&&std::strstr(window->Name,"###Unsaved"))popup=window;
                    Check(popup!=nullptr,"Unsaved changes modal opens at each DPI");
                    Check(popup->ScrollMax.x<.5f&&popup->ScrollMax.y<.5f,"Modal content fits without clipping");
                    Check(popup->Pos.x>=0&&popup->Pos.y>=0&&popup->Pos.x+popup->Size.x<=io.DisplaySize.x+.5f&&popup->Pos.y+popup->Size.y<=io.DisplaySize.y+.5f,"Modal stays inside the viewport");
                    if(korean){char filename[80];std::snprintf(filename,sizeof(filename),"ui-unsaved-%d.rgba",static_cast<int>(dpi*100));saveImage(filename);}
                    ImGui::ActivateItemByID(ImHashStr(korean?"취소":"Cancel",0,popup->ID));tick();tick();
                    Check(!app.WantQuit()&&EditorAppTestAccess::Dirty(app),"Cancel keeps unsaved work at each DPI");
                    Check(!ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopupId),"Cancel closes the modal");
                }
                for(int mode:{0,1,2}){EditorAppTestAccess::Workspace(app,mode);for(int i=0;i<3;++i)frame(1920,1080,false,i==2);}
            }
            ConfigureUiTheme(1);app.SetLanguage(true);for(int i=0;i<3;++i)frame(1440,900,false,i==2);
            EditorAppTestAccess::BeginThinking(app);for(int i=0;i<4;++i)tick();
            Check(EditorAppTestAccess::AiActive(app)&&EditorAppTestAccess::AiElapsed(app)>0,"Waiting AI displays activity with elapsed time");
            EditorAppTestAccess::NoteAi(app,"Copilot Studio에서 현재 장면의 편집 계획을 생성하고 있습니다.");EditorAppTestAccess::NoteAi(app,"Copilot Studio에서 현재 장면의 편집 계획을 생성하고 있습니다.");tick();
            Check(EditorAppTestAccess::AiActivity(app)==2,"Thinking stages are listed once each");
            saveImage("ui-ai-thinking.rgba");
            EditorAppTestAccess::CancelAi(app);tick();
            Check(!EditorAppTestAccess::AiActive(app),"Stop clears the thinking state");
            EditorAppTestAccess::BeginThinking(app);for(int i=0;i<3;++i)tick();
            EditorAppTestAccess::ReplyAi(app,"검을 앞으로 내려 베는 동작을 만들었습니다.");tick();tick();
            Check(EditorAppTestAccess::LastThought(app)>0&&EditorAppTestAccess::LastActivity(app)==1&&!EditorAppTestAccess::AiActive(app),"Reply keeps how long the AI thought and its stages");
            saveImage("ui-ai-thought.rgba");
            const auto beforeAiDuration=EditorAppTestAccess::Duration(app);const auto beforeAiEffects=EditorAppTestAccess::Effects(app).size();
            EditorAppTestAccess::BeginAi(app);tick();
            Check(EditorAppTestAccess::Completed(app)==1&&EditorAppTestAccess::Duration(app)==1.5&&EditorAppTestAccess::Effects(app).size()==beforeAiEffects,"AI applies one step per visible update");
            EditorAppTestAccess::NextAi(app);tick();Check(EditorAppTestAccess::Completed(app)==2&&EditorAppTestAccess::Effects(app).size()==beforeAiEffects+1,"AI particle step updates actual effect data");
            EditorAppTestAccess::PauseAt(app,.6);tick();saveImage("ui-copilot-steps.rgba");
            EditorAppTestAccess::Undo(app);tick();Check(EditorAppTestAccess::Duration(app)==beforeAiDuration&&EditorAppTestAccess::Effects(app).size()==beforeAiEffects,"One undo restores both AI animation and effect edits");
            EditorAppTestAccess::OpenFbx(app);tick();tick();
            ImGuiWindow* fbxPopup=nullptr;for(auto* window:ImGui::GetCurrentContext()->Windows)if(window->Active&&std::strstr(window->Name,"###FbxImport"))fbxPopup=window;
            Check(fbxPopup!=nullptr&&fbxPopup->Size.x<io.DisplaySize.x&&fbxPopup->Size.y<io.DisplaySize.y,"FBX import modes fit in their dialog");
            saveImage("ui-fbx-import.rgba");
            ImGui::ActivateItemByID(ImHashStr("취소",0,fbxPopup->ID));tick();tick();
            Check(!ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopupId),"FBX chooser can be dismissed without importing or changing the scene");
            const auto beforeAdaptClips=EditorAppTestAccess::Clips(app);EditorAppTestAccess::OpenAdaptation(app);tick();tick();
            ImGuiWindow* adaptation=nullptr;for(auto* window:ImGui::GetCurrentContext()->Windows)if(window->Active&&std::strstr(window->Name,"###Proportions"))adaptation=window;
            Check(adaptation!=nullptr&&adaptation->Size.x<io.DisplaySize.x&&adaptation->Size.y<io.DisplaySize.y,"Proportion options fit in the dialog");saveImage("ui-proportions.rgba");
            ImGui::ActivateItemByID(ImHashStr("###AdaptFbx",0,adaptation->ID));tick();tick();
            Check(EditorAppTestAccess::Clips(app)==beforeAdaptClips+1&&EditorAppTestAccess::Adapted(app),"FBX adaptation UI adds a real motion and report");
            EditorAppTestAccess::Undo(app);tick();Check(EditorAppTestAccess::Clips(app)==beforeAdaptClips,"Imported adapted motion is undoable");
            {
                // Unity/Blender navigation, timeline and editing shortcuts driven through real input events.
                using A=EditorAppTestAccess;constexpr float pi=3.14159265f;
                A::UseDemo(app);A::Workspace(app,1);A::SelectHand(app);ImGui::SetWindowFocus("LunaFlow");for(int i=0;i<3;++i)tick();
                auto press=[&](ImGuiKey key,std::initializer_list<ImGuiKey> modifiers={}){
                    for(const auto modifier:modifiers)io.AddKeyEvent(modifier,true);
                    io.AddKeyEvent(key,true);tick();io.AddKeyEvent(key,false);
                    for(const auto modifier:modifiers)io.AddKeyEvent(modifier,false);
                    tick();
                };
                auto settle=[&]{for(int i=0;i<16;++i)tick();};
                auto sameAngle=[](float a,float b){return std::abs(std::remainder(a-b,2*pi))<1e-4f;};
                const float wide=A::Camera(app).distance;
                press(ImGuiKey_F);settle();
                const auto hand=A::BonePosition(app);const auto& framed=A::Camera(app);
                Check(framed.distance<wide*.6f&&std::hypot(framed.target.x-hand.x,framed.target.y-hand.y,framed.target.z-hand.z)<.25f,"F frames the selected bone");
                press(ImGuiKey_F,{ImGuiMod_Shift});settle();Check(A::Camera(app).distance>wide*.8f,"Shift+F frames the whole scene");

                press(ImGuiKey_Keypad1);settle();
                Check(A::ViewAxis(app)==0&&A::Camera(app).orthographic&&sameAngle(A::Camera(app).yaw,0)&&sameAngle(A::Camera(app).pitch,0),"Numpad 1 snaps to an orthographic front view");
                Check(A::ViewName(app)=="정면 · 직교","Viewport overlay names the axis view");
                press(ImGuiKey_Keypad3,{ImGuiMod_Ctrl});settle();Check(A::ViewAxis(app)==3&&sameAngle(A::Camera(app).yaw,-pi*.5f),"Ctrl+Numpad 3 shows the left view");
                press(ImGuiKey_Keypad9);settle();Check(A::ViewAxis(app)==2&&sameAngle(A::Camera(app).yaw,pi*.5f),"Numpad 9 flips to the opposite view");
                press(ImGuiKey_Keypad4);settle();
                Check(A::ViewAxis(app)==-1&&!A::Camera(app).orthographic&&sameAngle(A::Camera(app).yaw,pi*.5f-pi/12),"Orbiting out of an axis view restores perspective");
                press(ImGuiKey_Keypad5);Check(A::Camera(app).orthographic,"Numpad 5 toggles orthographic");
                press(ImGuiKey_Keypad7);settle();press(ImGuiKey_Keypad2);settle();
                Check(A::Camera(app).orthographic&&A::ViewAxis(app)==-1&&sameAngle(A::Camera(app).pitch,pi*.5f-pi/12),"A manually chosen orthographic view survives orbiting");
                press(ImGuiKey_Keypad5);Check(!A::Camera(app).orthographic,"Numpad 5 returns to perspective");

                auto gizmoPoint=[&](int index,bool center){
                    const auto rect=A::ViewportRect(app);double x=0,y=0;int count=0;
                    for(float py=rect.Min.y;py<rect.Min.y+UiScale(150);py+=1)for(float px=rect.Max.x-UiScale(150);px<rect.Max.x;px+=1)
                        if(A::GizmoHit(app,{px,py})==index){if(!center)return ImVec2{px,py};x+=px;y+=py;++count;}
                    Check(count>0,"Orientation gizmo exposes every handle");
                    return ImVec2{static_cast<float>(x/count),static_cast<float>(y/count)};
                };
                auto click=[&](ImVec2 at){mouse=at;tick();io.AddMouseButtonEvent(0,true);tick();io.AddMouseButtonEvent(0,false);tick();tick();};
                click(gizmoPoint(2,true));settle();
                Check(A::ViewAxis(app)==4&&A::Camera(app).orthographic&&sameAngle(A::Camera(app).pitch,pi*.5f),"Clicking the +Y gizmo ball shows the top view");
                click(gizmoPoint(2,true));settle();Check(A::ViewAxis(app)==5&&sameAngle(A::Camera(app).pitch,-pi*.5f),"Clicking the active axis again shows the opposite view");
                click(gizmoPoint(7,true));Check(!A::Camera(app).orthographic&&A::ViewName(app)=="하단 · 원근","Gizmo label toggles perspective");
                const float yaw=A::Camera(app).yaw;mouse=gizmoPoint(6,false);tick();io.AddMouseButtonEvent(0,true);tick();
                for(int i=0;i<4;++i){mouse.x+=12;tick();}
                io.AddMouseButtonEvent(0,false);tick();tick();
                Check(A::ViewAxis(app)==-1&&std::abs(A::Camera(app).yaw-yaw)>.05f&&A::ViewName(app).rfind("사용자",0)==0,"Dragging the gizmo orbits into a user view");

                const auto viewport=A::ViewportRect(app);mouse={(viewport.Min.x+viewport.Max.x)*.5f,(viewport.Min.y+viewport.Max.y)*.5f};tick();
                const auto before=A::Camera(app);const int tool=A::ToolIndex(app);
                io.AddMouseButtonEvent(1,true);tick();io.AddKeyEvent(ImGuiKey_W,true);for(int i=0;i<12;++i)tick();
                io.AddKeyEvent(ImGuiKey_W,false);io.AddMouseButtonEvent(1,false);tick();tick();
                XMFLOAT3 forward;XMStoreFloat3(&forward,XMMatrixInverse(nullptr,before.View()).r[2]);
                const auto& flown=A::Camera(app);const XMFLOAT3 moved{flown.target.x-before.target.x,flown.target.y-before.target.y,flown.target.z-before.target.z};
                const float length=std::hypot(moved.x,moved.y,moved.z);
                Check(length>before.sceneRadius*.05f&&moved.x*forward.x+moved.y*forward.y+moved.z*forward.z>length*.95f,"RMB + W flies the camera forward");
                Check(A::ToolIndex(app)==tool,"W while flying does not switch tools");
                mouse={4,70};tick();

                const double frameStep=1.0/A::Fps(app);A::PauseAt(app,0);tick();
                press(ImGuiKey_RightArrow);Check(std::abs(A::Time(app)-frameStep)<1e-9,"Right arrow steps one frame");
                press(ImGuiKey_Period);Check(std::abs(A::Time(app)-2*frameStep)<1e-9,"Period steps forward");
                press(ImGuiKey_Comma);press(ImGuiKey_LeftArrow);Check(A::Time(app)<1e-9,"Comma and left arrow step back");
                press(ImGuiKey_UpArrow);Check(std::abs(A::Time(app)-.125)<1e-9,"Up arrow jumps to the next key");
                press(ImGuiKey_Period,{ImGuiMod_Alt});Check(std::abs(A::Time(app)-.25)<1e-9,"Alt+period jumps to the next key");
                press(ImGuiKey_DownArrow);press(ImGuiKey_Comma,{ImGuiMod_Alt});Check(A::Time(app)<1e-9,"Down arrow and Alt+comma jump to earlier keys");
                press(ImGuiKey_End);Check(std::abs(A::Time(app)-3)<1e-9,"End jumps to the last frame");
                press(ImGuiKey_LeftArrow,{ImGuiMod_Shift});Check(A::Time(app)<1e-9,"Shift+left returns to the first frame");
                press(ImGuiKey_Space);Check(A::Playing(app),"Space starts playback");press(ImGuiKey_Space);Check(!A::Playing(app),"Space pauses playback");
                press(ImGuiKey_W);Check(A::ToolIndex(app)==0,"W selects the move tool");
                press(ImGuiKey_R);Check(A::ToolIndex(app)==2,"R selects the scale tool");
                press(ImGuiKey_E);Check(A::ToolIndex(app)==1,"E selects the rotate tool");

                A::PauseAt(app,.06);tick();const auto keys=A::BoneKeys(app);
                press(ImGuiKey_K);Check(A::BoneKeys(app)==keys+1,"K inserts a key at the current frame");
                press(ImGuiKey_Z,{ImGuiMod_Ctrl});Check(A::BoneKeys(app)==keys,"Ctrl+Z removes the inserted key");
                press(ImGuiKey_Z,{ImGuiMod_Ctrl,ImGuiMod_Shift});Check(A::BoneKeys(app)==keys+1,"Ctrl+Shift+Z redoes");
                press(ImGuiKey_Z,{ImGuiMod_Ctrl});press(ImGuiKey_Y,{ImGuiMod_Ctrl});Check(A::BoneKeys(app)==keys+1,"Ctrl+Y redoes");
                A::SelectKey(app,.06);press(ImGuiKey_Delete);Check(A::BoneKeys(app)==keys&&A::SelectedKey(app)<0,"Delete removes the selected key");
                press(ImGuiKey_Z,{ImGuiMod_Ctrl});Check(A::BoneKeys(app)==keys+1,"Deleting a key is undoable");
                A::SelectKey(app,.06);press(ImGuiKey_Escape);Check(A::SelectedKey(app)<0,"Escape clears the key selection");
                const auto clips=A::Clips(app);press(ImGuiKey_D,{ImGuiMod_Ctrl});Check(A::Clips(app)==clips+1,"Ctrl+D duplicates the motion");

                for(float dpi:{1.0f,1.5f,2.0f}){
                    ConfigureUiTheme(dpi);
                    for(bool korean:{true,false}){
                        app.SetLanguage(korean);for(int i=0;i<3;++i)frame(static_cast<int>(1000*dpi),static_cast<int>(700*dpi),false,i==2);
                        press(ImGuiKey_F1);tick();
                        ImGuiWindow *help=nullptr,*list=nullptr;
                        for(auto* window:ImGui::GetCurrentContext()->Windows)if(window->Active&&std::strstr(window->Name,"###Shortcuts")){
                            if(std::strstr(window->Name,"##shortcut-list"))list=window;else if(!(window->Flags&ImGuiWindowFlags_ChildWindow))help=window;
                        }
                        Check(help!=nullptr&&list!=nullptr,"F1 opens the shortcut reference");
                        Check(help->Pos.x>=0&&help->Pos.y>=0&&help->Pos.x+help->Size.x<=io.DisplaySize.x+.5f&&help->Pos.y+help->Size.y<=io.DisplaySize.y+.5f,"Shortcut reference stays inside the window");
                        Check(help->ScrollMax.x<.5f&&help->ScrollMax.y<.5f&&list->ScrollMax.x<.5f,"Shortcut reference wraps without clipping");
                        if(korean&&dpi==1.0f)saveImage("ui-shortcuts.rgba");
                        press(korean?ImGuiKey_F1:ImGuiKey_Escape);Check(!ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopupId),"F1 or Escape closes the shortcut reference");
                    }
                }
                ConfigureUiTheme(1);app.SetLanguage(true);for(int i=0;i<3;++i)frame(1440,900,false,i==2);
            }
            {
                // Webcam capture states, drawn without a tracker process (Frame's pump would end a fake session).
                auto& m=EditorAppTestAccess::MocapState(app);auto& c=m.capture;
                c.Configure(EditorAppTestAccess::Model(app).skeleton);c.BeginCalibration();
                m.show=m.active=m.cameraReady=true;
                uint64_t sequence=0;double received=100;
                auto feed=[&](int count){for(int i=0;i<count;++i){++sequence;received+=1/30.;m.lastFrame=MocapFrame(received-100,sequence);c.Consume(*m.lastFrame,received);}};
                auto drawMocap=[&](const char* file){
                    for(int pass=0;pass<2;++pass) {
                        io.DisplaySize={1440,900};io.DeltaTime=1.0f/60;io.AddMousePosEvent(-1,-1);
                        ImGui_ImplDX11_NewFrame();ImGui::NewFrame();
                        ImGui::SetNextWindowPos({0,0});ImGui::SetNextWindowSize({900,900});
                        ImGui::Begin("mocap-view",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoInputs|ImGuiWindowFlags_NoBringToFrontOnFocus);
                        EditorAppTestAccess::DrawMocap(app,{900,900});ImGui::End();ImGui::Render();
                    }
                    auto* panel=ImGui::FindWindowByName("###Mocap");Check(panel&&panel->Active,"Mocap panel renders");
                    Check(panel->Pos.x>=0&&panel->Pos.y>=0&&panel->Pos.x+panel->Size.x<=1440&&panel->Pos.y+panel->Size.y<=900,"Mocap panel stays on screen");
                    if(panel->DC.CursorMaxPos.x>panel->InnerRect.Max.x+.5f){std::cerr<<file<<" content width "<<panel->DC.CursorMaxPos.x<<" visible "<<panel->InnerRect.Max.x<<"\n";throw std::runtime_error("Mocap panel content is clipped horizontally");}
                    ID3D11RenderTargetView* view=rtv.Get();context->OMSetRenderTargets(1,&view,nullptr);const float clear[]={.043f,.051f,.075f,1};
                    context->ClearRenderTargetView(view,clear);ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());saveImage(file);
                };
                feed(5);drawMocap("ui-mocap-calibrating.rgba");
                feed(26);Check(c.Calibrated(),"Mocap UI fixture calibrates");drawMocap("ui-mocap-ready.rgba");
                m.countdownEnd=Mocap::Clock()+2.5;drawMocap("ui-mocap-countdown.rgba");
                m.countdownEnd=-1;c.StartRecording("UI take",received);feed(45);drawMocap("ui-mocap-recording.rgba");
                c.StopRecording();m.userStopped=true;auto take=c.TakeCompleted();Check(take.has_value(),"Mocap UI fixture records");
                EditorAppTestAccess::AddTake(app,std::move(*take));drawMocap("ui-mocap-review.rgba");
                EditorAppTestAccess::DiscardTake(app);
                m.active=m.cameraReady=false;m.lastFrame.reset();m.takes=0;drawMocap("ui-mocap-off.rgba");
                m.show=false;for(int i=0;i<3;++i)frame(1440,900,false,i==2);
            }
            EditorAppTestAccess::Workspace(app,1);ImGui::SetWindowFocus("LunaFlow");tick();tick();
            const auto savedProject=std::filesystem::current_path()/"test-output"/"shortcut-save.lunaflow";
            EditorAppTestAccess::PrepareSave(app,savedProject);
            io.AddKeyEvent(ImGuiMod_Ctrl,true);io.AddKeyEvent(ImGuiKey_LeftCtrl,true);io.AddKeyEvent(ImGuiKey_S,true);tick();tick();
            Check(std::filesystem::exists(savedProject)&&!EditorAppTestAccess::Dirty(app),"Ctrl+S saves on the initial key press");
            EditorAppTestAccess::MarkDirty(app);
            for(int i=0;i<60;++i)tick();
            Check(EditorAppTestAccess::Dirty(app),"Holding Ctrl+S does not repeat save commands");
            io.AddKeyEvent(ImGuiKey_S,false);tick();io.AddKeyEvent(ImGuiKey_S,true);tick();tick();
            Check(!EditorAppTestAccess::Dirty(app),"A fresh Ctrl+S press still saves");
            EditorAppTestAccess::MarkDirty(app);
            // Model events arriving during the native dialog's nested message loop.
            io.AddKeyEvent(ImGuiKey_O,true);io.AddMouseButtonEvent(0,true);io.AddInputCharacter('x');io.AddFocusEvent(false);io.AddFocusEvent(true);
            EditorAppTestAccess::ReleaseDialogInput(app);
            Check(ImGui::GetCurrentContext()->InputEventsQueue.empty(),"Dialog dismissal clears queued stale input");
            Check(!io.KeyCtrl&&!ImGui::IsKeyDown(ImGuiKey_S)&&!io.MouseDown[0],"Dialog dismissal releases shortcut and click state");
            for(int i=0;i<30;++i)tick();
            Check(EditorAppTestAccess::Dirty(app),"Cancelled dialog input cannot silently save or reopen a dialog");
            // Dismissing the native save dialog must not swallow a later title-bar close.
            EditorAppTestAccess::PrepareSave(app,savedProject);
            io.AddKeyEvent(ImGuiMod_Ctrl,true);io.AddKeyEvent(ImGuiKey_LeftCtrl,true);io.AddKeyEvent(ImGuiKey_S,true);tick();tick();
            Check(!EditorAppTestAccess::Dirty(app),"Project saves before the close regression check");
            EditorAppTestAccess::ReleaseDialogInput(app);tick();
            app.RequestClose();tick();
            Check(app.WantQuit()&&!ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopupId),"Saved project closes after Ctrl+S and dialog input cleanup");
            Check(std::filesystem::exists(preferences)==hadPreferences,"Rendering does not create preferences implicitly");
            if(hadPreferences)Check(std::filesystem::last_write_time(preferences)==previousWrite,"Rendering does not rewrite preferences every frame");
        }
        TestViewportUx();
        std::cout<<"PASS Korean/English, responsive panels, 100/125/150/200% DPI, timeline/modal layout, nonrepeating Ctrl+S, saved-project close, dialog input cleanup, AI waiting, default character, workspace navigation and effect creation\n";
    }catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<"\n";code=1;}
    if(backend)ImGui_ImplDX11_Shutdown();if(ImGui::GetCurrentContext())ImGui::DestroyContext();
    if(context){context->ClearState();context->Flush();}if(SUCCEEDED(com))CoUninitialize();return code;
}

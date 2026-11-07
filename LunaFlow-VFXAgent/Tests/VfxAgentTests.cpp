#include "VfxAgent.h"
#include "AgentBridge/EditPlan.h"
#include "App/EditorApp.h"
#include "App/UiTheme.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_impl_dx11.h"
#include "ImGuizmo.h"
#include <algorithm>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace LunaFlow;
static int checks=0;
static void Check(bool value,const char* message){
    if(!value)throw std::runtime_error(message);
    ++checks;
}
template<class Action> static void Reject(Action action,const char* message){
    try{action();}catch(const std::exception&){++checks;return;}
    throw std::runtime_error(message);
}
static Json Step(const char* id,const char* operation,const char* target,const Json& args,const char* result=""){
    return {{"stepId",id},{"operation",operation},{"targetRef",target},{"resultRef",result},{"description",operation},{"argumentsJson",args.dump()}};
}
static Json Plan(const Json& request,Json steps){
    return {{"format","lunaflow.edit.plan"},{"version",1},{"requestId",request.at("requestId")},{"sessionId",request.at("sessionId")},
        {"summary","VFX test"},{"needsClarification",false},{"steps",std::move(steps)}};
}
static Json Request(const ModelAsset& model,AgentSession& session,int effect=-1){
    return EditSceneContext(model,0,9,effect,0,session.Draft(model,model.clips[0],"Refine the selected VFX"));
}
static void CoreTests(){
    auto model=MakeDemoScene();AgentSession session;VfxAgentState state;
    const auto original=Request(model,session);
    auto request=VfxAgentRequest(original,state);
    Check(request["requestId"]==original["requestId"]&&request["prompt"]==original["prompt"]&&request["sceneFingerprint"]==original["sceneFingerprint"],"Preserve captured request identity and scene");
    Check(request["agentId"]=="lunaflow.vfx"&&request["allowedOperations"]==Json::array({"create_particles","update_particles","attach_effect","preview_motion"}),"Exactly four VFX operations");
    const auto clips=original["selectedClip"];
    auto plan=Plan(request,Json::array({
        Step("s1","create_particles","selectedBone",{{"preset","magic"},{"particles",{{"rate",80}}}},"magic"),
        Step("s2","update_particles","magic",{{"particles",{{"lifetime",2}}},{"color",{.1,.3,1,1}}}),
        Step("s3","preview_motion","selectedClip",{{"play",true}})}));
    EditPlan edit;auto fingerprint=EditStateFingerprint(model);edit.Begin(model,request,plan);
    Check(EditStateFingerprint(model)==fingerprint,"Preflight leaves scene unchanged");
    edit.Advance(model);Check(model.effects.size()==1&&edit.Completed()==1&&model.effects[0].bone==9,"Create particles on the captured bone one step at a time");
    edit.Advance(model);Check(model.effects[0].particles.rate==80&&model.effects[0].particles.lifetime==2,"Update the created alias without resetting other fields");
    Check(edit.Advance(model).play&&!edit.Running(),"Preview runs without a clip edit");
    Check(Request(model,session)["selectedClip"]==clips,"VFX edits preserve animation data");
    const auto effectId=model.effects[0].id;
    state.messages={{true,"Make blue magic",""},{false,"Which effect?",""},{true,"The selected effect",""}};
    request=VfxAgentRequest(Request(model,session,0),state);
    Check(request["selectedEffectId"]==effectId&&request["conversationHistory"].size()==3,"Follow-up carries current selection and clarification history");
    plan=Plan(request,Json::array({Step("s1","update_particles","selectedEffect",{{"particles",{{"rate",160},{"shape",3},{"velocity",{0,1,0}}}},{"enabled",false}})}));
    edit.Begin(model,request,plan);edit.Advance(model);
    Check(model.effects.size()==1&&model.effects[0].id==effectId&&model.effects[0].particles.rate==160&&!model.effects[0].enabled,"Follow-up edits the existing effect in place");
    Check(model.effects[0].particles.shape==ParticleShape::Circle&&model.effects[0].particles.velocity.y==1,"VFX updates support the existing shape and velocity modules");
    request=VfxAgentRequest(Request(model,session,0),state);fingerprint=EditStateFingerprint(model);
    auto forbidden=Plan(request,Json::array({
        Step("s1","create_particles","selectedBone",{{"preset","fire"}}),
        Step("s2","retime_motion","selectedClip",{{"duration",1.5}})}));
    Reject([&]{edit.Begin(model,request,forbidden);},"Reject a late animation operation in a VFX plan");
    Check(EditStateFingerprint(model)==fingerprint&&!edit.Running(),"Reject the whole out-of-scope plan before any edits");
    auto invalid=Plan(request,Json::array({Step("s1","update_particles","selectedEffect",{{"particles",{{"rate",2001}}}})}));
    Reject([&]{edit.Begin(model,request,invalid);},"Reject out-of-range particles");
    Check(EditStateFingerprint(model)==fingerprint,"Invalid particles leave the scene intact");
    invalid=Plan(request,Json::array({Step("s1","attach_effect","selectedEffect",{{"bonePath","missing"}})}));
    Reject([&]{edit.Begin(model,request,invalid);},"Reject invented attachment bone");
    auto noSelection=VfxAgentRequest(Request(model,session),state);
    invalid=Plan(noSelection,Json::array({Step("s1","update_particles","selectedEffect",{{"enabled",true}})}));
    Reject([&]{edit.Begin(model,noSelection,invalid);},"Missing selection does not create or edit a random effect");
    plan=Plan(request,Json::array({Step("s1","attach_effect","selectedEffect",{{"bonePath",model.skeleton.bones[0].path}})}));
    edit.Begin(model,request,plan);edit.Cancel();edit.Advance(model);
    Check(EditStateFingerprint(model)==fingerprint&&edit.Steps()[0].state=="cancelled","Cancellation applies no pending steps");
    edit.Begin(model,request,plan);model.effects[0].name="Manual edit";
    Reject([&]{edit.Advance(model);},"Detect manual edits before applying the next VFX step");
    Check(model.effects[0].name=="Manual edit"&&model.effects[0].bone==9,"Conflict preserves the user's edit");
    Reject([&]{edit.Begin(model,request,plan);},"Reject a response to a stale scene snapshot");
    request=VfxAgentRequest(Request(model,session,0),state);
    auto clarification=Plan(request,Json::array());clarification["needsClarification"]=true;clarification["question"]="Which color?";
    edit.Begin(model,request,clarification);
    Check(!edit.Running()&&edit.Summary()=="Which color?","Clarification does not execute a plan");
    auto animationRequest=Request(model,session,0);
    plan=Plan(animationRequest,Json::array({Step("s1","retime_motion","selectedClip",{{"duration",1.5}})}));
    edit.Begin(model,animationRequest,plan);edit.Advance(model);
    Check(PlaybackDuration(model.clips[0])==1.5,"Animation agent retains its original operations");
    state.messages.clear();
    for(int i=0;i<20;++i)state.messages.push_back({i%2==0,std::to_string(i),""});
    auto history=VfxAgentRequest(original,state)["conversationHistory"];
    Check(history.size()==12&&history[0]["text"]=="8"&&history[11]["text"]=="19","Only the latest twelve messages are sent in order");
    state.messages={{false,std::string(32769,'x'),""}};
    Check(VfxAgentRequest(original,state)["conversationHistory"].empty(),"History respects the 32 KiB request budget");
    state.messages={{true,"파란 마법을 더 작게",""}};
    Check(Json::parse(VfxAgentRequest(original,state).dump())["conversationHistory"][0]["text"]==state.messages[0].text,"Korean history remains valid UTF-8");
    const auto root=std::filesystem::current_path()/"test-output"/("vfx-settings-"+NewClipId());
    Reject([&]{VfxAgentSettings(root);},"Missing dedicated settings cannot fall back to the animation agent");
    const auto config=root/"LunaFlow-VFXAgent"/"settings.local.json";
    std::filesystem::create_directories(config.parent_path());{std::ofstream file(config);file<<"{}";}
    Check(VfxAgentSettings(root)==config,"Resolve only the dedicated agent configuration");
    std::filesystem::remove(config);std::filesystem::remove(config.parent_path());std::filesystem::remove(root);
}
namespace LunaFlow {
struct EditorAppTestAccess {
    static void Workspace(EditorApp& app,int workspace){app.SelectWorkspace(workspace);}
    static void BeginVfx(EditorApp& app){
        app.selectedBone_=9;
        auto request=VfxAgentRequest(EditSceneContext(app.model_,app.selectedClip_,9,-1,0,app.agent_.Draft(app.model_,app.Clip(),"Blue magic")),app.vfxAgent_);
        auto plan=Plan(request,Json::array({
            Step("s1","create_particles","selectedBone",{{"preset","magic"}},"magic"),
            Step("s2","update_particles","magic",{{"particles",{{"rate",90}}}})}));
        app.editPlan_.Begin(app.model_,request,plan);app.copilotRequest_=request;app.copilotActive_=true;
        app.copilotVfx_=true;app.copilotUndoCaptured_=false;app.copilotStepTime_=0;
        app.AddCopilotMessage(true,"Blue magic","VFX");
    }
    static void Next(EditorApp& app){app.copilotStepTime_=0;app.PumpCopilot(.01f);}
    static size_t Effects(const EditorApp& app){return app.model_.effects.size();}
    static size_t VfxMessages(const EditorApp& app){return app.vfxAgent_.messages.size();}
    static size_t AnimationMessages(const EditorApp& app){return app.messages_.size();}
    static bool Running(const EditorApp& app){return app.copilotActive_;}
    static void Undo(EditorApp& app){app.Undo();}
    static int SelectedEffect(const EditorApp& app){return app.selectedEffect_;}
    static void SeedInputs(EditorApp& app){strcpy_s(app.prompt_,"animation input");strcpy_s(app.vfxAgent_.prompt.data(),app.vfxAgent_.prompt.size(),"vfx input");}
    static bool InputsPreserved(const EditorApp& app){return std::string(app.prompt_)=="animation input"&&std::string(app.vfxAgent_.prompt.data())=="vfx input";}
    static void Stop(EditorApp& app){app.CancelCopilot();}
    static void Reset(EditorApp& app){app.UseModel(MakeDemoScene());}
    static void LongReply(EditorApp& app){
        app.copilotVfx_=true;std::string text;
        for(int i=0;i<10000;++i)text+="한";
        app.AddCopilotMessage(false,text,"VFX");
        const auto& message=app.vfxAgent_.messages.back().text;
        Check(message.size()<=16384&&message.find("[truncated]")!=std::string::npos,"Oversized replies are bounded and visibly marked");
        const auto request=VfxAgentRequest(Json::object(),app.vfxAgent_);
        Check(Json::parse(request.dump())["conversationHistory"].back()["text"]==message,"Clipped Korean replies remain valid in the next JSON request");
    }
    static void CheckSelectedContext(EditorApp& app){VfxAgentRequest(EditSceneContext(app.model_,app.selectedClip_,app.selectedBone_,app.selectedEffect_,app.time_,app.agent_.Draft(app.model_,app.Clip(),"Next VFX")),app.vfxAgent_);}
};
}
static void UiTests(){
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;D3D_FEATURE_LEVEL feature;
    Check(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,&feature,&context)),"Create WARP device");
    ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=nullptr;
    ConfigureUiTheme();LoadUiFonts(std::filesystem::path(LUNAFLOW_SOURCE_DIR)/"Assets"/"Fonts");
    Check(ImGui_ImplDX11_Init(device.Get(),context.Get()),"Initialize UI backend");
    try{
        EditorApp app(nullptr,device.Get(),context.Get(),std::filesystem::path(LUNAFLOW_SOURCE_DIR)/"Rendering"/"Shaders",
            std::filesystem::current_path()/"test-output"/"vfx-ui-preferences.json");
        auto frame=[&](float width,float height,bool stable){
            io.DisplaySize={width,height};io.DeltaTime=1.0f/60;
            ImGui_ImplDX11_NewFrame();ImGui::NewFrame();ImGuizmo::BeginFrame();app.Frame(io.DeltaTime);ImGui::Render();
            ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
            bool found=false;
            for(auto* window:ImGui::GetCurrentContext()->Windows){
                if(!window->Active||!std::strstr(window->Name,"/vfx-agent_"))continue;
                if(std::strstr(window->Name,"/vfx-conversation_"))continue;
                found=true;
                if(stable)Check(window->DC.CursorMaxPos.y<=window->InnerRect.Max.y+.5f&&window->ScrollMax.y<.5f,"VFX composer and stop/send controls fit in the panel");
            }
            Check(found,"VFX workspace renders its dedicated agent panel");
        };
        EditorAppTestAccess::Workspace(app,2);EditorAppTestAccess::SeedInputs(app);
        for(float dpi:{1.0f,1.25f,1.5f,2.0f}){
            ConfigureUiTheme(dpi);
            for(bool korean:{true,false}){
                app.SetLanguage(korean);
                for(auto size:{ImVec2{1000,700},ImVec2{1440,900}})
                    for(int i=0;i<3;++i)frame(size.x*dpi,size.y*dpi,i==2);
            }
        }
        Check(EditorAppTestAccess::InputsPreserved(app),"VFX and animation inputs are separate");
        const auto animationMessages=EditorAppTestAccess::AnimationMessages(app);
        EditorAppTestAccess::BeginVfx(app);
        EditorAppTestAccess::Workspace(app,1);EditorAppTestAccess::Next(app);
        Check(EditorAppTestAccess::Effects(app)==1&&EditorAppTestAccess::Running(app),"VFX step executes after switching to the animation workspace");
        EditorAppTestAccess::Next(app);
        Check(!EditorAppTestAccess::Running(app)&&EditorAppTestAccess::VfxMessages(app)==2,"VFX completion returns to the captured conversation");
        Check(EditorAppTestAccess::AnimationMessages(app)==animationMessages,"VFX responses never leak into animation chat");
        EditorAppTestAccess::Undo(app);
        Check(EditorAppTestAccess::Effects(app)==0&&EditorAppTestAccess::SelectedEffect(app)==-1,"One undo restores the VFX job and clears a removed selection");
        EditorAppTestAccess::CheckSelectedContext(app);
        EditorAppTestAccess::BeginVfx(app);EditorAppTestAccess::Stop(app);EditorAppTestAccess::Next(app);
        Check(EditorAppTestAccess::Effects(app)==0&&!EditorAppTestAccess::Running(app),"Stopping cancels pending VFX execution");
        EditorAppTestAccess::Reset(app);
        Check(EditorAppTestAccess::VfxMessages(app)==0,"New scene clears old VFX conversation");
        EditorAppTestAccess::LongReply(app);
    }catch(...){ImGui_ImplDX11_Shutdown();ImGui::DestroyContext();throw;}
    ImGui_ImplDX11_Shutdown();ImGui::DestroyContext();
}
int main(){
    const HRESULT com=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);int code=0;
    try{CoreTests();UiTests();std::cout<<"PASS "<<checks<<" VFX agent checks (offline; no sign-in or service request)\n";}
    catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<"\n";code=1;}
    if(SUCCEEDED(com))CoUninitialize();return code;
}

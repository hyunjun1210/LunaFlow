#include "Mocap.h"
#include "TrackerProcess.h"
#include "App/EditorApp.h"
#include "Core/UnityPackage.h"
#include "imgui.h"
#include "imgui_impl_dx11.h"
#include "ImGuizmo.h"
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace LunaFlow;
using namespace LunaFlow::Mocap;
namespace {
void Check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
template<class F> void Reject(F action,const char* message) {
    bool threw=false;try{action();}catch(const std::exception&){threw=true;}Check(threw,message);
}
void SetPoint(Frame& f,int index,float x,float y,float z=0) {
    // Test coordinates are Luna space (anatomical left=+X); MediaPipe world is y-down with the same unmirrored x.
    f.landmarks[index]={{x,-y,z},1,1,.5f+x*.3f,.9f-y*.4f};
}
Frame Neutral(double time,uint64_t seq) {
    Frame f;f.time=time;f.sequence=seq;f.detected=true;
    for(auto& p:f.landmarks)p={{0,0,0},1,1,.5f,.5f};
    SetPoint(f,11,.35f,1.5f);SetPoint(f,12,-.35f,1.5f);
    SetPoint(f,13,.35f,1.15f);SetPoint(f,14,-.35f,1.15f);
    SetPoint(f,15,.35f,.8f);SetPoint(f,16,-.35f,.8f);
    SetPoint(f,23,.18f,1.f);SetPoint(f,24,-.18f,1.f);
    SetPoint(f,25,.18f,.55f);SetPoint(f,26,-.18f,.55f);
    SetPoint(f,27,.18f,.1f);SetPoint(f,28,-.18f,.1f);return f;
}
void Calibrate(Capture& c) {
    c.BeginCalibration();
    for(uint64_t i=0;i<=30;++i)c.Consume(Neutral(i/30.,i+1),100+i/30.);
    Check(c.Calibrated(),"Stable full-body samples calibrate");
}
float Distance(const XMFLOAT4& a,const XMFLOAT4& b) {
    return 1-std::abs(XMVectorGetX(XMVector4Dot(XMLoadFloat4(&a),XMLoadFloat4(&b))));
}
Json Packet(const Frame& f) {
    Json points=Json::array();
    if(f.detected)for(const auto& p:f.landmarks)points.push_back({p.world.x,p.world.y,p.world.z,p.visibility,p.presence,p.u,p.v});
    return {{"version",1},{"type","frame"},{"seq",f.sequence},{"time_ms",f.time*1000},{"detected",f.detected},{"points",points}};
}
void CoreTests() {
    auto model=MakeDemoScene();Capture c;c.Configure(model.skeleton);
    auto j=Packet(Neutral(0,1));
    Check(ParseEvent(j.dump()).frame->landmarks.size()==33,"Parse 33 world/image landmarks");
    j["version"]=2;Reject([&]{ParseEvent(j.dump());},"Reject wrong protocol");j["version"]=1;
    j["points"][0][0]=nullptr;Reject([&]{ParseEvent(j.dump());},"Reject null coordinate");
    j=Packet(Neutral(0,1));j["points"][0][3]=2;Reject([&]{ParseEvent(j.dump());},"Reject confidence outside range");
    j=Packet(Neutral(0,1));j["points"].erase(0);Reject([&]{ParseEvent(j.dump());},"Reject 32 landmarks");
    Reject([&]{ParseEvent(std::string(32769,'x'));},"Reject oversized packet");
    Reject([&]{c.StartRecording("uncalibrated",100);},"Cannot record uncalibrated");
    c.BeginCalibration();
    for(uint64_t i=0;i<32;++i){auto f=Neutral(i/30.,i+1);f.landmarks[15].visibility=.1f;c.Consume(f,100+i/30.);}
    Check(!c.Calibrated(),"Occluded wrist cannot calibrate");
    c.Configure(model.skeleton);Calibrate(c);
    Check(c.Preview().local.size()==model.skeleton.bones.size(),"Complete preview pose");
    for(size_t i=0;i<model.skeleton.bones.size();++i)
        Check(Distance(c.Preview().local[i].rotation,model.skeleton.bones[i].bindLocal.rotation)<1e-5,"Neutral returns Luna bind");
    c.smoothingSeconds=0;
    auto raised=Neutral(1.1,32);SetPoint(raised,14,-.70f,1.5f);SetPoint(raised,16,-1.05f,1.5f);
    c.Consume(raised,101.1);
    const auto& g=c.Preview().global;
    Check(g[8]._41<g[7]._41-.3f&&std::abs(g[8]._42-g[7]._42)<.01f,"Right arm raises to anatomical right -X");
    Check(std::abs(g[5]._41-g[4]._41)<.01f&&g[5]._42<g[4]._42-.3f,"Left arm stays down");
    Check(Distance(c.Preview().local[8].rotation,{0,0,0,1})<1e-5,"Child rotation removes current parent rotation");
    auto forward=Neutral(1.2,33);SetPoint(forward,14,-.35f,1.5f,-.35f);SetPoint(forward,16,-.35f,1.5f,-.70f);
    c.Consume(forward,101.2);
    Check(c.Preview().global[8]._43<-.3f,"Toward camera maps to Luna front -Z");
    Reject([&]{c.Consume(forward,101.2);},"Reject replay/out-of-order frames");
    const auto saved=c.Preview();auto lost=Neutral(1.3,34);lost.detected=false;c.Consume(lost,101.3);
    Check(!c.Tracking()&&Distance(c.Preview().local[7].rotation,saved.local[7].rotation)<1e-5,"No detection holds last pose");
    Reject([&]{c.StartRecording("lost",101.3);},"Cannot start with lost pose");
    auto tRig=model.skeleton;tRig.bones[4].bindLocal.rotation=QuaternionFromDegrees({0,0,90});
    tRig.bones[7].bindLocal.rotation=QuaternionFromDegrees({0,0,-90});
    c.Configure(tRig);c.smoothingSeconds=0;Calibrate(c);
    Check(c.Preview().global[8]._42<c.Preview().global[7]._42-.3f,"T-bind rig calibrates arms down");
    auto unsupported=MakeCubeScene();Reject([&]{c.Configure(unsupported.skeleton);},"Cube rig explicitly rejected");
    auto duplicate=model.skeleton;duplicate.bones.push_back(duplicate.bones[4]);Reject([&]{c.Configure(duplicate);},"Duplicate humanoid names rejected");
    auto scaled=model.skeleton;scaled.bones[0].bindLocal.scale={1,2,1};Reject([&]{c.Configure(scaled);},"Nonuniform scale rejected");
    auto wrong=model.skeleton;wrong.bones[8].parent=1;Reject([&]{c.Configure(wrong);},"Wrong limb hierarchy rejected");
    auto alias=model.skeleton;
    for(auto& bone:alias.bones) {
        if(bone.name=="Chest")bone.name="Spine2";
        if(bone.name=="LeftUpperArm")bone.name="LeftArm";
        if(bone.name=="RightUpperArm")bone.name="RightArm";
        if(bone.name=="LeftUpperLeg")bone.name="LeftUpLeg";
        if(bone.name=="RightUpperLeg")bone.name="RightUpLeg";
        if(bone.name=="LeftLowerLeg")bone.name="LeftLeg";
        if(bone.name=="RightLowerLeg")bone.name="RightLeg";
        bone.name="mixamorig:"+bone.name;
    }
    c.Configure(alias);Calibrate(c);Check(c.Calibrated(),"Documented namespace and Mixamo aliases map");
    auto smoothAt=[&](int hz) {
        Capture s;s.Configure(model.skeleton);s.smoothingSeconds=.2f;Calibrate(s);
        for(int i=1;i<=hz;++i) {
            auto f=Neutral(1+static_cast<double>(i)/hz,31+i);
            SetPoint(f,14,-.7f,1.5f);SetPoint(f,16,-1.05f,1.5f);s.Consume(f,100+f.time);
        }
        return s.Preview().local[7].rotation;
    };
    Check(Distance(smoothAt(12),smoothAt(60))<1e-5,"Smoothing uses elapsed time, not render/inference frame count");
    c.Configure(model.skeleton);Calibrate(c);c.StartRecording("non-grid endpoint",101);
    for(uint64_t i=1;i<=10;++i)c.Consume(Neutral(1+i*.101,31+i),101+i*.101);
    c.StopRecording();auto nonGrid=c.TakeCompleted();
    Check(nonGrid&&std::abs(nonGrid->duration-1.01)<1e-8&&nonGrid->tracks[0].rotations.size()==32,"Non-grid endpoint is preserved without padding duration");
    c.Configure(model.skeleton);Calibrate(c);c.StartRecording("resampled",101);
    for(uint64_t i=1;i<=24;++i){auto f=Neutral(1+i/12.,31+i);SetPoint(f,14,-.7f,1.5f);SetPoint(f,16,-1.05f,1.5f);c.Consume(f,100+f.time);}
    c.StopRecording();auto clip=c.TakeCompleted();
    Check(clip&&std::abs(clip->duration-2)<1e-9&&clip->tracks.size()==10,"Recording has source seconds and mapped bones");
    Check(clip->tracks[0].rotations.size()==61,"12Hz tracking resampled to 61 keys at 30Hz/2 seconds");
    for(const auto& track:clip->tracks) {
        double previous=-1;
        for(const auto& key:track.rotations){Check(key.time>previous,"Key times strictly monotonic");previous=key.time;}
    }
    const auto before=model.clips[0].revision;const auto oldId=model.clips[0].id;
    model.clips.push_back(*clip);
    Check(model.clips[0].revision==before&&model.clips[0].id==oldId&&clip->id!=oldId,"Existing motions untouched and unique take ID");
    const auto output=std::filesystem::current_path()/L"test-output"/L"mocap";std::filesystem::create_directories(output);
    SaveClip(output/L"motion.json",model.skeleton,*clip);
    const auto loaded=LoadClip(output/L"motion.json",model.skeleton);
    Check(Distance(EvaluatePose(model.skeleton,&loaded,1.5).local[7].rotation,EvaluatePose(model.skeleton,&*clip,1.5).local[7].rotation)<1e-6,"Save/load playback matches recorded pose");
    ProjectData project;project.model=model;project.selectedClipId=clip->id;SaveProject(output/L"project.lunaflow",project);
    auto restored=LoadProject(output/L"project.lunaflow");
    Check(restored.model.clips.size()==model.clips.size()&&restored.model.clips.back().id==clip->id,"Project persistence includes new take");
    auto bake=BakeClip(model.skeleton,*clip,30);Check(bake.frames.size()==61,"Existing bake supports take");
    auto editable=*clip;SetTransformKey(editable,7,.5,model.skeleton.bones[7].bindLocal);
    Check(Distance(EvaluatePose(model.skeleton,&editable,.5).local[7].rotation,{0,0,0,1})<1e-6,"Recorded keys remain editable");
    ExportUnityPackage(output/L"unity"/L"manifest.json",model,30,
        std::filesystem::path(LUNAFLOW_SOURCE_DIR).parent_path()/L"unity_tools",clip->id);
    c.Configure(model.skeleton);Calibrate(c);c.StartRecording("dropout",101);
    c.Consume(Neutral(1.1,32),101.1);c.Tick(101.61);
    auto ended=c.TakeCompleted();Check(ended&&std::abs(ended->duration-.1)<1e-6,"Watchdog ends at last good time, not UI stall time");
    c.Configure(model.skeleton);Calibrate(c);c.StartRecording("gap",101);
    c.Consume(Neutral(1.6,32),101.6);ended=c.TakeCompleted();
    Check(ended&&ended->duration==0,"Large source timestamp gaps are not interpolated");
    c.Configure(model.skeleton);Calibrate(c);c.StartRecording("limit",101);
    for(uint64_t i=1;i<=1201&&c.Recording();++i)c.Consume(Neutral(1+i*.1,31+i),101+i*.1);
    ended=c.TakeCompleted();Check(ended&&std::abs(ended->duration-120)<1e-8,"Duration budget ends at exactly 120 seconds");
    Check(ended->tracks[0].rotations.size()==3601,"Duration budget bounds key allocation");
    // Helpers behind the one-button review flow: key cleanup, trimming and the framing guide.
    auto degrees=[](const XMFLOAT4& a,const XMFLOAT4& b){return XMConvertToDegrees(2*std::acos(std::min(1.f,1-Distance(a,b))));};
    AnimationClip wave;wave.id="wave-test";wave.name="Wave";wave.duration=2;
    BoneTrack moving;moving.bone=7;BoneTrack still;still.bone=4;
    for(int i=0;i<=60;++i) {
        const double t=i/30.;
        moving.rotations.push_back({t,QuaternionFromDegrees({0,0,static_cast<float>(40*std::sin(t*3.14159265))})});
        still.rotations.push_back({t,{0,0,0,1}});
    }
    wave.tracks={moving,still};
    auto simple=wave;const auto kept=SimplifyTake(simple,1.f);
    Check(kept==KeyCount(simple)&&kept<KeyCount(wave)/2,"Simplify removes most dense keys");
    Check(simple.tracks[1].rotations.size()==2,"Constant track collapses to its end keys");
    for(int i=0;i<120;++i) {
        const double t=i/60.;
        Check(degrees(EvaluatePose(model.skeleton,&simple,t).local[7].rotation,EvaluatePose(model.skeleton,&wave,t).local[7].rotation)<1.05f,
            "Simplified take stays within the rotation tolerance");
    }
    Reject([&]{auto bad=wave;SimplifyTake(bad,-1.f);},"Negative simplify tolerance rejected");
    auto trimmed=wave;TrimTake(trimmed,.5,1.5);
    Check(std::abs(trimmed.duration-1)<1e-9&&trimmed.revision==wave.revision+1,"Trim keeps the selected seconds");
    Check(Distance(EvaluatePose(model.skeleton,&trimmed,0).local[7].rotation,EvaluatePose(model.skeleton,&wave,.5).local[7].rotation)<1e-5,"Trimmed take starts at the in point");
    Check(Distance(EvaluatePose(model.skeleton,&trimmed,.7).local[7].rotation,EvaluatePose(model.skeleton,&wave,1.2).local[7].rotation)<1e-5,"Trimmed take is re-timed to start at zero");
    Reject([&]{auto bad=wave;TrimTake(bad,1.5,.5);},"Reversed trim rejected");
    Reject([&]{auto bad=wave;TrimTake(bad,0,3);},"Trim past the take rejected");
    Check(CheckFraming(Neutral(0,1))==Framing::Good,"Neutral pose is well framed");
    auto cut=Neutral(0,1);cut.landmarks[27].v=.995f;Check(CheckFraming(cut)==Framing::StepBack,"Cut-off ankle asks to step back");
    auto edge=Neutral(0,1);edge.landmarks[12].u=.005f;Check(CheckFraming(edge)==Framing::MoveLeft,"Body at the image edge asks to move over");
    auto hidden=Neutral(0,1);hidden.landmarks[15].visibility=.3f;Check(CheckFraming(hidden)==Framing::LowConfidence,"Hidden wrist is low confidence");
    auto nobody=Neutral(0,1);nobody.detected=false;Check(CheckFraming(nobody)==Framing::NoPerson,"Undetected frame has no person");
}
void ProcessTests(const std::filesystem::path& python) {
    const auto script=std::filesystem::path(LUNAFLOW_MOCAP_DIR)/L"Tests"/L"fake_tracker.py";
    TrackerProcess p;
    for(int mode:{0,1,2,3}) {
        p.Start(python,script,script,mode);bool ready=false,error=false,stopped=false,frames=false;
        const auto start=Clock();bool requested=false;
        while(p.Busy()&&Clock()-start<8) {
            for(const auto& e:p.Poll()){ready|=e.state=="ready";error|=e.state=="error";stopped|=e.state=="stopped";frames|=e.frame.has_value();}
            if((mode==0&&frames)||(mode==2&&ready)) {p.RequestStop();requested=true;}
            std::this_thread::sleep_for(std::chrono::milliseconds(15));
        }
        for(const auto& e:p.Poll()){error|=e.state=="error";stopped|=e.state=="stopped";}
        Check(!p.Busy(),"Owned tracker shuts down within budget");
        if(mode==0)Check(ready&&requested&&frames&&stopped&&!error,"Graceful stop drains protocol and releases process");
        else Check(error,"Malformed/crashed/hung worker reports visible error");
    }
}
void ModelSmokeTest(const std::filesystem::path& python) {
    const auto feature=std::filesystem::path(LUNAFLOW_MOCAP_DIR);
    TrackerProcess p;p.Start(python,feature/L"Tests"/L"real_model_smoke.py",
        feature/L"Models"/L"pose_landmarker_lite.task",0);
    bool ready=false,frame=false,stopped=false;std::string error;
    auto drain=[&] {
        for(const auto& e:p.Poll()) {
            ready|=e.state=="ready";stopped|=e.state=="stopped";
            if(e.state=="error")error=e.message;
            if(e.frame){Check(!e.frame->detected,"Blank-image inference must not invent a person");frame=true;}
        }
    };
    const auto start=Clock();
    while(p.Busy()&&Clock()-start<70){drain();std::this_thread::sleep_for(std::chrono::milliseconds(15));}
    drain();
    if(!error.empty())throw std::runtime_error(error);
    Check(!p.Busy()&&ready&&frame&&stopped,"Real-model process completes native IPC handshake, frame and shutdown");
}
}
namespace LunaFlow {
struct EditorAppTestAccess {
    static void DefaultCharacter(EditorApp& app) {
        // The shipped FBX character is auto-rigged at load; webcam capture must drive it directly.
        const auto& s=app.model_.skeleton;Capture c;c.Configure(s);c.smoothingSeconds=0;Calibrate(c);
        auto index=[&](const char* name){for(size_t i=0;i<s.bones.size();++i)if(s.bones[i].name==name)return i;throw std::runtime_error(name);};
        const auto upper=index("RightUpperArm"),hand=index("RightHand"),leftUpper=index("LeftUpperArm");
        Check(c.Preview().global.size()==s.bones.size(),"Default character preview is complete");
        Check(c.Preview().global[upper]._41<c.Preview().global[leftUpper]._41,"Default character right side is -X");
        auto raised=Neutral(1.1,32);SetPoint(raised,14,-.70f,1.5f);SetPoint(raised,16,-1.05f,1.5f);c.Consume(raised,101.1);
        const auto& g=c.Preview().global;const float arm=std::hypot(g[hand]._41-g[upper]._41,g[hand]._42-g[upper]._42,g[hand]._43-g[upper]._43);
        Check(arm>0&&g[hand]._41<g[upper]._41-.8f*arm&&std::abs(g[hand]._42-g[upper]._42)<.2f*arm,"Default character right arm raises to its right");
        c.StartRecording("Default character take",101.1);
        for(uint64_t i=1;i<=12;++i){auto f=Neutral(1.1+i/12.,32+i);SetPoint(f,14,-.7f,1.5f);SetPoint(f,16,-1.05f,1.5f);c.Consume(f,100+f.time);}
        c.StopRecording();auto take=c.TakeCompleted();
        Check(take&&std::abs(take->duration-1)<1e-9&&take->tracks.size()==10,"Default character records a take");
        // Saved for the Unity importer contract check (LunaFlow-Unity/Tools~/ContractTests).
        ProjectData project;project.model=app.model_;project.model.clips.push_back(*take);project.selectedClipId=take->id;
        const auto output=std::filesystem::current_path()/L"test-output"/L"mocap"/L"default-character-take.lunaflow";
        std::filesystem::create_directories(output.parent_path());SaveProject(output,project);
        const auto restored=LoadProject(output);
        Check(restored.model.clips.back().id==take->id&&restored.model.skeleton.bones.size()==s.bones.size(),"Default character take persists");
    }
    static void Recorded(EditorApp& app) {
        app.UseModel(MakeDemoScene());
        const auto original=app.model_.clips[0];
        app.mocap_.capture.Configure(app.model_.skeleton);Calibrate(app.mocap_.capture);
        app.mocap_.capture.StartRecording("UI take",101);
        app.mocap_.capture.Consume(Neutral(1.1,32),101.1);
        app.mocap_.active=true;app.StopMocap();app.PumpMocap();
        Check(app.model_.clips.size()==4&&app.selectedClip_==3&&app.dirty_&&!app.mocap_.active,"Finish integrates take into editor without replacing existing motion");
        Check(app.model_.clips[0].id==original.id&&app.model_.clips[0].revision==original.revision,"UI capture preserves source clip");
        app.SetTime(.05);Check(app.pose_.local.size()==14,"Recorded clip plays through editor evaluator");
        app.mocap_.active=true;bool changed=false;app.RequestSceneChange([&]{changed=true;});
        Check(!changed&&app.error_,"Scene replacement blocked during capture");
        app.mocap_.active=false;app.mocap_.show=true;
    }
    static void Review(EditorApp& app) {
        auto& m=app.mocap_;auto& c=m.capture;
        auto calibrate=[&]{c.Configure(app.model_.skeleton);Calibrate(c);};
        auto record=[&](const char* name) {
            calibrate();c.StartRecording(name,101);
            for(uint64_t i=1;i<=30;++i)c.Consume(Neutral(1+i/30.,31+i),101+i/30.);
            c.StopRecording();auto take=c.TakeCompleted();Check(take.has_value(),"Review fixture records a take");
            return std::move(*take);
        };
        const auto clips=app.model_.clips.size();const auto undo=app.undo_.size();
        app.selectedClip_=1;app.dirty_=false;m.active=m.cameraReady=m.show=true;
        app.AddMocapTake(record("Rejected take"),true);
        Check(m.review==static_cast<int>(clips)&&app.selectedClip_==m.review&&app.dirty_&&app.undo_.size()==undo+1,"Finished take opens for review as one undo step");
        Check(m.keptKeys<m.rawKeys&&m.keptKeys==KeyCount(app.Clip()),"Review take is simplified");
        app.DiscardMocapTake();
        Check(m.review<0&&app.model_.clips.size()==clips&&app.undo_.size()==undo&&!app.dirty_&&app.selectedClip_==1,"Discard leaves no clip, history or dirty flag");
        app.AddMocapTake(record("Kept take"),true);
        m.trimIn=.25f;m.trimOut=.75f;app.FinishMocapReview();
        Check(m.review<0&&app.model_.clips.size()==clips+1&&std::abs(app.model_.clips.back().duration-.5)<1e-6&&app.selectedClip_==static_cast<int>(clips),
            "Keeping a take applies its trim");
        calibrate();c.Tick(102);m.countdown=3;m.countdownEnd=50;app.UpdateMocapRecording(102);
        Check(!c.Recording()&&std::abs(m.countdownEnd-105)<1e-9,"Countdown waits while the performer is out of frame");
        calibrate();app.model_.clips.back().name="Take 1";strcpy_s(m.takeName,"Take");
        const int takes=m.takes;m.countdownEnd=50;app.UpdateMocapRecording(101);
        Check(c.Recording()&&m.countdownEnd<0&&m.takes==takes+1,"Elapsed countdown starts recording");
        m.autoStop=1;
        for(uint64_t i=1;i<=33&&c.Recording();++i){c.Consume(Neutral(1+i/30.,31+i),101+i/30.);app.UpdateMocapRecording(101+i/30.);}
        auto take=c.TakeCompleted();
        Check(!c.Recording()&&m.userStopped&&take&&take->name=="Take 2"&&take->duration>=1-1e-6,"Auto stop ends a uniquely named take");
        m.active=m.cameraReady=m.userStopped=false;m.review=-1;m.countdownEnd=-1;m.autoStop=0;m.takeName[0]=0;
    }
    static void BeginFixture(EditorApp& app,const std::filesystem::path& python) {
        const auto script=std::filesystem::path(LUNAFLOW_MOCAP_DIR)/L"Tests"/L"fake_tracker.py";
        app.mocap_.capture.Configure(app.model_.skeleton);app.selectedClip_=0;app.SetTime(1);
        app.mocap_.tracker.Start(python,script,script,0);app.mocap_.active=true;app.playing_=false;
    }
    static void CheckLocked(EditorApp& app) {
        Check(app.mocap_.active&&!app.playing_,"Space cannot start existing playback during capture");
        Check(Distance(app.pose_.local[7].rotation,app.model_.skeleton.bones[7].bindLocal.rotation)<1e-5,"Viewport uses capture pose instead of underlying Wave clip");
        app.StartJointDrag({100,100},0);Check(!app.draggingBone_,"Direct viewport edit locked during capture");
    }
    static void CloseFixture(EditorApp& app) {
        app.RequestClose();const auto start=Clock();
        while(app.mocap_.active&&Clock()-start<5){app.PumpMocap();std::this_thread::sleep_for(std::chrono::milliseconds(15));}
        Check(!app.mocap_.active&&!app.mocap_.tracker.Busy(),"Window close releases owned worker before unsaved prompt");
        Check(app.dirty_&&!app.WantQuit(),"Close does not silently discard unsaved clips");
        app.closeRequested_=false;
    }
};
}
int wmain(int argc,wchar_t** argv) {
    const auto com=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    try {
        if(argc==3&&std::wstring(argv[2])==L"--model-smoke") {
            ModelSmokeTest(argv[1]);
            if(SUCCEEDED(com))CoUninitialize();
            std::cout<<"Real MediaPipe model inference through native IPC passed (blank image; no webcam).\n";return 0;
        }
        CoreTests();
        if(argc>1)ProcessTests(argv[1]);
        else std::cout<<"Process fixture skipped: pass an existing Python executable to enable.\n";
        ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;D3D_FEATURE_LEVEL level;
        Check(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,&level,&context)),"Create mocap UI WARP device");
        ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.DisplaySize={1280,800};io.DeltaTime=1.f/60;
        ImGui_ImplDX11_Init(device.Get(),context.Get());
        {
            EditorApp app(nullptr,device.Get(),context.Get(),std::filesystem::path(LUNAFLOW_SOURCE_DIR)/L"Rendering"/L"Shaders",
                std::filesystem::current_path()/L"test-output"/L"mocap"/L"preferences.json");
            EditorAppTestAccess::DefaultCharacter(app);
            EditorAppTestAccess::Recorded(app);
            EditorAppTestAccess::Review(app);
            for(bool korean:{true,false}) {
                app.SetLanguage(korean);
                for(int i=0;i<3;++i){ImGui_ImplDX11_NewFrame();ImGui::NewFrame();ImGuizmo::BeginFrame();app.Frame(1.f/60);ImGui::Render();}
            }
            if(argc>1) {
                EditorAppTestAccess::BeginFixture(app,argv[1]);io.AddKeyEvent(ImGuiKey_Space,true);
                ImGui_ImplDX11_NewFrame();ImGui::NewFrame();ImGuizmo::BeginFrame();app.Frame(1.f/60);ImGui::Render();
                EditorAppTestAccess::CheckLocked(app);io.AddKeyEvent(ImGuiKey_Space,false);
                EditorAppTestAccess::CloseFixture(app);
            }
        }
        ImGui_ImplDX11_Shutdown();ImGui::DestroyContext();
        if(SUCCEEDED(com))CoUninitialize();
        std::cout<<"Mocap protocol, calibration, mapping, recording, persistence, lifecycle and UI tests passed.\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<"\n";if(SUCCEEDED(com))CoUninitialize();return 1;}
}

#include "Rendering/RendererDX11.h"
#include "Rendering/ViewportInteraction.h"
#include "Core/Primitives.h"
#include "Core/ProjectIO.h"
#include "Core/EditorCommands.h"
#include "Core/SceneExporter.h"
#include "App/EditorSettings.h"
#include "Core/SceneAssembly.h"
#include "Core/UnityPackage.h"
#include "Core/Particles.h"
#include "AgentBridge/AgentSession.h"
#include "AgentBridge/EditPlan.h"
#include "AgentBridge/CopilotProcess.h"
#include "Core/MotionRetarget.h"
#include "Core/AutoRig.h"
#include <thread>
#include <assimp/Importer.hpp>
#include <assimp/config.h>
#include <assimp/scene.h>
#include <d3d11sdklayers.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstring>
#include <fstream>
#include <functional>
#include <iostream>
#include <stdexcept>

using namespace LunaFlow;
using Json=nlohmann::json;
static unsigned assertions=0;
static void Require(bool value,const char* text) { ++assertions; if(!value) throw std::runtime_error(text); }
static void Near(double a,double b,double epsilon,const char* text) { Require(std::abs(a-b)<=epsilon,text); }
template<class F> static void Throws(F action,const char* text) { bool caught=false; try { action(); } catch(const std::exception&) { caught=true; } Require(caught,text); }
static void SamePose(const Pose& a,const Pose& b,float epsilon=1e-5f) {
    Require(a.global.size()==b.global.size(),"Pose dimensions");
    for(size_t i=0;i<a.global.size();++i) for(unsigned j=0;j<16;++j)
        Near(reinterpret_cast<const float*>(&a.global[i])[j],reinterpret_cast<const float*>(&b.global[i])[j],epsilon,"Deterministic pose matrices");
}
static void TestEvaluation() {
    SkeletonData skeleton;
    Transform root; root.translation={1,2,3}; root.rotation=QuaternionFromDegrees({0,90,0});
    Transform child; child.translation={2,0,0}; child.scale={2,1,1};
    skeleton.bones={{"Root","/Root",-1,root},{"Child","/Root/Child",0,child}};
    auto bind=EvaluatePose(skeleton,nullptr,0);
    Near(bind.global[1]._41,1,1e-5,"Parent rotation affects child x");
    Near(bind.global[1]._42,2,1e-5,"Parent translation affects child y");
    Near(bind.global[1]._43,1,1e-5,"Parent rotation affects child z");
    AnimationClip clip{"test","Test",2,1,{}}; BoneTrack track; track.bone=1;
    track.positions={{0,{0,0,0}},{2,{4,2,0}}};
    track.rotations={{0,QuaternionFromDegrees({0,0,0})},{2,QuaternionFromDegrees({0,90,0})}};
    track.scales={{0,{1,1,1}},{2,{3,1,1}}}; clip.tracks.push_back(track);
    auto half=EvaluatePose(skeleton,&clip,1);
    Near(half.local[1].translation.x,2,1e-6,"Linear position interpolation");
    Near(half.local[1].scale.x,2,1e-6,"Linear scale interpolation");
    Near(DegreesFromQuaternion(half.local[1].rotation).y,45,1e-4,"Quaternion SLERP");
    Near(half.local[0].translation.x,1,1e-6,"Missing track uses bind pose");
    SamePose(half,EvaluatePose(skeleton,&clip,1));
    SamePose(EvaluatePose(skeleton,&clip,20),EvaluatePose(skeleton,&clip,2));
    SamePose(EvaluatePose(skeleton,&clip,-1),EvaluatePose(skeleton,&clip,0));
    SamePose(EvaluatePose(skeleton,&clip,2,true),EvaluatePose(skeleton,&clip,0));
    SamePose(EvaluatePose(skeleton,&clip,-1,true),half);
    clip.speed=2; SamePose(EvaluatePose(skeleton,&clip,.5),half);
    Near(PlaybackDuration(clip),1,1e-8,"Playback speed changes duration");
    Transform edited=half.local[1]; edited.translation.x=9;
    SetTransformKey(clip,1,1,edited);
    Near(EvaluatePose(skeleton,&clip,.5).local[1].translation.x,9,1e-6,"Inserted key is visible");
    edited.translation.x=8; SetTransformKey(clip,1,1,edited);
    Require(clip.tracks[0].positions.size()==3,"Editing replaces existing key");
    DeleteTransformKey(clip,1,1);
    Near(EvaluatePose(skeleton,&clip,.5).local[1].translation.x,2,1e-6,"Deleting key restores interpolation");
    for(XMFLOAT3 angle: {XMFLOAT3{20,30,40},XMFLOAT3{-30,85,-65},XMFLOAT3{90,25,0},XMFLOAT3{-90,25,0}}) {
        const auto q=QuaternionFromDegrees(angle),roundtrip=QuaternionFromDegrees(DegreesFromQuaternion(q));
        const float dot=std::abs(XMVectorGetX(XMVector4Dot(XMLoadFloat4(&q),XMLoadFloat4(&roundtrip))));
        Near(dot,1,2e-5,"Euler/quaternion roundtrip including gimbal lock");
    }
    clip.duration=1.037; clip.speed=1.7;
    // Keep this bake's channels inside its duration.
    clip.tracks.clear(); SetTransformKey(clip,1,0,child); SetTransformKey(clip,1,1.037,edited);
    const auto baked=BakeClip(skeleton,clip,30);
    Near(baked.frames.front().time,0,1e-9,"Bake contains start");
    Near(baked.frames.back().time,PlaybackDuration(clip),1e-9,"Bake contains exact non-frame-aligned endpoint");
    for(size_t i=0;i<baked.frames.size();++i) {
        if(i>0) Require(baked.frames[i].time>baked.frames[i-1].time,"Bake times strictly increase");
        const auto pose=EvaluatePose(skeleton,&clip,baked.frames[i].time);
        Near(pose.local[1].translation.x,baked.frames[i].local[1].translation.x,1e-7,"Bake uses preview evaluator");
    }
    Throws([&]{BakeClip(skeleton,clip,0);},"Invalid bake FPS rejected");
    AnimationClip zero{"zero","Zero duration",0,1,{}};
    Require(BakeClip(skeleton,zero).frames.size()==1,"Zero-duration bake contains one pose");
    Transform invalid; invalid.rotation={0,0,0,0};
    Throws([&]{SetTransformKey(clip,1,0,invalid);},"Invalid edit quaternion rejected");
    clip.speed=0; Throws([&]{EvaluatePose(skeleton,&clip,0);},"Invalid speed rejected");
    skeleton.bones[0].parent=1; Throws([&]{EvaluatePose(skeleton,nullptr,0);},"Cyclic/unordered hierarchy rejected");
    std::cout<<"PASS evaluation, hierarchy, edits, speed, bake, pose math\n";
}
static void TestJson(const std::filesystem::path& out) {
    auto model=MakeDemoScene(); auto& clip=model.clips[0]; clip.speed=1.5;
    auto example=LoadClip(std::filesystem::path(LUNAFLOW_SOURCE_DIR)/"Assets"/"TeamC_MotionExample.json",model.skeleton);
    Require(example.tracks.size()==2,"Published Team C example follows the motion contract");
    auto pose=EvaluatePose(model.skeleton,&clip,.8); auto tr=pose.local[7]; tr.rotation=QuaternionFromDegrees({20,30,40});
    SetTransformKey(clip,7,1.2,tr); SaveClip(out/L"동작.json",model.skeleton,clip);
    const auto loaded=LoadClip(out/L"동작.json",model.skeleton);
    Require(loaded.id==clip.id,"Clip ID survives save");
    for(double t:{0.0,.2,.8,2.0}) SamePose(EvaluatePose(model.skeleton,&clip,t),EvaluatePose(model.skeleton,&loaded,t));
    SaveSkeleton(out/"skeleton.json",model.skeleton);
    auto bake=BakeClip(model.skeleton,loaded); SaveBake(out/"baked.json",model.skeleton,bake);
    Json baked; {std::ifstream f(out/"baked.json");f>>baked;}
    Require(baked["frames"].size()==bake.frames.size(),"Bake JSON frame count");
    Require(baked["bones"][7]["path"]==model.skeleton.bones[7].path,"Bake exports canonical bone paths");
    Json original; {std::ifstream f(out/L"동작.json");f>>original;}
    auto reject=[&](Json bad,const char* message) {
        {std::ofstream f(out/"invalid.json");f<<bad.dump();}
        Throws([&]{LoadClip(out/"invalid.json",model.skeleton);},message);
    };
    Json bad=original; bad["tracks"][0]["bonePath"]="/Other/Rig"; reject(bad,"Unknown skeleton path rejected");
    bad=original; bad["tracks"][0]["rotations"][0]["value"]={0,0,0,0}; reject(bad,"Zero quaternion rejected");
    bad=original; bad["tracks"][0]["rotations"].push_back(bad["tracks"][0]["rotations"][0]); reject(bad,"Duplicate key time rejected");
    bad=original; bad["coordinates"]["unit"]="centimeter"; reject(bad,"Incompatible coordinates rejected");
    bad=original; bad["speed"]=-1; reject(bad,"Negative speed rejected");
    bad=original; bad["tracks"][0]["positions"][0]["time"]=1000; reject(bad,"Out of duration key rejected");
    std::cout<<"PASS saved edits, JSON roundtrip, motion contract, malformed data\n";
}
static float ModelExtent(const ModelAsset& model) {
    const auto pose=EvaluatePose(model.skeleton,nullptr,0);
    XMVECTOR low=XMVectorReplicate(FLT_MAX),high=XMVectorReplicate(-FLT_MAX);
    for(const auto& m:model.meshes) for(const auto& v:m.vertices) {
        XMVECTOR p=XMVectorZero();
        for(size_t j=0;j<4;++j) if(v.weights[j]>0) {
            const auto& b=m.skin[v.joints[j]];
            p+=XMVector3TransformCoord(XMLoadFloat3(&v.position),XMLoadFloat4x4(&b.inverseBind)*XMLoadFloat4x4(&pose.global[b.bone]))*v.weights[j];
        }
        low=XMVectorMin(low,p); high=XMVectorMax(high,p);
    }
    XMFLOAT3 span; XMStoreFloat3(&span,high-low); return std::max({span.x,span.y,span.z});
}
static void TestFbxScale(const std::filesystem::path& path) {
    Assimp::Importer importer; importer.SetPropertyBool(AI_CONFIG_IMPORT_FBX_IGNORE_UP_DIRECTION,true);
    const aiScene* scene=importer.ReadFile(path.u8string(),0);
    Require(scene && scene->mRootNode,"Read scale reference FBX");
    aiVector3D low(FLT_MAX,FLT_MAX,FLT_MAX),high(-FLT_MAX,-FLT_MAX,-FLT_MAX);
    std::function<void(const aiNode*,aiMatrix4x4)> visit=[&](const aiNode* node,aiMatrix4x4 parent) {
        const auto global=parent*node->mTransformation;
        for(unsigned i=0;i<node->mNumMeshes;++i) {
            const auto* mesh=scene->mMeshes[node->mMeshes[i]];
            for(unsigned v=0;v<mesh->mNumVertices;++v) {
                const auto p=global*mesh->mVertices[v];
                low.x=std::min(low.x,p.x);low.y=std::min(low.y,p.y);low.z=std::min(low.z,p.z);
                high.x=std::max(high.x,p.x);high.y=std::max(high.y,p.y);high.z=std::max(high.z,p.z);
            }
        }
        for(unsigned i=0;i<node->mNumChildren;++i) visit(node->mChildren[i],global);
    };
    visit(scene->mRootNode,aiMatrix4x4()); double unit=1;
    if(!scene->mMetaData->Get("UnitScaleFactor",unit)) {float f=1;Require(scene->mMetaData->Get("UnitScaleFactor",f),"FBX declares a unit scale");unit=f;}
    const float expected=std::max({high.x-low.x,high.y-low.y,high.z-low.z})*static_cast<float>(unit*.01);
    const auto model=ImportScene(path);
    std::cout<<"Scale reference "<<path.filename()<<": sourceUnit="<<unit<<", expected="<<expected<<", actual="<<ModelExtent(model)<<"\n";
    Near(ModelExtent(model),expected,std::max(1e-5f,expected*1e-4f),"FBX unit scaling is applied exactly once");
    const auto smaller=ImportScene(path,.01f);
    Near(ModelExtent(smaller),expected*.01,std::max(1e-6f,expected*1e-5f),"Additional unit correction affects bind geometry once");
}
static ModelAsset TestImport() {
    const std::filesystem::path samples=ASSIMP_TEST_MODELS;
    TestFbxScale(samples/"FBX"/"box.fbx");
    TestFbxScale(samples/"FBX"/"global_settings.fbx");
    ModelAsset animated=ImportScene(samples/"FBX"/"animation_with_skeleton.fbx");
    Require(animated.skeleton.bones.size()>4 && !animated.meshes.empty(),"FBX skeleton and geometry imported");
    Require(!animated.clips[0].tracks.empty(),"FBX animation channels imported");
    for(const auto& mesh:animated.meshes) {
        Require(mesh.skin.size()<=MaxSkinBones,"Skin palette fits shader");
        for(const auto& v:mesh.vertices) {
            float sum=0;for(size_t j=0;j<4;++j) {sum+=v.weights[j];Require(v.joints[j]<mesh.skin.size(),"Vertex palette indices valid");}
            Near(sum,1,1e-5,"Skin weights normalized");
        }
    }
    const auto start=EvaluatePose(animated.skeleton,&animated.clips[0],0);
    const auto middle=EvaluatePose(animated.skeleton,&animated.clips[0],PlaybackDuration(animated.clips[0])*.37);
    bool different=false;
    for(size_t i=0;i<start.global.size();++i) for(unsigned j=0;j<16;++j)
        different|=std::abs(reinterpret_cast<const float*>(&start.global[i])[j]-reinterpret_cast<const float*>(&middle.global[i])[j])>1e-4f;
    Require(different,"FBX animation changes pose");
    auto embedded=ImportScene(samples/"glTF2"/"BoxTextured-glTF-Embedded"/"BoxTextured.gltf");
    Require(std::any_of(embedded.materials.begin(),embedded.materials.end(),[](const auto& m){return !m.textureBytes.empty() || !m.textureRGBA.empty();}),"Embedded texture data survives importer lifetime");
    std::cout<<"PASS real FBX import, units, weights, animation, embedded glTF texture\n";
    return animated;
}
static std::vector<uint8_t> Pixels(ID3D11Device* device,ID3D11DeviceContext* context,RendererDX11& renderer) {
    ComPtr<ID3D11Resource> resource;renderer.Texture()->GetResource(&resource);
    ComPtr<ID3D11Texture2D> source;resource.As(&source);
    D3D11_TEXTURE2D_DESC desc{};source->GetDesc(&desc);desc.BindFlags=0;desc.Usage=D3D11_USAGE_STAGING;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> staging;Require(SUCCEEDED(device->CreateTexture2D(&desc,nullptr,&staging)),"Readback allocation");context->CopyResource(staging.Get(),source.Get());
    D3D11_MAPPED_SUBRESOURCE map{};Require(SUCCEEDED(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&map)),"GPU readback");
    std::vector<uint8_t> result(static_cast<size_t>(desc.Width)*desc.Height*4);
    for(unsigned y=0;y<desc.Height;++y) std::memcpy(result.data()+static_cast<size_t>(y)*desc.Width*4,static_cast<uint8_t*>(map.pData)+static_cast<size_t>(y)*map.RowPitch,desc.Width*4);
    context->Unmap(staging.Get(),0);return result;
}
static void TestEditorFeatures(const std::filesystem::path& out) {
    auto scene=MakeEmptyScene();
    for(auto type:{Primitive::Cube,Primitive::Sphere,Primitive::Cylinder,Primitive::Cone,Primitive::Plane,Primitive::Torus})AddPrimitive(scene,type,{static_cast<float>(scene.meshes.size())*2,0,0});
    Require(scene.meshes.size()==6,"All six primitives are additive");
    for(const auto& mesh:scene.meshes){Require(!mesh.vertices.empty()&&!mesh.indices.empty(),"Primitive has geometry");for(auto index:mesh.indices)Require(index<mesh.vertices.size(),"Primitive indices valid");}
    auto& clip=scene.clips[0];Transform tr;tr.translation={2,0,0};SetTransformKey(clip,1,1,tr);tr.translation.x=4;SetTransformKey(clip,1,2,tr);
    auto placement=scene;placement.clips.push_back(NewClip("Other motion"));Transform base;base.translation={3,0,0};SetObjectTransform(placement,1,base);
    Near(EvaluatePose(placement.skeleton,&placement.clips[0],1).local[1].translation.x,5,1e-6,"Scene placement preserves animation delta");
    Near(EvaluatePose(placement.skeleton,&placement.clips[1],1).local[1].translation.x,3,1e-6,"Scene placement persists across motions");
    const auto oldRevision=clip.revision;const auto originalId=clip.id;
    Json rename={{"type","rename_clip"},{"clipId",clip.id},{"expectedRevision",clip.revision},{"args",{{"name","테스트 동작"}}}};
    ApplyEditorCommand(scene.clips,scene.skeleton,rename);Require(scene.clips[0].name=="테스트 동작","Shared rename command");
    Throws([&]{ApplyEditorCommand(scene.clips,scene.skeleton,rename);},"Stale command rejected");
    Require(scene.clips[0].revision==oldRevision+1,"Revision conflict is nonmutating");
    ApplyEditorCommand(scene.clips,scene.skeleton,{{"type","duplicate_clip"},{"clipId",originalId},{"expectedRevision",scene.clips[0].revision}});
    Require(scene.clips[1].id!=originalId,"Duplicate gets unique ID");
    tr.translation.x=20;SetTransformKey(scene.clips[1],1,1,tr);Near(EvaluatePose(scene.skeleton,&scene.clips[0],1).local[1].translation.x,2,1e-6,"Duplicate keys are independent");
    RetimeClip(scene.clips[0],1.5);Near(scene.clips[0].tracks[0].positions[0].time,.5,1e-8,"Retime scales key times");
    MoveBoneKey(scene.clips[0],1,.5,1);Require(scene.clips[0].tracks[0].positions.size()==1,"Moving key onto another replaces duplicate timestamp");
    ProjectData project;project.model=scene;project.selectedClipId=scene.clips[1].id;project.selectedBonePath=scene.skeleton.bones[2].path;project.time=.7;
    SaveProject(out/L"작업.lunaflow",project);const auto restored=LoadProject(out/L"작업.lunaflow");
    Require(restored.model.meshes.size()==6&&restored.model.clips.size()==2,"Project restores geometry and all motions");
    Require(restored.selectedClipId==project.selectedClipId,"Project restores stable selection");
    for(size_t i=0;i<scene.clips.size();++i)SamePose(EvaluatePose(scene.skeleton,&scene.clips[i],.5),EvaluatePose(restored.model.skeleton,&restored.model.clips[i],.5));
    Json bad;{std::ifstream f(out/L"작업.lunaflow");f>>bad;}bad["scene"]["meshes"][0]["indices"][0]=999999;
    {std::ofstream f(out/"bad-project.json");f<<bad.dump();}Throws([&]{LoadProject(out/"bad-project.json");},"Broken project mesh rejected");
    EditorSettings settings;settings.korean=false;settings.vsync=false;settings.frameLimit=240;settings.bakeFps=120;
    SaveSettings(out/"preferences.json",settings);auto saved=LoadSettings(out/"preferences.json");Require(!saved.korean&&!saved.vsync&&saved.frameLimit==240&&saved.bakeFps==120,"Language and frame preferences persist");
    const auto view=XMMatrixLookAtLH(XMVectorSet(0,.5f,-3,1),XMVectorSet(0,.5f,0,1),XMVectorSet(0,1,0,0));
    const auto vp=view*XMMatrixPerspectiveFovLH(XM_PIDIV4,4.0f/3,.01f,100);
    const auto center=ProjectPoint({0,.5f,0},vp,800,600);Near(center.pixel.x,400,1e-4,"Projection x");Near(center.pixel.y,300,1e-4,"Projection y");
    auto cube=MakeEmptyScene();auto bone=AddPrimitive(cube,Primitive::Cube);auto pose=EvaluatePose(cube.skeleton,nullptr,0);
    Require(PickSurfaceBone(cube,pose,MakePickRay({400,300},vp,800,600))==static_cast<int>(bone),"Surface picking selects the correct object");
    const auto joint=ProjectPoint({0,0,0},vp,800,600);Require(PickJoint(cube.skeleton,pose,vp,800,600,joint.pixel)>=0,"Viewport joint picking");
    const auto parent=XMMatrixScaling(2,3,4)*XMMatrixRotationY(.6f)*XMMatrixTranslation(1,2,3);
    Transform start;start.translation={.2f,.3f,.4f};const XMFLOAT3 delta{1,-2,.5f};auto moved=TranslateInWorld(start,parent,delta);
    XMFLOAT4X4 before,after;XMStoreFloat4x4(&before,Matrix(start)*parent);XMStoreFloat4x4(&after,Matrix(moved)*parent);
    Near(after._41-before._41,delta.x,1e-5,"World drag respects scaled/rotated parent x");Near(after._42-before._42,delta.y,1e-5,"World drag respects parent y");Near(after._43-before._43,delta.z,1e-5,"World drag respects parent z");
    auto rotated=RotateInWorld(start,parent,{0,1,0},XM_PIDIV2);Near(XMVectorGetX(XMVector4Length(XMLoadFloat4(&rotated.rotation))),1,1e-6,"Viewport rotation keeps unit quaternion");
    std::cout<<"PASS additive primitives, commands/revisions, project restore, preferences, picking and manipulation\n";
}
static void TestParticles(const std::filesystem::path& out) {
    auto model=MakeDemoScene();const auto& clip=model.clips[0];
    for(auto preset:{ParticlePreset::Custom,ParticlePreset::Fire,ParticlePreset::Smoke,ParticlePreset::Magic,ParticlePreset::Snow,ParticlePreset::Rain,ParticlePreset::Explosion}){
        auto effect=MakeParticleEffect(preset,0,clip.id,PlaybackDuration(clip));
        auto encoded=EncodeParticles(effect.particles);Require(EncodeParticles(DecodeParticles(encoded))==encoded,"Every particle preset roundtrips its modules");
        Require(!SampleParticles(effect,.5).empty(),"Every preset produces live particles");
    }
    auto e=GenerateParticleEffect("파란 마법 파티클, 초당 80개, 수명 1.5초, 속도 2, 크기 0.12, 반경 0.3",7,clip.id,3);
    Require(e.bone==7&&e.clipId==clip.id&&e.type==EffectType::Particles,"Prompt captures bone and clip");
    Near(e.particles.rate,80,1e-6,"Korean emission rate");Near(e.particles.lifetime,1.5,1e-6,"Korean lifetime");Near(e.particles.startSize,.12,1e-6,"Korean size");Near(e.color.z,1,1e-6,"Korean color");
    auto english=GenerateParticleEffect("RED explosion particles burst 250 lifetime 1.8 speed 4 once",0,clip.id,3);
    Require(!english.particles.loop&&english.particles.burst==250&&english.particles.rate==0,"English burst prompt");
    Near(english.particles.speed,4,1e-6,"English speed prompt");Require(english.color.x>english.color.z,"English color prompt");
    Throws([&]{GenerateParticleEffect("   ",0,clip.id,3);},"Empty prompt rejected");
    Throws([&]{GenerateParticleEffect("walk naturally",0,clip.id,3);},"Unsupported prompt does not fake generation");
    Throws([&]{GenerateParticleEffect("fire rate 999999",0,clip.id,3);},"Excessive prompt rate rejected");
    Throws([&]{GenerateParticleEffect("fire size -2",0,clip.id,3);},"Negative particle size rejected");
    auto once=MakeParticleEffect(ParticlePreset::Explosion,0,clip.id,3);once.particles.lifetimeRandomness=0;once.particles.lifetime=1;
    Require(SampleParticles(once,0).size()==180,"Burst emits at effect start");Require(SampleParticles(once,1.1).empty(),"One shot has no phantom late particles");
    once.particles.loop=true;once.particles.duration=1.5f;Require(SampleParticles(once,1.6).size()==180,"Loop repeats burst at duration");
    e.particles.maxParticles=12;e.particles.rate=2000;auto first=SampleParticles(e,.8);SampleParticles(e,2.3);auto replay=SampleParticles(e,.8);
    Require(first.size()==12&&replay.size()==first.size(),"Particle budget is bounded");
    for(size_t i=0;i<first.size();++i){Near(first[i].position.x,replay[i].position.x,0,"Scrubbing gives deterministic particles");Near(first[i].color.w,replay[i].color.w,0,"Scrubbing gives deterministic color");}
    Require(SampleParticles(e,-.1).empty()&&SampleParticles(e,3.1).empty(),"Effect timing gates all particles");e.enabled=false;Require(SampleParticles(e,.8).empty(),"Disabled effect produces no particles");e.enabled=true;
    const auto previous=SampleParticles(e,.8);e.particles.seed=31;const auto reseeded=SampleParticles(e,.8);Require(previous[0].position.x!=reseeded[0].position.x,"Seed changes the particle pattern");
    auto malformed=EncodeParticles(e.particles);malformed["maxParticles"]=100000;Throws([&]{DecodeParticles(malformed);},"Malformed particle JSON rejected");
    model.effects.push_back(e);ProjectData project;project.model=model;project.selectedClipId=clip.id;project.selectedBonePath=model.skeleton.bones[7].path;
    SaveProject(out/"particles.lunaflow",project);const auto restored=LoadProject(out/"particles.lunaflow");
    Require(restored.model.effects[0].sourcePrompt==e.sourcePrompt&&EncodeParticles(restored.model.effects[0].particles)==EncodeParticles(e.particles),"Project preserves editable prompt-generated modules");
    ExportUnityPackage(out/"ParticlePackage"/"Particles.lunaflow.json",model,30,std::filesystem::path(LUNAFLOW_SOURCE_DIR).parent_path()/"unity_tools");
    Json metadata;{std::ifstream f(out/"ParticlePackage"/"Particles.lunaflow.json");f>>metadata;}
    Require(metadata["effects"][0]["type"]==3&&metadata["effects"][0]["particles"]==EncodeParticles(e.particles),"Unity package preserves native particle module settings");
    std::cout<<"PASS Korean/English particle generation, presets, deterministic scrub, burst/loop/budget, validation, project and Unity roundtrip\n";
}
static void TestFbxExport(const std::filesystem::path& out) {
    auto demo=MakeDemoScene();auto tr=EvaluatePose(demo.skeleton,&demo.clips[0],1).local[7];tr.rotation=QuaternionFromDegrees({20,10,110});SetTransformKey(demo.clips[0],7,1,tr);
    RetimeClip(demo.clips[0],1.5);ExportFbx(out/L"수정한_캐릭터.fbx",demo,60);auto restored=ImportScene(out/L"수정한_캐릭터.fbx");
    Require(restored.clips.size()==demo.clips.size(),"FBX exports multiple animation takes");
    Require(!restored.meshes.empty(),"Exported FBX retains mesh geometry");
    auto shortFbxRig=demo.skeleton;for(auto& bone:shortFbxRig.bones){bone.bindLocal.translation.x*=.5f;bone.bindLocal.translation.y*=.5f;bone.bindLocal.translation.z*=.5f;}
    auto adaptedFbx=AdaptMotion(restored.skeleton,restored.clips.front(),shortFbxRig);
    Require(adaptedFbx.report.mappedBones==static_cast<int>(shortFbxRig.bones.size())&&!adaptedFbx.clip.tracks.empty(),"Actual FBX import feeds the proportion adaptation pipeline");
    for(size_t clip=0;clip<demo.clips.size();++clip){
        auto it=std::find_if(restored.clips.begin(),restored.clips.end(),[&](const auto& c){return c.name==demo.clips[clip].name;});Require(it!=restored.clips.end(),"FBX take names retained");
        Near(PlaybackDuration(*it),PlaybackDuration(demo.clips[clip]),.002,"FBX retains retimed duration");
        for(double fraction:{0.0,.5,1.0}){const double time=PlaybackDuration(demo.clips[clip])*fraction;const auto a=EvaluatePose(demo.skeleton,&demo.clips[clip],time),b=EvaluatePose(restored.skeleton,&*it,time);
            for(size_t i=0;i<demo.skeleton.bones.size();++i){auto bone=std::find_if(restored.skeleton.bones.begin(),restored.skeleton.bones.end(),[&](const auto& n){return n.name==demo.skeleton.bones[i].name;});Require(bone!=restored.skeleton.bones.end(),"FBX retains bone names");const size_t index=static_cast<size_t>(bone-restored.skeleton.bones.begin());for(size_t n=0;n<16;++n)Near(reinterpret_cast<const float*>(&a.global[i])[n],reinterpret_cast<const float*>(&b.global[index])[n],.004,"FBX preview/export pose agreement");}
        }
    }
    std::cout<<"PASS edited multi-take FBX export, reimport, hierarchy, timing and pose roundtrip\n";
    auto textured=MakeEmptyScene();AddPrimitive(textured,Primitive::Cube);auto& mat=textured.materials[textured.meshes[0].material];mat.textureWidth=mat.textureHeight=2;
    mat.textureRGBA={255,0,0,255,0,255,0,255,0,0,255,255,255,255,255,255};ExportFbx(out/"embedded-texture.fbx",textured,60);auto loaded=ImportScene(out/"embedded-texture.fbx");
    Require(std::any_of(loaded.materials.begin(),loaded.materials.end(),[](const auto& material){return !material.textureBytes.empty();}),"Raw embedded texture survives FBX as PNG");
}
static void TestAgentBoundary() {
    auto model=MakeDemoScene();AgentSession session;session.Reset();
    auto request=session.Draft(model,model.clips[0],"Rename this motion");const auto otherName=model.clips[1].name;
    Json response={{"requestId",request["requestId"]},{"sessionId",request["sessionId"]},{"status","completed"},
        {"command",{{"type","rename_clip"},{"clipId",request["clipId"]},{"expectedRevision",request["expectedRevision"]},{"args",{{"name","Agent renamed"}}}}}};
    std::thread worker([&]{session.Push(response);});worker.join();bool changed=false;auto messages=session.Drain(model,&changed);
    Require(changed&&model.clips[0].name=="Agent renamed"&&model.clips[1].name==otherName,"Queued agent result targets its captured clip");
    const auto revision=model.clips[0].revision;session.Push(response);session.Drain(model,&changed);Require(!changed&&model.clips[0].revision==revision,"Duplicate completion is idempotent");
    auto stale=session.Draft(model,model.clips[0],"Later response");Transform t=model.skeleton.bones[7].bindLocal;SetTransformKey(model.clips[0],7,1,t);
    response["requestId"]=stale["requestId"];response["sessionId"]=stale["sessionId"];response["command"]["expectedRevision"]=stale["expectedRevision"];session.Push(response);session.Drain(model,&changed);
    Require(!changed,"Stale asynchronous response does not overwrite edited motion");
    session.Push({{"requestId",42}});session.Drain(model,&changed);Require(!changed,"Malformed response is safely rejected");
    std::cout<<"PASS agent target capture, thread-safe queue, revisions and duplicate/stale response rejection\n";
}
static void TestEditPlansAndProportions(){
    auto model=MakeDemoScene();AgentSession session;session.Reset();
    auto request=EditSceneContext(model,0,9,-1,0,session.Draft(model,model.clips[0],"Duplicate, speed up, add blue fire"));
    auto step=[](const char* id,const char* op,const char* target,const char* result,const Json& args){return Json{{"stepId",id},{"operation",op},{"targetRef",target},{"resultRef",result},{"description",op},{"argumentsJson",args.dump()}};};
    Json plan={{"format","lunaflow.edit.plan"},{"version",1},{"requestId",request["requestId"]},{"sessionId",request["sessionId"]},{"summary","Edit test"},{"needsClarification",false},{"steps",Json::array({
        step("s1","duplicate_motion","selectedClip","fast",{{"name","Fast copy"}}),
        step("s2","retime_motion","fast","",{{"duration",1.5}}),
        step("s3","create_particles","selectedBone","flame",{{"preset","fire"},{"clipRef","fast"},{"color",{.1,.3,1,1}},{"rate",80}}),
        step("s4","preview_motion","fast","",{{"play",true}})})}};
    EditPlan executor;const auto before=EditStateFingerprint(model);executor.Begin(model,request,plan);
    Require(EditStateFingerprint(model)==before&&executor.Completed()==0,"Plan preflight has no scene side effects");
    executor.Advance(model);Require(model.clips.size()==4&&model.effects.empty()&&executor.Completed()==1,"One advance applies one visible step");
    executor.Advance(model);Near(PlaybackDuration(model.clips.back()),1.5,1e-6,"Alias resolves to duplicated motion");
    executor.Advance(model);Require(model.effects.size()==1&&model.effects[0].clipId==model.clips.back().id&&model.effects[0].bone==9,"Particles attach to captured bone and new clip");
    auto focus=executor.Advance(model);Require(focus.play&&!executor.Running(),"Final preview is a completed actual step");
    model=MakeDemoScene();request=EditSceneContext(model,0,9,-1,0,session.Draft(model,model.clips[0],"Invalid late step"));
    plan["requestId"]=request["requestId"];plan["sessionId"]=request["sessionId"];auto invalid=plan;invalid["steps"][2]["argumentsJson"]=Json{{"preset","fire"},{"rate",999999}}.dump();
    const auto clean=EditStateFingerprint(model);Throws([&]{executor.Begin(model,request,invalid);},"Bad late step rejects whole plan");Require(EditStateFingerprint(model)==clean,"Preflight failure preserves entire scene");
    executor.Begin(model,request,plan);executor.Advance(model);model.clips[0].name="User edit";
    Throws([&]{executor.Advance(model);},"Manual edits stop remaining AI steps");Require(model.clips[0].name=="User edit"&&model.effects.empty(),"Conflict keeps user's edit");
    auto fresh=[&](const char* prompt){model=MakeDemoScene();request=EditSceneContext(model,0,9,-1,0,session.Draft(model,model.clips[0],prompt));plan["requestId"]=request["requestId"];plan["sessionId"]=request["sessionId"];};
    fresh("Edited while waiting");model.clips[0].name="Edited while waiting";SetTransformKey(model.clips[1],9,.25,model.skeleton.bones[9].bindLocal);
    executor.Begin(model,request,plan);Require(executor.Rebased()&&executor.Running()&&model.clips.size()==3,"Edits made while waiting revalidate the plan instead of discarding the reply");
    while(executor.Running())executor.Advance(model);
    Require(model.clips.size()==4&&model.clips[0].name=="Edited while waiting"&&model.clips.back().name=="Fast copy"&&model.effects.size()==1,"Rebased plan keeps the user's edits and applies every step");
    fresh("Untouched");executor.Begin(model,request,plan);Require(!executor.Rebased(),"An unchanged scene is not reported as rebased");
    fresh("Target deleted");model.clips.erase(model.clips.begin());const auto deleted=EditStateFingerprint(model);
    Throws([&]{executor.Begin(model,request,plan);},"Rebased plan whose target was deleted is rejected");Require(EditStateFingerprint(model)==deleted&&!executor.Running(),"Rejected rebase changes nothing");
    fresh("Rig changed");model.skeleton.bones[9].bindLocal.translation.x+=.1f;
    try{executor.Begin(model,request,plan);throw std::runtime_error("Rig change must reject the plan");}catch(const std::runtime_error& e){Require(std::string(e.what())==PlanRigChangedError,"Rig changes while waiting reject the plan");}
    model=MakeDemoScene();request=EditSceneContext(model,0,9,-1,0,session.Draft(model,model.clips[0],"Cancelled"));plan["requestId"]=request["requestId"];plan["sessionId"]=request["sessionId"];executor.Begin(model,request,plan);executor.Cancel();executor.Advance(model);Require(model.clips.size()==3,"Cancelled plan changes nothing");
    auto source=MakeDemoScene();ProportionSettings fk;fk.handIK=false;fk.footIK=false;fk.balance=false;fk.fps=8;
    auto same=AdaptMotion(source.skeleton,source.clips[0],source.skeleton,fk);
    for(double t:{0.0,.5,1.0,1.5,2.0,3.0})SamePose(EvaluatePose(source.skeleton,&source.clips[0],t),EvaluatePose(source.skeleton,&same.clip,t));
    auto shortRig=source.skeleton;for(auto& b:shortRig.bones){b.bindLocal.translation.x*=.5f;b.bindLocal.translation.y*=.5f;b.bindLocal.translation.z*=.5f;}
    auto moving=NewClip("Root stride",1);auto root=source.skeleton.bones[0].bindLocal;SetTransformKey(moving,0,0,root);root.translation.z=2;SetTransformKey(moving,0,1,root);
    auto scaled=AdaptMotion(source.skeleton,moving,shortRig,fk);Near(scaled.report.heightRatio,.5,.001,"Height ratio from rest skeleton");Near(EvaluatePose(shortRig,&scaled.clip,1).global[0]._43,1,.001,"Smaller character stride scales by height");
    auto scaledRoot=source.skeleton;scaledRoot.bones[0].bindLocal.scale={2,2,2};auto hipMotion=NewClip("Hip stride",1);auto hip=source.skeleton.bones[1].bindLocal;SetTransformKey(hipMotion,1,0,hip);hip.translation.z=1;SetTransformKey(hipMotion,1,1,hip);
    auto rootAdapt=AdaptMotion(source.skeleton,hipMotion,scaledRoot,fk);Near(EvaluatePose(scaledRoot,&rootAdapt.clip,1).global[1]._43,2,.001,"Root scale is not applied twice to pelvis motion");
    auto longArms=source.skeleton;longArms.bones[8].bindLocal.translation.y*=1.5f;longArms.bones[9].bindLocal.translation.y*=1.5f;
    auto raw=AdaptMotion(source.skeleton,source.clips[0],longArms,fk);auto ik=fk;ik.handIK=true;auto corrected=AdaptMotion(source.skeleton,source.clips[0],longArms,ik);
    auto distance=[](const Pose& a,const Pose& b,int bone){const auto& x=a.global[bone];const auto& y=b.global[bone];return std::sqrt((x._41-y._41)*(x._41-y._41)+(x._42-y._42)*(x._42-y._42)+(x._43-y._43)*(x._43-y._43));};
    const auto original=EvaluatePose(source.skeleton,&source.clips[0],1.5);Require(distance(original,EvaluatePose(longArms,&corrected.clip,1.5),9)<distance(original,EvaluatePose(longArms,&raw.clip,1.5),9),"Long-arm IK improves hand contact");
    Near(EvaluatePose(longArms,&corrected.clip,1.5).local[8].translation.y,longArms.bones[8].bindLocal.translation.y,1e-5,"Retarget preserves target limb lengths");
    auto unknown=shortRig;for(auto& b:unknown.bones){b.name="unknown_"+b.name;b.path="unknown_"+b.path;}
    Throws([&]{AdaptMotion(source.skeleton,source.clips[0],unknown,fk);},"Unmapped rig rejected instead of invented mapping");
    auto withFeet=source.skeleton;
    for(const auto& item:std::vector<std::pair<std::string,int>>{{"LeftFoot",11},{"RightFoot",13}}){Transform foot;foot.translation={0,-.35f,.05f};withFeet.bones.push_back({item.first,withFeet.bones[item.second].path+"/"+item.first,item.second,foot});}
    auto grounded=NewClip("Ground and balance test",1);auto below=withFeet.bones[0].bindLocal;below.translation.y=-.2f;SetTransformKey(grounded,0,0,below);SetTransformKey(grounded,0,1,below);
    auto leaning=withFeet.bones[3].bindLocal;leaning.translation.z+=.8f;SetTransformKey(grounded,3,0,leaning);SetTransformKey(grounded,3,1,leaning);
    ProportionSettings legs;legs.fps=8;auto groundResult=AdaptMotion(withFeet,grounded,withFeet,legs);
    Require(groundResult.report.groundCorrections>0&&groundResult.report.balanceCorrections>0,"Recognized foot chains drive ground and support-balance correction");
    auto penetrating=NewClip("Torso penetration",1);auto hand=withFeet.bones[9].bindLocal;hand.translation={.39f,.09f,0};SetTransformKey(penetrating,9,0,hand);SetTransformKey(penetrating,9,1,hand);
    Require(InspectMotion(withFeet,penetrating,8).bodyWarnings>0,"Pose diagnostics detect a hand inside the torso proxy");
    std::cout<<"PASS multi-step AI plan preflight, aliases, visible execution, cancellation/conflicts, proportion FK/IK and stride adaptation\n";
}
// A sword swing must move the whole body: hips drop, shoulders coil and uncoil, the sword-side
// foot steps into the cut while the other foot stays planted, and no foot sinks into the floor.
using Ankle=std::pair<int,XMFLOAT3>;
static void RequireFullBodySwing(const SkeletonData& skeleton,const AnimationClip& clip,int leftArm,int rightArm,int hips,const Ankle& lead,const Ankle& trail,FXMVECTOR forward,float legLength){
    const double duration=PlaybackDuration(clip);
    auto ankle=[](const Pose& pose,const Ankle& foot){return XMVector3TransformCoord(XMLoadFloat3(&foot.second),XMLoadFloat4x4(&pose.global[foot.first]));};
    const auto rest=EvaluatePose(skeleton,nullptr,0),start=EvaluatePose(skeleton,&clip,0);
    float drift=0,lowest=1e9f;
    for(int i=0;i<=120;++i){const auto pose=EvaluatePose(skeleton,&clip,duration*i/120);
        drift=std::max(drift,XMVectorGetX(XMVector3Length(ankle(pose,trail)-ankle(start,trail))));
        for(const Ankle* foot:{&lead,&trail})lowest=std::min(lowest,XMVectorGetY(ankle(pose,*foot))-XMVectorGetY(ankle(rest,*foot)));
    }
    Require(drift<.02f*legLength,"Trailing foot stays planted during the full-body swing");
    Require(lowest>-.01f*legLength,"Feet never sink below their rest height");
    const auto strike=EvaluatePose(skeleton,&clip,duration*.55);
    Require(XMVectorGetX(XMVector3Dot(ankle(strike,lead)-ankle(start,lead),forward))>.15f*legLength,"Sword-side foot steps forward into the cut");
    Require(start.global[hips]._42-strike.global[hips]._42>.04f*legLength,"Hips drop into the strike");
    auto shoulders=[&](double t){const auto p=EvaluatePose(skeleton,&clip,t);
        return XMVector3Normalize(XMVectorSet(p.global[rightArm]._41-p.global[leftArm]._41,0,p.global[rightArm]._43-p.global[leftArm]._43,0));};
    Require(XMVectorGetX(XMVector3Dot(shoulders(duration*.28),shoulders(duration*.62)))<std::cos(XMConvertToRadians(30.f)),"Shoulders coil back for the wind-up and uncoil through the strike");
}
static void TestDirectionalMotion(const std::filesystem::path& out){
    auto makePlan=[](const Json& request,const Json& steps){return Json{{"format","lunaflow.edit.plan"},{"version",1},{"requestId",request["requestId"]},{"sessionId",request["sessionId"]},{"summary","Directional regression"},{"steps",steps}};};
    auto step=[](const char* operation,const Json& args){return Json{{"stepId","s1"},{"operation",operation},{"targetRef","selectedClip"},{"resultRef","swing"},{"description",operation},{"arguments",args}};};
    AgentSession session;session.Reset();
    for(const float yaw:{0.f,90.f,180.f})for(const std::string side:{"right","left"}){
        auto model=MakeDemoScene();model.skeleton.bones[0].bindLocal.rotation=QuaternionFromDegrees({0,yaw,0});
        if(yaw==90)model.skeleton.bones[0].bindLocal.scale={1.3f,.8f,.6f};
        // Reorient local bind axes without changing the arm's global rest landmarks.
        // This reproduces FBX rigs whose X/Y/Z axes differ from Luna's local axes.
        const int arm=side=="right"?7:4,fore=side=="right"?8:5,hand=side=="right"?9:6;
        const auto rest=EvaluatePose(model.skeleton,nullptr,0);
        model.skeleton.bones[arm].bindLocal.rotation=QuaternionFromDegrees({25,35,60});
        auto rotated=EvaluatePose(model.skeleton,nullptr,0);
        model.skeleton.bones[fore].bindLocal=Decompose(XMLoadFloat4x4(&rest.global[fore])*XMMatrixInverse(nullptr,XMLoadFloat4x4(&rotated.global[arm])));
        model.skeleton.bones[fore].bindLocal.rotation=QuaternionFromDegrees({-20,15,-35});
        rotated=EvaluatePose(model.skeleton,nullptr,0);
        model.skeleton.bones[hand].bindLocal=Decompose(XMLoadFloat4x4(&rest.global[hand])*XMMatrixInverse(nullptr,XMLoadFloat4x4(&rotated.global[fore])));
        auto request=EditSceneContext(model,0,hand,-1,0,session.Draft(model,model.clips[0],"검 휘두르기"));
        Require(request["characterFrame"]["available"].get<bool>(),"Humanoid facing is available with arbitrary local bind axes");
        const auto& f=request["characterFrame"]["forwardWorld"];XMFLOAT3 forward{f[0].get<float>(),f[1].get<float>(),f[2].get<float>()};
        const auto expected=XMVector3TransformNormal(XMVectorSet(0,0,-1,0),XMMatrixRotationY(XMConvertToRadians(yaw)));
        Near(XMVectorGetX(XMVector3Dot(XMLoadFloat3(&forward),expected)),1,1e-5,"Facing follows the character instead of assuming world +Z");
        const auto original=EditStateFingerprint(model);EditPlan executor;
        const auto plan=makePlan(request,Json::array({step("create_sword_swing",{{"name","Forward sword slash"},{"duration",1.2},{"side",side}})}));
        executor.Begin(model,request,plan);Require(EditStateFingerprint(model)==original,"Swing preflight preserves original scene");executor.Advance(model);
        Require(model.clips.size()==4&&!model.clips.back().loop,"Swing creates a separate non-looping clip");
        const auto& clip=model.clips.back();
        for(int i=0;i<=120;++i){const double t=1.2*i/120;const auto pose=EvaluatePose(model.skeleton,&clip,t);
            const auto& shoulder=pose.global[arm];const auto& palm=pose.global[hand];const auto distance=XMVectorSet(palm._41-shoulder._41,palm._42-shoulder._42,palm._43-shoulder._43,0);
            const float front=XMVectorGetX(XMVector3Dot(distance,XMLoadFloat3(&forward)));
            Require(front>-.0001f,"Sword hand never swings behind the shoulder throughout interpolated motion");
            if(i>=20&&i<=90)Require(front>.08f,"Active sword strike stays in the character's front hemisphere");
            for(const int bone:{arm,fore,hand}){
                const auto& a=pose.local[bone].translation;const auto& b=model.skeleton.bones[bone].bindLocal.translation;
                Near(a.x,b.x,1e-5,"Directional swing preserves local bone position x");Near(a.y,b.y,1e-5,"Directional swing preserves local bone position y");Near(a.z,b.z,1e-5,"Directional swing preserves local bone position z");
            }
        }
        if(yaw!=90){ // Luna's shins are leaf bones; the ankle sits one shin length (0.40) below the knee.
            const bool right=side=="right";
            RequireFullBodySwing(model.skeleton,clip,4,7,1,{right?13:11,{0,-.40f,0}},{right?11:13,{0,-.40f,0}},XMLoadFloat3(&forward),.8f);
        }
        // Reusing the same semantic direction on a reoriented rig must aim forward exactly.
        request=EditSceneContext(model,0,hand,-1,0,session.Draft(model,model.clips[0],"Point forward"));
        auto aimPlan=makePlan(request,Json::array({step("aim_bone_key",{{"bonePath",model.skeleton.bones[arm].path},{"childBonePath",model.skeleton.bones[fore].path},{"time",0},{"direction",{0,0,1}},{"space","character"}})}));
        executor.Begin(model,request,aimPlan);executor.Advance(model);const auto aimed=EvaluatePose(model.skeleton,&model.clips[0],0);
        const auto& a=aimed.global[arm];const auto& b=aimed.global[fore];const auto axis=XMVector3Normalize(XMVectorSet(b._41-a._41,b._42-a._42,b._43-a._43,0));
        Near(XMVectorGetX(XMVector3Dot(axis,XMLoadFloat3(&forward))),1,1e-5,"Aim converts anatomical world direction into reoriented bone-local rotation");
        auto invalid=aimPlan;invalid["steps"][0]["arguments"]["direction"]={0,0,0};const auto stable=EditStateFingerprint(model);
        request=EditSceneContext(model,0,hand,-1,0,session.Draft(model,model.clips[0],"Invalid direction"));invalid["requestId"]=request["requestId"];invalid["sessionId"]=request["sessionId"];
        Throws([&]{executor.Begin(model,request,invalid);},"Zero direction is rejected during preflight");Require(EditStateFingerprint(model)==stable,"Invalid aiming preserves the scene");
        if(yaw==0&&side=="right"){
            auto prop=MakeEmptyScene();const auto blade=AddPrimitive(prop,Primitive::Cube);prop.skeleton.bones[blade].bindLocal.scale={.06f,.65f,.025f};
            const auto id=AppendModel(model,prop,"PracticeSword");AttachObject(model,id,hand,&model.clips.back(),0,false);
            ProjectData project;project.model=model;project.selectedClipId=model.clips.back().id;project.selectedBonePath=model.skeleton.bones[hand].path;project.time=.5;project.cameraYaw=.5f;
            SaveProject(out/"Forward-Sword-Swing.lunaflow",project);
        }
    }
    for(const bool mirrored:{false,true}){
        auto model=MakeDemoScene();
        if(mirrored)model.skeleton.bones[0].bindLocal.scale={-1,1,1};
        else model.skeleton.bones[8].parent=0; // Named forearm belongs to a different branch.
        auto request=EditSceneContext(model,0,9,-1,0,session.Draft(model,model.clips[0],"Unsupported arm rig"));
        if(mirrored)Require(!request["characterFrame"]["available"].get<bool>(),"Mirrored character does not infer the wrong front direction");
        const auto before=EditStateFingerprint(model);EditPlan executor;auto plan=makePlan(request,Json::array({step("create_sword_swing",{{"side","right"}})}));
        Throws([&]{executor.Begin(model,request,plan);},"Mirrored or disconnected arm rig is rejected before execution");Require(EditStateFingerprint(model)==before,"Unsupported rig rejection preserves every original clip");
    }
    const auto defaultCharacter=std::filesystem::path(LUNAFLOW_SOURCE_DIR)/"Assets"/"Characters"/"DefaultCharacter.fbx";
    if(std::filesystem::exists(defaultCharacter)){
        auto model=ImportScene(defaultCharacter);if(model.clips.empty())model.clips.push_back(NewClip("Idle",1));
        auto request=EditSceneContext(model,0,0,-1,0,session.Draft(model,model.clips[0],"검 휘두르기"));
        Require(!request["characterFrame"]["available"].get<bool>(),"Supplied static FBX does not invent humanoid facing or bone mapping");
        const auto before=EditStateFingerprint(model);EditPlan executor;auto plan=makePlan(request,Json::array({step("create_sword_swing",{{"side","right"}})}));
        Throws([&]{executor.Begin(model,request,plan);},"Unrigged supplied FBX explicitly rejects humanoid swing generation");Require(EditStateFingerprint(model)==before,"Missing rig rejection preserves imported character");
        // The editor auto-rigs this static mesh on startup; the rigged character must then slash forward.
        RemoveReferenceImagePlanes(model);const auto rig=AutoRig(model);
        const int arm=static_cast<int>(rig.bones[12]),hand=static_cast<int>(rig.bones[14]);
        request=EditSceneContext(model,0,hand,-1,0,session.Draft(model,model.clips[0],"검 휘두르기"));
        Require(request["characterFrame"]["available"].get<bool>(),"Auto-rigged default character resolves a humanoid frame");
        const auto& f=request["characterFrame"]["forwardWorld"];const XMVECTOR forward=XMVectorSet(f[0].get<float>(),f[1].get<float>(),f[2].get<float>(),0);
        Require(XMVectorGetX(XMVector3Dot(forward,XMLoadFloat3(&rig.forward)))>.99f,"Agent facing agrees with the facing auto-rig detected from the feet");
        plan=makePlan(request,Json::array({step("create_sword_swing",{{"name","Forward sword slash"},{"duration",1.2},{"side","right"}})}));
        executor.Begin(model,request,plan);executor.Advance(model);const auto& clip=model.clips.back();
        float minFront=1,highest=-1;
        for(int i=0;i<=120;++i){const auto pose=EvaluatePose(model.skeleton,&clip,1.2*i/120);const auto& s=pose.global[arm];const auto& h=pose.global[hand];
            const float front=XMVectorGetX(XMVector3Dot(XMVectorSet(h._41-s._41,h._42-s._42,h._43-s._43,0),forward));minFront=std::min(minFront,front);highest=std::max(highest,h._42-s._42);
            if(i>=20&&i<=90)Require(front>.05f,"Auto-rigged default character strikes in front of the body");}
        Require(minFront>-.0001f&&highest>.1f,"Auto-rigged default character winds up overhead and never swings the sword arm backward");
        const auto rest=EvaluatePose(model.skeleton,nullptr,0);const auto bone=[&](int joint){return static_cast<int>(rig.bones[static_cast<size_t>(joint)]);};
        const float legLength=XMVectorGetX(XMVector3Length(XMVectorSet(rest.global[bone(23)]._41-rest.global[bone(21)]._41,rest.global[bone(23)]._42-rest.global[bone(21)]._42,rest.global[bone(23)]._43-rest.global[bone(21)]._43,0)));
        RequireFullBodySwing(model.skeleton,clip,bone(7),arm,bone(0),{bone(23),{}},{bone(18),{}},forward,legLength);
    }
    std::cout<<"PASS anatomical facing, rotated FBX-style bind axes, both-hand forward sword motion, directional aiming and unrigged-model rejection\n";
}
static void TestImportedRigSwordSwing(){
    // Regression: imported FBX rigs (e.g. Mixamo after ConvertToLeftHanded) have the right
    // shoulder at -X and face -Z. A right-handed facing formula swung the sword backward.
    ModelAsset model;model.source="Imported LH T-pose";
    auto add=[&](const char* name,int parent,XMFLOAT3 position){Transform t;t.translation=position;
        const std::string path=parent<0?std::string("/")+name:model.skeleton.bones[parent].path+"/"+name;
        model.skeleton.bones.push_back({name,path,parent,t});return static_cast<int>(model.skeleton.bones.size()-1);};
    const int hips=add("mixamorig:Hips",-1,{0,1,0}),spine=add("mixamorig:Spine",hips,{0,.35f,0});add("mixamorig:Head",spine,{0,.3f,0});
    const int rightArm=add("mixamorig:RightArm",spine,{-.2f,.2f,0}),rightFore=add("mixamorig:RightForeArm",rightArm,{-.28f,0,0}),rightHand=add("mixamorig:RightHand",rightFore,{-.25f,0,0});
    const int leftArm=add("mixamorig:LeftArm",spine,{.2f,.2f,0}),leftFore=add("mixamorig:LeftForeArm",leftArm,{.28f,0,0});add("mixamorig:LeftHand",leftFore,{.25f,0,0});
    // Toes point toward the character's real front (-Z) independently of the facing heuristic.
    const int foot=add("mixamorig:RightFoot",hips,{-.1f,-.9f,0});const int toe=add("mixamorig:RightToeBase",foot,{0,-.05f,-.15f});
    model.clips.push_back(NewClip("Idle",1));
    AgentSession session;session.Reset();
    auto request=EditSceneContext(model,0,rightHand,-1,0,session.Draft(model,model.clips[0],"검 휘두르기"));
    Require(request["characterFrame"]["available"].get<bool>(),"Imported Mixamo-style rig resolves a character frame");
    const auto bind=EvaluatePose(model.skeleton,nullptr,0);
    const XMVECTOR front=XMVector3Normalize(XMVectorSet(bind.global[toe]._41-bind.global[foot]._41,0,bind.global[toe]._43-bind.global[foot]._43,0));
    const auto& f=request["characterFrame"]["forwardWorld"];const auto& r=request["characterFrame"]["rightWorld"];
    Near(XMVectorGetX(XMVector3Dot(XMVectorSet(f[0].get<float>(),f[1].get<float>(),f[2].get<float>(),0),front)),1,1e-5,"Left-handed facing points where the toes point");
    Near(r[0].get<float>(),-1,1e-5,"Anatomical right of a -Z facing LH rig is -X");
    Json plan={{"format","lunaflow.edit.plan"},{"version",1},{"requestId",request["requestId"]},{"sessionId",request["sessionId"]},{"summary","Imported sword"},{"steps",Json::array({
        Json{{"stepId","s1"},{"operation","create_sword_swing"},{"targetRef","selectedClip"},{"resultRef","swing"},{"description","swing"},{"arguments",{{"side","right"},{"duration",1.0}}}}})}};
    EditPlan executor;executor.Begin(model,request,plan);executor.Advance(model);const auto& clip=model.clips.back();
    float highest=-1,lowest=1,minFront=1;
    for(int i=0;i<=100;++i){const double t=i/100.0;const auto pose=EvaluatePose(model.skeleton,&clip,t);
        const auto& s=pose.global[rightArm];const auto& h=pose.global[rightHand];
        const auto reach=XMVectorSet(h._41-s._41,h._42-s._42,h._43-s._43,0);const float ahead=XMVectorGetX(XMVector3Dot(reach,front));
        minFront=std::min(minFront,ahead);
        if(i>=10&&i<=30)highest=std::max(highest,h._42-s._42);
        if(i>=60&&i<=85)lowest=std::min(lowest,h._42-s._42);
        if(i>=15&&i<=80)Require(ahead>.08f,"Imported rig strike stays in front of the shoulder");
    }
    Require(minFront>-.0001f,"Imported rig sword hand never passes behind the shoulder");
    Require(highest>.2f&&lowest<-.2f,"Sword swing winds up overhead and cuts downward");
    const auto guard=EvaluatePose(model.skeleton,&clip,0);
    Require(guard.global[leftFore]._42<guard.global[leftArm]._42-.1f,"Free arm relaxes from the T-pose instead of staying horizontal");
    // Without leg bones the pelvis stays put (feet cannot slide), but the spine still coils and uncoils.
    auto across=[&](double t){const auto p=EvaluatePose(model.skeleton,&clip,t);
        return XMVector3Normalize(XMVectorSet(p.global[rightArm]._41-p.global[leftArm]._41,0,p.global[rightArm]._43-p.global[leftArm]._43,0));};
    Require(XMVectorGetX(XMVector3Dot(across(.28),across(.62)))<std::cos(XMConvertToRadians(30.f)),"Spine twist drives the swing on rigs without leg bones");
    for(int i=0;i<=20;++i){const auto pose=EvaluatePose(model.skeleton,&clip,i/20.0);
        Near(pose.global[foot]._41,bind.global[foot]._41,1e-5,"Unsolvable legs keep the foot planted x");Near(pose.global[foot]._43,bind.global[foot]._43,1e-5,"Unsolvable legs keep the foot planted z");}
    std::cout<<"PASS imported left-handed rig facing and forward overhead sword slash\n";
}
// Unskinned box humanoid: a single static mesh under one root node, like an unrigged FBX character.
static void AddBox(MeshData& mesh,XMFLOAT3 lo,XMFLOAT3 hi){
    constexpr float step=.025f;const float l[3]={lo.x,lo.y,lo.z},h[3]={hi.x,hi.y,hi.z};
    for(int axis=0;axis<3;++axis)for(int side=0;side<2;++side){
        const int u=(axis+1)%3,v=(axis+2)%3;
        const int nu=std::max(1,static_cast<int>(std::ceil((h[u]-l[u])/step))),nv=std::max(1,static_cast<int>(std::ceil((h[v]-l[v])/step)));
        const auto base=static_cast<uint32_t>(mesh.vertices.size()),row=static_cast<uint32_t>(nu+1);
        for(int b=0;b<=nv;++b)for(int a=0;a<=nu;++a){
            float p[3],n[3]={0,0,0};p[axis]=side?h[axis]:l[axis];n[axis]=side?1.f:-1.f;
            p[u]=l[u]+(h[u]-l[u])*static_cast<float>(a)/static_cast<float>(nu);p[v]=l[v]+(h[v]-l[v])*static_cast<float>(b)/static_cast<float>(nv);
            Vertex vertex;vertex.position={p[0],p[1],p[2]};vertex.normal={n[0],n[1],n[2]};vertex.weights={1,0,0,0};mesh.vertices.push_back(vertex);
        }
        for(uint32_t b=0;b<static_cast<uint32_t>(nv);++b)for(uint32_t a=0;a<static_cast<uint32_t>(nu);++a){
            const uint32_t i=base+b*row+a;
            if(side)mesh.indices.insert(mesh.indices.end(),{i,i+1,i+row+1,i,i+row+1,i+row});
            else mesh.indices.insert(mesh.indices.end(),{i,i+row+1,i+1,i,i+row,i+row+1});
        }
    }
}
enum class BoxPose{T,A,I};
static ModelAsset BoxHero(float yaw,BoxPose pose,bool robe=false){
    ModelAsset model;model.source="BoxHero";
    Bone root;root.name="BoxHero";root.path="/BoxHero";root.bindLocal.rotation=QuaternionFromDegrees({0,yaw,0});model.skeleton.bones.push_back(root);
    MeshData mesh;mesh.name="Body";SkinBinding binding;XMStoreFloat4x4(&binding.inverseBind,XMMatrixIdentity());mesh.skin.push_back(binding);
    for(const float side:{1.f,-1.f}){
        const float a=side>0?.05f:-.17f,b=side>0?.17f:-.05f;
        if(!robe)AddBox(mesh,{a,.08f,-.06f},{b,.9f,.06f});
        AddBox(mesh,{a,0,-.22f},{b,.08f,.06f}); // Feet point toward the character's front (-Z).
        if(pose==BoxPose::T)AddBox(mesh,{side>0?.18f:-.85f,1.33f,-.05f},{side>0?.85f:-.18f,1.43f,.05f});
        if(pose==BoxPose::I){ // Arm hangs beside the torso, joined only by a shoulder block.
            AddBox(mesh,{side>0?.21f:-.31f,.8f,-.05f},{side>0?.31f:-.21f,1.43f,.05f});
            AddBox(mesh,{side>0?.12f:-.31f,1.33f,-.05f},{side>0?.31f:-.12f,1.45f,.05f});
        }
        if(pose==BoxPose::A){
            MeshData arm;AddBox(arm,{0,-.05f,-.05f},{.62f,.05f,.05f});
            const XMMATRIX m=XMMatrixRotationZ(XMConvertToRadians(side>0?-45.f:225.f))*XMMatrixTranslation(side*.16f,1.38f,0);
            const auto base=static_cast<uint32_t>(mesh.vertices.size());
            for(auto vertex:arm.vertices){XMStoreFloat3(&vertex.position,XMVector3TransformCoord(XMLoadFloat3(&vertex.position),m));XMStoreFloat3(&vertex.normal,XMVector3TransformNormal(XMLoadFloat3(&vertex.normal),m));mesh.vertices.push_back(vertex);}
            for(const auto index:arm.indices)mesh.indices.push_back(base+index);
        }
    }
    if(robe)AddBox(mesh,{-.2f,.08f,-.1f},{.2f,.9f,.1f});
    AddBox(mesh,{-.18f,.85f,-.1f},{.18f,1.45f,.1f});
    AddBox(mesh,{-.05f,1.44f,-.05f},{.05f,1.56f,.05f});
    AddBox(mesh,{-.11f,1.55f,-.11f},{.11f,1.82f,.11f});
    model.meshes.push_back(std::move(mesh));model.materials.push_back({});model.objects.push_back({"object-1","BoxHero",0});
    model.clips.push_back(NewClip("Idle",1));
    return model;
}
static std::vector<XMFLOAT3> SkinnedPositions(const ModelAsset& model,const Pose& pose){
    std::vector<XMFLOAT3> positions;bool valid=true;
    for(const auto& mesh:model.meshes)for(const auto& vertex:mesh.vertices){
        XMVECTOR position=XMVectorZero();float sum=0;
        for(size_t k=0;k<vertex.weights.size();++k){
            const float weight=vertex.weights[k];if(weight==0)continue;
            if(!(weight>0)||vertex.joints[k]>=mesh.skin.size()||mesh.skin[vertex.joints[k]].bone>=pose.global.size()){valid=false;continue;}
            const auto& binding=mesh.skin[vertex.joints[k]];
            position+=XMVector3TransformCoord(XMLoadFloat3(&vertex.position),XMLoadFloat4x4(&binding.inverseBind)*XMLoadFloat4x4(&pose.global[binding.bone]))*weight;sum+=weight;
        }
        valid=valid&&std::abs(sum-1)<1e-4f;positions.emplace_back();XMStoreFloat3(&positions.back(),position);
    }
    Require(valid,"Skin weights are positive, reference existing bones and sum to one");
    return positions;
}
static float MaxShift(const std::vector<XMFLOAT3>& a,const std::vector<XMFLOAT3>& b,const std::function<bool(const XMFLOAT3&)>& include){
    Require(a.size()==b.size(),"Vertex counts match");float shift=0;
    for(size_t i=0;i<a.size();++i)if(include(a[i]))shift=std::max(shift,XMVectorGetX(XMVector3Length(XMLoadFloat3(&a[i])-XMLoadFloat3(&b[i]))));
    return shift;
}
static void TestAutoRig(){
    auto joint=[](const AutoRigReport& report,const std::string& name){
        for(const auto& j:report.joints)if(j.name==name)return j.position;
        throw std::runtime_error("Missing auto-rig joint "+name);
    };
    for(const float yaw:{0.f,-135.f})for(const BoxPose pose:{BoxPose::T,BoxPose::A}){
        auto model=BoxHero(yaw,pose);
        Require(NeedsAutoRig(model),"Unskinned box character is offered auto-rig");
        const auto before=SkinnedPositions(model,EvaluatePose(model.skeleton,nullptr,0));
        const auto report=AutoRig(model);
        Require(!NeedsAutoRig(model)&&report.joints.size()==26&&report.bones.size()==26&&report.warnings.empty(),"T/A pose rigs a complete humanoid without fallbacks");
        Require(model.skeleton.bones[report.bones[0]].name=="Hips"&&model.skeleton.bones[report.bones[14]].name=="RightHand","Auto rig uses humanoid bone names the motion tools recognize");
        // Compare in the character's own frame: front -Z, left +X before the root yaw.
        const XMMATRIX toCharacter=XMMatrixRotationY(XMConvertToRadians(-yaw));
        auto local=[&](const std::string& name){const auto w=joint(report,name);XMFLOAT3 p;XMStoreFloat3(&p,XMVector3TransformNormal(XMLoadFloat3(&w),toCharacter));return p;};
        XMFLOAT3 forward;XMStoreFloat3(&forward,XMVector3TransformNormal(XMLoadFloat3(&report.forward),toCharacter));
        Near(forward.z,-1,.01,"Auto rig detects the facing from the feet for any yaw");
        const auto hips=local("Hips"),neck=local("Neck"),head=local("Head");
        Require(hips.y>.85f&&hips.y<1.05f&&neck.y>1.38f&&neck.y<1.5f&&head.y>neck.y&&head.y<1.6f,"Spine joints sit at the pelvis, neck and skull base");
        for(const float side:{1.f,-1.f}){
            const std::string prefix=side>0?"Left":"Right";
            const auto arm=local(prefix+"UpperArm"),fore=local(prefix+"ForeArm"),hand=local(prefix+"Hand");
            const auto thigh=local(prefix+"UpperLeg"),knee=local(prefix+"LowerLeg"),foot=local(prefix+"Foot"),toes=local(prefix+"Toes_End");
            Require(side*arm.x>.05f&&side*arm.x<.25f&&arm.y>1.3f&&arm.y<1.45f,"Shoulder joints sit at the top of the torso on the correct side");
            Require(side*fore.x>side*arm.x+.12f&&side*hand.x>side*fore.x+.1f,"Elbow and wrist follow the arm outward");
            if(pose==BoxPose::T)Require(std::abs(hand.y-arm.y)<.06f&&side*hand.x>.55f,"T-pose wrist stays level near the end of the arm");
            else Require(hand.y<fore.y-.08f&&fore.y<arm.y-.08f,"A-pose elbow and wrist follow the arm downward");
            Require(side*thigh.x>.05f&&side*thigh.x<.17f&&knee.y>.35f&&knee.y<.65f&&foot.y<.2f&&toes.z<foot.z-.1f,"Leg joints follow the leg and the toes point forward");
        }
        const auto rest=SkinnedPositions(model,EvaluatePose(model.skeleton,nullptr,0));
        Require(MaxShift(before,rest,[](const XMFLOAT3&){return true;})<1e-4f,"Skinned bind pose reproduces the modeled mesh exactly");
        // Posing the left upper arm moves the left arm only.
        const auto turn=QuaternionFromDegrees({30,0,40});auto& rotation=model.skeleton.bones[report.bones[7]].bindLocal.rotation;
        XMStoreFloat4(&rotation,XMQuaternionMultiply(XMLoadFloat4(&rotation),XMLoadFloat4(&turn)));
        const auto posed=SkinnedPositions(model,EvaluatePose(model.skeleton,nullptr,0));
        auto sideOf=[&](const XMFLOAT3& p){return XMVectorGetX(XMVector3TransformNormal(XMLoadFloat3(&p),toCharacter));};
        const auto leftHand=joint(report,"LeftHand");
        Require(MaxShift(rest,posed,[&](const XMFLOAT3& p){return XMVectorGetX(XMVector3Length(XMLoadFloat3(&p)-XMLoadFloat3(&leftHand)))<.06f;})>.15f,"Rotating the upper arm carries the hand");
        Require(MaxShift(rest,posed,[](const XMFLOAT3& p){return p.y<.8f||p.y>1.5f;})<1e-4f,"Arm pose leaves the legs and head in place");
        Require(MaxShift(rest,posed,[&](const XMFLOAT3& p){return sideOf(p)<-.3f;})<1e-4f,"Arm pose leaves the other arm in place");
    }
    {   // Arms resting at the sides (I pose) still receive elbows and wrists along the arm.
        auto model=BoxHero(0,BoxPose::I);const auto report=AutoRig(model);
        const auto arm=joint(report,"LeftUpperArm"),fore=joint(report,"LeftForeArm"),hand=joint(report,"LeftHand");
        Require(fore.y<arm.y-.15f&&hand.y<fore.y-.1f&&hand.x>.15f&&hand.y>.75f,"I-pose elbow and wrist follow the hanging arm");
    }
    {   // A robe hides the legs: the feet sticking out below the hem still give the facing.
        auto model=BoxHero(180,BoxPose::A,true);const auto report=AutoRig(model);
        Require(report.forward.z>.99f,"Robed character facing +Z is detected from its feet");
        Require(joint(report,"LeftHand").x<-.3f,"Left side follows the detected facing");
    }
    {   // Clean T-pose sword swing on a rotated, auto-rigged character.
        auto model=BoxHero(-135,BoxPose::T);const auto report=AutoRig(model);
        const int arm=static_cast<int>(report.bones[12]),hand=static_cast<int>(report.bones[14]);
        AgentSession session;session.Reset();
        const auto request=EditSceneContext(model,0,hand,-1,0,session.Draft(model,model.clips[0],"검 휘두르기"));
        Require(request["characterFrame"]["available"].get<bool>(),"Auto-rigged character exposes a humanoid frame to the agent");
        const auto& f=request["characterFrame"]["forwardWorld"];const XMVECTOR forward=XMVectorSet(f[0].get<float>(),f[1].get<float>(),f[2].get<float>(),0);
        Require(XMVectorGetX(XMVector3Dot(forward,XMLoadFloat3(&report.forward)))>.99f,"Agent facing agrees with the auto-rig facing");
        const Json plan={{"format","lunaflow.edit.plan"},{"version",1},{"requestId",request["requestId"]},{"sessionId",request["sessionId"]},{"summary","Auto-rig sword"},{"steps",Json::array({
            Json{{"stepId","s1"},{"operation","create_sword_swing"},{"targetRef","selectedClip"},{"resultRef","swing"},{"description","swing"},{"arguments",{{"side","right"},{"duration",1.0}}}}})}};
        EditPlan executor;executor.Begin(model,request,plan);executor.Advance(model);const auto& clip=model.clips.back();
        float minFront=1;
        for(int i=0;i<=100;++i){const auto pose=EvaluatePose(model.skeleton,&clip,i/100.0);const auto& s=pose.global[arm];const auto& h=pose.global[hand];
            minFront=std::min(minFront,XMVectorGetX(XMVector3Dot(XMVectorSet(h._41-s._41,h._42-s._42,h._43-s._43,0),forward)));}
        Require(minFront>-.0001f,"Sword swing on an auto-rigged character stays in front of the shoulder");
        const auto rest=EvaluatePose(model.skeleton,nullptr,0);const auto bone=[&](int joint){return static_cast<int>(report.bones[static_cast<size_t>(joint)]);};
        const float legLength=XMVectorGetX(XMVector3Length(XMVectorSet(rest.global[bone(23)]._41-rest.global[bone(21)]._41,rest.global[bone(23)]._42-rest.global[bone(21)]._42,rest.global[bone(23)]._43-rest.global[bone(21)]._43,0)));
        RequireFullBodySwing(model.skeleton,clip,bone(7),arm,bone(0),{bone(23),{}},{bone(18),{}},forward,legLength);
    }
    auto unchanged=[](const ModelAsset& a,const ModelAsset& b){
        if(a.skeleton.bones.size()!=b.skeleton.bones.size()||a.meshes.size()!=b.meshes.size())return false;
        for(size_t m=0;m<a.meshes.size();++m){
            const auto& x=a.meshes[m];const auto& y=b.meshes[m];
            if(x.skin.size()!=y.skin.size()||x.vertices.size()!=y.vertices.size())return false;
            for(size_t v=0;v<x.vertices.size();++v)
                if(std::memcmp(&x.vertices[v].position,&y.vertices[v].position,sizeof(XMFLOAT3))!=0||x.vertices[v].weights!=y.vertices[v].weights||x.vertices[v].joints!=y.vertices[v].joints)return false;
        }
        return EditStateFingerprint(a)==EditStateFingerprint(b);
    };
    auto pillar=BoxHero(0,BoxPose::T);pillar.meshes[0].vertices.clear();pillar.meshes[0].indices.clear();AddBox(pillar.meshes[0],{-.15f,0,-.15f},{.15f,1.8f,.15f});
    for(auto shape:{pillar,MakeCubeScene()}){
        const auto original=shape;
        Throws([&]{AutoRig(shape);},"Shapes without legs, neck or arms are rejected instead of receiving a nonsense skeleton");
        Require(unchanged(shape,original),"Rejected auto rig leaves the model untouched");
    }
    auto demo=MakeDemoScene();const auto original=demo;
    Require(!NeedsAutoRig(demo),"Rigged characters are not offered auto-rig");
    Throws([&]{AutoRig(demo);},"Auto rig never replaces an existing humanoid skeleton");Require(unchanged(demo,original),"Refused auto rig leaves the rigged model untouched");
    std::cout<<"PASS auto-rig landmarks, facing, exact bind pose, limb isolation, I-pose and robe fallbacks, rejection and forward sword swing\n";
}
static ModelAsset TestAssembly(const std::filesystem::path& out) {
    auto model=MakeDemoScene();auto prop=MakeEmptyScene();const auto cube=AddPrimitive(prop,Primitive::Cube);
    prop.skeleton.bones[cube].bindLocal.scale={.12f,1.1f,.06f};
    const auto objectId=AppendModel(model,prop,"PracticeSword");Require(model.objects.size()==2,"Character and prop coexist");
    auto hand=std::find_if(model.skeleton.bones.begin(),model.skeleton.bones.end(),[](const auto& b){return b.name=="RightHand";});const int handIndex=static_cast<int>(hand-model.skeleton.bones.begin());
    AttachObject(model,objectId,handIndex,&model.clips[0],0,false);
    auto object=std::find_if(model.objects.begin(),model.objects.end(),[&](const auto& o){return o.id==objectId;});const uint32_t swordRoot=object->root;
    for(double time:{0.0,.7,1.5}){const auto pose=EvaluatePose(model.skeleton,&model.clips[0],time);const int parent=model.skeleton.bones[swordRoot].parent;for(int n=0;n<16;++n)Near(reinterpret_cast<const float*>(&pose.global[swordRoot])[n],reinterpret_cast<const float*>(&pose.global[parent])[n],1e-5,"Attached sword follows hand motion");}
    Throws([&]{AttachObject(model,objectId,static_cast<int>(swordRoot),&model.clips[0],0);},"Attachment cycle rejected");
    AddEffect(model,EffectType::Trail,swordRoot,model.clips[0].id,3);model.effects.back().size=1.1f;model.effects.back().intensity=1.7f;
    AddEffect(model,EffectType::Sparks,swordRoot,model.clips[0].id,3);model.effects.back().color={1,.65f,.2f,1};
    AddEffect(model,EffectType::Aura,1,model.clips[0].id,3);
    ProjectData project;project.model=model;project.selectedClipId=model.clips[0].id;project.selectedBonePath=model.skeleton.bones[swordRoot].path;SaveProject(out/"assembly.lunaflow",project);
    const auto loaded=LoadProject(out/"assembly.lunaflow");Require(loaded.model.objects.size()==2&&loaded.model.effects.size()==3,"Project restores props, attachments and effects");
    SamePose(EvaluatePose(model.skeleton,&model.clips[0],1),EvaluatePose(loaded.model.skeleton,&loaded.model.clips[0],1));
    const auto package=out/"UnityPackage"/"Character.lunaflow.json";
    ExportUnityPackage(package,model,60,std::filesystem::path(LUNAFLOW_SOURCE_DIR).parent_path()/"unity_tools");
    Require(std::filesystem::exists(package.parent_path()/"Character.fbx"),"Unity package contains real FBX");
    Require(std::filesystem::exists(package.parent_path()/"Runtime"/"LunaFlowScene.cs"),"Unity runtime included");
    Json metadata;{std::ifstream f(package);f>>metadata;}Require(metadata["effects"].size()==3&&metadata["objects"].size()==2,"Unity manifest contains effect and assembly contracts");
    auto roundtrip=ImportScene(package.parent_path()/"Character.fbx");Require(roundtrip.clips.size()==model.clips.size(),"Assembled FBX has all motions");
    Json reference=Json::array();for(double time:{0.0,.7,1.5}){const auto pose=EvaluatePose(model.skeleton,&model.clips[0],time);Json positions=Json::array();for(size_t i=0;i<model.skeleton.bones.size();++i){const auto& g=pose.global[i];positions.push_back({{"name",model.skeleton.bones[i].name},{"position",{g._41,g._42,g._43}}});}reference.push_back({{"time",time},{"positions",positions}});}WriteJsonFile(out/"unity-reference.json",{{"frames",reference}});
    std::cout<<"PASS scene assembly, sword attachment, effects, package, native FBX and project roundtrip\n";return model;
}
static void TestRendering(const std::filesystem::path& out,const ModelAsset& animated,const ModelAsset& assembly) {
    ComPtr<ID3D11Device> device; ComPtr<ID3D11DeviceContext> context;
    D3D_FEATURE_LEVEL feature; const D3D_FEATURE_LEVEL levels[]={D3D_FEATURE_LEVEL_11_0};
    HRESULT hr=D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,D3D11_CREATE_DEVICE_DEBUG,levels,1,D3D11_SDK_VERSION,&device,&feature,&context);
    if(FAILED(hr)) hr=D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,levels,1,D3D11_SDK_VERSION,&device,&feature,&context);
    Require(SUCCEEDED(hr),"Create DirectX 11 WARP device");
    ComPtr<ID3D11InfoQueue> debug;device.As(&debug);
    if(debug) debug->ClearStoredMessages();
    RendererDX11 renderer;renderer.Initialize(device.Get(),context.Get(),LUNAFLOW_SHADER_DIR);renderer.Resize(960,720);
    auto demo=MakeDemoScene(); renderer.SetModel(demo);PreviewCamera camera;renderer.FitCamera(camera);
    RenderOptions options;options.grid=false;
    renderer.Render(demo,EvaluatePose(demo.skeleton,&demo.clips[0],0),camera,options);
    auto a=Pixels(device.Get(),context.Get(),renderer);
    renderer.Render(demo,EvaluatePose(demo.skeleton,&demo.clips[0],1.3),camera,options);
    auto b=Pixels(device.Get(),context.Get(),renderer);size_t changed=0,drawn=0;
    for(size_t i=0;i<a.size();i+=4) {if(a[i]!=b[i] || a[i+1]!=b[i+1] || a[i+2]!=b[i+2])++changed;if(b[i]>40 || b[i+1]>40 || b[i+2]>40)++drawn;}
    Require(drawn>5000,"GPU rasterizes model geometry");Require(changed>1000,"GPU skinning changes image across poses");
    options.grid=true;renderer.Render(demo,EvaluatePose(demo.skeleton,&demo.clips[0],1.3),camera,options);renderer.SaveScreenshot(out/"demo-wave.png");
    options.skeleton=true;options.selectedBone=7;renderer.Render(demo,EvaluatePose(demo.skeleton,&demo.clips[0],1.3),camera,options);renderer.SaveScreenshot(out/"demo-skeleton.png");
    renderer.Resize(0,0);Require(renderer.Width()==1 && renderer.Height()==1,"Zero-size viewport safely clamped");
    renderer.Resize(320,800);renderer.FitCamera(camera);renderer.Render(demo,EvaluatePose(demo.skeleton,&demo.clips[0],1.3),camera,options);
    renderer.SetModel(animated);renderer.Resize(960,720);renderer.FitCamera(camera); options.skeleton=false;
    renderer.Render(animated,EvaluatePose(animated.skeleton,&animated.clips[0],PlaybackDuration(animated.clips[0])*.37),camera,options);
    renderer.SaveScreenshot(out/"fbx-animation.png");
    const auto fbxPixels=Pixels(device.Get(),context.Get(),renderer);
    Require(std::any_of(fbxPixels.begin(),fbxPixels.end(),[](uint8_t p){return p>80 && p<255;}),"FBX produces shaded pixels");
    auto textured=ImportScene(std::filesystem::path(ASSIMP_TEST_MODELS)/"glTF2"/"BoxTextured-glTF-Embedded"/"BoxTextured.gltf");
    renderer.SetModel(textured);Require(renderer.Warnings().empty(),"WIC loads embedded texture without fallback");renderer.FitCamera(camera);
    renderer.Render(textured,EvaluatePose(textured.skeleton,nullptr,0),camera,options);renderer.SaveScreenshot(out/"textured-box.png");
    auto distant=MakeEmptyScene();const auto distantBone=AddPrimitive(distant,Primitive::Cube,{30,2,0});renderer.SetModel(distant);renderer.UpdateBounds(distant,EvaluatePose(distant.skeleton,nullptr,0));renderer.FitCamera(camera);
    Near(camera.target.x,30,1e-4,"Frame all follows moved scene objects");Near(camera.target.y,2.5,1e-4,"Frame all follows object height");Require(distantBone>0,"Distant primitive has object root");
    renderer.SetModel(assembly);renderer.FitCamera(camera);options.activeClip=&assembly.clips[0];options.time=1.3;options.skeleton=false;
    options.showEffects=false;renderer.Render(assembly,EvaluatePose(assembly.skeleton,&assembly.clips[0],options.time),camera,options);auto noFx=Pixels(device.Get(),context.Get(),renderer);options.showEffects=true;
    renderer.Render(assembly,EvaluatePose(assembly.skeleton,&assembly.clips[0],options.time),camera,options);renderer.SaveScreenshot(out/"sword-vfx.png");
    auto withFx=Pixels(device.Get(),context.Get(),renderer);size_t fxPixels=0;for(size_t i=0;i<withFx.size();i+=4)if(noFx[i]!=withFx[i]||noFx[i+1]!=withFx[i+1]||noFx[i+2]!=withFx[i+2])++fxPixels;Require(fxPixels>200,"VFX produces visible rendered pixels");
    auto particles=MakeEmptyScene();auto fire=MakeParticleEffect(ParticlePreset::Fire,0,particles.clips[0].id,3);fire.local.translation.y=.25f;particles.effects.push_back(fire);
    renderer.SetModel(particles);renderer.FitCamera(camera);options.skeleton=false;options.grid=false;options.activeClip=&particles.clips[0];options.time=.8;
    auto particlePose=EvaluatePose(particles.skeleton,options.activeClip,options.time);
    options.showEffects=false;renderer.Render(particles,particlePose,camera,options);auto blank=Pixels(device.Get(),context.Get(),renderer);
    options.showEffects=true;renderer.Render(particles,particlePose,camera,options);auto glowing=Pixels(device.Get(),context.Get(),renderer);size_t particlePixels=0;
    for(size_t i=0;i<glowing.size();i+=4)if(blank[i]!=glowing[i]||blank[i+1]!=glowing[i+1]||blank[i+2]!=glowing[i+2])++particlePixels;
    Require(particlePixels>200,"Soft billboard particles rasterize visible pixels");renderer.SaveScreenshot(out/"particles-fire.png");
    for(size_t i=3;i<glowing.size();i+=4)Require(glowing[i]==255,"Particle quads preserve viewport alpha for ImGui compositing");
    particles.effects[0].particles.additive=false;particles.effects[0].particles.worldSpace=true;
    renderer.Render(particles,particlePose,camera,options);auto alpha=Pixels(device.Get(),context.Get(),renderer);Require(alpha!=glowing,"Alpha and additive modes produce distinct images");
    for(size_t i=3;i<alpha.size();i+=4)Require(alpha[i]==255,"Alpha blended particles preserve opaque viewport alpha");
    options.time=1.5;renderer.Render(particles,particlePose,camera,options);options.time=.8;particles.effects[0].particles.additive=true;particles.effects[0].particles.worldSpace=false;
    renderer.Render(particles,particlePose,camera,options);Require(Pixels(device.Get(),context.Get(),renderer)==glowing,"Rendered particles are stable after time scrubbing");
    if(debug) for(UINT64 i=0;i<debug->GetNumStoredMessages();++i) {
        SIZE_T size=0;debug->GetMessage(i,nullptr,&size);std::vector<uint8_t> bytes(size);auto* msg=reinterpret_cast<D3D11_MESSAGE*>(bytes.data());debug->GetMessage(i,msg,&size);
        if(msg->Severity==D3D11_MESSAGE_SEVERITY_ERROR || msg->Severity==D3D11_MESSAGE_SEVERITY_CORRUPTION) throw std::runtime_error(msg->pDescription);
    }
    context->ClearState();context->Flush();
    std::cout<<"PASS DX11 WARP shader compilation, skinning, resize, PNG, textured FBX/glTF"<<(debug?" (debug layer clean)":"")<<"\n";
}
static void TestLiveCopilot(const std::filesystem::path& out){
    auto model=MakeDemoScene();AgentSession session;session.Reset();
    auto request=EditSceneContext(model,0,9,-1,0,session.Draft(model,model.clips[0],"선택한 동작을 복제해서 1.5초로 줄이고, 복제한 동작의 선택된 오른손에 파란 불꽃 파티클을 붙여 재생해 줘. 초당 80개, 수명 1.5초로 해 줘."));
    WriteJsonFile(out/"copilot-live-request.json",request);CopilotProcess bridge;
    bridge.Submit(std::filesystem::path(LUNAFLOW_SOURCE_DIR).parent_path(),request);
    const auto start=GetTickCount64();bool complete=false;
    while(GetTickCount64()-start<360000&&!complete){
        for(const auto& event:bridge.Poll()){
            const auto state=event.value("status",std::string{});std::cout<<state<<": "<<event.value("message",std::string{})<<"\n";
            if(state=="failed")throw std::runtime_error(event.value("message",std::string("SDK failed")));
            if(state=="completed"){
                WriteJsonFile(out/"copilot-live-response.json",event);
                Require(event.contains("plan")&&event["plan"].is_object(),"Live agent returns a machine-readable plan");
                EditPlan plan;plan.Begin(model,request,event["plan"]);while(plan.Running())plan.Advance(model);
                Require(model.clips.size()==4&&model.effects.size()==1,"Live agent creates actual duplicate and particle effect");
                Near(PlaybackDuration(model.clips.back()),1.5,.001,"Live plan changes actual clip timing");
                Require(model.effects[0].bone==9&&model.effects[0].clipId==model.clips.back().id,"Live effect targets the right hand and duplicated clip");
                ProjectData saved;saved.model=model;saved.selectedClipId=model.clips.back().id;saved.selectedBonePath=model.skeleton.bones[9].path;
                SaveProject(out/"Copilot-live.lunaflow",saved);complete=true;
            }
        }
        if(!complete)Sleep(50);
    }
    Require(complete,"Live SDK response timed out");
    std::cout<<"PASS live Copilot Studio -> SDK worker -> validated animation/VFX execution -> saved project\n";
}
static void TestReferenceImagePlanes(){
    auto backdrop=[](float z){
        auto model=MakeEmptyScene();AddPrimitive(model,Primitive::Cube);const auto floor=AddPrimitive(model,Primitive::Plane);
        const auto sheet=AddPrimitive(model,Primitive::Plane);auto& local=model.skeleton.bones[sheet].bindLocal;
        local.rotation=QuaternionFromDegrees({90,0,0});local.translation={0,.5f,z};
        Require(model.meshes.size()==3,"Reference plane scene has character, floor and sheet");
        return std::make_tuple(model,model.skeleton.bones[floor].name,model.skeleton.bones[sheet].name);
    };
    {
        auto [model,floor,sheet]=backdrop(2);const auto removed=RemoveReferenceImagePlanes(model);
        Require(removed.size()==1&&removed[0]==sheet,"Vertical sheet behind the character is detected as a modeling reference");
        Require(model.meshes.size()==2&&std::any_of(model.meshes.begin(),model.meshes.end(),[&](const MeshData& mesh){return mesh.name==floor;}),"Floor planes and character geometry stay visible");
    }
    {
        auto [model,floor,sheet]=backdrop(0);
        Require(RemoveReferenceImagePlanes(model).empty()&&model.meshes.size()==3,"Planes that pass through the character are real geometry");
    }
    auto lone=MakeEmptyScene();AddPrimitive(lone,Primitive::Plane);
    Require(RemoveReferenceImagePlanes(lone).empty()&&lone.meshes.size()==1,"A model is never reduced to nothing");
    std::cout<<"PASS reference image plane cleanup keeps character, floor and intersecting planes\n";
}
int main(int argc,char** argv) {
    std::cout<<std::unitbuf;
    const HRESULT com=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    int code=0;
    try {
        const auto out=std::filesystem::current_path()/"test-output";std::filesystem::create_directories(out);
        if(argc>1&&std::string(argv[1])=="--live-copilot"){TestLiveCopilot(out);return 0;}
        TestEvaluation();TestJson(out);TestEditorFeatures(out);TestReferenceImagePlanes();TestAgentBoundary();TestEditPlansAndProportions();TestDirectionalMotion(out);TestImportedRigSwordSwing();TestAutoRig();TestParticles(out);TestFbxExport(out);auto assembly=TestAssembly(out);auto animated=TestImport();TestRendering(out,animated,assembly);
        std::cout<<"All checks passed ("<<assertions<<" assertions). Artifacts: "<<out<<"\n";
    } catch(const std::exception& e) {std::cerr<<"FAIL: "<<e.what()<<"\n";code=1;}
    if(SUCCEEDED(com))CoUninitialize();return code;
}

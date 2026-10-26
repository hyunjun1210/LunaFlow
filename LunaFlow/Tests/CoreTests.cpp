#include "Rendering/RendererDX11.h"
#include "Rendering/ViewportInteraction.h"
#include "Core/Primitives.h"
#include "Core/ProjectIO.h"
#include "Core/EditorCommands.h"
#include "Core/SceneExporter.h"
#include "App/EditorSettings.h"
#include "Core/SceneAssembly.h"
#include "Core/UnityPackage.h"
#include "AgentBridge/AgentSession.h"
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
static void TestFbxExport(const std::filesystem::path& out) {
    auto demo=MakeDemoScene();auto tr=EvaluatePose(demo.skeleton,&demo.clips[0],1).local[7];tr.rotation=QuaternionFromDegrees({20,10,110});SetTransformKey(demo.clips[0],7,1,tr);
    RetimeClip(demo.clips[0],1.5);ExportFbx(out/L"수정한_캐릭터.fbx",demo,60);auto restored=ImportScene(out/L"수정한_캐릭터.fbx");
    Require(restored.clips.size()==demo.clips.size(),"FBX exports multiple animation takes");
    Require(!restored.meshes.empty(),"Exported FBX retains mesh geometry");
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
    if(debug) for(UINT64 i=0;i<debug->GetNumStoredMessages();++i) {
        SIZE_T size=0;debug->GetMessage(i,nullptr,&size);std::vector<uint8_t> bytes(size);auto* msg=reinterpret_cast<D3D11_MESSAGE*>(bytes.data());debug->GetMessage(i,msg,&size);
        if(msg->Severity==D3D11_MESSAGE_SEVERITY_ERROR || msg->Severity==D3D11_MESSAGE_SEVERITY_CORRUPTION) throw std::runtime_error(msg->pDescription);
    }
    context->ClearState();context->Flush();
    std::cout<<"PASS DX11 WARP shader compilation, skinning, resize, PNG, textured FBX/glTF"<<(debug?" (debug layer clean)":"")<<"\n";
}
int main() {
    const HRESULT com=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    int code=0;
    try {
        const auto out=std::filesystem::current_path()/"test-output";std::filesystem::create_directories(out);
        TestEvaluation();TestJson(out);TestEditorFeatures(out);TestAgentBoundary();TestFbxExport(out);auto assembly=TestAssembly(out);auto animated=TestImport();TestRendering(out,animated,assembly);
        std::cout<<"All checks passed ("<<assertions<<" assertions). Artifacts: "<<out<<"\n";
    } catch(const std::exception& e) {std::cerr<<"FAIL: "<<e.what()<<"\n";code=1;}
    if(SUCCEEDED(com))CoUninitialize();return code;
}

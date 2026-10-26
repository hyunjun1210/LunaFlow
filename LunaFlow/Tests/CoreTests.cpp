#include "Rendering/RendererDX11.h"
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
static void TestRendering(const std::filesystem::path& out,const ModelAsset& animated) {
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
        TestEvaluation();TestJson(out);auto animated=TestImport();TestRendering(out,animated);
        std::cout<<"All checks passed ("<<assertions<<" assertions). Artifacts: "<<out<<"\n";
    } catch(const std::exception& e) {std::cerr<<"FAIL: "<<e.what()<<"\n";code=1;}
    if(SUCCEEDED(com))CoUninitialize();return code;
}

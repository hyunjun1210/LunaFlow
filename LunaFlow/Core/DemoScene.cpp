#include "Core/Scene.h"
#include <cmath>

namespace LunaFlow {
static uint32_t AddBone(ModelAsset& a, const char* name, int parent, XMFLOAT3 position) {
    Transform t; t.translation = position;
    const std::string path = parent < 0 ? std::string("/") + name : a.skeleton.bones[parent].path + "/" + name;
    const auto index = static_cast<uint32_t>(a.skeleton.bones.size());
    a.skeleton.bones.push_back({name,path,parent,t}); return index;
}
static void Box(ModelAsset& a, const Pose& bind, uint32_t bone, XMFLOAT3 center, XMFLOAT3 size, uint32_t material) {
    MeshData mesh; mesh.name = a.skeleton.bones[bone].name; mesh.material = material;
    SkinBinding skin; skin.bone = bone;
    const XMMATRIX global = XMLoadFloat4x4(&bind.global[bone]);
    XMStoreFloat4x4(&skin.inverseBind, XMMatrixInverse(nullptr,global)); mesh.skin.push_back(skin);
    const XMFLOAT3 normals[] = {{0,0,-1},{0,0,1},{-1,0,0},{1,0,0},{0,1,0},{0,-1,0}};
    const XMFLOAT3 corners[6][4] = {
        {{-1,-1,-1},{-1,1,-1},{1,1,-1},{1,-1,-1}}, {{1,-1,1},{1,1,1},{-1,1,1},{-1,-1,1}},
        {{-1,-1,1},{-1,1,1},{-1,1,-1},{-1,-1,-1}}, {{1,-1,-1},{1,1,-1},{1,1,1},{1,-1,1}},
        {{-1,1,-1},{-1,1,1},{1,1,1},{1,1,-1}}, {{-1,-1,1},{-1,-1,-1},{1,-1,-1},{1,-1,1}}};
    const XMFLOAT2 uv[] = {{0,1},{0,0},{1,0},{1,1}};
    for (unsigned f=0;f<6;++f) {
        for (unsigned c=0;c<4;++c) {
            const auto& p=corners[f][c]; Vertex v;
            const XMFLOAT3 local{center.x + p.x*size.x*.5f,center.y+p.y*size.y*.5f,center.z+p.z*size.z*.5f};
            XMStoreFloat3(&v.position,XMVector3TransformCoord(XMLoadFloat3(&local),global));
            XMStoreFloat3(&v.normal,XMVector3TransformNormal(XMLoadFloat3(&normals[f]),global));
            v.uv=uv[c]; v.weights[0]=1; mesh.vertices.push_back(v);
        }
        const uint32_t first=f*4;
        mesh.indices.insert(mesh.indices.end(),{first,first+1,first+2,first,first+2,first+3});
    }
    a.meshes.push_back(std::move(mesh));
}
ModelAsset MakeDemoScene() {
    ModelAsset a; a.source="Built-in Luna rig";
    Material suit; suit.name="Lavender shell"; suit.baseColor={.48f,.39f,.83f,1};
    Material trim; trim.name="Porcelain"; trim.baseColor={.84f,.87f,.94f,1};
    Material dark; dark.name="Graphite joints"; dark.baseColor={.10f,.15f,.24f,1};
    Material accent; accent.name="Mint visor"; accent.baseColor={.25f,.88f,.72f,1};
    a.materials={suit,trim,dark,accent};
    const auto root=AddBone(a,"Root",-1,{0,0,0});
    const auto hips=AddBone(a,"Hips",root,{0,1.03f,0});
    const auto chest=AddBone(a,"Chest",hips,{0,.43f,0});
    const auto head=AddBone(a,"Head",chest,{0,.43f,0});
    const auto left=AddBone(a,"LeftUpperArm",chest,{-.39f,.06f,0});
    const auto leftFore=AddBone(a,"LeftForeArm",left,{0,-.36f,0});
    const auto leftHand=AddBone(a,"LeftHand",leftFore,{0,-.30f,0});
    const auto right=AddBone(a,"RightUpperArm",chest,{.39f,.06f,0});
    const auto rightFore=AddBone(a,"RightForeArm",right,{0,-.36f,0});
    const auto rightHand=AddBone(a,"RightHand",rightFore,{0,-.30f,0});
    const auto leftLeg=AddBone(a,"LeftUpperLeg",hips,{-.18f,-.13f,0});
    const auto leftShin=AddBone(a,"LeftLowerLeg",leftLeg,{0,-.40f,0});
    const auto rightLeg=AddBone(a,"RightUpperLeg",hips,{.18f,-.13f,0});
    const auto rightShin=AddBone(a,"RightLowerLeg",rightLeg,{0,-.40f,0});
    const Pose bind=EvaluatePose(a.skeleton,nullptr,0);
    Box(a,bind,hips,{0,0,0},{.51f,.25f,.30f},2);
    Box(a,bind,chest,{0,-.14f,0},{.58f,.44f,.33f},0);
    Box(a,bind,chest,{0,-.08f,-.18f},{.22f,.12f,.035f},3);
    Box(a,bind,head,{0,0,0},{.40f,.37f,.36f},1);
    Box(a,bind,head,{0,.025f,-.192f},{.30f,.105f,.03f},3);
    for(auto bone:{left,right}) Box(a,bind,bone,{0,-.16f,0},{.20f,.29f,.23f},0);
    for(auto bone:{leftFore,rightFore}) Box(a,bind,bone,{0,-.13f,0},{.17f,.25f,.20f},1);
    for(auto bone:{leftHand,rightHand}) Box(a,bind,bone,{0,-.065f,0},{.18f,.14f,.20f},2);
    for(auto bone:{leftLeg,rightLeg}) Box(a,bind,bone,{0,-.18f,0},{.23f,.34f,.26f},0);
    for(auto bone:{leftShin,rightShin}) {
        Box(a,bind,bone,{0,-.16f,0},{.20f,.30f,.23f},1);
        Box(a,bind,bone,{0,-.30f,-.08f},{.24f,.14f,.40f},2);
    }
    AnimationClip idle{"demo-idle","Idle / breathing",3,1,{}};
    AnimationClip wave{"demo-wave","Wave / right hand",3,1,{}};
    AnimationClip walk{"demo-walk","Walk / in place",1.2,1,{}};
    for(int i=0;i<=24;++i) {
        const double t=i/8.0; const float phase=static_cast<float>(t/3*XM_2PI);
        Transform chestPose=a.skeleton.bones[chest].bindLocal;
        chestPose.rotation=QuaternionFromDegrees({std::sin(phase)*2,0,0});
        SetTransformKey(idle,chest,t,chestPose);
        const float ease=std::sin(static_cast<float>(t/3)*XM_PI);
        Transform arm=a.skeleton.bones[right].bindLocal; arm.rotation=QuaternionFromDegrees({0,0,145*ease});
        SetTransformKey(wave,right,t,arm);
        Transform fore=a.skeleton.bones[rightFore].bindLocal;
        fore.rotation=QuaternionFromDegrees({0,0,std::sin(phase*3)*28*ease});
        SetTransformKey(wave,rightFore,t,fore);
        Transform hand=a.skeleton.bones[rightHand].bindLocal;
        hand.rotation=QuaternionFromDegrees({0,std::sin(phase*3)*20*ease,0});
        SetTransformKey(wave,rightHand,t,hand);
    }
    for(int i=0;i<=24;++i) {
        const double t=i/20.0; const float phase=static_cast<float>(t/1.2*XM_2PI);
        for(auto bone:{leftLeg,rightLeg,left,right}) {
            Transform tr=a.skeleton.bones[bone].bindLocal;
            const float sign=(bone==leftLeg || bone==right)?1.0f:-1.0f;
            tr.rotation=QuaternionFromDegrees({std::sin(phase)*sign*28,0,0}); SetTransformKey(walk,bone,t,tr);
        }
        for(auto bone:{leftShin,rightShin}) {
            Transform tr=a.skeleton.bones[bone].bindLocal;
            const float sign=bone==leftShin?1.0f:-1.0f;
            tr.rotation=QuaternionFromDegrees({-std::max(0.0f,std::sin(phase)*sign)*35,0,0}); SetTransformKey(walk,bone,t,tr);
        }
    }
    a.clips={wave,idle,walk};a.objects.push_back({"luna-character","Luna",root});return a;
}
ModelAsset MakeCubeScene() {
    ModelAsset a; a.source="Built-in DX11 cube"; a.materials.emplace_back();
    AddBone(a,"Cube",-1,{0,.7f,0});
    Box(a,EvaluatePose(a.skeleton,nullptr,0),0,{0,0,0},{1,1,1},0);
    AnimationClip spin{"demo-cube","Cube / rotation",4,1,{}};
    for(int i=0;i<=8;++i) {
        auto t=a.skeleton.bones[0].bindLocal; t.rotation=QuaternionFromDegrees({20,static_cast<float>(i)*45,0});
        SetTransformKey(spin,0,i*.5,t);
    }
    a.clips.push_back(spin);a.objects.push_back({"cube-character","Cube",0});return a;
}
}

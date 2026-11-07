#include "Core/Scene.h"
#include "Core/AutoRig.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <windows.h>

using namespace LunaFlow;

static void Require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
// Bind-pose positions of every vertex, validating the skin palette on the way.
static std::vector<XMFLOAT3> Skinned(const ModelAsset& model,const Pose& pose,std::vector<uint32_t>* owners=nullptr){
    std::vector<XMFLOAT3> positions;
    for(const auto& mesh:model.meshes)for(const auto& vertex:mesh.vertices){
        XMVECTOR position=XMVectorZero();float sum=0,best=0;uint32_t owner=0;
        for(size_t k=0;k<vertex.weights.size();++k){
            const float weight=vertex.weights[k];
            Require(std::isfinite(weight)&&weight>=0,"Packaged skin weights are finite and positive.");
            if(weight==0)continue;
            Require(vertex.joints[k]<mesh.skin.size(),"Packaged skin palette index exists.");
            const auto& binding=mesh.skin[vertex.joints[k]];
            Require(binding.bone<pose.global.size(),"Packaged skin bone exists.");
            position+=XMVector3TransformCoord(XMLoadFloat3(&vertex.position),XMLoadFloat4x4(&binding.inverseBind)*XMLoadFloat4x4(&pose.global[binding.bone]))*weight;
            sum+=weight;if(weight>best){best=weight;owner=binding.bone;}
        }
        Require(std::abs(sum-1)<.001f,"Packaged skin weights sum to one.");
        XMFLOAT3 world;XMStoreFloat3(&world,position);
        Require(std::isfinite(world.x)&&std::isfinite(world.y)&&std::isfinite(world.z),"Packaged character geometry is finite.");
        positions.push_back(world);if(owners)owners->push_back(owner);
    }
    return positions;
}
static float Distance(const XMFLOAT3& a,const XMFLOAT3& b){return XMVectorGetX(XMVector3Length(XMLoadFloat3(&a)-XMLoadFloat3(&b)));}
// The editor auto-rigs the unskinned default character at startup; check the generated rig on the real asset.
static void TestAutoRig(const ModelAsset& model,const std::vector<XMFLOAT3>& rest){
    Require(NeedsAutoRig(model),"Packaged default character is an unskinned mesh that is offered auto-rig.");
    auto rigged=model;const auto report=AutoRig(rigged);
    Require(!NeedsAutoRig(rigged)&&report.joints.size()==26&&report.bones.size()==26,"Auto rig builds a complete humanoid skeleton.");
    auto joint=[&](const std::string& name){
        for(const auto& j:report.joints)if(j.name==name)return j.position;
        throw std::runtime_error("Missing auto-rig joint "+name);
    };
    Require(report.forward.z<-.99f,"Auto rig finds that the default character faces -Z.");
    const auto hips=joint("Hips"),neck=joint("Neck"),head=joint("Head");
    Require(hips.y>.8f&&hips.y<.95f&&neck.y>1.42f&&neck.y<1.56f&&head.y>neck.y,"Spine joints sit at the pelvis, neck and skull base.");
    for(const float side:{1.f,-1.f}){
        const std::string prefix=side>0?"Left":"Right";
        const auto arm=joint(prefix+"UpperArm"),fore=joint(prefix+"ForeArm"),hand=joint(prefix+"Hand");
        const auto foot=joint(prefix+"Foot"),toes=joint(prefix+"Toes"),toesEnd=joint(prefix+"Toes_End");
        Require(side*arm.x>.18f&&side*arm.x<.32f&&arm.y>1.3f&&arm.y<1.5f,"Shoulders sit at the top of the torso on the anatomical side.");
        Require(side*hand.x>.45f&&side*hand.x<.6f&&hand.y<fore.y&&fore.y<arm.y,"Elbows and wrists follow the A-pose arms.");
        Require(foot.y>toes.y&&toesEnd.z<toes.z-.1f,"Feet point toward the character's front.");
    }
    for(const auto& j:report.joints)if(j.name.rfind("Left",0)==0){
        const auto mirror=joint("Right"+j.name.substr(4));
        Require(std::abs(j.position.x+mirror.x)<.02f&&std::abs(j.position.y-mirror.y)<.02f&&std::abs(j.position.z-mirror.z)<.02f,"Left and right joints mirror each other.");
    }
    std::vector<uint32_t> owners;
    const auto bind=Skinned(rigged,EvaluatePose(rigged.skeleton,nullptr,0),&owners);
    Require(bind.size()==rest.size(),"Auto rig keeps every vertex.");
    float drift=0;size_t above=0,headOwned=0;
    for(size_t i=0;i<bind.size();++i){
        drift=std::max(drift,Distance(bind[i],rest[i]));
        if(rest[i].y>head.y+.05f){++above;if(owners[i]==report.bones[3]||owners[i]==report.bones[4]||owners[i]==report.bones[5])++headOwned;}
    }
    Require(drift<1e-4f,"Auto rig reproduces the modeled bind pose exactly.");
    Require(above>0&&headOwned*1000>=above*995,"The head is weighted to the neck and head bones.");
    // Raising the right upper arm moves the right hand but never the head, legs or left arm.
    auto& rotation=rigged.skeleton.bones[report.bones[12]].bindLocal.rotation;const auto turn=QuaternionFromDegrees({0,0,-50});
    XMStoreFloat4(&rotation,XMQuaternionMultiply(XMLoadFloat4(&rotation),XMLoadFloat4(&turn)));
    const auto posed=Skinned(rigged,EvaluatePose(rigged.skeleton,nullptr,0));
    const auto rightHand=joint("RightHand"),leftArm=joint("LeftUpperArm");
    float handShift=0,stillShift=0;
    for(size_t i=0;i<bind.size();++i){
        const float shift=Distance(bind[i],posed[i]);
        if(Distance(bind[i],rightHand)<.08f)handShift=std::max(handShift,shift);
        if(bind[i].y>head.y+.05f||bind[i].y<hips.y-.1f||bind[i].x>leftArm.x)stillShift=std::max(stillShift,shift);
    }
    Require(handShift>.2f,"Rotating the upper arm carries the hand.");
    Require(stillShift<1e-4f,"Posing one arm leaves the head, legs and other arm in place.");
    std::cout<<"PASS auto-rigged default character: bones="<<report.bones.size()<<", forward=("<<report.forward.x<<", "<<report.forward.y<<", "<<report.forward.z<<"), height="<<report.height
        <<", headVertices="<<headOwned<<"/"<<above<<", maxBindDrift="<<drift<<"\n";
    for(const auto& warning:report.warnings)std::cout<<"Auto rig note: "<<warning<<"\n";
}

int main(){
    try{
        wchar_t executable[32768]{};
        Require(GetModuleFileNameW(nullptr,executable,32768)>0,"Cannot locate packaged character.");
        const auto path=std::filesystem::path(executable).parent_path()/"Characters"/"DefaultCharacter.fbx";
        Require(std::filesystem::is_regular_file(path),"Default character must be packaged beside the application.");
        auto model=ImportScene(path);
        const auto references=RemoveReferenceImagePlanes(model);
        Require(!model.meshes.empty()&&!model.skeleton.bones.empty(),"Default character has renderable geometry and a valid hierarchy.");
        Require(!model.clips.empty(),"Default character has an editable motion slot.");
        const auto pose=EvaluatePose(model.skeleton,nullptr,0);
        const float infinity=std::numeric_limits<float>::infinity();
        XMFLOAT3 lower{infinity,infinity,infinity},upper{-infinity,-infinity,-infinity};
        size_t vertices=0,triangles=0,deformableMeshes=0,textures=0;
        for(const auto& mesh:model.meshes){
            Require(!mesh.vertices.empty()&&!mesh.indices.empty(),"Packaged mesh must be nonempty.");
            Require(mesh.material<model.materials.size(),"Packaged mesh material exists.");
            vertices+=mesh.vertices.size();triangles+=mesh.indices.size()/3;
            if(mesh.skin.size()>1)++deformableMeshes;
        }
        const auto rest=Skinned(model,pose);
        for(const auto& world:rest){
            lower={std::min(lower.x,world.x),std::min(lower.y,world.y),std::min(lower.z,world.z)};
            upper={std::max(upper.x,world.x),std::max(upper.y,world.y),std::max(upper.z,world.z)};
        }
        Require(std::max({upper.x-lower.x,upper.y-lower.y,upper.z-lower.z})>1e-6f,"Default character has visible extent.");
        const float height=upper.y-lower.y;
        Require(height>=upper.x-lower.x&&height>=upper.z-lower.z,"Default character stands upright without reference sheets widening its bounds.");
        Require(std::abs(lower.y)<=height*.1f,"Default character stands on the ground plane.");
        for(const auto& material:model.materials){
            if(!material.texturePath.empty()&&material.textureBytes.empty()&&material.textureRGBA.empty()&&!std::filesystem::is_regular_file(material.texturePath))
                throw std::runtime_error("Default character external texture is missing: "+material.texturePath.u8string());
            if(!material.texturePath.empty()||!material.textureBytes.empty()||!material.textureRGBA.empty())++textures;
        }
        std::cout<<"PASS packaged default character: meshes="<<model.meshes.size()<<", vertices="<<vertices<<", triangles="<<triangles
            <<", nodes="<<model.skeleton.bones.size()<<", deformableMeshes="<<deformableMeshes<<", clips="<<model.clips.size()<<", textures="<<textures<<"\n";
        std::cout<<"Bind bounds: ("<<lower.x<<", "<<lower.y<<", "<<lower.z<<") to ("<<upper.x<<", "<<upper.y<<", "<<upper.z<<") meters\n";
        for(size_t i=0;i<model.skeleton.bones.size();++i){const auto& bone=model.skeleton.bones[i];std::cout<<"Node "<<i<<" parent="<<bone.parent<<" "<<bone.path<<"\n";}
        for(const auto& warning:model.warnings)std::cout<<"Import warning: "<<warning<<"\n";
        for(const auto& reference:references)std::cout<<"Hidden modeling reference plane: "<<reference<<"\n";
        TestAutoRig(model,rest);
        return 0;
    }catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<"\n";return 1;}
}

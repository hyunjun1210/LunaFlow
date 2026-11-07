#include "Core/MotionRetarget.h"
#include "Core/EditorCommands.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>
#include <stdexcept>

namespace LunaFlow {
namespace {
std::string Name(std::string text){
    const auto colon=text.find_last_of(':');if(colon!=std::string::npos)text=text.substr(colon+1);
    std::transform(text.begin(),text.end(),text.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
    if(text.size()>2&&(text.substr(text.size()-2)=="_l"||text.substr(text.size()-2)==".l"))text="left"+text.substr(0,text.size()-2);
    if(text.size()>2&&(text.substr(text.size()-2)=="_r"||text.substr(text.size()-2)==".r"))text="right"+text.substr(0,text.size()-2);
    text.erase(std::remove_if(text.begin(),text.end(),[](unsigned char c){return !std::isalnum(c);}),text.end());
    if(text.rfind("mixamorig",0)==0)text=text.substr(9);
    const std::map<std::string,std::string> aliases={{"pelvis","hips"},{"spine2","chest"},{"spine02","chest"},{"upperchest","chest"},
        {"leftarm","leftupperarm"},{"rightarm","rightupperarm"},{"leftlowerarm","leftforearm"},{"rightlowerarm","rightforearm"},
        {"leftupleg","leftupperleg"},{"rightupleg","rightupperleg"},{"leftthigh","leftupperleg"},{"rightthigh","rightupperleg"},
        {"leftleg","leftlowerleg"},{"rightleg","rightlowerleg"},{"leftcalf","leftlowerleg"},{"rightcalf","rightlowerleg"},
        {"leftwrist","lefthand"},{"rightwrist","righthand"},{"leftankle","leftfoot"},{"rightankle","rightfoot"}};
    auto found=aliases.find(text);return found==aliases.end()?text:found->second;
}
int Find(const SkeletonData& s,const std::string& name){int result=-1;for(size_t i=0;i<s.bones.size();++i)if(Name(s.bones[i].name)==name){if(result>=0)return -1;result=static_cast<int>(i);}return result;}
XMVECTOR Position(const Pose& p,int bone){const auto& m=p.global.at(static_cast<size_t>(bone));return XMVectorSet(m._41,m._42,m._43,1);}
float Length(FXMVECTOR v){return XMVectorGetX(XMVector3Length(v));}
XMMATRIX Rotation(const XMFLOAT4X4& matrix){auto t=Decompose(XMLoadFloat4x4(&matrix));return XMMatrixRotationQuaternion(XMLoadFloat4(&t.rotation));}
Pose EvaluateLocal(const SkeletonData& s,const std::vector<Transform>& local){
    Pose pose;pose.local=local;pose.global.resize(local.size());
    for(size_t i=0;i<local.size();++i){const int parent=s.bones[i].parent;if(parent>=static_cast<int>(i)||parent< -1)throw std::runtime_error("Retarget skeleton must be in parent-first order.");
        XMStoreFloat4x4(&pose.global[i],Matrix(local[i])*(parent<0?XMMatrixIdentity():XMLoadFloat4x4(&pose.global[static_cast<size_t>(parent)])));}
    return pose;
}
float Height(const SkeletonData& s,const Pose& bind){
    float low=0,high=0;const int head=Find(s,"head");
    if(head>=0){
        high=bind.global[head]._42;int root=head;while(s.bones[root].parent>=0)root=s.bones[root].parent;low=bind.global[root]._42;
        for(const auto* name:{"leftfoot","rightfoot"}){const int foot=Find(s,name);if(foot>=0)low=std::min(low,bind.global[foot]._42);}
        return std::max(high-low,.01f);
    }
    for(size_t i=0;i<s.bones.size();++i){auto n=Name(s.bones[i].name);if(n.find("hand")!=std::string::npos||n.find("arm")!=std::string::npos)continue;low=std::min(low,bind.global[i]._42);high=std::max(high,bind.global[i]._42);}
    return std::max(high-low,.01f);
}
struct Chain {int end=-1,middle=-1,start=-1;bool foot=false;};
std::vector<Chain> Chains(const SkeletonData& s){
    std::vector<Chain> result;
    for(const std::string side:{"left","right"})for(bool foot:{false,true}){
        const int end=Find(s,side+(foot?"foot":"hand")),middle=Find(s,side+(foot?"lowerleg":"forearm")),start=Find(s,side+(foot?"upperleg":"upperarm"));
        auto ancestor=[&](int a,int b){for(int i=b;i>=0;i=s.bones[i].parent)if(i==a)return true;return false;};
        if(end>=0&&middle>=0&&start>=0&&ancestor(start,middle)&&ancestor(middle,end))result.push_back({end,middle,start,foot});
    }return result;
}
XMVECTOR ShortestArc(FXMVECTOR from,FXMVECTOR to){
    const auto a=XMVector3Normalize(from),b=XMVector3Normalize(to);const float dot=std::clamp(XMVectorGetX(XMVector3Dot(a,b)),-1.0f,1.0f);
    if(dot<-.9999f){auto axis=XMVector3Cross(a,XMVectorSet(1,0,0,0));if(Length(axis)<.001f)axis=XMVector3Cross(a,XMVectorSet(0,1,0,0));return XMQuaternionRotationAxis(axis,XM_PI);}
    return XMQuaternionNormalize(XMVectorSetW(XMVector3Cross(a,b),1+dot));
}
void Solve(const SkeletonData& s,std::vector<Transform>& local,const Chain& chain,FXMVECTOR goal){
    for(int iteration=0;iteration<10;++iteration){
        auto pose=EvaluateLocal(s,local);if(Length(Position(pose,chain.end)-goal)<.0005f)break;
        for(const int joint:{chain.middle,chain.start}){
            pose=EvaluateLocal(s,local);const auto origin=Position(pose,joint),from=Position(pose,chain.end)-origin,to=goal-origin;
            if(Length(from)<.00001f||Length(to)<.00001f)continue;
            const int parent=s.bones[joint].parent;const auto parentRotation=parent<0?XMMatrixIdentity():Rotation(pose.global[parent]);
            auto r=Rotation(pose.global[joint])*XMMatrixRotationQuaternion(ShortestArc(from,to))*XMMatrixInverse(nullptr,parentRotation);
            XMStoreFloat4(&local[joint].rotation,XMQuaternionNormalize(XMQuaternionRotationMatrix(r)));
        }
    }
}
bool Torso(const SkeletonData& s,const Pose& pose,FXMVECTOR point,XMVECTOR& adjusted){
    const int hip=Find(s,"hips"),chest=Find(s,"chest");if(hip<0||chest<0)return false;
    const auto a=Position(pose,hip),b=Position(pose,chest),axis=b-a;
    const float lengthSquared=XMVectorGetX(XMVector3LengthSq(axis));if(lengthSquared<1e-8f)return false;
    const float along=XMVectorGetX(XMVector3Dot(point-a,axis))/lengthSquared;if(along<0||along>1)return false;
    const auto center=a+axis*along;auto radial=point-center;
    const float radius=std::max(.02f,std::sqrt(lengthSquared)*.28f);
    if(Length(radial)>=radius)return false;
    if(Length(radial)<1e-5f)radial=XMVectorSet(0,0,-1,0);
    adjusted=center+XMVector3Normalize(radial)*radius;return true;
}
int Frames(double duration,int fps,size_t bones){
    if(fps<1||fps>120||!std::isfinite(duration)||duration<=0)throw std::runtime_error("Invalid retarget sampling settings.");
    const auto frames=std::ceil(duration*fps)+1;if(frames>3601||frames*bones>500000)throw std::runtime_error("Motion exceeds the retarget sampling budget. Use a shorter clip or lower FPS.");return static_cast<int>(frames);
}
void InspectPose(const SkeletonData& s,const Pose& pose,const Pose& bind,const std::vector<Chain>& chains,ProportionReport& report){
    for(const auto& chain:chains){
        const auto p=Position(pose,chain.end);XMVECTOR adjusted;
        if(chain.foot&&XMVectorGetY(p)<bind.global[chain.end]._42-.015f)++report.groundWarnings;
        if(!chain.foot&&Torso(s,pose,p,adjusted))++report.bodyWarnings;
        const auto a=Position(pose,chain.start)-Position(pose,chain.middle),b=p-Position(pose,chain.middle);
        if(Length(a)>1e-5f&&Length(b)>1e-5f&&XMVectorGetX(XMVector3Dot(XMVector3Normalize(a),XMVector3Normalize(b)))>.996f)++report.poseWarnings;
    }
}
}
ProportionReport InspectMotion(const SkeletonData& skeleton,const AnimationClip& clip,int fps){
    const auto duration=PlaybackDuration(clip);const int frames=Frames(duration,fps,skeleton.bones.size());auto bind=EvaluatePose(skeleton,nullptr,0);auto chains=Chains(skeleton);ProportionReport report;
    report.targetHeight=Height(skeleton,bind);report.totalBones=static_cast<int>(skeleton.bones.size());
    for(int frame=0;frame<frames;++frame)InspectPose(skeleton,EvaluatePose(skeleton,&clip,std::min(frame/static_cast<double>(fps),duration)),bind,chains,report);
    return report;
}
ProportionResult AdaptMotion(const SkeletonData& source,const AnimationClip& clip,const SkeletonData& target,const ProportionSettings& settings){
    if(source.bones.empty()||target.bones.empty())throw std::runtime_error("Both source and target skeletons are required.");
    const auto sourceBind=EvaluatePose(source,nullptr,0),targetBind=EvaluatePose(target,nullptr,0);const double duration=PlaybackDuration(clip);const int frames=Frames(duration,settings.fps,target.bones.size());
    ProportionResult result;auto& report=result.report;report.sourceHeight=Height(source,sourceBind);report.targetHeight=Height(target,targetBind);report.heightRatio=report.targetHeight/report.sourceHeight;
    if(report.heightRatio<.05f||report.heightRatio>20)throw std::runtime_error("Skeleton scale ratio is outside the supported range. Check FBX units.");
    report.totalBones=static_cast<int>(target.bones.size());std::vector<int> mapping(target.bones.size(),-1);
    for(size_t i=0;i<target.bones.size();++i){
        for(size_t j=0;j<source.bones.size();++j)if(source.bones[j].path==target.bones[i].path){mapping[i]=static_cast<int>(j);break;}
        if(mapping[i]<0)mapping[i]=Find(source,Name(target.bones[i].name));
        if(mapping[i]>=0)++report.mappedBones;else report.unmatchedBones.push_back(target.bones[i].name);
    }
    if(report.mappedBones==0)throw std::runtime_error("No matching bones. This rig needs an explicit bone mapping.");
    int tracked=0;for(const auto& track:clip.tracks)if(std::find(mapping.begin(),mapping.end(),static_cast<int>(track.bone))!=mapping.end())++tracked;
    if(tracked==0)throw std::runtime_error("No animated source bones map to the current character.");
    result.clip=NewClip(clip.name+" / adapted",duration);result.clip.loop=clip.loop;
    const auto chains=Chains(target);const int targetHips=Find(target,"hips"),sourceHips=Find(source,"hips");
    for(int frame=0;frame<frames;++frame){
        const double time=std::min(frame/static_cast<double>(settings.fps),duration);const auto original=EvaluatePose(source,&clip,time);std::vector<Transform> local;for(const auto& bone:target.bones)local.push_back(bone.bindLocal);
        std::vector<XMFLOAT4X4> globals(target.bones.size());
        for(size_t i=0;i<target.bones.size();++i){
            const int mapped=mapping[i],parent=target.bones[i].parent;
            const auto parentMatrix=parent<0?XMMatrixIdentity():XMLoadFloat4x4(&globals[parent]);
            if(mapped<0){XMStoreFloat4x4(&globals[i],Matrix(local[i])*parentMatrix);continue;}
            const auto& from=original.local[mapped];const auto& rest=source.bones[mapped].bindLocal;auto& to=local[i];
            float ratio=report.heightRatio;const auto sourceLength=Length(XMLoadFloat3(&rest.translation));
            if(target.bones[i].parent>=0&&static_cast<int>(i)!=targetHips&&sourceLength>.0001f)ratio=Length(XMLoadFloat3(&to.translation))/sourceLength;
            auto delta=XMLoadFloat3(&from.translation)-XMLoadFloat3(&rest.translation);
            if(static_cast<int>(i)==targetHips||parent<0){
                const int sourceParent=source.bones[mapped].parent;
                if(sourceParent>=0)delta=XMVector3TransformNormal(delta,XMLoadFloat4x4(&sourceBind.global[sourceParent]));
                delta*=report.heightRatio;
                if(parent>=0)delta=XMVector3TransformNormal(delta,XMMatrixInverse(nullptr,XMLoadFloat4x4(&targetBind.global[parent])));
            }else delta*=ratio;
            XMFLOAT3 shift;XMStoreFloat3(&shift,delta);to.translation.x+=shift.x;to.translation.y+=shift.y;to.translation.z+=shift.z;
            for(int n=0;n<3;++n){const float base=(&rest.scale.x)[n];if(std::abs(base)<1e-6f)throw std::runtime_error("A source bind scale is zero.");(&to.scale.x)[n]*=(&from.scale.x)[n]/base;}
            auto rotation=Rotation(targetBind.global[i])*XMMatrixInverse(nullptr,Rotation(sourceBind.global[mapped]))*Rotation(original.global[mapped]);
            if(parent>=0)rotation*=XMMatrixInverse(nullptr,Rotation(globals[parent]));
            XMStoreFloat4(&to.rotation,XMQuaternionNormalize(XMQuaternionRotationMatrix(rotation)));
            XMStoreFloat4x4(&globals[i],Matrix(to)*parentMatrix);
        }
        std::vector<std::pair<Chain,XMFLOAT3>> goals;auto pose=EvaluateLocal(target,local);
        for(const auto& chain:chains){
            if((chain.foot&&!settings.footIK)||(!chain.foot&&!settings.handIK)||mapping[chain.end]<0)continue;
            const auto rootFrom=sourceHips>=0?Position(original,sourceHips):XMVectorZero(),rootTo=targetHips>=0?Position(pose,targetHips):XMVectorZero();
            auto goal=rootTo+(Position(original,mapping[chain.end])-rootFrom)*report.heightRatio;
            if(chain.foot){const float floor=targetBind.global[chain.end]._42;if(XMVectorGetY(goal)<floor){goal=XMVectorSetY(goal,floor);++report.groundCorrections;}}
            else{XMVECTOR adjusted;if(Torso(target,pose,goal,adjusted)){goal=adjusted;++report.bodyCorrections;}}
            XMFLOAT3 value;XMStoreFloat3(&value,goal);goals.push_back({chain,value});
        }
        if(settings.balance&&targetHips>=0){
            std::vector<XMFLOAT3> feet;for(const auto& goal:goals)if(goal.first.foot&&goal.second.y<=targetBind.global[goal.first.end]._42+.025f)feet.push_back(goal.second);
            if(feet.size()==2){
                const int head=Find(target,"head");auto center=Position(pose,targetHips);if(head>=0)center=center*.75f+Position(pose,head)*.25f;
                const float limit=report.targetHeight*.08f;
                const float dx=std::clamp(std::clamp(XMVectorGetX(center),std::min(feet[0].x,feet[1].x)-.04f,std::max(feet[0].x,feet[1].x)+.04f)-XMVectorGetX(center),-limit,limit);
                const float dz=std::clamp(std::clamp(XMVectorGetZ(center),std::min(feet[0].z,feet[1].z)-.08f,std::max(feet[0].z,feet[1].z)+.08f)-XMVectorGetZ(center),-limit,limit);
                if(std::abs(dx)+std::abs(dz)>.001f){auto delta=XMVectorSet(dx,0,dz,0);const int parent=target.bones[targetHips].parent;if(parent>=0)delta=XMVector3TransformNormal(delta,XMMatrixInverse(nullptr,XMLoadFloat4x4(&pose.global[parent])));XMFLOAT3 shift;XMStoreFloat3(&shift,delta);local[targetHips].translation.x+=shift.x;local[targetHips].translation.z+=shift.z;++report.balanceCorrections;}
            }
        }
        for(const auto& item:goals){
            pose=EvaluateLocal(target,local);const auto& chain=item.first;auto goal=XMLoadFloat3(&item.second);const auto origin=Position(pose,chain.start);const float reach=Length(Position(pose,chain.middle)-origin)+Length(Position(pose,chain.end)-Position(pose,chain.middle));
            if(Length(goal-origin)>reach-.00001f){goal=origin+XMVector3Normalize(goal-origin)*std::max(.00001f,reach-.00001f);++report.reachCorrections;}
            Solve(target,local,chain,goal);
        }
        pose=EvaluateLocal(target,local);InspectPose(target,pose,targetBind,chains,report);
        for(uint32_t i=0;i<target.bones.size();++i)if(mapping[i]>=0)SetTransformKey(result.clip,i,time,local[i]);
    }
    return result;
}
}

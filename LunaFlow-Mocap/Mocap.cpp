#include "Mocap.h"
#include "Core/EditorCommands.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <map>
#include <stdexcept>

namespace LunaFlow::Mocap {
namespace {
constexpr int Required[]={11,12,13,14,15,16,23,24,25,26,27,28};
float Length(FXMVECTOR v) { return XMVectorGetX(XMVector3Length(v)); }
XMVECTOR Point(const Frame& f,int i) {
    const auto& p=f.landmarks[i].world;
    // Unmirrored MediaPipe (anatomical left=+x, y down, camera=-z) -> left-handed Luna: left=+X, up=+Y, front=-Z.
    return XMVectorSet(p.x,-p.y,p.z,0);
}
XMMATRIX Basis(const Frame& f,bool shoulders) {
    const auto up=(Point(f,11)+Point(f,12))-(Point(f,23)+Point(f,24));
    const auto side=shoulders?Point(f,11)-Point(f,12):Point(f,23)-Point(f,24);
    const auto x=XMVector3Normalize(side),z=XMVector3Normalize(XMVector3Cross(x,up));
    const auto y=XMVector3Normalize(XMVector3Cross(z,x));
    return XMMATRIX(x,y,z,XMVectorSet(0,0,0,1));
}
XMMATRIX Rotation(const XMFLOAT4X4& m) {
    const auto t=Decompose(XMLoadFloat4x4(&m));
    return XMMatrixRotationQuaternion(XMLoadFloat4(&t.rotation));
}
XMVECTOR Arc(FXMVECTOR from,FXMVECTOR to) {
    const auto a=XMVector3Normalize(from),b=XMVector3Normalize(to);
    const float dot=std::clamp(XMVectorGetX(XMVector3Dot(a,b)),-1.f,1.f);
    if(dot<-.9999f) {
        auto axis=XMVector3Cross(a,XMVectorSet(1,0,0,0));
        if(Length(axis)<.001f)axis=XMVector3Cross(a,XMVectorSet(0,1,0,0));
        return XMQuaternionRotationAxis(axis,XM_PI);
    }
    return XMQuaternionNormalize(XMVectorSetW(XMVector3Cross(a,b),1+dot));
}
Pose Globals(const SkeletonData& s,std::vector<Transform> local) {
    Pose p;p.local=std::move(local);p.global.resize(s.bones.size());
    for(size_t i=0;i<s.bones.size();++i) {
        const int parent=s.bones[i].parent;
        XMStoreFloat4x4(&p.global[i],Matrix(p.local[i])*(parent<0?XMMatrixIdentity():XMLoadFloat4x4(&p.global[parent])));
    }
    return p;
}
Pose Blend(const SkeletonData& s,const Pose& a,const Pose& b,float alpha) {
    auto local=a.local;
    for(size_t i=0;i<local.size();++i)
        XMStoreFloat4(&local[i].rotation,XMQuaternionNormalize(XMQuaternionSlerp(
            XMLoadFloat4(&a.local[i].rotation),XMLoadFloat4(&b.local[i].rotation),alpha)));
    return Globals(s,std::move(local));
}
std::string Canonical(std::string name) {
    const auto colon=name.find_last_of(':');if(colon!=std::string::npos)name=name.substr(colon+1);
    // Deliberately narrower than offline retarget: exact Luna/Mixamo spellings only.
    const std::map<std::string,std::string> aliases={
        {"LeftArm","LeftUpperArm"},{"RightArm","RightUpperArm"},
        {"LeftUpLeg","LeftUpperLeg"},{"RightUpLeg","RightUpperLeg"},
        {"LeftLeg","LeftLowerLeg"},{"RightLeg","RightLowerLeg"},{"Spine2","Chest"}};
    const auto it=aliases.find(name);return it==aliases.end()?name:it->second;
}
}
double Clock() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
namespace {
// Same blends as the editor evaluator, so kept keys replay the original curve.
XMFLOAT3 LerpKey(const XMFLOAT3& a,const XMFLOAT3& b,float f){XMFLOAT3 r;XMStoreFloat3(&r,XMVectorLerp(XMLoadFloat3(&a),XMLoadFloat3(&b),f));return r;}
XMFLOAT4 SlerpKey(const XMFLOAT4& a,const XMFLOAT4& b,float f){XMFLOAT4 r;XMStoreFloat4(&r,XMQuaternionNormalize(XMQuaternionSlerp(XMLoadFloat4(&a),XMLoadFloat4(&b),f)));return r;}
template<class T,class Blend> T SampleKeys(const std::vector<Key<T>>& keys,double t,Blend blend) {
    if(t<=keys.front().time)return keys.front().value;
    if(t>=keys.back().time)return keys.back().value;
    const auto right=std::upper_bound(keys.begin(),keys.end(),t,[](double time,const Key<T>& k){return time<k.time;});
    const auto& a=*(right-1);const auto& b=*right;
    return blend(a.value,b.value,static_cast<float>((t-a.time)/(b.time-a.time)));
}
template<class T,class Blend,class Error> void Reduce(std::vector<Key<T>>& keys,Blend blend,Error error,float tolerance) {
    if(keys.size()<3)return;
    // Greedy span growth; a 3 s cap bounds the cost of long still poses.
    constexpr size_t MaxSpan=90;
    std::vector<Key<T>> out{keys.front()};
    for(size_t anchor=0;anchor+1<keys.size();) {
        size_t end=anchor+1;
        for(size_t candidate=anchor+2;candidate<keys.size()&&candidate-anchor<=MaxSpan;++candidate) {
            bool fits=true;const double span=keys[candidate].time-keys[anchor].time;
            for(size_t i=anchor+1;i<candidate&&fits;++i)
                fits=error(blend(keys[anchor].value,keys[candidate].value,static_cast<float>((keys[i].time-keys[anchor].time)/span)),keys[i].value)<=tolerance;
            if(!fits)break;
            end=candidate;
        }
        out.push_back(keys[end]);anchor=end;
    }
    keys=std::move(out);
}
template<class T,class Blend> void TrimKeys(std::vector<Key<T>>& keys,double in,double out,Blend blend) {
    if(keys.empty())return;
    std::vector<Key<T>> result{{0,SampleKeys(keys,in,blend)}};
    for(const auto& k:keys)if(k.time>in+1e-6&&k.time<out-1e-6)result.push_back({k.time-in,k.value});
    if(out-in>1e-6)result.push_back({out-in,SampleKeys(keys,out,blend)});
    keys=std::move(result);
}
}
size_t KeyCount(const AnimationClip& clip) {
    size_t count=0;
    for(const auto& t:clip.tracks)count+=t.positions.size()+t.rotations.size()+t.scales.size();
    return count;
}
size_t SimplifyTake(AnimationClip& clip,float rotationDegrees,float positionTolerance) {
    if(!(rotationDegrees>=0)||!(positionTolerance>=0))throw std::runtime_error("Simplify tolerance must be nonnegative.");
    const float rotationError=1-std::cos(XMConvertToRadians(rotationDegrees)*.5f);
    auto quaternion=[](const XMFLOAT4& a,const XMFLOAT4& b){return 1-std::abs(XMVectorGetX(XMVector4Dot(XMLoadFloat4(&a),XMLoadFloat4(&b))));};
    auto vector=[](const XMFLOAT3& a,const XMFLOAT3& b){return Length(XMLoadFloat3(&a)-XMLoadFloat3(&b));};
    for(auto& t:clip.tracks) {
        Reduce(t.rotations,SlerpKey,quaternion,rotationError);
        Reduce(t.positions,LerpKey,vector,positionTolerance);
        Reduce(t.scales,LerpKey,vector,positionTolerance);
    }
    ++clip.revision;return KeyCount(clip);
}
void TrimTake(AnimationClip& clip,double in,double out) {
    if(!std::isfinite(in)||!std::isfinite(out)||in<0||out>clip.duration+1e-9||out-in<0)
        throw std::runtime_error("Trim range must lie inside the take.");
    out=std::min(out,clip.duration);
    if(in<=1e-9&&out>=clip.duration-1e-9)return;
    for(auto& t:clip.tracks) {
        TrimKeys(t.rotations,in,out,SlerpKey);TrimKeys(t.positions,in,out,LerpKey);TrimKeys(t.scales,in,out,LerpKey);
    }
    clip.duration=out-in;++clip.revision;
}
Framing CheckFraming(const Frame& f) {
    if(!f.detected)return Framing::NoPerson;
    bool vertical=false,left=false,right=false,weak=false;
    for(int i:Required) {
        const auto& p=f.landmarks[i];
        vertical|=p.v<.01f||p.v>.99f;
        // Unmirrored camera image: small u is the person's own right side.
        left|=p.u<.01f;right|=p.u>.99f;
        weak|=p.visibility<.6f||p.presence<.6f;
    }
    if(vertical||(left&&right))return Framing::StepBack;
    if(left)return Framing::MoveLeft;
    if(right)return Framing::MoveRight;
    return weak?Framing::LowConfidence:Framing::Good;
}
Event ParseEvent(const std::string& line) {
    if(line.size()>32768)throw std::runtime_error("Mocap packet exceeds 32 KiB.");
    const auto j=Json::parse(line);
    if(!j.is_object()||j.at("version")!=1)throw std::runtime_error("Unsupported mocap protocol version.");
    Event event;const auto type=j.at("type").get<std::string>();
    if(type=="status") {
        event.state=j.at("state").get<std::string>();event.message=j.at("message").get<std::string>();
        if((event.state!="ready"&&event.state!="error"&&event.state!="stopped")||event.message.size()>2048)
            throw std::runtime_error("Invalid mocap status.");
        return event;
    }
    if(type!="frame")throw std::runtime_error("Unknown mocap packet type.");
    Frame f;
    if(!j.at("seq").is_number_unsigned())throw std::runtime_error("Invalid mocap sequence.");
    f.sequence=j.at("seq").get<uint64_t>();f.time=j.at("time_ms").get<double>()/1000.;
    if(!f.sequence||!std::isfinite(f.time)||f.time<0||f.time>86400)throw std::runtime_error("Invalid mocap timestamp.");
    f.detected=j.at("detected").get<bool>();
    const auto& points=j.at("points");
    if(!points.is_array()||points.size()!=(f.detected?33u:0u))throw std::runtime_error("Expected 33 mocap landmarks.");
    for(size_t i=0;i<points.size();++i) {
        const auto& p=points[i];
        if(!p.is_array()||p.size()!=7)throw std::runtime_error("Invalid mocap landmark shape.");
        float values[7];
        for(size_t c=0;c<7;++c) {
            values[c]=p[c].get<float>();
            if(!std::isfinite(values[c])||std::abs(values[c])>100)
                throw std::runtime_error("Non-finite/out-of-range mocap landmark.");
        }
        if(values[3]<0||values[3]>1||values[4]<0||values[4]>1)throw std::runtime_error("Invalid landmark confidence.");
        f.landmarks[i]={{values[0],values[1],values[2]},values[3],values[4],values[5],values[6]};
    }
    event.frame=f;return event;
}
void Capture::Configure(const SkeletonData& skeleton) {
    *this=Capture{};
    if(skeleton.bones.empty()||skeleton.bones.size()>512)throw std::runtime_error("Mocap requires a single humanoid rig (up to 512 nodes).");
    skeleton_=skeleton;bind_=EvaluatePose(skeleton_,nullptr,0);
    std::map<std::string,int> names;
    for(size_t i=0;i<skeleton_.bones.size();++i) {
        const auto& b=skeleton_.bones[i];
        const auto scale=Decompose(XMLoadFloat4x4(&bind_.global[i])).scale;
        if(scale.x<=0||scale.y<=0||scale.z<=0||std::abs(scale.x-scale.y)>1e-4f*scale.x||std::abs(scale.x-scale.z)>1e-4f*scale.x)
            throw std::runtime_error("Mocap does not support reflected/non-uniform bind scale.");
        const auto n=Canonical(b.name);
        if(!names.emplace(n,static_cast<int>(i)).second)names[n]=-1;
    }
    auto find=[&](const char* n) {
        auto it=names.find(n);
        if(it==names.end()||it->second<0)throw std::runtime_error(std::string("Unsupported/ambiguous rig: missing unique ")+n);
        return it->second;
    };
    auto ancestor=[&](int a,int b) {
        for(int p=skeleton_.bones[b].parent;p>=0;p=skeleton_.bones[p].parent)if(p==a)return true;
        return false;
    };
    hips_=find("Hips");chest_=find("Chest");
    if(!ancestor(hips_,chest_)||!ancestor(chest_,find("Head")))throw std::runtime_error("Unsupported torso hierarchy.");
    links_={{find("LeftUpperArm"),11,13},{find("LeftForeArm"),13,15},
        {find("RightUpperArm"),12,14},{find("RightForeArm"),14,16},
        {find("LeftUpperLeg"),23,25},{find("LeftLowerLeg"),25,27},
        {find("RightUpperLeg"),24,26},{find("RightLowerLeg"),26,28}};
    for(size_t i=0;i<links_.size();i+=2) {
        if(!ancestor(i<4?chest_:hips_,links_[i].bone)||!ancestor(links_[i].bone,links_[i+1].bone))
            throw std::runtime_error("Unsupported arm/leg hierarchy.");
    }
    if(!ancestor(links_[1].bone,find("LeftHand"))||!ancestor(links_[3].bone,find("RightHand")))
        throw std::runtime_error("Unsupported hand hierarchy.");
    for(size_t i=0;i<links_.size();++i) {
        const auto& origin=bind_.global[links_[i].bone];
        int end=-1;
        if(i%2==0)end=links_[i+1].bone;
        else if(i<4)end=find(i==1?"LeftHand":"RightHand");
        else {
            const auto foot=names.find(i==5?"LeftFoot":"RightFoot");
            if(foot!=names.end()&&foot->second>=0&&ancestor(links_[i].bone,foot->second))end=foot->second;
            else if(std::any_of(skeleton_.bones.begin(),skeleton_.bones.end(),[&](const Bone& b){return b.parent==links_[i].bone;}))
                throw std::runtime_error("Unsupported shin endpoint: requires named LeftFoot/RightFoot or Luna leaf shin.");
        }
        const auto& other=bind_.global[end<0?links_[i-1].bone:end];
        XMFLOAT3 direction{other._41-origin._41,other._42-origin._42,other._43-origin._43};
        if(end<0){direction.x=-direction.x;direction.y=-direction.y;direction.z=-direction.z;}
        if(Length(XMLoadFloat3(&direction))<.01f)throw std::runtime_error("Unsupported zero-length limb.");
        links_[i].bindDirection=direction;
    }
    // Only upright, Luna-facing rigs are supported; do not silently guess FBX axes.
    const auto& l=bind_.global[links_[0].bone];const auto& r=bind_.global[links_[2].bone];
    const auto& hip=bind_.global[hips_];const auto& chest=bind_.global[chest_];
    if(l._41-r._41<.02f||std::abs(r._43-l._43)>(l._41-r._41)*.3f||chest._42-hip._42<.02f||
        std::abs(chest._41-hip._41)>(chest._42-hip._42)*.3f)
        throw std::runtime_error("Unsupported bind axes: requires +Y up, anatomical left +X, front -Z.");
    pose_=bind_;previousPose_=bind_;
    mapping_="10 bones: Hips, Chest, left/right upper arm, forearm, thigh, shin. Head/hands/feet: inherited bind rotation. No root translation or twist solving.";
    status_="Ready to start calibration.";
}
bool Capture::Usable(const Frame& f) const {
    if(!f.detected)return false;
    for(int i:Required) {
        const auto& p=f.landmarks[i];
        if(p.visibility<.6f||p.presence<.6f||p.u<.01f||p.u>.99f||p.v<.01f||p.v>.99f)return false;
        if(!std::isfinite(p.world.x)||!std::isfinite(p.world.y)||!std::isfinite(p.world.z))return false;
    }
    const auto up=(Point(f,11)+Point(f,12))*.5f-(Point(f,23)+Point(f,24))*.5f;
    if(Length(up)<.15f||Length(up)>1.2f)return false;
    for(bool shoulders:{false,true}) {
        const auto side=shoulders?Point(f,11)-Point(f,12):Point(f,23)-Point(f,24);
        if(Length(side)<.08f||Length(side)>.9f||Length(XMVector3Cross(XMVector3Normalize(side),XMVector3Normalize(up)))<.5f)return false;
    }
    for(const auto& l:links_) {
        const auto length=Length(Point(f,l.to)-Point(f,l.from));
        if(length<.06f||length>1.f)return false;
    }
    return true;
}
void Capture::BeginCalibration() {
    if(skeleton_.bones.empty()||Recording())throw std::runtime_error("Stop recording before calibration.");
    calibrated_=false;calibrating_=true;samples_=0;pose_=bind_;
    status_="Stand facing the camera, arms straight down, full body visible; hold still for 0.75 seconds.";
}
void Capture::SampleCalibration(const Frame& f) {
    const auto up=XMVector3Normalize((Point(f,11)+Point(f,12))-(Point(f,23)+Point(f,24)));
    const auto left=XMVector3Normalize(Point(f,11)-Point(f,12));
    bool stable=XMVectorGetY(up)>.9f&&XMVectorGetX(left)>.9f;
    if(samples_>0)stable&=XMVectorGetX(XMVector3Dot(left,XMVector3Normalize(Point(anchor_,11)-Point(anchor_,12))))>.978f;
    for(const auto& l:links_) {
        const auto direction=XMVector3Normalize(Point(f,l.to)-Point(f,l.from));
        stable&=XMVectorGetX(XMVector3Dot(direction,-up))>.85f;
        if(samples_>0)stable&=XMVectorGetX(XMVector3Dot(direction,
            XMVector3Normalize(Point(anchor_,l.to)-Point(anchor_,l.from))))>.978f;
    }
    if(!stable) { samples_=0;status_="Calibration: face forward, straighten arms/legs and hold still.";return; }
    if(samples_==0){sums_={};anchor_=f;calibrationStart_=f.time;}
    for(int i:Required) {
        sums_[i].x+=f.landmarks[i].world.x;sums_[i].y+=f.landmarks[i].world.y;sums_[i].z+=f.landmarks[i].world.z;
    }
    ++samples_;status_="Calibrating: hold still...";
    if(samples_<15||f.time-calibrationStart_<.75)return;
    reference_=f;
    for(int i:Required)reference_.landmarks[i].world={sums_[i].x/samples_,sums_[i].y/samples_,sums_[i].z/samples_};
    XMStoreFloat4x4(&referenceBasis_,Basis(reference_,false));
    calibrated_=true;calibrating_=false;status_="Calibrated. Preview active; ready to record.";
}
Pose Capture::Solve(const Frame& f) const {
    auto local=bind_.local;
    Pose out;out.global.resize(local.size());
    const auto referenceBasis=XMLoadFloat4x4(&referenceBasis_);
    // Express source deltas in the calibrated, camera-independent character basis.
    const auto align=XMMatrixTranspose(referenceBasis);
    for(size_t i=0;i<local.size();++i) {
        const int parent=skeleton_.bones[i].parent;
        const auto parentMatrix=parent<0?XMMatrixIdentity():XMLoadFloat4x4(&out.global[parent]);
        const auto link=std::find_if(links_.begin(),links_.end(),[i](const Link& l){return l.bone==static_cast<int>(i);});
        XMMATRIX delta=XMMatrixIdentity();bool mapped=false;
        if(static_cast<int>(i)==hips_||static_cast<int>(i)==chest_) {
            const bool shoulders=static_cast<int>(i)==chest_;
            delta=referenceBasis*XMMatrixTranspose(Basis(reference_,shoulders))*Basis(f,shoulders)*align;mapped=true;
        }else if(link!=links_.end()) {
            const auto from=XMVector3TransformNormal(Point(reference_,link->to)-Point(reference_,link->from),align);
            const auto to=XMVector3TransformNormal(Point(f,link->to)-Point(f,link->from),align);
            // Calibrated arms-down is neutral even for a target with a T/A bind pose.
            const auto desired=XMVector3Rotate(XMVectorSet(0,-1,0,0),Arc(from,to));
            delta=XMMatrixRotationQuaternion(Arc(XMLoadFloat3(&link->bindDirection),desired));mapped=true;
        }
        if(mapped) {
            const auto target=Rotation(bind_.global[i])*delta;
            const auto parentRotation=parent<0?XMMatrixIdentity():Rotation(out.global[parent]);
            XMStoreFloat4(&local[i].rotation,XMQuaternionNormalize(XMQuaternionRotationMatrix(target*XMMatrixTranspose(parentRotation))));
        }
        XMStoreFloat4x4(&out.global[i],Matrix(local[i])*parentMatrix);
    }
    out.local=std::move(local);return out;
}
void Capture::Consume(const Frame& f,double received) {
    if(f.sequence<=sequence_||f.time<=frameTime_||!std::isfinite(f.time)||!std::isfinite(received))
        throw std::runtime_error("Mocap timestamps/sequence must increase.");
    sequence_=f.sequence;frameTime_=f.time;
    if(Recording()&&goodTime_>=0&&f.time-goodTime_>LostAfter) {
        StopRecording();status_="Tracking gap >0.5s: take preserved up to the last valid pose.";
    }
    if(!Usable(f)) {
        tracking_=false;samples_=0;
        status_="Low confidence / occluded / out of frame: holding last pose. Recording stops after 0.5s.";
        Tick(received);return;
    }
    const double dt=goodTime_<0?0:f.time-goodTime_;
    if(calibrating_&&dt>LostAfter)samples_=0;
    tracking_=true;
    if(calibrating_)SampleCalibration(f);
    if(calibrated_) {
        previousPose_=pose_;
        const auto solved=Solve(f);
        const float alpha=smoothingSeconds<=0||dt>LostAfter?1.f:1.f-std::exp(-static_cast<float>(dt)/smoothingSeconds);
        pose_=Blend(skeleton_,pose_,solved,alpha);
        if(Recording()) {
            const double end=std::min(f.time-recordStart_,MaxSeconds);
            while(static_cast<double>(nextSample_)/RecordFps<=end+1e-9) {
                const double t=static_cast<double>(nextSample_)/RecordFps;
                const float weight=dt>0?static_cast<float>((recordStart_+t-goodTime_)/dt):1.f;
                WriteSample(t,Blend(skeleton_,previousPose_,pose_,std::clamp(weight,0.f,1.f)));++nextSample_;
            }
            if(f.time-recordStart_>=MaxSeconds) {
                const float weight=static_cast<float>((recordStart_+MaxSeconds-goodTime_)/dt);
                Finish(MaxSeconds,Blend(skeleton_,previousPose_,pose_,std::clamp(weight,0.f,1.f)));
                status_="120-second take limit reached; take preserved.";
            }else status_="Recording at 30 FPS (timestamp resampling).";
        }else if(!completed_)status_="Calibrated. Preview active; ready to record.";
    }
    goodTime_=f.time;goodReceived_=received;
}
void Capture::Tick(double now) {
    if(goodReceived_>=0&&now-goodReceived_>LostAfter) {
        tracking_=false;
        if(Recording()){StopRecording();status_="Tracking timeout >0.5s: take preserved up to the last valid pose.";}
        else if(calibrated_&&!completed_)status_="Tracking lost: holding last pose; check full-body visibility/camera.";
        if(calibrating_)samples_=0;
    }
}
void Capture::StartRecording(const std::string& name,double now) {
    if(!calibrated_||!tracking_||now-goodReceived_>LostAfter||Recording()||completed_)
        throw std::runtime_error("A fresh calibrated pose is required before recording.");
    recording_=NewClip(name.empty()?"Webcam take":name,0);recording_->loop=false;
    recordStart_=goodTime_;nextSample_=1;WriteSample(0,pose_);
}
void Capture::WriteSample(double time,const Pose& p) {
    SetTransformKey(*recording_,static_cast<uint32_t>(hips_),time,p.local[hips_]);
    SetTransformKey(*recording_,static_cast<uint32_t>(chest_),time,p.local[chest_]);
    for(const auto& l:links_)SetTransformKey(*recording_,static_cast<uint32_t>(l.bone),time,p.local[l.bone]);
}
void Capture::Finish(double duration,const Pose& p) {
    WriteSample(duration,p);recording_->duration=duration;
    completed_=std::move(recording_);recording_.reset();
}
void Capture::StopRecording() {
    if(Recording())Finish(std::max(0.,goodTime_-recordStart_),pose_);
}
std::optional<AnimationClip> Capture::TakeCompleted() {
    auto result=std::move(completed_);completed_.reset();return result;
}
double Capture::RecordedSeconds() const {return Recording()?std::max(0.,goodTime_-recordStart_):0;}
}

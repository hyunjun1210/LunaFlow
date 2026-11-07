#include "AgentBridge/EditPlan.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <initializer_list>
#include <set>
#include <stdexcept>

namespace LunaFlow {
namespace {
Json V3(const XMFLOAT3& v){return {v.x,v.y,v.z};}
Json V4(const XMFLOAT4& v){return {v.x,v.y,v.z,v.w};}
Json TRS(const Transform& t){return {{"translation",V3(t.translation)},{"rotation",V4(t.rotation)},{"scale",V3(t.scale)}};}
Json Effects(const ModelAsset& m){
    Json result=Json::array();
    for(const auto& e:m.effects)result.push_back({{"id",e.id},{"name",e.name},{"clipId",e.clipId},{"bonePath",m.skeleton.bones.at(e.bone).path},
        {"type",static_cast<int>(e.type)},{"local",TRS(e.local)},{"color",V4(e.color)},{"size",e.size},{"intensity",e.intensity},
        {"start",e.start},{"end",e.end},{"enabled",e.enabled},{"particles",e.type==EffectType::Particles?EncodeParticles(e.particles):Json(nullptr)}});
    return result;
}
double Number(const Json& j,double lo,double hi){const auto v=j.get<double>();if(!std::isfinite(v)||v<lo||v>hi)throw std::runtime_error("Plan number is outside the supported range.");return v;}
std::string Text(const Json& j,size_t limit=512){auto s=j.get<std::string>();if(s.empty()||s.size()>limit)throw std::runtime_error("Invalid plan text.");return s;}
uint32_t FindPlanBone(const ModelAsset& m,const std::string& path){for(uint32_t i=0;i<m.skeleton.bones.size();++i)if(m.skeleton.bones[i].path==path)return i;throw std::runtime_error("Unknown plan bone path.");}
AnimationClip& Clip(ModelAsset& m,const std::string& id){for(auto& c:m.clips)if(c.id==id)return c;throw std::runtime_error("Plan motion no longer exists.");}
EffectData& Effect(ModelAsset& m,const std::string& id){for(auto& e:m.effects)if(e.id==id)return e;throw std::runtime_error("Plan effect no longer exists.");}
const Json Operations=Json::array({"create_motion","create_sword_swing","duplicate_motion","rename_motion","retime_motion","set_loop","set_bone_key","rotate_bone_key","aim_bone_key","create_particles","update_particles","attach_effect","preview_motion"});
std::string AnatomicalName(std::string name){
    const auto colon=name.find_last_of(':');if(colon!=std::string::npos)name=name.substr(colon+1);
    std::transform(name.begin(),name.end(),name.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
    if(name.size()>2&&(name.substr(name.size()-2)=="_l"||name.substr(name.size()-2)==".l"))name="left"+name.substr(0,name.size()-2);
    if(name.size()>2&&(name.substr(name.size()-2)=="_r"||name.substr(name.size()-2)==".r"))name="right"+name.substr(0,name.size()-2);
    name.erase(std::remove_if(name.begin(),name.end(),[](unsigned char c){return !std::isalnum(c);}),name.end());
    if(name.rfind("mixamorig",0)==0)name=name.substr(9);
    if(name=="pelvis")return "hips";
    for(const std::string side:{"left","right"}){
        if(name==side+"arm")return side+"upperarm";
        if(name==side+"lowerarm")return side+"forearm";
        if(name==side+"wrist")return side+"hand";
    }return name;
}
int AnatomicalBone(const SkeletonData& skeleton,const std::string& name){
    int found=-1;for(size_t i=0;i<skeleton.bones.size();++i)if(AnatomicalName(skeleton.bones[i].name)==name){if(found>=0)return -1;found=static_cast<int>(i);}return found;
}
XMVECTOR Position(const Pose& pose,int bone){const auto& m=pose.global.at(static_cast<size_t>(bone));return XMVectorSet(m._41,m._42,m._43,0);}
float Length(FXMVECTOR value){return XMVectorGetX(XMVector3Length(value));}
struct CharacterFrame {XMFLOAT3 right{},up{},forward{};bool valid=false;};
CharacterFrame Facing(const SkeletonData& skeleton,const Pose& bind){
    CharacterFrame frame;const int left=AnatomicalBone(skeleton,"leftupperarm"),right=AnatomicalBone(skeleton,"rightupperarm"),hips=AnatomicalBone(skeleton,"hips"),head=AnatomicalBone(skeleton,"head");
    if(left<0||right<0||hips<0||head<0)return frame;
    // Landmarks must belong to one character; never infer facing across unrelated rigs.
    auto root=[&](int bone){while(skeleton.bones[bone].parent>=0)bone=skeleton.bones[bone].parent;return bone;};
    if(root(left)!=root(right)||root(left)!=root(hips)||root(left)!=root(head))return frame;
    // A reflection reverses the cross-product facing inference. Without an explicit
    // user-facing direction mapping, fail instead of silently generating a rearward strike.
    for(const int bone:{left,right,hips,head}){
        const float determinant=XMVectorGetX(XMMatrixDeterminant(XMLoadFloat4x4(&bind.global[bone])));
        if(!std::isfinite(determinant)||determinant<=1e-12f)return frame;
    }
    auto up=Position(bind,head)-Position(bind,hips),across=Position(bind,right)-Position(bind,left);
    if(Length(up)<1e-5f||Length(across)<1e-5f)return frame;
    up=XMVector3Normalize(up);across-=up*XMVectorGetX(XMVector3Dot(up,across));if(Length(across)<1e-5f)return frame;
    // LunaFlow scenes are left-handed (+Y up), so anatomical forward = right x up.
    // Example: an imported Mixamo rig has its right shoulder at -X and faces -Z.
    across=XMVector3Normalize(across);const auto forward=XMVector3Normalize(XMVector3Cross(across,up));
    XMStoreFloat3(&frame.right,across);XMStoreFloat3(&frame.up,up);XMStoreFloat3(&frame.forward,forward);frame.valid=true;return frame;
}
XMVECTOR CharacterDirection(const CharacterFrame& frame,const XMFLOAT3& direction){
    if(!frame.valid)throw std::runtime_error("Cannot determine character facing. Map a unique head, hips and left/right upper-arm rig, and apply mirrored or singular object scales first.");
    return XMLoadFloat3(&frame.right)*direction.x+XMLoadFloat3(&frame.up)*direction.y+XMLoadFloat3(&frame.forward)*direction.z;
}
XMVECTOR ShortestArc(FXMVECTOR from,FXMVECTOR to){
    const auto a=XMVector3Normalize(from),b=XMVector3Normalize(to);const float dot=std::clamp(XMVectorGetX(XMVector3Dot(a,b)),-1.0f,1.0f);
    if(dot<-.9999f){auto axis=XMVector3Cross(a,XMVectorSet(1,0,0,0));if(Length(axis)<.001f)axis=XMVector3Cross(a,XMVectorSet(0,1,0,0));return XMQuaternionRotationAxis(axis,XM_PI);}
    return XMQuaternionNormalize(XMVectorSetW(XMVector3Cross(a,b),1+dot));
}
Transform Aim(const SkeletonData& skeleton,const AnimationClip& clip,double sourceTime,uint32_t bone,uint32_t child,FXMVECTOR direction){
    if(child==bone)throw std::runtime_error("Aim requires a distinct descendant bone.");
    int ancestor=skeleton.bones[child].parent;while(ancestor>=0&&ancestor!=static_cast<int>(bone))ancestor=skeleton.bones[ancestor].parent;
    if(ancestor<0)throw std::runtime_error("Aim child must be a descendant of the rotating bone.");
    auto pose=EvaluatePose(skeleton,&clip,sourceTime/clip.speed);auto result=pose.local[bone];const int parent=skeleton.bones[bone].parent;
    const auto inverseParent=parent<0?XMMatrixIdentity():XMMatrixInverse(nullptr,XMLoadFloat4x4(&pose.global[parent]));
    const auto from=XMVector3TransformNormal(Position(pose,child)-Position(pose,bone),inverseParent),to=XMVector3TransformNormal(direction,inverseParent);
    if(Length(from)<1e-5f||Length(to)<1e-5f)throw std::runtime_error("Aim direction and bone length must be nonzero.");
    const auto rotation=XMMatrixRotationQuaternion(XMLoadFloat4(&result.rotation))*XMMatrixRotationQuaternion(ShortestArc(from,to));
    XMStoreFloat4(&result.rotation,XMQuaternionNormalize(XMQuaternionRotationMatrix(rotation)));return result;
}
bool DescendsFrom(const SkeletonData& skeleton,int bone,int ancestor){
    for(int b=bone>=0?skeleton.bones[bone].parent:-1;b>=0;b=skeleton.bones[b].parent)if(b==ancestor)return true;return false;
}
// Procedural pose solver used to bake full-body presets. Rotations are world-space deltas
// converted into each parent's frame, so arbitrary FBX bone-local axes behave like Luna's rig.
struct PoseRig {
    const SkeletonData& skeleton;std::vector<Transform> local;std::vector<XMFLOAT4X4> global;
    explicit PoseRig(const SkeletonData& s):skeleton(s){Reset();}
    void Reset(){local.clear();for(const auto& b:skeleton.bones)local.push_back(b.bindLocal);Update();}
    void Update(){
        global.resize(skeleton.bones.size());
        for(size_t i=0;i<skeleton.bones.size();++i){
            XMMATRIX m=Matrix(local[i]);const int p=skeleton.bones[i].parent;
            if(p>=0)m*=XMLoadFloat4x4(&global[static_cast<size_t>(p)]);
            XMStoreFloat4x4(&global[i],m);
        }
    }
    XMMATRIX Global(int bone)const{return XMLoadFloat4x4(&global[static_cast<size_t>(bone)]);}
    XMVECTOR At(int bone)const{const auto& m=global[static_cast<size_t>(bone)];return XMVectorSet(m._41,m._42,m._43,0);}
    XMVECTOR Point(int bone,const XMFLOAT3& offset)const{return XMVectorSetW(XMVector3TransformCoord(XMLoadFloat3(&offset),Global(bone)),0);}
    XMMATRIX ParentInverse(int bone)const{const int p=skeleton.bones[static_cast<size_t>(bone)].parent;return p<0?XMMatrixIdentity():XMMatrixInverse(nullptr,Global(p));}
    void Rotate(int bone,FXMVECTOR worldDelta){
        // Conjugating by the parent keeps the angle and expresses the world axis in parent space.
        const auto axis=XMVectorSetW(worldDelta,0);const float size=Length(axis);if(size<1e-7f)return;
        const auto parentAxis=XMVector3Normalize(XMVector3TransformNormal(axis,ParentInverse(bone)))*size;
        auto& q=local[static_cast<size_t>(bone)].rotation;
        XMStoreFloat4(&q,XMQuaternionNormalize(XMQuaternionMultiply(XMLoadFloat4(&q),XMVectorSetW(parentAxis,XMVectorGetW(worldDelta)))));Update();
    }
    void Aim(int bone,FXMVECTOR tip,FXMVECTOR direction){
        const auto inverse=ParentInverse(bone);
        const auto from=XMVector3TransformNormal(tip-At(bone),inverse),to=XMVector3TransformNormal(direction,inverse);
        if(Length(from)<1e-6f||Length(to)<1e-6f)return;
        auto& q=local[static_cast<size_t>(bone)].rotation;
        XMStoreFloat4(&q,XMQuaternionNormalize(XMQuaternionMultiply(XMLoadFloat4(&q),ShortestArc(from,to))));Update();
    }
    void Move(int bone,FXMVECTOR worldOffset){
        auto& t=local[static_cast<size_t>(bone)].translation;
        XMStoreFloat3(&t,XMLoadFloat3(&t)+XMVector3TransformNormal(worldOffset,ParentInverse(bone)));Update();
    }
};
// Tilts `from` toward `toward` by degrees; Swing returns that as a world rotation.
XMVECTOR Tilt(FXMVECTOR from,FXMVECTOR toward,float degrees){
    const auto a=XMVector3Normalize(from);const auto b=toward-a*XMVectorGetX(XMVector3Dot(a,toward));
    if(Length(b)<1e-6f)return a;const float r=XMConvertToRadians(degrees);
    return a*std::cos(r)+XMVector3Normalize(b)*std::sin(r);
}
XMVECTOR Swing(FXMVECTOR axis,FXMVECTOR toward,float degrees){return ShortestArc(axis,Tilt(axis,toward,degrees));}
int CommonAncestor(const SkeletonData& s,int a,int b){
    std::set<int> path;for(int x=a;x>=0;x=s.bones[static_cast<size_t>(x)].parent)path.insert(x);
    for(int x=b;x>=0;x=s.bones[static_cast<size_t>(x)].parent)if(path.count(x))return x;
    return -1;
}
// Bones strictly below `ancestor` down to and including `bone`, parent first; empty when unrelated.
std::vector<int> Between(const SkeletonData& s,int ancestor,int bone){
    std::vector<int> chain;if(ancestor<0||bone<0)return chain;
    int x=bone;for(;x>=0&&x!=ancestor;x=s.bones[static_cast<size_t>(x)].parent)chain.push_back(x);
    if(x!=ancestor)return {};
    std::reverse(chain.begin(),chain.end());return chain;
}
bool Regular(const Pose& bind,int bone){
    const float d=XMVectorGetX(XMMatrixDeterminant(XMLoadFloat4x4(&bind.global[static_cast<size_t>(bone)])));return std::isfinite(d)&&d>1e-12f;
}
struct Leg {int upper=-1,lower=-1,foot=-1;float thigh=0,shin=0;XMFLOAT3 tip{},ankle{};XMFLOAT4X4 footRest{};};
int SideBone(const SkeletonData& s,const std::string& side,std::initializer_list<const char*> names){
    for(const char* name:names){const int bone=AnatomicalBone(s,side+name);if(bone>=0)return bone;}
    return -1;
}
bool FindLeg(const SkeletonData& s,const Pose& bind,int hips,const std::string& side,Leg& leg){
    // UpperLeg/LowerLeg (Luna, auto-rig), UpLeg/Leg (Mixamo), thigh/calf (Unreal), thigh/shin (Blender).
    leg.upper=SideBone(s,side,{"upperleg","upleg","thigh"});leg.lower=SideBone(s,side,{"lowerleg","leg","calf","shin","knee"});
    if(hips<0||leg.upper<0||leg.lower<0||!DescendsFrom(s,leg.upper,hips)||!DescendsFrom(s,leg.lower,leg.upper)||!Regular(bind,leg.upper)||!Regular(bind,leg.lower))return false;
    leg.foot=SideBone(s,side,{"foot","ankle"});
    if(leg.foot>=0&&(!DescendsFrom(s,leg.foot,leg.lower)||!Regular(bind,leg.foot)))leg.foot=-1;
    const auto hip=Position(bind,leg.upper),knee=Position(bind,leg.lower);auto ankle=knee+(knee-hip);
    if(leg.foot>=0)ankle=Position(bind,leg.foot);
    else{
        // A leaf shin (Luna's demo rig) is assumed to end one thigh length below the knee; a single end bone marks it exactly.
        int child=-1,count=0;
        for(size_t i=0;i<s.bones.size();++i)if(s.bones[i].parent==leg.lower){child=static_cast<int>(i);++count;}
        if(count==1)ankle=Position(bind,child);
    }
    leg.thigh=Length(knee-hip);leg.shin=Length(ankle-knee);if(leg.thigh<1e-4f||leg.shin<1e-4f)return false;
    XMStoreFloat3(&leg.ankle,ankle);
    XMStoreFloat3(&leg.tip,XMVector3TransformCoord(ankle,XMMatrixInverse(nullptr,XMLoadFloat4x4(&bind.global[static_cast<size_t>(leg.lower)]))));
    if(leg.foot>=0)leg.footRest=bind.global[static_cast<size_t>(leg.foot)];
    return true;
}
// Returns a bone to a world orientation (keeps a planted or stepping foot level with the ground).
void Restore(PoseRig& rig,int bone,const XMFLOAT4X4& rest){
    const auto current=rig.Global(bone),goal=XMLoadFloat4x4(&rest);
    const auto cx=XMVector3Normalize(current.r[0]),cy=XMVector3Normalize(current.r[1]),gx=XMVector3Normalize(goal.r[0]),gy=XMVector3Normalize(goal.r[1]);
    const auto first=ShortestArc(cx,gx);const auto turned=XMVector3TransformNormal(cy,XMMatrixRotationQuaternion(first));
    const auto a=turned-gx*XMVectorGetX(XMVector3Dot(gx,turned)),b=gy-gx*XMVectorGetX(XMVector3Dot(gx,gy));
    rig.Rotate(bone,Length(a)>1e-5f&&Length(b)>1e-5f?XMQuaternionMultiply(first,ShortestArc(a,b)):first);
}
// Two-bone leg IK: the knee bends toward `pole` and the ankle reaches `target` when in range.
void SolveLeg(PoseRig& rig,const Leg& leg,FXMVECTOR target,FXMVECTOR pole){
    const auto hip=rig.At(leg.upper);const auto reach=target-hip;const float distance=Length(reach);if(distance<1e-5f)return;
    const auto along=reach/distance;const float a=leg.thigh,b=leg.shin;
    const float d=std::clamp(distance,std::abs(a-b)+1e-4f,(a+b)*.999f);
    const float x=(a*a-b*b+d*d)/(2*d),h=std::sqrt(std::max(0.f,a*a-x*x));
    const auto bend=pole-along*XMVectorGetX(XMVector3Dot(pole,along));if(Length(bend)<1e-5f)return;
    rig.Aim(leg.upper,rig.At(leg.lower),along*x+XMVector3Normalize(bend)*h);
    rig.Aim(leg.lower,rig.Point(leg.lower,leg.tip),hip+along*d-rig.At(leg.lower));
    if(leg.foot>=0)Restore(rig,leg.foot,leg.footRest);
}
// Full-body overhead slash. Columns: phase (0..1); hips drop and advance (leg lengths); pelvis and
// chest yaw (degrees, + coils the sword shoulder back); forward bend; lead foot advance and lift and
// trailing foot advance (leg lengths); clavicle shrug; head nod; then sword upper-arm, sword forearm,
// free upper-arm and free forearm directions as [outward, up, forward] in the character frame.
enum SwingColumn {SwPhase,SwDrop,SwAdvance,SwPelvis,SwChest,SwBend,SwLead,SwLift,SwTrail,SwShrug,SwNod,
    SwArm,SwFore=SwArm+3,SwFreeArm=SwFore+3,SwFreeFore=SwFreeArm+3,SwColumns=SwFreeFore+3};
constexpr float SwingKeys[][SwColumns]={
    // Guard: soft knees, sword foot ahead, blade low in front, free arm relaxed.
    {0, .05f,.02f, -6,-4,4, .14f,0,-.10f, 0,2, .20f,-.80f,.56f, .10f,-.20f,.97f, .18f,-.97f,.12f, .08f,-.93f,.35f},
    // Wind-up: weight rocks back, the sword shoulder coils back, the blade rises overhead,
    // the lead foot lifts and the free hand reaches toward the target for balance.
    {.16f, .04f,-.05f, 8,24,-5, .16f,.03f,-.10f, 10,0, .22f,.78f,.45f, .08f,.70f,.65f, .30f,-.40f,.86f, .12f,-.10f,.99f},
    {.28f, .03f,-.03f, 10,28,-8, .26f,.08f,-.10f, 12,0, .18f,.65f,.70f, .06f,.25f,.97f, .30f,-.45f,.84f, .12f,-.15f,.98f},
    // Strike: the lead foot lands, hips drop and drive forward, the torso uncoils and bends into the cut
    // while the free arm pulls back.
    {.44f, .13f,.17f, -10,-16,16, .48f,0,-.10f, 3,2, .12f,.05f,.99f, -.04f,-.55f,.84f, .30f,-.85f,-.40f, .15f,-.90f,.05f},
    {.62f, .16f,.20f, -14,-24,22, .48f,0,-.10f, 0,2, .10f,-.45f,.89f, -.04f,-.88f,.47f, .28f,-.88f,-.38f, .12f,-.92f,.10f},
    // Recovery: rise, then step back into guard.
    {.80f, .11f,.14f, -8,-12,12, .48f,0,-.10f, 0,2, .16f,-.80f,.50f, .04f,-.90f,.43f, .22f,-.95f,-.15f, .10f,-.94f,.20f},
    {.90f, .07f,.07f, -7,-7,7, .30f,.05f,-.10f, 0,2, .18f,-.80f,.54f, .07f,-.55f,.80f, .20f,-.96f,0, .09f,-.94f,.28f},
    {1, .05f,.02f, -6,-4,4, .14f,0,-.10f, 0,2, .20f,-.80f,.56f, .10f,-.20f,.97f, .18f,-.97f,.12f, .08f,-.93f,.35f},
};
// Monotone cubic (Fritsch-Butland) through the key poses: smooth, but planted feet never slide past
// a key and lifted feet never dip below the ground.
float SwingValue(int column,float u){
    constexpr size_t count=sizeof(SwingKeys)/sizeof(SwingKeys[0]);
    if(u<=SwingKeys[0][SwPhase])return SwingKeys[0][column];
    if(u>=SwingKeys[count-1][SwPhase])return SwingKeys[count-1][column];
    size_t i=0;while(i+2<count&&u>SwingKeys[i+1][SwPhase])++i;
    auto slope=[&](size_t k){return (SwingKeys[k+1][column]-SwingKeys[k][column])/(SwingKeys[k+1][SwPhase]-SwingKeys[k][SwPhase]);};
    auto tangent=[&](size_t k){
        if(k==0||k+1>=count)return 0.f;
        const float a=slope(k-1),b=slope(k);if(a*b<=0)return 0.f;
        const float h0=SwingKeys[k][SwPhase]-SwingKeys[k-1][SwPhase],h1=SwingKeys[k+1][SwPhase]-SwingKeys[k][SwPhase];
        return 3*(h0+h1)/((2*h1+h0)/a+(h1+2*h0)/b);
    };
    const float h=SwingKeys[i+1][SwPhase]-SwingKeys[i][SwPhase],t=(u-SwingKeys[i][SwPhase])/h,t2=t*t,t3=t2*t;
    return (2*t3-3*t2+1)*SwingKeys[i][column]+(t3-2*t2+t)*h*tangent(i)+(3*t2-2*t3)*SwingKeys[i+1][column]+(t3-t2)*h*tangent(i+1);
}
AnimationClip SwordSwing(const SkeletonData& skeleton,const Json& args){
    const auto side=args.value("side",std::string("right"));if(side!="left"&&side!="right")throw std::runtime_error("Sword swing side must be left or right.");
    const auto bind=EvaluatePose(skeleton,nullptr,0);const auto frame=Facing(skeleton,bind);
    const int arm=AnatomicalBone(skeleton,side+"upperarm"),fore=AnatomicalBone(skeleton,side+"forearm"),hand=AnatomicalBone(skeleton,side+"hand");
    if(!frame.valid||arm<0||fore<0||hand<0||!DescendsFrom(skeleton,fore,arm)||!DescendsFrom(skeleton,hand,fore))throw std::runtime_error("Sword swing needs a unique humanoid head, hips and upper-arm/forearm/hand chain with non-mirrored, nonsingular transforms. Map the rig (an unrigged mesh can use Edit > Auto rig character) and apply mirrored scales before generating this motion.");
    const double duration=args.contains("duration")?Number(args["duration"],.2,30):1.2;
    auto clip=NewClip(args.contains("name")?Text(args["name"],256):"Sword / forward slash",duration);clip.loop=false;
    const float sign=side=="right"?1.f:-1.f;const std::string other=side=="right"?"left":"right";
    const auto up=XMLoadFloat3(&frame.up),forward=XMLoadFloat3(&frame.forward),swordSide=XMLoadFloat3(&frame.right)*sign;
    // Arm poses are defined in anatomical right/up/forward space, never rig-local Euler axes, and stay
    // absolute so the sword hand remains in front of the shoulder however the torso turns.
    auto direction=[&](const float* v,float mirror){return CharacterDirection(frame,{v[0]*mirror,v[1],v[2]});};
    const int freeArm=AnatomicalBone(skeleton,other+"upperarm"),freeFore=AnatomicalBone(skeleton,other+"forearm"),freeHand=AnatomicalBone(skeleton,other+"hand");
    const bool freeChain=freeArm>=0&&freeFore>=0&&freeHand>=0&&DescendsFrom(skeleton,freeFore,freeArm)&&DescendsFrom(skeleton,freeHand,freeFore);
    // Whole-body chains come from the hierarchy: hips -> spine -> chest (shared parent of both arms) -> neck/head.
    const int hips=AnatomicalBone(skeleton,"hips"),head=AnatomicalBone(skeleton,"head");
    const int chest=CommonAncestor(skeleton,AnatomicalBone(skeleton,"leftupperarm"),AnatomicalBone(skeleton,"rightupperarm"));
    const auto spine=Between(skeleton,hips,chest),neck=Between(skeleton,chest,head);
    const int armParent=skeleton.bones[static_cast<size_t>(arm)].parent;
    const int clavicle=armParent>=0&&armParent!=chest&&DescendsFrom(skeleton,armParent,chest)?armParent:-1;
    // Hips travel and pelvis turns need both legs so the feet can stay planted; otherwise the torso
    // still twists and bends above a fixed pelvis.
    Leg lead,trail;
    const bool legs=FindLeg(skeleton,bind,hips,side,lead)&&FindLeg(skeleton,bind,hips,other,trail)&&lead.upper!=trail.upper;
    const float legLength=legs?.5f*(Length(XMLoadFloat3(&lead.ankle)-Position(bind,lead.upper))+Length(XMLoadFloat3(&trail.ankle)-Position(bind,trail.upper))):0.f;
    std::vector<int> keyed;auto keep=[&](int bone){if(bone>=0&&std::find(keyed.begin(),keyed.end(),bone)==keyed.end())keyed.push_back(bone);};
    if(legs){keep(hips);for(const Leg* leg:{&lead,&trail}){keep(leg->upper);keep(leg->lower);keep(leg->foot);}}
    for(const int bone:spine)keep(bone);
    for(const int bone:neck)keep(bone);
    keep(clavicle);keep(arm);keep(fore);keep(hand);
    if(freeChain){keep(freeArm);keep(freeFore);keep(freeHand);}
    // Bake dense keys: the editor interpolates keys linearly, and dense samples keep the solved
    // feet, torso and arm directions consistent between frames.
    PoseRig rig(skeleton);
    const int samples=std::clamp(static_cast<int>(std::ceil(duration*30)),24,900);
    float pose[SwColumns];
    for(int i=0;i<=samples;++i){
        const float u=static_cast<float>(i)/static_cast<float>(samples);const double time=i==samples?duration:duration*i/samples;
        for(int c=0;c<SwColumns;++c)pose[c]=SwingValue(c,u);
        rig.Reset();
        const float pelvis=legs?pose[SwPelvis]:0.f,pelvisBend=legs?pose[SwBend]*.25f:0.f;
        if(legs){
            rig.Move(hips,(forward*pose[SwAdvance]-up*pose[SwDrop])*legLength);
            rig.Rotate(hips,XMQuaternionMultiply(Swing(forward,swordSide,pelvis),Swing(up,forward,pelvisBend)));
        }
        if(!spine.empty()){
            const float n=static_cast<float>(spine.size());
            for(const int bone:spine)rig.Rotate(bone,XMQuaternionMultiply(Swing(forward,swordSide,(pose[SwChest]-pelvis)/n),Swing(up,forward,(pose[SwBend]-pelvisBend)/n)));
        }
        if(!neck.empty()){
            // Counter-turn the head so the eyes stay on the target while the shoulders coil.
            const float n=static_cast<float>(neck.size());
            for(const int bone:neck)rig.Rotate(bone,XMQuaternionMultiply(Swing(forward,swordSide,-.8f*pose[SwChest]/n),Swing(up,forward,(pose[SwNod]-.5f*pose[SwBend])/n)));
        }
        if(clavicle>=0){const auto reach=rig.At(arm)-rig.At(clavicle);if(Length(reach)>1e-5f)rig.Rotate(clavicle,Swing(reach,up,pose[SwShrug]));}
        if(legs){
            SolveLeg(rig,lead,XMLoadFloat3(&lead.ankle)+(forward*pose[SwLead]+up*std::max(0.f,pose[SwLift]))*legLength,forward);
            SolveLeg(rig,trail,XMLoadFloat3(&trail.ankle)+forward*(pose[SwTrail]*legLength),forward);
        }
        rig.Aim(arm,rig.At(fore),direction(&pose[SwArm],sign));rig.Aim(fore,rig.At(hand),direction(&pose[SwFore],sign));
        if(freeChain){rig.Aim(freeArm,rig.At(freeFore),direction(&pose[SwFreeArm],-sign));rig.Aim(freeFore,rig.At(freeHand),direction(&pose[SwFreeFore],-sign));}
        for(const int bone:keyed)SetTransformKey(clip,static_cast<uint32_t>(bone),time,rig.local[static_cast<size_t>(bone)]);
    }
    return clip;
}
ParticlePreset Preset(const std::string& s){
    const std::vector<std::string> names={"custom","fire","smoke","magic","snow","rain","explosion"};
    auto it=std::find(names.begin(),names.end(),s);if(it==names.end())throw std::runtime_error("Unknown particle preset.");return static_cast<ParticlePreset>(it-names.begin());
}
void PatchEffect(EffectData& e,const Json& args){
    if(e.type!=EffectType::Particles)throw std::runtime_error("This command requires a particle system.");
    Json p=EncodeParticles(e.particles);
    if(args.contains("particles")){
        if(!args["particles"].is_object())throw std::runtime_error("Particle parameters must be an object.");
        for(const auto& item:args["particles"].items()){if(!p.contains(item.key()))throw std::runtime_error("Unknown particle parameter.");p[item.key()]=item.value();}
    }
    for(auto& item:p.items())if(args.contains(item.key()))item.value()=args[item.key()];
    e.particles=DecodeParticles(p);
    if(args.contains("color")){
        const auto& c=args["color"];Json values=c.is_object()?Json::array({c.at("r"),c.at("g"),c.at("b"),c.at("a")}):c;
        if(!values.is_array()||values.size()!=4)throw std::runtime_error("Color needs four components.");
        e.color={static_cast<float>(Number(values[0],0,1)),static_cast<float>(Number(values[1],0,1)),static_cast<float>(Number(values[2],0,1)),static_cast<float>(Number(values[3],0,1))};
    }
    if(args.contains("name"))e.name=Text(args["name"],256);
    if(args.contains("size"))e.size=static_cast<float>(Number(args["size"],.001,10));
    if(args.contains("intensity"))e.intensity=static_cast<float>(Number(args["intensity"],0,20));
    if(args.contains("enabled"))e.enabled=args["enabled"].get<bool>();
    if(args.contains("start"))e.start=Number(args["start"],0,3600);
    if(args.contains("end"))e.end=Number(args["end"],0,3600);
    if(e.end<e.start)throw std::runtime_error("Effect end is before its start.");
    if(args.contains("offset")){
        const auto& a=args["offset"];if(!a.is_array()||a.size()!=3)throw std::runtime_error("Offset needs three components.");
        e.local.translation={static_cast<float>(Number(a[0],-100,100)),static_cast<float>(Number(a[1],-100,100)),static_cast<float>(Number(a[2],-100,100))};
    }
}
}
std::string EditStateFingerprint(const ModelAsset& m){
    Json state={{"rig",RigFingerprint(m.skeleton)},{"clips",Json::array()},{"effects",Effects(m)}};
    for(const auto& c:m.clips){auto encoded=EncodeClip(m.skeleton,c);encoded.erase("bones");state["clips"].push_back(std::move(encoded));}
    uint64_t h=14695981039346656037ull;for(unsigned char c:state.dump()){h^=c;h*=1099511628211ull;}return std::to_string(h);
}
Json EditSceneContext(const ModelAsset& m,int clip,int bone,int effect,double time,Json request){
    const auto& c=m.clips.at(static_cast<size_t>(clip));const auto& b=m.skeleton.bones.at(static_cast<size_t>(bone));
    request["sceneFingerprint"]=EditStateFingerprint(m);request["selectedBonePath"]=b.path;
    request["selectedEffectId"]=effect>=0?m.effects.at(static_cast<size_t>(effect)).id:"";
    request["selectedClip"]=EncodeClip(m.skeleton,c);request["selectedClip"].erase("bones");
    request["time"]=time;request["effects"]=Effects(m);request["allowedOperations"]=Operations;request["clips"]=Json::array();
    for(const auto& motion:m.clips)request["clips"].push_back({{"id",motion.id},{"name",motion.name},{"duration",PlaybackDuration(motion)},{"revision",motion.revision}});
    const auto pose=EvaluatePose(m.skeleton,&c,time);request["currentLocalPose"]=Json::array();
    for(size_t i=0;i<pose.local.size();++i)request["currentLocalPose"].push_back({{"bonePath",m.skeleton.bones[i].path},{"transform",TRS(pose.local[i])}});
    request["coordinates"]={{"handedness","left"},{"up","+Y"},{"unit","meter"},{"quaternion","xyzw"},{"keyTime","source clip seconds; preview time is playback seconds"},
        {"rotationDegrees","bone-local delta, DirectX pitch(X), yaw(Y), roll(Z); never assume these axes are anatomical forward/up/right"}};
    const auto bind=EvaluatePose(m.skeleton,nullptr,0);const auto frame=Facing(m.skeleton,bind);
    request["characterFrame"]={{"available",frame.valid},{"directionComponents","[anatomical right, up, forward]"},
        {"source","unique bind-pose head/hips and right/left upper-arm landmarks; mirrored or singular transforms require an explicit direction mapping"}};
    if(frame.valid){request["characterFrame"]["rightWorld"]=V3(frame.right);request["characterFrame"]["upWorld"]=V3(frame.up);request["characterFrame"]["forwardWorld"]=V3(frame.forward);}
    request["worldBones"]=Json::array();
    for(size_t i=0;i<pose.global.size();++i){const auto& p=pose.global[i];const auto& rest=bind.global[i];
        request["worldBones"].push_back({{"bonePath",m.skeleton.bones[i].path},{"position",{p._41,p._42,p._43}},
            {"bindPosition",{rest._41,rest._42,rest._43}},{"localAxisXWorld",{p._11,p._12,p._13}},
            {"localAxisYWorld",{p._21,p._22,p._23}},{"localAxisZWorld",{p._31,p._32,p._33}}});
    }
    return request;
}
PlanFocus EditPlan::Apply(ModelAsset& m,const Json& step,References& refs,const std::string& prompt){
    const auto op=step.at("operation").get<std::string>(),target=step.value("targetRef",std::string("selectedClip"));
    Json args=step.contains("argumentsJson")?Json::parse(step.at("argumentsJson").get<std::string>()):step.value("arguments",Json::object());
    if(!args.is_object()||args.dump().size()>65536)throw std::runtime_error("Invalid command arguments.");
    auto ref=[&](const std::string& name,const char* kind)->std::string{auto it=refs.find(name);if(it==refs.end()||it->second.kind!=kind)throw std::runtime_error("Unknown or incompatible plan reference: "+name);return it->second.id;};
    PlanFocus focus;Reference result;
    if(op=="create_sword_swing"){
        if(m.clips.size()>=256)throw std::runtime_error("Too many motions.");
        auto clip=SwordSwing(m.skeleton,args);focus.clipId=clip.id;focus.changed=true;result={"clip",clip.id};m.clips.push_back(std::move(clip));
    }else if(op=="create_particles"){
        if(m.effects.size()>=512)throw std::runtime_error("Too many effects.");
        auto bonePath=args.contains("bonePath")?Text(args["bonePath"]):ref(target,"bone");
        auto clipId=ref(args.value("clipRef",std::string("selectedClip")),"clip");
        auto e=MakeParticleEffect(Preset(args.value("preset",std::string("custom"))),FindPlanBone(m,bonePath),clipId,PlaybackDuration(Clip(m,clipId)));
        PatchEffect(e,args);e.sourcePrompt=prompt;focus.effectId=e.id;focus.bonePath=bonePath;focus.clipId=clipId;focus.changed=true;
        result={"effect",e.id};m.effects.push_back(std::move(e));
    }else if(op=="update_particles"||op=="attach_effect"){
        auto& e=Effect(m,ref(target,"effect"));
        if(op=="update_particles")PatchEffect(e,args);
        if(args.contains("bonePath"))e.bone=FindPlanBone(m,Text(args["bonePath"]));
        else if(op=="attach_effect")e.bone=FindPlanBone(m,ref("selectedBone","bone"));
        if(args.contains("clipRef"))e.clipId=ref(args["clipRef"].get<std::string>(),"clip");
        focus.effectId=e.id;focus.bonePath=m.skeleton.bones[e.bone].path;focus.clipId=e.clipId;focus.changed=true;result={"effect",e.id};
    }else if(op=="preview_motion"){
        focus.clipId=ref(target,"clip");focus.time=args.contains("time")?Number(args["time"],0,PlaybackDuration(Clip(m,focus.clipId))):0;focus.play=args.value("play",true);result={"clip",focus.clipId};
    }else{
        std::string type;
        if(op=="create_motion"){type="new_clip";Number(args.at("duration"),.05,3600);Text(args.at("name"),256);if(m.clips.size()>=256)throw std::runtime_error("Too many motions.");}
        else if(op=="duplicate_motion"){type="duplicate_clip";if(m.clips.size()>=256)throw std::runtime_error("Too many motions.");if(args.contains("name"))Text(args["name"],256);}
        else if(op=="rename_motion")type="rename_clip";
        else if(op=="retime_motion")type="retime_clip";
        else if(op=="set_loop")type="set_loop";
        else if(op=="set_bone_key"||op=="rotate_bone_key"||op=="aim_bone_key")type="set_bone_key";
        else throw std::runtime_error("Unsupported plan operation: "+op);
        auto clipId=op=="create_motion"?ref("selectedClip","clip"):ref(target,"clip");
        auto& clip=Clip(m,clipId);
        if(type=="set_bone_key"){
            const auto path=Text(args.at("bonePath"));const auto index=FindPlanBone(m,path);const double keyTime=Number(args.at("time"),0,3600);
            if(op=="aim_bone_key"){
                const auto child=FindPlanBone(m,Text(args.at("childBonePath")));const auto& values=args.at("direction");
                if(!values.is_array()||values.size()!=3)throw std::runtime_error("Aim direction needs three components.");
                XMFLOAT3 direction{static_cast<float>(Number(values[0],-100,100)),static_cast<float>(Number(values[1],-100,100)),static_cast<float>(Number(values[2],-100,100))};
                auto world=XMLoadFloat3(&direction);const auto space=args.value("space",std::string("character"));
                if(space=="character")world=CharacterDirection(Facing(m.skeleton,EvaluatePose(m.skeleton,nullptr,0)),direction);
                else if(space!="world")throw std::runtime_error("Aim space must be character or world.");
                args["transform"]=TRS(Aim(m.skeleton,clip,keyTime,index,child,world));
            }else if(op=="rotate_bone_key"){
                const auto& angles=args.at("rotationDegrees");if(!angles.is_array()||angles.size()!=3)throw std::runtime_error("Rotation needs three angles.");
                auto transform=EvaluatePose(m.skeleton,&clip,keyTime/clip.speed).local[index];
                auto delta=QuaternionFromDegrees({static_cast<float>(Number(angles[0],-360,360)),static_cast<float>(Number(angles[1],-360,360)),static_cast<float>(Number(angles[2],-360,360))});
                XMStoreFloat4(&transform.rotation,XMQuaternionNormalize(XMQuaternionMultiply(XMLoadFloat4(&delta),XMLoadFloat4(&transform.rotation))));args["transform"]=TRS(transform);
            }
            const auto& trs=args.at("transform");for(const auto* key:{"translation","scale"}){
                const auto& a=trs.at(key);if(!a.is_array()||a.size()!=3)throw std::runtime_error("Transform needs three components.");
                for(const auto& value:a)Number(value,key==std::string("scale")?.0001:-10000,key==std::string("scale")?1000:10000);
            }
            focus.bonePath=path;focus.time=keyTime/clip.speed;
        }
        auto output=ApplyEditorCommand(m.clips,m.skeleton,{{"type",type},{"clipId",clipId},{"expectedRevision",clip.revision},{"args",args}});
        focus.clipId=output.at("clipId").get<std::string>();focus.changed=true;result={"clip",focus.clipId};
    }
    auto alias=step.value("resultRef",std::string{});if(!alias.empty()){
        if(alias.size()>128||refs.count(alias))throw std::runtime_error("Duplicate or reserved result reference.");refs.emplace(alias,std::move(result));
    }
    return focus;
}
void EditPlan::Begin(const ModelAsset& model,const Json& request,const Json& plan){
    *this=EditPlan{};
    if(plan.at("format")!="lunaflow.edit.plan"||plan.at("version")!=1||plan.at("requestId")!=request.at("requestId")||plan.at("sessionId")!=request.at("sessionId"))throw std::runtime_error("Plan does not match this request/session.");
    if(request.at("sceneFingerprint").get<std::string>()!=EditStateFingerprint(model))throw std::runtime_error("The scene changed while Copilot was working. Send a new request.");
    summary_=plan.value("summary",std::string{});if(summary_.size()>4096)throw std::runtime_error("Plan summary is too long.");
    if(plan.value("needsClarification",false)){summary_=Text(plan.at("question"),4096);return;}
    const auto& steps=plan.at("steps");if(!steps.is_array()||steps.empty()||steps.size()>64||plan.dump().size()>1024*1024)throw std::runtime_error("Expected 1 to 64 plan steps within 1 MB.");
    for(const auto& c:model.clips)refs_[c.id]={"clip",c.id};
    for(const auto& e:model.effects)refs_[e.id]={"effect",e.id};
    for(const auto& b:model.skeleton.bones)refs_[b.path]={"bone",b.path};
    refs_["selectedClip"]={"clip",request.at("clipId").get<std::string>()};refs_["selectedBone"]={"bone",request.at("selectedBonePath").get<std::string>()};
    auto effectId=request.value("selectedEffectId",std::string{});if(!effectId.empty())refs_["selectedEffect"]={"effect",effectId};
    // Dedicated agents (e.g. the VFX agent) narrow the operation set; the whole plan is rejected before any edit.
    const auto allowed=request.value("allowedOperations",Operations);
    if(!allowed.is_array())throw std::runtime_error("Request operations must be an array.");
    ModelAsset scratch;scratch.skeleton=model.skeleton;scratch.clips=model.clips;scratch.effects=model.effects;auto dryRefs=refs_;std::set<std::string> ids;
    for(const auto& step:steps){
        if(!ids.insert(Text(step.at("stepId"),128)).second)throw std::runtime_error("Duplicate plan step ID.");
        const auto op=Text(step.at("operation"),64);if(std::find(Operations.begin(),Operations.end(),op)==Operations.end()||std::find(allowed.begin(),allowed.end(),op)==allowed.end())throw std::runtime_error("Operation is not allowed for this agent request: "+op);
        Apply(scratch,step,dryRefs,request.at("prompt").get<std::string>());
        steps_.push_back(step);views_.push_back({Text(step.at("description"),1024),op,"pending"});
    }
    prompt_=request.at("prompt").get<std::string>();expected_=EditStateFingerprint(model);running_=true;
}
PlanFocus EditPlan::Advance(ModelAsset& model){
    if(!running_)return {};
    try{
        if(EditStateFingerprint(model)!=expected_)throw std::runtime_error("Scene edits detected. Remaining AI steps were stopped.");
        // Apply to a data copy so an invalid step never leaves a partial change.
        ModelAsset next;next.skeleton=model.skeleton;next.clips=model.clips;next.effects=model.effects;auto refs=refs_;
        auto result=Apply(next,steps_.at(next_),refs,prompt_);refs_=std::move(refs);
        model.clips=std::move(next.clips);model.effects=std::move(next.effects);expected_=EditStateFingerprint(model);
        views_[next_++].state="completed";running_=next_<steps_.size();return result;
    }catch(const std::exception& e){error_=e.what();views_[next_].state="failed";running_=false;throw;}
}
void EditPlan::Cancel(){running_=false;for(auto& step:views_)if(step.state=="pending")step.state="cancelled";}
}

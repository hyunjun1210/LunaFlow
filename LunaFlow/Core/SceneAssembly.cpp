#include "Core/SceneAssembly.h"
#include "Core/EditorCommands.h"
#include <algorithm>
#include <functional>
#include <set>
#include <stdexcept>

namespace LunaFlow {
static std::string Component(const std::string& name){std::string out;for(char c:name){if(c=='/')out+="%2F";else if(c=='%')out+="%25";else out+=c;}return out;}
std::string AppendModel(ModelAsset& scene,const ModelAsset& source,std::string name) {
    ModelAsset next=scene;
    const uint32_t group=static_cast<uint32_t>(next.skeleton.bones.size());
    const std::string id=NewClipId(),prefix="Object_"+std::to_string(group)+"_";
    if(name.empty())name="Object";const std::string groupName=name+"_"+std::to_string(group);
    next.skeleton.bones.push_back({groupName,"/"+Component(groupName),-1,{}});
    const uint32_t boneOffset=group+1,materialOffset=static_cast<uint32_t>(next.materials.size());
    std::set<std::string> names;for(const auto& bone:next.skeleton.bones)names.insert(bone.name);
    for(const auto& sourceBone:source.skeleton.bones){auto bone=sourceBone;const auto base=bone.name.empty()?std::string("Node"):bone.name;bone.name=base;if(names.count(bone.name))bone.name=prefix+base;unsigned suffix=1;while(names.count(bone.name))bone.name=prefix+base+"_"+std::to_string(suffix++);names.insert(bone.name);bone.parent=sourceBone.parent<0?static_cast<int>(group):static_cast<int>(sourceBone.parent+boneOffset);bone.path=next.skeleton.bones[bone.parent].path+"/"+Component(bone.name);next.skeleton.bones.push_back(std::move(bone));}
    next.materials.insert(next.materials.end(),source.materials.begin(),source.materials.end());
    for(const auto& sourceMesh:source.meshes){auto mesh=sourceMesh;mesh.name=prefix+mesh.name;mesh.material+=materialOffset;for(auto& b:mesh.skin)b.bone+=boneOffset;next.meshes.push_back(std::move(mesh));}
    // Imported prop animations remain independent clips, without overwriting the character.
    for(const auto& sourceClip:source.clips)if(!sourceClip.tracks.empty()){
        auto clip=sourceClip;clip.id=NewClipId();clip.name=name+" / "+sourceClip.name;for(auto& track:clip.tracks)track.bone+=boneOffset;next.clips.push_back(std::move(clip));
    }
    next.objects.push_back({id,name,group});scene=std::move(next);return id;
}
int ObjectForBone(const ModelAsset& m,uint32_t bone) {
    for(int node=static_cast<int>(bone);node>=0;node=m.skeleton.bones.at(node).parent)
        for(size_t i=0;i<m.objects.size();++i)if(m.objects[i].root==static_cast<uint32_t>(node))return static_cast<int>(i);
    return -1;
}
void SetObjectTransform(ModelAsset& model,uint32_t root,const Transform& local) {
    if(root>=model.skeleton.bones.size())throw std::runtime_error("Unknown object root.");
    ModelAsset next=model;
    for(size_t c=0;c<model.clips.size();++c){const auto& source=model.clips[c];auto& target=next.clips[c];
        for(auto& track:target.tracks)if(track.bone==root){
            std::set<double> times;for(auto& k:track.positions)times.insert(k.time);for(auto& k:track.rotations)times.insert(k.time);for(auto& k:track.scales)times.insert(k.time);
            BoneTrack converted;converted.bone=root;
            for(double time:times){const auto pose=EvaluatePose(model.skeleton,&source,time/source.speed);const auto delta=Matrix(pose.local[root])*XMMatrixInverse(nullptr,Matrix(model.skeleton.bones[root].bindLocal));const auto value=Decompose(delta*Matrix(local));converted.positions.push_back({time,value.translation});converted.rotations.push_back({time,value.rotation});converted.scales.push_back({time,value.scale});}
            track=std::move(converted);
        }++target.revision;
    }
    next.skeleton.bones[root].bindLocal=local;model=std::move(next);
}
void AttachObject(ModelAsset& model,const std::string& id,int parentBone,const AnimationClip* active,double time,bool keepWorld) {
    const auto object=std::find_if(model.objects.begin(),model.objects.end(),[&](const auto& o){return o.id==id;});
    if(object==model.objects.end())throw std::runtime_error("Object no longer exists.");
    if(parentBone< -1||parentBone>=static_cast<int>(model.skeleton.bones.size()))throw std::runtime_error("Unknown attachment target.");
    const auto root=object->root;
    for(int p=parentBone;p>=0;p=model.skeleton.bones[p].parent)if(p==static_cast<int>(root))throw std::runtime_error("An object cannot be parented to its own descendant.");
    const auto current=EvaluatePose(model.skeleton,active,time);Transform local;
    if(keepWorld){const auto parent=parentBone<0?XMMatrixIdentity():XMLoadFloat4x4(&current.global[parentBone]);local=Decompose(XMLoadFloat4x4(&current.global[root])*XMMatrixInverse(nullptr,parent));}
    ModelAsset next=model;
    for(size_t c=0;c<model.clips.size();++c){
        const auto& clip=model.clips[c];auto& target=next.clips[c];
        for(auto& track:target.tracks)if(track.bone==root){
            std::set<double> times;for(auto& k:track.positions)times.insert(k.time);for(auto& k:track.rotations)times.insert(k.time);for(auto& k:track.scales)times.insert(k.time);
            BoneTrack result;result.bone=root;
            for(double t:times){const auto pose=EvaluatePose(model.skeleton,&clip,t/clip.speed);const auto delta=Matrix(pose.local[root])*XMMatrixInverse(nullptr,Matrix(model.skeleton.bones[root].bindLocal));
                const auto transformed=Decompose(delta*Matrix(local));result.positions.push_back({t,transformed.translation});result.rotations.push_back({t,transformed.rotation});result.scales.push_back({t,transformed.scale});}
            track=std::move(result);
        }++target.revision;
    }
    next.skeleton.bones[root].parent=parentBone;next.skeleton.bones[root].bindLocal=local;
    std::vector<std::vector<uint32_t>> children(next.skeleton.bones.size());std::vector<uint32_t> roots,order;
    for(uint32_t i=0;i<next.skeleton.bones.size();++i){int parent=next.skeleton.bones[i].parent;if(parent<0)roots.push_back(i);else children[parent].push_back(i);}
    std::function<void(uint32_t)> walk=[&](uint32_t i){order.push_back(i);for(auto child:children[i])walk(child);};for(auto r:roots)walk(r);
    if(order.size()!=next.skeleton.bones.size())throw std::runtime_error("Attachment introduces a cycle.");
    std::vector<uint32_t> remap(order.size());SkeletonData skeleton;
    for(uint32_t i=0;i<order.size();++i)remap[order[i]]=i;
    for(auto old:order){auto bone=next.skeleton.bones[old];if(bone.parent>=0)bone.parent=static_cast<int>(remap[bone.parent]);bone.path=bone.parent<0?"/"+Component(bone.name):skeleton.bones[bone.parent].path+"/"+Component(bone.name);skeleton.bones.push_back(std::move(bone));}
    next.skeleton=std::move(skeleton);
    for(auto& mesh:next.meshes)for(auto& skin:mesh.skin)skin.bone=remap[skin.bone];
    for(auto& clip:next.clips)for(auto& track:clip.tracks)track.bone=remap[track.bone];
    for(auto& item:next.objects)item.root=remap[item.root];for(auto& effect:next.effects)effect.bone=remap[effect.bone];
    model=std::move(next);
}
std::string AddEffect(ModelAsset& model,EffectType type,uint32_t bone,std::string clipId,double duration) {
    if(bone>=model.skeleton.bones.size())throw std::runtime_error("Unknown effect attachment bone.");
    EffectData effect;effect.id=NewClipId();effect.type=type;effect.bone=bone;effect.clipId=std::move(clipId);effect.end=std::max(duration,.1);
    effect.name=type==EffectType::Trail?"Blade trail":type==EffectType::Sparks?"Sparks":"Aura";
    model.effects.push_back(effect);return effect.id;
}
}

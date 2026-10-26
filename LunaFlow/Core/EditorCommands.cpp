#include "Core/EditorCommands.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <stdexcept>

namespace LunaFlow {
std::string NewClipId() {
    static std::atomic<uint64_t> sequence{0};
    return "clip-"+std::to_string(std::chrono::high_resolution_clock::now().time_since_epoch().count())+"-"+std::to_string(++sequence);
}
AnimationClip NewClip(std::string name,double duration) {
    if(name.empty()||!std::isfinite(duration)||duration<0)throw std::runtime_error("Invalid new clip name or duration.");
    return {NewClipId(),std::move(name),duration,1,{}};
}
void RetimeClip(AnimationClip& clip,double duration) {
    if(!std::isfinite(duration)||duration<=0||duration>3600)throw std::runtime_error("Motion length must be between 0 and 3600 seconds.");
    const double factor=clip.duration>0?duration/clip.duration:1;
    for(auto& t:clip.tracks){auto retime=[factor](auto& keys){for(auto& k:keys)k.time*=factor;};retime(t.positions);retime(t.rotations);retime(t.scales);}
    clip.duration=duration;clip.speed=1;++clip.revision;
}
void MoveBoneKey(AnimationClip& clip,uint32_t bone,double from,double to) {
    if(!std::isfinite(from)||!std::isfinite(to)||from<0||to<0||to>3600)throw std::runtime_error("Invalid key time.");
    AnimationClip next=clip;bool changed=false;
    for(auto& track:next.tracks)if(track.bone==bone){auto move=[&](auto& keys){
        auto found=std::find_if(keys.begin(),keys.end(),[&](const auto& k){return std::abs(k.time-from)<1e-5;});
        if(found==keys.end())return;auto key=*found;key.time=to;
        keys.erase(std::remove_if(keys.begin(),keys.end(),[&](const auto& k){return std::abs(k.time-from)<1e-5||std::abs(k.time-to)<1e-5;}),keys.end());
        keys.push_back(key);std::sort(keys.begin(),keys.end(),[](const auto& a,const auto& b){return a.time<b.time;});changed=true;
    };move(track.positions);move(track.rotations);move(track.scales);}
    if(changed){next.duration=std::max(next.duration,to);++next.revision;clip=std::move(next);}
}
Json ApplyEditorCommand(std::vector<AnimationClip>& clips,const SkeletonData& s,const Json& command) {
    const auto type=command.at("type").get<std::string>();const Json args=command.value("args",Json::object());
    if(type=="new_clip"){auto clip=NewClip(args.value("name","Animation"),args.value("duration",3.0));const auto id=clip.id;clips.push_back(std::move(clip));return {{"clipId",id},{"revision",0}};}
    const auto id=command.at("clipId").get<std::string>();
    auto it=std::find_if(clips.begin(),clips.end(),[&](const auto& c){return c.id==id;});if(it==clips.end())throw std::runtime_error("Target clip no longer exists.");
    if(command.at("expectedRevision").get<uint64_t>()!=it->revision)throw std::runtime_error("Revision conflict: the target motion changed after this request.");
    AnimationClip next=*it;
    if(type=="rename_clip"){next.name=args.at("name").get<std::string>();if(next.name.empty()||next.name.size()>256)throw std::runtime_error("Invalid clip name.");++next.revision;}
    else if(type=="duplicate_clip"){next.id=NewClipId();next.name=args.value("name",next.name+" copy");next.revision=0;clips.push_back(next);return {{"clipId",next.id},{"revision",0}};}
    else if(type=="delete_clip"){if(clips.size()==1)throw std::runtime_error("Keep at least one motion in the scene.");clips.erase(it);return {{"clipId",id},{"deleted",true}};}
    else if(type=="retime_clip")RetimeClip(next,args.at("duration").get<double>());
    else if(type=="set_loop"){next.loop=args.at("loop").get<bool>();++next.revision;}
    else {
        const auto path=args.at("bonePath").get<std::string>();auto bone=std::find_if(s.bones.begin(),s.bones.end(),[&](const auto& b){return b.path==path;});
        if(bone==s.bones.end())throw std::runtime_error("Unknown target bone.");const auto index=static_cast<uint32_t>(bone-s.bones.begin());
        if(type=="move_key")MoveBoneKey(next,index,args.at("from").get<double>(),args.at("to").get<double>());
        else if(type=="delete_key")DeleteTransformKey(next,index,args.at("time").get<double>());
        else if(type=="set_bone_key"){
            // Use the same schema validator as imported motion data.
            Json j=EncodeClip(s,next);Json track={{"bonePath",path}};const double time=args.at("time").get<double>();
            j["duration"]=std::max(next.duration,time);j["tracks"]=Json::array();
            const auto& trs=args.at("transform");track["positions"]=Json::array({{{"time",time},{"value",trs.at("translation")}}});
            track["rotations"]=Json::array({{{"time",time},{"value",trs.at("rotation")}}});track["scales"]=Json::array({{{"time",time},{"value",trs.at("scale")}}});j["tracks"].push_back(track);
            auto value=DecodeClip(j,s);const auto pose=EvaluatePose(s,&value,time/next.speed);SetTransformKey(next,index,time,pose.local[index]);
        }else throw std::runtime_error("Unsupported editor command.");
    }
    *it=std::move(next);return {{"clipId",it->id},{"revision",it->revision}};
}
}

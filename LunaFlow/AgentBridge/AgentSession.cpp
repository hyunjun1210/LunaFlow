#include "AgentBridge/AgentSession.h"
#include <algorithm>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace LunaFlow {
std::string RigFingerprint(const SkeletonData& skeleton) {
    const auto clip=NewClip("Rig signature",0);const auto bones=EncodeClip(skeleton,clip).at("bones").dump();
    uint64_t hash=14695981039346656037ull;for(unsigned char c:bones){hash^=c;hash*=1099511628211ull;}
    std::ostringstream out;out<<std::hex<<std::setw(16)<<std::setfill('0')<<hash;return out.str();
}
void AgentSession::Reset(){std::lock_guard<std::mutex> lock(mutex_);session_=NewClipId();requests_.clear();pending_.clear();}
Json AgentSession::Draft(const ModelAsset& model,const AnimationClip& clip,const std::string& prompt) {
    if(prompt.empty()||prompt.size()>16384)throw std::runtime_error("Invalid prompt length.");
    std::lock_guard<std::mutex> lock(mutex_);if(session_.empty())session_=NewClipId();if(requests_.size()>=1000)throw std::runtime_error("Start a new project to clear the request history.");
    const auto id=NewClipId(),rig=RigFingerprint(model.skeleton);requests_[id]={session_,clip.id,rig,clip.revision,false};
    return {{"format","lunaflow.agent.request"},{"version",1},{"requestId",id},{"sessionId",session_},{"clipId",clip.id},{"expectedRevision",clip.revision},
        {"rigFingerprint",rig},{"prompt",prompt},{"duration",PlaybackDuration(clip)},{"bones",EncodeClip(model.skeleton,clip).at("bones")}};
}
void AgentSession::Push(Json response){std::lock_guard<std::mutex> lock(mutex_);if(pending_.size()>=1000)throw std::runtime_error("Agent response queue is full.");pending_.push_back(std::move(response));}
std::vector<std::string> AgentSession::Drain(ModelAsset& model,bool* changed) {
    if(changed)*changed=false;
    std::lock_guard<std::mutex> lock(mutex_);std::vector<std::string> messages;
    while(!pending_.empty()){
        Json response=std::move(pending_.front());pending_.pop_front();
        if(!response.is_object()||!response.contains("requestId")||!response["requestId"].is_string()){messages.push_back("Agent: malformed response ignored.");continue;}
        const auto id=response.value("requestId","");auto found=requests_.find(id);if(found==requests_.end()||found->second.finished)continue;
        auto& request=found->second;
        try {
            if(response.at("sessionId").get<std::string>()!=session_||request.session!=session_)throw std::runtime_error("Agent result belongs to an older project session.");
            const auto status=response.at("status").get<std::string>();
            if(status=="queued"||status=="running"){messages.push_back(response.value("message",status));continue;}
            request.finished=true;
            if(status=="failed")throw std::runtime_error(response.value("message","Motion generation failed."));
            if(status!="completed")throw std::runtime_error("Unknown agent job status.");
            if(request.rig!=RigFingerprint(model.skeleton))throw std::runtime_error("The character skeleton changed while the job was pending.");
            auto target=std::find_if(model.clips.begin(),model.clips.end(),[&](const auto& c){return c.id==request.clipId;});
            if(target==model.clips.end()||target->revision!=request.revision)throw std::runtime_error("The target motion changed while the job was pending. No data was overwritten.");
            auto clips=model.clips;
            if(response.contains("motion")){auto clip=DecodeClip(response.at("motion"),model.skeleton);clip.id=NewClipId();clip.revision=0;clips.push_back(std::move(clip));}
            else {
                const auto& command=response.at("command");
                if(command.at("clipId").get<std::string>()!=request.clipId||command.at("expectedRevision").get<uint64_t>()!=request.revision)throw std::runtime_error("Agent command does not match the captured request target.");
                ApplyEditorCommand(clips,model.skeleton,command);
            }
            model.clips=std::move(clips);if(changed)*changed=true;messages.push_back("Agent result applied to its original target.");
        }catch(const std::exception& e){request.finished=true;messages.push_back(std::string("Agent: ")+e.what());}
    }return messages;
}
}

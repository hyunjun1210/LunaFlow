#pragma once
#include "Core/EditorCommands.h"
#include <deque>
#include <mutex>
#include <unordered_map>

namespace LunaFlow {
// Provider-neutral boundary. Network transport is intentionally not enabled yet.
// Workers submit immutable result JSON; only Drain on the UI thread edits the scene.
class AgentSession {
public:
    void Reset();
    Json Draft(const ModelAsset&,const AnimationClip&,const std::string& prompt);
    void Push(Json response);
    std::vector<std::string> Drain(ModelAsset&,bool* changed=nullptr);
private:
    struct Request {std::string session,clipId,rig;uint64_t revision=0;bool finished=false;};
    std::mutex mutex_;
    std::string session_;
    std::unordered_map<std::string,Request> requests_;
    std::deque<Json> pending_;
};
std::string RigFingerprint(const SkeletonData&);
}

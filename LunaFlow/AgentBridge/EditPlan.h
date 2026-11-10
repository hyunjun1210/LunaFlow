#pragma once
#include "AgentBridge/AgentSession.h"
#include "Core/Particles.h"
#include <map>

namespace LunaFlow {
inline constexpr const char* PlanRigChangedError="The character rig changed while Copilot was working. Send a new request.";
inline constexpr const char* PlanSceneEditedError="Scene edits detected. Remaining AI steps were stopped.";
std::string EditStateFingerprint(const ModelAsset&);
Json EditSceneContext(const ModelAsset&,int clip,int bone,int effect,double time,Json request);
struct PlanStepView { std::string description,operation,state="pending"; };
struct PlanFocus {
    bool changed=false,play=false;
    std::string clipId,effectId,bonePath;
    double time=0;
};
class EditPlan {
public:
    // Preflight the whole plan on an editable-data copy before changing the scene. Motion or effect edits made
    // while waiting are kept: the plan is revalidated and applied on top of the live scene (see Rebased()).
    void Begin(const ModelAsset&,const Json& request,const Json& plan);
    PlanFocus Advance(ModelAsset&);
    void Cancel();
    bool Running() const {return running_;}
    bool Rebased() const {return rebased_;}
    size_t Completed() const {return next_;}
    const std::vector<PlanStepView>& Steps() const {return views_;}
    const std::string& Summary() const {return summary_;}
    const std::string& Error() const {return error_;}
private:
    struct Reference {std::string kind,id;};
    using References=std::map<std::string,Reference>;
    static PlanFocus Apply(ModelAsset&,const Json&,References&,const std::string& prompt);
    References refs_;
    std::vector<Json> steps_;
    std::vector<PlanStepView> views_;
    std::string expected_,summary_,error_,prompt_;
    size_t next_=0;
    bool running_=false,rebased_=false;
};
}

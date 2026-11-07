#pragma once
#include "Core/Scene.h"

namespace LunaFlow {
struct ProportionSettings {
    int fps=30;
    bool handIK=true,footIK=true,balance=true;
};
struct ProportionReport {
    float sourceHeight=0,targetHeight=0,heightRatio=1;
    int mappedBones=0,totalBones=0,reachCorrections=0,groundCorrections=0,bodyCorrections=0,balanceCorrections=0;
    int groundWarnings=0,bodyWarnings=0,poseWarnings=0;
    std::vector<std::string> unmatchedBones;
};
struct ProportionResult {AnimationClip clip;ProportionReport report;};
ProportionResult AdaptMotion(const SkeletonData& source,const AnimationClip&,const SkeletonData& target,const ProportionSettings& settings={});
// Checks use joint positions and a torso capsule, not mesh-level collision tests.
ProportionReport InspectMotion(const SkeletonData&,const AnimationClip&,int fps=30);
}

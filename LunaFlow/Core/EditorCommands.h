#pragma once
#include "Core/Serialization.h"

namespace LunaFlow {
std::string NewClipId();
AnimationClip NewClip(std::string name="Animation",double duration=3);
void RetimeClip(AnimationClip&,double playbackDuration);
void MoveBoneKey(AnimationClip&,uint32_t bone,double fromSourceTime,double toSourceTime);
// Transactional command API shared by UI and future agent bridge.
// {type, clipId, expectedRevision, args}. Revision mismatches never mutate data.
Json ApplyEditorCommand(std::vector<AnimationClip>&,const SkeletonData&,const Json& command);
}

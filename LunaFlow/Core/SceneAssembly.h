#pragma once
#include "Core/Scene.h"

namespace LunaFlow {
std::string AppendModel(ModelAsset&,const ModelAsset&,std::string objectName);
int ObjectForBone(const ModelAsset&,uint32_t bone);
void AttachObject(ModelAsset&,const std::string& objectId,int parentBone,const AnimationClip* activeClip,double time,bool keepWorld=false);
std::string AddEffect(ModelAsset&,EffectType,uint32_t bone,std::string clipId,double duration);
void SetObjectTransform(ModelAsset&,uint32_t root,const Transform&);
}

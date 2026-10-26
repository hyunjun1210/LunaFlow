#pragma once
#include "Core/Scene.h"
#include <nlohmann/json.hpp>

namespace LunaFlow {
using Json=nlohmann::json;
Json EncodeClip(const SkeletonData&,const AnimationClip&);
AnimationClip DecodeClip(const Json&,const SkeletonData&);
void WriteJsonFile(const std::filesystem::path&,const Json&);
}

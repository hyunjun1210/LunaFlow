#pragma once
#include "Core/Scene.h"

namespace LunaFlow {
void ExportUnityPackage(const std::filesystem::path& manifest,const ModelAsset&,double fps,const std::filesystem::path& scripts,const std::string& defaultClipId="");
}

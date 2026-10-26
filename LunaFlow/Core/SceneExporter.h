#pragma once
#include "Core/Scene.h"

namespace LunaFlow {
// Exports mesh, materials, skin, bind hierarchy, and edited clips as FBX takes.
void ExportFbx(const std::filesystem::path&,const ModelAsset&,double fps=60);
}

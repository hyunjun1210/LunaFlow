#pragma once
#include "Core/Scene.h"

namespace LunaFlow {
struct ProjectData {
    ModelAsset model;
    std::string selectedClipId,selectedBonePath;
    double time=0;
    XMFLOAT3 cameraTarget{0,1,0};
    float cameraYaw=.35f,cameraPitch=.12f,cameraDistance=4;
};
void SaveProject(const std::filesystem::path&,const ProjectData&);
ProjectData LoadProject(const std::filesystem::path&);
}

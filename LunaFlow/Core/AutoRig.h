#pragma once
#include "Core/Scene.h"

namespace LunaFlow {
struct AutoRigJoint { std::string name; XMFLOAT3 position{}; };
struct AutoRigReport {
    int object = -1;
    std::vector<uint32_t> bones;      // New skeleton indices, in AutoRig joint order.
    std::vector<AutoRigJoint> joints; // Bone names and bind-pose world positions.
    std::vector<std::string> warnings;
    size_t vertices = 0, meshes = 0;
    float height = 0;
    XMFLOAT3 forward{0, 0, -1};
};
// The requested object, or the object owning the most mesh vertices; -1 when there is nothing to rig.
int AutoRigObject(const ModelAsset&, int object = -1);
// True for an unskinned mesh object that has no hips/head/hand hierarchy.
bool NeedsAutoRig(const ModelAsset&, int object = -1);
// Fits a humanoid skeleton to the bind-pose surface and skins the object's meshes with geodesic voxel weights.
// The model is left unchanged when this throws.
AutoRigReport AutoRig(ModelAsset&, int object = -1);
}

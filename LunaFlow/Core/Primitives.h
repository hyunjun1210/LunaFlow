#pragma once
#include "Core/Scene.h"

namespace LunaFlow {
enum class Primitive { Cube, Sphere, Cylinder, Cone, Plane, Torus };
const char* PrimitiveName(Primitive);
ModelAsset MakeEmptyScene();
// Appends a rigid, independently animatable scene object. Returns its scene node.
uint32_t AddPrimitive(ModelAsset&, Primitive, XMFLOAT3 position = {0,0,0});
}

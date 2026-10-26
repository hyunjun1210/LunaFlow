#pragma once
#include "Core/Scene.h"

namespace LunaFlow {
struct ScreenPoint { XMFLOAT2 pixel{}; float depth=0; bool visible=false; };
struct PickRay { XMFLOAT3 origin{},direction{}; };
ScreenPoint ProjectPoint(XMFLOAT3 world,FXMMATRIX viewProjection,float width,float height);
PickRay MakePickRay(XMFLOAT2 pixel,FXMMATRIX viewProjection,float width,float height);
int PickJoint(const SkeletonData&,const Pose&,FXMMATRIX viewProjection,float width,float height,XMFLOAT2 pixel,float radius=12);
int PickSurfaceBone(const ModelAsset&,const Pose&,const PickRay&);
XMFLOAT3 ScreenDragWorldDelta(float dx,float dy,FXMMATRIX view,float distance,float viewportHeight);
Transform TranslateInWorld(const Transform&,FXMMATRIX parentGlobal,XMFLOAT3 worldDelta);
Transform RotateInWorld(const Transform&,FXMMATRIX parentGlobal,XMFLOAT3 worldAxis,float radians);
float ScreenSegmentDistance(XMFLOAT2 p,XMFLOAT2 a,XMFLOAT2 b);
}

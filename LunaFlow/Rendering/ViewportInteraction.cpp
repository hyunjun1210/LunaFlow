#include "Rendering/ViewportInteraction.h"
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <stdexcept>

namespace LunaFlow {
ScreenPoint ProjectPoint(XMFLOAT3 world,FXMMATRIX vp,float w,float h) {
    const XMVECTOR clip=XMVector4Transform(XMVectorSet(world.x,world.y,world.z,1),vp);
    const float cw=XMVectorGetW(clip);if(cw<=1e-6f)return {};
    XMFLOAT3 ndc;XMStoreFloat3(&ndc,clip/cw);
    return {{(ndc.x*.5f+.5f)*w,(.5f-ndc.y*.5f)*h},ndc.z,ndc.z>=0&&ndc.z<=1};
}
PickRay MakePickRay(XMFLOAT2 p,FXMMATRIX vp,float w,float h) {
    const auto inverse=XMMatrixInverse(nullptr,vp);
    XMVECTOR a=XMVector3TransformCoord(XMVectorSet(2*p.x/w-1,1-2*p.y/h,0,1),inverse);
    XMVECTOR b=XMVector3TransformCoord(XMVectorSet(2*p.x/w-1,1-2*p.y/h,1,1),inverse);
    PickRay ray;XMStoreFloat3(&ray.origin,a);XMStoreFloat3(&ray.direction,XMVector3Normalize(b-a));return ray;
}
float ScreenSegmentDistance(XMFLOAT2 p,XMFLOAT2 a,XMFLOAT2 b) {
    const float x=b.x-a.x,y=b.y-a.y,len=x*x+y*y;
    const float t=len>1e-6f?std::clamp(((p.x-a.x)*x+(p.y-a.y)*y)/len,0.0f,1.0f):0;
    return std::hypot(p.x-a.x-t*x,p.y-a.y-t*y);
}
int PickJoint(const SkeletonData& s,const Pose& pose,FXMMATRIX vp,float w,float h,XMFLOAT2 pixel,float radius) {
    int best=-1;float distance=radius,depth=FLT_MAX;
    for(size_t i=0;i<s.bones.size();++i){const auto& g=pose.global.at(i);auto p=ProjectPoint({g._41,g._42,g._43},vp,w,h);if(!p.visible)continue;
        const float d=std::hypot(p.pixel.x-pixel.x,p.pixel.y-pixel.y);
        if(d<distance || (std::abs(d-distance)<.1f && p.depth<depth)){distance=d;depth=p.depth;best=static_cast<int>(i);}
    }return best;
}
static bool HitTriangle(const PickRay& ray,FXMVECTOR a,FXMVECTOR b,FXMVECTOR c,float& t) {
    const auto origin=XMLoadFloat3(&ray.origin),direction=XMLoadFloat3(&ray.direction),e1=b-a,e2=c-a;
    const auto p=XMVector3Cross(direction,e2);const float det=XMVectorGetX(XMVector3Dot(e1,p));if(std::abs(det)<1e-9f)return false;
    const auto v=origin-a;const float u=XMVectorGetX(XMVector3Dot(v,p))/det;if(u<0||u>1)return false;
    const auto q=XMVector3Cross(v,e1);const float bary=XMVectorGetX(XMVector3Dot(direction,q))/det;if(bary<0||u+bary>1)return false;
    t=XMVectorGetX(XMVector3Dot(e2,q))/det;return t>=0;
}
int PickSurfaceBone(const ModelAsset& model,const Pose& pose,const PickRay& ray) {
    float nearest=FLT_MAX;int selected=-1;
    for(const auto& mesh:model.meshes){
        std::vector<XMFLOAT3> points;points.reserve(mesh.vertices.size());
        for(const auto& v:mesh.vertices){XMVECTOR p=XMVectorZero();for(size_t k=0;k<4;++k)if(v.weights[k]>0){const auto& b=mesh.skin.at(v.joints[k]);p+=XMVector3TransformCoord(XMLoadFloat3(&v.position),XMLoadFloat4x4(&b.inverseBind)*XMLoadFloat4x4(&pose.global.at(b.bone)))*v.weights[k];}XMFLOAT3 f;XMStoreFloat3(&f,p);points.push_back(f);}
        for(size_t i=0;i+2<mesh.indices.size();i+=3){float t=0;const auto a=mesh.indices[i],b=mesh.indices[i+1],c=mesh.indices[i+2];
            if(HitTriangle(ray,XMLoadFloat3(&points[a]),XMLoadFloat3(&points[b]),XMLoadFloat3(&points[c]),t)&&t<nearest){
                nearest=t;const auto& v=mesh.vertices[a];const auto max=std::max_element(v.weights.begin(),v.weights.end());
                selected=static_cast<int>(mesh.skin[v.joints[static_cast<size_t>(max-v.weights.begin())]].bone);
            }
        }
    }return selected;
}
XMFLOAT3 ScreenDragWorldDelta(float dx,float dy,FXMMATRIX view,float distance,float h) {
    const auto inv=XMMatrixInverse(nullptr,view);const float step=2*distance*std::tan(XMConvertToRadians(21))/std::max(h,1.0f);
    XMFLOAT3 result;XMStoreFloat3(&result,(inv.r[0]*dx-inv.r[1]*dy)*step);return result;
}
Transform TranslateInWorld(const Transform& start,FXMMATRIX parent,XMFLOAT3 delta) {
    if(std::abs(XMVectorGetX(XMMatrixDeterminant(parent)))<1e-12f)throw std::runtime_error("Cannot manipulate a joint under a singular parent transform.");
    Transform out=start;const auto local=XMVector3TransformNormal(XMLoadFloat3(&delta),XMMatrixInverse(nullptr,parent));
    XMStoreFloat3(&out.translation,XMLoadFloat3(&start.translation)+local);return out;
}
Transform RotateInWorld(const Transform& start,FXMMATRIX parent,XMFLOAT3 axis,float angle) {
    const Transform parentTransform=Decompose(parent);
    const auto parentRotation=XMMatrixRotationQuaternion(XMLoadFloat4(&parentTransform.rotation));
    const auto rotation=XMMatrixRotationQuaternion(XMLoadFloat4(&start.rotation))*parentRotation*XMMatrixRotationAxis(XMLoadFloat3(&axis),angle)*XMMatrixTranspose(parentRotation);
    Transform out=start;XMStoreFloat4(&out.rotation,XMQuaternionNormalize(XMQuaternionRotationMatrix(rotation)));return out;
}
}

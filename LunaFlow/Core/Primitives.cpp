#include "Core/Primitives.h"
#include <cmath>

namespace LunaFlow {
const char* PrimitiveName(Primitive p) {
    switch(p) {
    case Primitive::Cube:return "Cube";case Primitive::Sphere:return "Sphere";
    case Primitive::Cylinder:return "Cylinder";case Primitive::Cone:return "Cone";
    case Primitive::Plane:return "Plane";case Primitive::Torus:return "Torus";
    }return "Object";
}
ModelAsset MakeEmptyScene() {
    ModelAsset m;m.source="Untitled scene";
    m.skeleton.bones.push_back({"Scene","/Scene",-1,{}});
    m.materials.emplace_back();m.clips.push_back({"scene-motion","Scene motion",3,1,{}});return m;
}
uint32_t AddPrimitive(ModelAsset& model,Primitive type,XMFLOAT3 position) {
    if(model.skeleton.bones.empty()) model=MakeEmptyScene();
    const uint32_t index=static_cast<uint32_t>(model.skeleton.bones.size());
    const std::string name=std::string(PrimitiveName(type))+"."+std::to_string(index);
    Transform transform;transform.translation=position;
    // Independent root: imported character root transforms must not move new objects.
    model.skeleton.bones.push_back({name,"/"+name,-1,transform});
    Material material;material.name=name;
    const XMFLOAT4 colors[]={{.48f,.39f,.83f,1},{.31f,.67f,.64f,1},{.66f,.47f,.37f,1}};
    material.baseColor=colors[index%3];
    MeshData mesh;mesh.name=name;mesh.material=static_cast<uint32_t>(model.materials.size());model.materials.push_back(material);
    SkinBinding skin;skin.bone=index;XMStoreFloat4x4(&skin.inverseBind,XMMatrixIdentity());mesh.skin.push_back(skin);
    auto vertex=[&](XMFLOAT3 p,XMFLOAT3 n,XMFLOAT2 uv){Vertex v;v.position=p;v.normal=n;v.uv=uv;v.weights[0]=1;mesh.vertices.push_back(v);};
    auto triangle=[&](uint32_t a,uint32_t b,uint32_t c){mesh.indices.insert(mesh.indices.end(),{a,b,c});};
    constexpr unsigned segments=32,rings=20;
    if(type==Primitive::Cube || type==Primitive::Plane) {
        const XMFLOAT3 normals[]={{0,0,-1},{0,0,1},{-1,0,0},{1,0,0},{0,1,0},{0,-1,0}};
        const XMFLOAT3 corners[6][4]={
            {{-.5f,0,-.5f},{-.5f,1,-.5f},{.5f,1,-.5f},{.5f,0,-.5f}},
            {{.5f,0,.5f},{.5f,1,.5f},{-.5f,1,.5f},{-.5f,0,.5f}},
            {{-.5f,0,.5f},{-.5f,1,.5f},{-.5f,1,-.5f},{-.5f,0,-.5f}},
            {{.5f,0,-.5f},{.5f,1,-.5f},{.5f,1,.5f},{.5f,0,.5f}},
            {{-.5f,1,-.5f},{-.5f,1,.5f},{.5f,1,.5f},{.5f,1,-.5f}},
            {{-.5f,0,.5f},{-.5f,0,-.5f},{.5f,0,-.5f},{.5f,0,.5f}}};
        const XMFLOAT2 uv[]={{0,1},{0,0},{1,0},{1,1}};
        for(unsigned f=0;f<(type==Primitive::Plane?1u:6u);++f) {
            const uint32_t start=static_cast<uint32_t>(mesh.vertices.size());
            for(unsigned c=0;c<4;++c) {
                auto p=corners[type==Primitive::Plane?4:f][c];
                if(type==Primitive::Plane){p.y=0;p.x*=2;p.z*=2;}
                vertex(p,type==Primitive::Plane?XMFLOAT3{0,1,0}:normals[f],uv[c]);
            }triangle(start,start+1,start+2);triangle(start,start+2,start+3);
        }
    } else if(type==Primitive::Sphere || type==Primitive::Torus) {
        for(unsigned y=0;y<=rings;++y)for(unsigned x=0;x<=segments;++x){
            const float u=static_cast<float>(x)/segments,v=static_cast<float>(y)/rings;
            const float a=u*XM_2PI,b=v*(type==Primitive::Sphere?XM_PI:XM_2PI);
            XMFLOAT3 n,p;
            if(type==Primitive::Sphere){n={std::sin(b)*std::cos(a),std::cos(b),std::sin(b)*std::sin(a)};p={n.x*.5f,n.y*.5f+.5f,n.z*.5f};}
            else {n={std::cos(b)*std::cos(a),std::sin(b),std::cos(b)*std::sin(a)};const float r=.5f+.17f*std::cos(b);p={r*std::cos(a),.2f+.17f*std::sin(b),r*std::sin(a)};}
            vertex(p,n,{u,v});
        }
        for(unsigned y=0;y<rings;++y)for(unsigned x=0;x<segments;++x){const uint32_t a=y*(segments+1)+x,b=a+segments+1;triangle(a,b,a+1);triangle(a+1,b,b+1);}
    } else {
        const bool cone=type==Primitive::Cone;
        for(unsigned y=0;y<=1;++y)for(unsigned x=0;x<=segments;++x){
            const float u=static_cast<float>(x)/segments,a=u*XM_2PI,r=(cone&&y)?0:.5f;
            XMFLOAT3 n{std::cos(a),cone?.5f:0,std::sin(a)};XMStoreFloat3(&n,XMVector3Normalize(XMLoadFloat3(&n)));
            vertex({r*std::cos(a),static_cast<float>(y),r*std::sin(a)},n,{u,1-static_cast<float>(y)});
        }
        for(unsigned x=0;x<segments;++x){const uint32_t b=x+segments+1;triangle(x,b,x+1);if(!cone)triangle(x+1,b,b+1);}
        for(unsigned cap=0;cap<(cone?1u:2u);++cap){
            const uint32_t center=static_cast<uint32_t>(mesh.vertices.size());const float y=static_cast<float>(cap);
            const XMFLOAT3 normal{0,cap?1.0f:-1.0f,0};vertex({0,y,0},normal,{.5f,.5f});
            for(unsigned x=0;x<=segments;++x){const float a=static_cast<float>(x)/segments*XM_2PI;vertex({.5f*std::cos(a),y,.5f*std::sin(a)},normal,{.5f+.5f*std::cos(a),.5f+.5f*std::sin(a)});}
            for(unsigned x=0;x<segments;++x)triangle(center,center+1+x,center+2+x);
        }
    }
    model.meshes.push_back(std::move(mesh));model.objects.push_back({"object-"+name,name,index});return index;
}
}

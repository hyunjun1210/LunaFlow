#include "Core/ProjectIO.h"
#include "Core/Serialization.h"
#include <cmath>
#include <fstream>
#include <set>
#include <stdexcept>

namespace LunaFlow {
static Json V3(const XMFLOAT3& v){return {v.x,v.y,v.z};}
static Json V4(const XMFLOAT4& v){return {v.x,v.y,v.z,v.w};}
static float Num(const Json& j){const float v=j.get<float>();if(!std::isfinite(v))throw std::runtime_error("Non-finite scene value.");return v;}
static XMFLOAT3 R3(const Json& j){if(j.size()!=3||!j.is_array())throw std::runtime_error("Invalid scene vector.");return {Num(j[0]),Num(j[1]),Num(j[2])};}
static XMFLOAT4 R4(const Json& j){if(j.size()!=4||!j.is_array())throw std::runtime_error("Invalid scene quaternion.");return {Num(j[0]),Num(j[1]),Num(j[2]),Num(j[3])};}
static Json TRS(const Transform& t){return {{"translation",V3(t.translation)},{"rotation",V4(t.rotation)},{"scale",V3(t.scale)}};}
static Transform ReadTRS(const Json& j){Transform t;t.translation=R3(j.at("translation"));t.rotation=R4(j.at("rotation"));t.scale=R3(j.at("scale"));const auto q=XMLoadFloat4(&t.rotation);if(XMVectorGetX(XMVector4LengthSq(q))<1e-12f)throw std::runtime_error("Zero scene quaternion.");XMStoreFloat4(&t.rotation,XMQuaternionNormalize(q));return t;}
void SaveProject(const std::filesystem::path& path,const ProjectData& project) {
    const auto& m=project.model;std::error_code ec;
    auto relative=std::filesystem::relative(m.source,path.parent_path(),ec);
    Json j={{"format","lunaflow.project"},{"schema_version",1},{"model_path",ec?m.source.u8string():relative.u8string()}};
    auto& scene=j["scene"];scene["bones"]=Json::array();scene["materials"]=Json::array();scene["meshes"]=Json::array();j["clips"]=Json::array();
    for(const auto& b:m.skeleton.bones)scene["bones"].push_back({{"name",b.name},{"path",b.path},{"parent",b.parent},{"bindLocal",TRS(b.bindLocal)}});
    for(const auto& material:m.materials){
        auto bytes=material.textureBytes;std::filesystem::path texture=material.texturePath;
        if(bytes.empty()&&!texture.empty()&&std::filesystem::is_regular_file(texture)){
            std::ifstream f(texture,std::ios::binary);bytes.assign(std::istreambuf_iterator<char>(f),{});
        }
        const auto textureRelative=texture.empty()?texture:std::filesystem::relative(texture,path.parent_path(),ec);
        scene["materials"].push_back({{"name",material.name},{"baseColor",V4(material.baseColor)},{"texturePath",ec?texture.u8string():textureRelative.u8string()},
            {"textureBytes",bytes},{"textureRGBA",material.textureRGBA},{"textureWidth",material.textureWidth},{"textureHeight",material.textureHeight}});
    }
    for(const auto& mesh:m.meshes){Json item={{"name",mesh.name},{"material",mesh.material},{"indices",mesh.indices},{"skin",Json::array()},{"vertices",Json::array()}};
        for(const auto& b:mesh.skin){std::vector<float> matrix(reinterpret_cast<const float*>(&b.inverseBind),reinterpret_cast<const float*>(&b.inverseBind)+16);item["skin"].push_back({{"bone",b.bone},{"inverseBind",matrix}});}
        for(const auto& v:mesh.vertices)item["vertices"].push_back({{"p",V3(v.position)},{"n",V3(v.normal)},{"uv",{v.uv.x,v.uv.y}},{"j",v.joints},{"w",v.weights}});
        scene["meshes"].push_back(std::move(item));
    }
    scene["objects"]=Json::array();for(const auto& o:m.objects)scene["objects"].push_back({{"id",o.id},{"name",o.name},{"root",o.root}});
    scene["effects"]=Json::array();for(const auto& e:m.effects)scene["effects"].push_back({{"id",e.id},{"name",e.name},{"clipId",e.clipId},{"type",static_cast<int>(e.type)},
        {"bone",e.bone},{"local",TRS(e.local)},{"color",V4(e.color)},{"size",e.size},{"intensity",e.intensity},{"start",e.start},{"end",e.end}});
    for(const auto& clip:m.clips){auto c=EncodeClip(m.skeleton,clip);c.erase("bones");j["clips"].push_back(std::move(c));}
    j["editor"]={{"clipId",project.selectedClipId},{"bonePath",project.selectedBonePath},{"time",project.time},
        {"cameraTarget",V3(project.cameraTarget)},{"cameraYaw",project.cameraYaw},{"cameraPitch",project.cameraPitch},{"cameraDistance",project.cameraDistance}};
    WriteJsonFile(path,j);
}
ProjectData LoadProject(const std::filesystem::path& path) {
    if(std::filesystem::file_size(path)>256*1024*1024)throw std::runtime_error("Project exceeds the 256 MB JSON limit.");
    std::ifstream file(path,std::ios::binary);if(!file)throw std::runtime_error("Cannot open the project.");Json j;file>>j;
    if(j.at("format")!="lunaflow.project"||j.at("schema_version")!=1)throw std::runtime_error("Unsupported project schema.");
    ProjectData project;auto& m=project.model;auto source=std::filesystem::u8path(j.at("model_path").get<std::string>());m.source=source.is_absolute()?source:path.parent_path()/source;
    const auto& scene=j.at("scene");std::set<std::string> paths;
    for(const auto& b:scene.at("bones")){
        Bone bone{b.at("name").get<std::string>(),b.at("path").get<std::string>(),b.at("parent").get<int>(),ReadTRS(b.at("bindLocal"))};
        if(bone.path.empty()||!paths.insert(bone.path).second)throw std::runtime_error("Duplicate or empty skeleton path.");m.skeleton.bones.push_back(std::move(bone));
    }
    if(m.skeleton.bones.empty()||m.skeleton.bones.size()>10000)throw std::runtime_error("Invalid skeleton size.");EvaluatePose(m.skeleton,nullptr,0);
    for(const auto& item:scene.at("materials")){
        Material material;material.name=item.at("name").get<std::string>();material.baseColor=R4(item.at("baseColor"));
        const auto texture=std::filesystem::u8path(item.value("texturePath",""));material.texturePath=texture.empty()?texture:(texture.is_absolute()?texture:path.parent_path()/texture);
        material.textureBytes=item.at("textureBytes").get<std::vector<uint8_t>>();material.textureRGBA=item.at("textureRGBA").get<std::vector<uint8_t>>();
        material.textureWidth=item.at("textureWidth").get<unsigned>();material.textureHeight=item.at("textureHeight").get<unsigned>();
        if(material.textureWidth>16384||material.textureHeight>16384||(!material.textureRGBA.empty()&&static_cast<uint64_t>(material.textureWidth)*material.textureHeight*4!=material.textureRGBA.size()))throw std::runtime_error("Invalid embedded scene texture.");m.materials.push_back(std::move(material));
    }
    if(m.materials.empty())throw std::runtime_error("Project has no materials.");
    for(const auto& item:scene.at("meshes")){
        MeshData mesh;mesh.name=item.at("name").get<std::string>();mesh.material=item.at("material").get<uint32_t>();if(mesh.material>=m.materials.size())throw std::runtime_error("Unknown material index.");
        for(const auto& skin:item.at("skin")){
            SkinBinding b;b.bone=skin.at("bone").get<uint32_t>();if(b.bone>=m.skeleton.bones.size())throw std::runtime_error("Unknown skin bone.");
            const auto& matrix=skin.at("inverseBind");if(!matrix.is_array()||matrix.size()!=16)throw std::runtime_error("Invalid inverse-bind matrix.");
            for(size_t n=0;n<16;++n)reinterpret_cast<float*>(&b.inverseBind)[n]=Num(matrix[n]);mesh.skin.push_back(b);
        }
        if(mesh.skin.empty()||mesh.skin.size()>MaxSkinBones)throw std::runtime_error("Invalid skin palette size.");
        for(const auto& v:item.at("vertices")){
            Vertex vertex;vertex.position=R3(v.at("p"));vertex.normal=R3(v.at("n"));const auto& uv=v.at("uv");if(uv.size()!=2)throw std::runtime_error("Invalid UV.");vertex.uv={Num(uv[0]),Num(uv[1])};
            vertex.joints=v.at("j").get<std::array<uint32_t,4>>();vertex.weights=v.at("w").get<std::array<float,4>>();float sum=0;
            for(size_t n=0;n<4;++n){if(vertex.joints[n]>=mesh.skin.size()||!std::isfinite(vertex.weights[n])||vertex.weights[n]<0)throw std::runtime_error("Invalid vertex influence.");sum+=vertex.weights[n];}
            if(sum<1e-8f)throw std::runtime_error("Vertex has no skin influence.");for(auto& weight:vertex.weights)weight/=sum;mesh.vertices.push_back(vertex);
        }
        mesh.indices=item.at("indices").get<std::vector<uint32_t>>();if(mesh.vertices.empty()||mesh.indices.empty()||mesh.indices.size()%3)throw std::runtime_error("Invalid triangle mesh.");for(auto index:mesh.indices)if(index>=mesh.vertices.size())throw std::runtime_error("Triangle index outside the mesh.");m.meshes.push_back(std::move(mesh));
    }
    std::set<std::string> clipIds;
    for(const auto& clip:j.at("clips")){auto c=DecodeClip(clip,m.skeleton);if(!clipIds.insert(c.id).second)throw std::runtime_error("Duplicate clip ID.");m.clips.push_back(std::move(c));}
    if(m.clips.empty())throw std::runtime_error("Project contains no motion.");
    if(scene.contains("objects"))for(const auto& item:scene["objects"]){SceneObject o{item.at("id").get<std::string>(),item.at("name").get<std::string>(),item.at("root").get<uint32_t>()};if(o.root>=m.skeleton.bones.size())throw std::runtime_error("Unknown object root.");m.objects.push_back(std::move(o));}
    if(m.objects.empty()&&!m.meshes.empty())m.objects.push_back({"scene-model","Model",0});
    if(scene.contains("effects"))for(const auto& item:scene["effects"]){
        EffectData e;e.id=item.at("id").get<std::string>();e.name=item.at("name").get<std::string>();e.clipId=item.at("clipId").get<std::string>();
        int type=item.at("type").get<int>();if(type<0||type>2)throw std::runtime_error("Unknown effect type.");e.type=static_cast<EffectType>(type);
        e.bone=item.at("bone").get<uint32_t>();e.local=ReadTRS(item.at("local"));e.color=R4(item.at("color"));e.size=Num(item.at("size"));e.intensity=Num(item.at("intensity"));
        e.start=item.at("start").get<double>();e.end=item.at("end").get<double>();if(e.bone>=m.skeleton.bones.size()||e.size<=0||e.intensity<0||!std::isfinite(e.start)||!std::isfinite(e.end)||e.start<0||e.end<e.start)throw std::runtime_error("Invalid effect definition.");m.effects.push_back(std::move(e));
    }
    const auto& editor=j.at("editor");project.selectedClipId=editor.value("clipId",m.clips[0].id);project.selectedBonePath=editor.value("bonePath",m.skeleton.bones[0].path);
    project.time=editor.value("time",0.0);if(!std::isfinite(project.time)||project.time<0)throw std::runtime_error("Invalid project playhead.");
    project.cameraTarget=R3(editor.at("cameraTarget"));project.cameraYaw=Num(editor.at("cameraYaw"));project.cameraPitch=Num(editor.at("cameraPitch"));project.cameraDistance=Num(editor.at("cameraDistance"));
    if(project.cameraDistance<=0||std::abs(project.cameraPitch)>1.55f)throw std::runtime_error("Invalid project camera.");return project;
}
}

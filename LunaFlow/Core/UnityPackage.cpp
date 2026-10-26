#include "Core/UnityPackage.h"
#include "Core/SceneExporter.h"
#include "Core/Serialization.h"
#include "Core/EditorCommands.h"
#include <stdexcept>
#include <windows.h>

namespace LunaFlow {
void ExportUnityPackage(const std::filesystem::path& manifest,const ModelAsset& model,double fps,const std::filesystem::path& scripts,const std::string& defaultClipId) {
    auto name=manifest.stem().u8string();const std::string suffix=".lunaflow";if(name.size()>suffix.size()&&name.substr(name.size()-suffix.size())==suffix)name.resize(name.size()-suffix.size());
    const auto folder=manifest.parent_path();std::filesystem::create_directories(folder);
    const auto fbx=folder/std::filesystem::u8path(name+".fbx");
    Json data={{"format","lunaflow.unity"},{"version",1},{"fbx",fbx.filename().u8string()},{"fps",fps},{"clips",Json::array()},{"objects",Json::array()},{"effects",Json::array()}};
    data["defaultClipId"]=defaultClipId.empty()?(model.clips.empty()?"":model.clips[0].id):defaultClipId;
    for(const auto& clip:model.clips)data["clips"].push_back({{"id",clip.id},{"name",clip.name},{"duration",PlaybackDuration(clip)},{"loop",clip.loop}});
    for(const auto& object:model.objects)data["objects"].push_back({{"id",object.id},{"name",object.name},{"path",model.skeleton.bones.at(object.root).path}});
    for(const auto& e:model.effects){
        data["effects"].push_back({{"id",e.id},{"name",e.name},{"type",static_cast<int>(e.type)},{"clipId",e.clipId},{"bonePath",model.skeleton.bones.at(e.bone).path},
            {"translation",{e.local.translation.x,e.local.translation.y,e.local.translation.z}},{"rotation",{e.local.rotation.x,e.local.rotation.y,e.local.rotation.z,e.local.rotation.w}},
            {"scale",{e.local.scale.x,e.local.scale.y,e.local.scale.z}},{"color",{e.color.x,e.color.y,e.color.z,e.color.w}},
            {"size",e.size},{"intensity",e.intensity},{"start",e.start},{"end",e.end}});
    }
    // Copy the shared Unity integration once per package directory.
    for(const auto& sub:{"Runtime","Editor"}){
        const auto source=scripts/sub;if(!std::filesystem::is_directory(source))throw std::runtime_error("Unity integration scripts are missing next to LunaFlow.exe.");
        std::filesystem::create_directories(folder/sub);
        for(const auto& entry:std::filesystem::directory_iterator(source))if(entry.is_regular_file()&&entry.path().extension()==L".cs")
            std::filesystem::copy_file(entry.path(),folder/sub/entry.path().filename(),std::filesystem::copy_options::overwrite_existing);
    }
    const auto stage=folder/std::filesystem::u8path(".lunaflow-export-"+NewClipId());std::filesystem::create_directory(stage);
    try {
        ExportFbx(stage/fbx.filename(),model,fps);WriteJsonFile(stage/manifest.filename(),data);
        // Publish metadata before the FBX so Unity's asset importer can read it.
        if(!MoveFileExW((stage/manifest.filename()).c_str(),manifest.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw std::runtime_error("Cannot publish Unity metadata.");
        if(!MoveFileExW((stage/fbx.filename()).c_str(),fbx.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw std::runtime_error("Cannot publish Unity FBX.");
        std::filesystem::remove(stage);
    }catch(...){std::error_code ignored;std::filesystem::remove_all(stage,ignored);throw;}
}
}

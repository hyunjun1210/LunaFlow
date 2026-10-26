#include "Core/Scene.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <set>
#include <stdexcept>
#include <unordered_map>
#include <windows.h>

namespace LunaFlow {
using Json = nlohmann::json;
static Json Vec(const XMFLOAT3& v) { return {v.x, v.y, v.z}; }
static Json Vec(const XMFLOAT4& v) { return {v.x, v.y, v.z, v.w}; }
static Json TRS(const Transform& t) { return {{"translation", Vec(t.translation)}, {"rotation", Vec(t.rotation)}, {"scale", Vec(t.scale)}}; }
static Json Skeleton(const SkeletonData& s) {
    Json bones = Json::array();
    for (const auto& b : s.bones) bones.push_back({{"name", b.name}, {"path", b.path}, {"parent", b.parent}, {"bindLocal", TRS(b.bindLocal)}});
    return bones;
}
static Json Header(const char* format) {
    return {{"format", format}, {"version", 1}, {"coordinates", {{"handedness", "left"}, {"up", "+Y"}, {"unit", "meter"},
        {"quaternion", "xyzw"}, {"matrix", "row-vector; local = S*R*T; global = local*parent"}}}};
}
static void Write(const std::filesystem::path& path, const Json& j) {
    // Serialize before opening so malformed values cannot truncate an existing file.
    const std::string contents = j.dump(2, ' ', false, Json::error_handler_t::strict);
    auto temporary=path;
    temporary+=L".tmp-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64());
    try {
        std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
        if (!stream) throw std::runtime_error("Cannot open the output file.");
        stream << contents << '\n'; stream.close();
        if (!stream) throw std::runtime_error("Failed to finish writing the output file.");
        if (!MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
            throw std::runtime_error("Cannot replace the output file. Check its permissions or whether it is open elsewhere.");
    } catch (...) {
        std::error_code ignored; std::filesystem::remove(temporary,ignored); throw;
    }
}
void SaveSkeleton(const std::filesystem::path& p, const SkeletonData& s) {
    Json j = Header("lunaflow.skeleton"); j["bones"] = Skeleton(s); Write(p, j);
}
template<class T> static Json Keys(const std::vector<Key<T>>& keys) {
    Json j = Json::array();
    for (const auto& k : keys) j.push_back({{"time", k.time}, {"value", Vec(k.value)}});
    return j;
}
void SaveClip(const std::filesystem::path& p, const SkeletonData& s, const AnimationClip& clip) {
    PlaybackDuration(clip);
    Json j = Header("lunaflow.clip");
    j["id"] = clip.id; j["name"] = clip.name; j["duration"] = clip.duration; j["speed"] = clip.speed;
    j["bones"] = Skeleton(s); j["tracks"] = Json::array();
    for (const auto& t : clip.tracks) {
        if (t.bone >= s.bones.size()) throw std::runtime_error("Cannot save a track with an unknown bone.");
        j["tracks"].push_back({{"bonePath", s.bones[t.bone].path}, {"positions", Keys(t.positions)},
            {"rotations", Keys(t.rotations)}, {"scales", Keys(t.scales)}});
    }
    Write(p, j);
}
static float Number(const Json& j) {
    float f = j.get<float>(); if (!std::isfinite(f)) throw std::runtime_error("Non-finite transform value."); return f;
}
template<class T> static T ReadVector(const Json& j);
template<> XMFLOAT3 ReadVector<XMFLOAT3>(const Json& j) {
    if (!j.is_array() || j.size() != 3) throw std::runtime_error("Expected a three-component vector.");
    return {Number(j[0]), Number(j[1]), Number(j[2])};
}
template<> XMFLOAT4 ReadVector<XMFLOAT4>(const Json& j) {
    if (!j.is_array() || j.size() != 4) throw std::runtime_error("Expected an xyzw quaternion.");
    XMFLOAT4 q{Number(j[0]), Number(j[1]), Number(j[2]), Number(j[3])};
    const XMVECTOR v = XMLoadFloat4(&q);
    if (XMVectorGetX(XMVector4LengthSq(v)) < 1e-12f) throw std::runtime_error("A zero quaternion is invalid.");
    XMStoreFloat4(&q, XMQuaternionNormalize(v)); return q;
}
template<class T> static std::vector<Key<T>> ReadKeys(const Json& track, const char* field, double duration) {
    std::vector<Key<T>> keys;
    if (!track.contains(field)) return keys;
    if (!track[field].is_array()) throw std::runtime_error("Key channels must be arrays.");
    for (const auto& k : track[field]) {
        const double time = k.at("time").get<double>();
        if (!std::isfinite(time) || time < 0 || time > duration + 1e-6) throw std::runtime_error("Key time is outside the clip duration.");
        keys.push_back({time, ReadVector<T>(k.at("value"))});
    }
    std::sort(keys.begin(), keys.end(), [](const auto& a, const auto& b) { return a.time < b.time; });
    for (size_t i = 1; i < keys.size(); ++i) if (keys[i].time - keys[i - 1].time < 1e-9) throw std::runtime_error("Duplicate key times in a channel.");
    return keys;
}
AnimationClip LoadClip(const std::filesystem::path& p, const SkeletonData& s) {
    std::ifstream f(p, std::ios::binary); if (!f) throw std::runtime_error("Cannot open the clip JSON.");
    if (std::filesystem::file_size(p) > 64 * 1024 * 1024) throw std::runtime_error("Clip JSON exceeds 64 MB.");
    Json j; f >> j;
    if (j.at("format") != "lunaflow.clip" || j.at("version") != 1) throw std::runtime_error("Expected lunaflow.clip version 1.");
    const auto& c = j.at("coordinates");
    if (c.at("handedness") != "left" || c.at("up") != "+Y" || c.at("unit") != "meter" || c.at("quaternion") != "xyzw")
        throw std::runtime_error("Motion coordinates do not match. Team C must convert them before import.");
    AnimationClip clip;
    clip.id = j.at("id").get<std::string>(); clip.name = j.at("name").get<std::string>();
    clip.duration = j.at("duration").get<double>(); clip.speed = j.value("speed", 1.0);
    PlaybackDuration(clip);
    std::unordered_map<std::string, uint32_t> indices;
    for (uint32_t i = 0; i < s.bones.size(); ++i) indices.emplace(s.bones[i].path, i);
    std::set<uint32_t> seen;
    for (const auto& t : j.at("tracks")) {
        const auto path = t.at("bonePath").get<std::string>(); const auto it = indices.find(path);
        if (it == indices.end()) throw std::runtime_error("Unknown bone path: " + path + ". Motion needs retargeting for this skeleton.");
        if (!seen.insert(it->second).second) throw std::runtime_error("Duplicate bone track.");
        BoneTrack track; track.bone = it->second;
        track.positions = ReadKeys<XMFLOAT3>(t, "positions", clip.duration);
        track.rotations = ReadKeys<XMFLOAT4>(t, "rotations", clip.duration);
        track.scales = ReadKeys<XMFLOAT3>(t, "scales", clip.duration);
        clip.tracks.push_back(std::move(track));
    }
    return clip;
}
void SaveBake(const std::filesystem::path& p, const SkeletonData& s, const BakedClip& clip) {
    Json j = Header("lunaflow.bake");
    j["clipId"] = clip.id; j["name"] = clip.name; j["duration"] = clip.duration; j["fps"] = clip.fps;
    j["bones"] = Skeleton(s); j["frames"] = Json::array();
    for (const auto& frame : clip.frames) {
        if (frame.local.size() != s.bones.size()) throw std::runtime_error("Baked pose size does not match the skeleton.");
        Json transforms = Json::array(); for (const auto& t : frame.local) transforms.push_back(TRS(t));
        j["frames"].push_back({{"time", frame.time}, {"local", std::move(transforms)}});
    }
    Write(p, j);
}
}

#include "Core/Scene.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace LunaFlow {
XMMATRIX Matrix(const Transform& t) {
    return XMMatrixScaling(t.scale.x, t.scale.y, t.scale.z)
        * XMMatrixRotationQuaternion(XMQuaternionNormalize(XMLoadFloat4(&t.rotation)))
        * XMMatrixTranslation(t.translation.x, t.translation.y, t.translation.z);
}
Transform Decompose(FXMMATRIX m) {
    XMVECTOR s, q, p;
    if (!XMMatrixDecompose(&s, &q, &p, m)) throw std::runtime_error("A node transform contains unsupported shear or a singular scale.");
    Transform t;
    XMStoreFloat3(&t.translation, p); XMStoreFloat4(&t.rotation, XMQuaternionNormalize(q)); XMStoreFloat3(&t.scale, s);
    return t;
}
XMFLOAT4 QuaternionFromDegrees(const XMFLOAT3& d) {
    XMFLOAT4 q;
    XMStoreFloat4(&q, XMQuaternionRotationRollPitchYaw(XMConvertToRadians(d.x), XMConvertToRadians(d.y), XMConvertToRadians(d.z)));
    return q;
}
XMFLOAT3 DegreesFromQuaternion(const XMFLOAT4& q) {
    XMFLOAT4X4 m;
    XMStoreFloat4x4(&m, XMMatrixRotationQuaternion(XMQuaternionNormalize(XMLoadFloat4(&q))));
    const float pitch = std::asin(std::clamp(-m._32, -1.0f, 1.0f));
    const bool regular = std::abs(std::cos(pitch)) > 1e-5f;
    return {XMConvertToDegrees(pitch), XMConvertToDegrees(regular ? std::atan2(m._31, m._33) : std::atan2(-m._13, m._11)),
        XMConvertToDegrees(regular ? std::atan2(m._12, m._22) : 0.0f)};
}
double PlaybackDuration(const AnimationClip& clip) {
    if (!std::isfinite(clip.duration) || clip.duration < 0 || !std::isfinite(clip.speed) || clip.speed <= 0)
        throw std::runtime_error("Clip duration must be finite and nonnegative; speed must be positive.");
    const double duration=clip.duration / clip.speed;
    if (!std::isfinite(duration)) throw std::runtime_error("Playback duration exceeds the supported range.");
    return duration;
}
template<class T, class Blend> static T Sample(const std::vector<Key<T>>& keys, double t, const T& fallback, Blend blend) {
    if (keys.empty()) return fallback;
    if (t <= keys.front().time) return keys.front().value;
    if (t >= keys.back().time) return keys.back().value;
    auto right = std::upper_bound(keys.begin(), keys.end(), t, [](double time, const Key<T>& k) { return time < k.time; });
    const auto& a = *(right - 1); const auto& b = *right;
    return blend(a.value, b.value, static_cast<float>((t - a.time) / (b.time - a.time)));
}
static XMFLOAT3 Lerp(const XMFLOAT3& a, const XMFLOAT3& b, float f) {
    XMFLOAT3 r; XMStoreFloat3(&r, XMVectorLerp(XMLoadFloat3(&a), XMLoadFloat3(&b), f)); return r;
}
static XMFLOAT4 Slerp(const XMFLOAT4& a, const XMFLOAT4& b, float f) {
    XMFLOAT4 r; XMStoreFloat4(&r, XMQuaternionNormalize(XMQuaternionSlerp(XMLoadFloat4(&a), XMLoadFloat4(&b), f))); return r;
}
Pose EvaluatePose(const SkeletonData& s, const AnimationClip* clip, double time, bool loop) {
    if (!std::isfinite(time)) throw std::runtime_error("Pose time must be finite.");
    Pose result;
    for (const Bone& b : s.bones) result.local.push_back(b.bindLocal);
    if (clip) {
        const double duration = PlaybackDuration(*clip);
        if (loop && duration > 0) time = std::fmod(std::fmod(time, duration) + duration, duration);
        else time = std::clamp(time, 0.0, duration);
        time = std::min(time * clip->speed, clip->duration);
        for (const auto& track : clip->tracks) {
            if (track.bone >= result.local.size()) throw std::runtime_error("Track refers to a missing bone.");
            Transform& t = result.local[track.bone];
            t.translation = Sample(track.positions, time, t.translation, Lerp);
            t.scale = Sample(track.scales, time, t.scale, Lerp);
            t.rotation = Sample(track.rotations, time, t.rotation, Slerp);
        }
    }
    result.global.resize(s.bones.size());
    for (size_t i = 0; i < s.bones.size(); ++i) {
        const int p = s.bones[i].parent;
        if (p < -1 || p >= static_cast<int>(i)) throw std::runtime_error("Skeleton must use parent-before-child order.");
        XMMATRIX m = Matrix(result.local[i]);
        if (p >= 0) m *= XMLoadFloat4x4(&result.global[p]);
        XMStoreFloat4x4(&result.global[i], m);
    }
    return result;
}
template<class T> static void Put(std::vector<Key<T>>& keys, double time, const T& value) {
    auto it = std::lower_bound(keys.begin(), keys.end(), time - 1e-6, [](const Key<T>& k, double t) { return k.time < t; });
    if (it != keys.end() && std::abs(it->time - time) <= 1e-6) *it = {time, value};
    else keys.insert(it, {time, value});
}
void SetTransformKey(AnimationClip& clip, uint32_t bone, double time, const Transform& t) {
    if (!std::isfinite(time) || time < 0) throw std::runtime_error("Key time must be nonnegative and finite.");
    const float components[]={t.translation.x,t.translation.y,t.translation.z,t.scale.x,t.scale.y,t.scale.z,
        t.rotation.x,t.rotation.y,t.rotation.z,t.rotation.w};
    for (float v:components) if (!std::isfinite(v)) throw std::runtime_error("Key transforms must be finite.");
    const XMVECTOR q=XMLoadFloat4(&t.rotation);
    if (XMVectorGetX(XMVector4LengthSq(q))<1e-12f) throw std::runtime_error("A zero quaternion is invalid.");
    XMFLOAT4 rotation; XMStoreFloat4(&rotation,XMQuaternionNormalize(q));
    auto it = std::find_if(clip.tracks.begin(), clip.tracks.end(), [bone](const BoneTrack& tr) { return tr.bone == bone; });
    if (it == clip.tracks.end()) { clip.tracks.push_back({bone}); it = clip.tracks.end() - 1; }
    Put(it->positions, time, t.translation); Put(it->rotations, time, rotation); Put(it->scales, time, t.scale);
    clip.duration = std::max(clip.duration, time);
}
void DeleteTransformKey(AnimationClip& clip, uint32_t bone, double time) {
    for (auto& t : clip.tracks) if (t.bone == bone) {
        auto remove = [time](auto& keys) { keys.erase(std::remove_if(keys.begin(), keys.end(), [time](const auto& k) { return std::abs(k.time - time) < 1e-5; }), keys.end()); };
        remove(t.positions); remove(t.rotations); remove(t.scales);
    }
}
BakedClip BakeClip(const SkeletonData& skeleton, const AnimationClip& clip, double fps) {
    if (!std::isfinite(fps) || fps < 1 || fps > 240) throw std::runtime_error("Bake FPS must be between 1 and 240.");
    BakedClip out{clip.id, clip.name, PlaybackDuration(clip), fps, {}};
    const double count = std::ceil(out.duration * fps);
    if (count > 500000 || (count + 1) * skeleton.bones.size() > 20000000) throw std::runtime_error("Bake is too large. Reduce duration or FPS.");
    const size_t steps = static_cast<size_t>(count);
    out.frames.reserve(steps + 1);
    for (size_t i = 0; i <= steps; ++i) {
        const double t = i == steps ? out.duration : static_cast<double>(i) / fps;
        out.frames.push_back({t, EvaluatePose(skeleton, &clip, t, false).local});
    }
    return out;
}
}

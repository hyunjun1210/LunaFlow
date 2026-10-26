#pragma once
#include <DirectXMath.h>
#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace LunaFlow {
using namespace DirectX;
constexpr size_t MaxSkinBones = 256;
struct Transform {
    XMFLOAT3 translation{0, 0, 0};
    XMFLOAT4 rotation{0, 0, 0, 1}; // quaternion xyzw
    XMFLOAT3 scale{1, 1, 1};
};
struct Bone {
    std::string name, path;
    int parent = -1; // All scene nodes, in parent-before-child order.
    Transform bindLocal;
};
struct SkeletonData { std::vector<Bone> bones; };
template<class T> struct Key { double time = 0; T value{}; };
struct BoneTrack {
    uint32_t bone = 0;
    std::vector<Key<XMFLOAT3>> positions, scales;
    std::vector<Key<XMFLOAT4>> rotations;
};
struct AnimationClip {
    std::string id, name;
    double duration = 0; // Source seconds; playback speed is applied by the evaluator.
    double speed = 1;
    std::vector<BoneTrack> tracks;
};
struct Vertex {
    XMFLOAT3 position{}, normal{};
    XMFLOAT2 uv{};
    std::array<uint32_t, 4> joints{}; // Mesh-local skin palette indices.
    std::array<float, 4> weights{};
};
struct SkinBinding { uint32_t bone = 0; XMFLOAT4X4 inverseBind{}; };
struct Material {
    std::string name;
    XMFLOAT4 baseColor{0.58f, 0.62f, 0.85f, 1};
    std::filesystem::path texturePath;
    std::vector<uint8_t> textureBytes; // Embedded compressed PNG/JPEG.
    std::vector<uint8_t> textureRGBA; // Embedded uncompressed aiTexel -> RGBA.
    unsigned textureWidth = 0, textureHeight = 0;
};
struct MeshData {
    std::string name;
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<SkinBinding> skin;
    uint32_t material = 0;
};
struct ModelAsset {
    std::filesystem::path source;
    SkeletonData skeleton;
    std::vector<Material> materials;
    std::vector<MeshData> meshes;
    std::vector<AnimationClip> clips;
    std::vector<std::string> warnings;
};
struct Pose { std::vector<Transform> local; std::vector<XMFLOAT4X4> global; };
struct BakedFrame { double time = 0; std::vector<Transform> local; };
struct BakedClip { std::string id, name; double duration = 0, fps = 30; std::vector<BakedFrame> frames; };

XMMATRIX Matrix(const Transform& transform);
Transform Decompose(FXMMATRIX matrix);
XMFLOAT4 QuaternionFromDegrees(const XMFLOAT3& xyz);
XMFLOAT3 DegreesFromQuaternion(const XMFLOAT4& rotation);
double PlaybackDuration(const AnimationClip& clip);
Pose EvaluatePose(const SkeletonData&, const AnimationClip*, double time, bool loop = false);
void SetTransformKey(AnimationClip&, uint32_t bone, double sourceTime, const Transform&);
void DeleteTransformKey(AnimationClip&, uint32_t bone, double sourceTime);
BakedClip BakeClip(const SkeletonData&, const AnimationClip&, double fps = 30);
ModelAsset ImportScene(const std::filesystem::path&, float unitsToMeters = 1.0f);
ModelAsset MakeDemoScene();
ModelAsset MakeCubeScene();
void SaveClip(const std::filesystem::path&, const SkeletonData&, const AnimationClip&);
AnimationClip LoadClip(const std::filesystem::path&, const SkeletonData&);
void SaveBake(const std::filesystem::path&, const SkeletonData&, const BakedClip&);
void SaveSkeleton(const std::filesystem::path&, const SkeletonData&);
}

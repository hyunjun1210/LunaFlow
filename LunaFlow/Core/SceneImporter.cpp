#include "Core/Scene.h"
#include <assimp/Importer.hpp>
#include <assimp/config.h>
#include <assimp/material.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <algorithm>
#include <cmath>
#include <functional>
#include <stdexcept>
#include <unordered_map>

namespace LunaFlow {
static XMMATRIX Convert(const aiMatrix4x4& m) {
    // Assimp uses column vectors. Transpose exactly once for DirectX row vectors.
    return XMMatrixSet(m.a1,m.b1,m.c1,m.d1, m.a2,m.b2,m.c2,m.d2,
        m.a3,m.b3,m.c3,m.d3, m.a4,m.b4,m.c4,m.d4);
}
static XMFLOAT3 Convert(const aiVector3D& v) { return {v.x, v.y, v.z}; }
static XMFLOAT4 Convert(const aiQuaternion& q) {
    XMFLOAT4 f{q.x,q.y,q.z,q.w};
    XMStoreFloat4(&f, XMQuaternionNormalize(XMLoadFloat4(&f))); return f;
}
static std::string PathComponent(const std::string& text) {
    std::string out;
    for (char c : text) {
        if (c == '%') out += "%25"; else if (c == '/') out += "%2F";
        else if (c == '~') out += "%7E"; else out += c;
    }
    return out.empty() ? "unnamed" : out;
}
template<class T> static void SortKeys(std::vector<Key<T>>& keys) {
    std::stable_sort(keys.begin(), keys.end(), [](const auto& a, const auto& b) { return a.time < b.time; });
    std::vector<Key<T>> unique;
    for (auto k : keys) {
        if (!std::isfinite(k.time)) throw std::runtime_error("The source contains an invalid key time.");
        k.time = std::max(0.0, k.time);
        if (!unique.empty() && std::abs(k.time - unique.back().time) < 1e-9) unique.back() = k;
        else unique.push_back(k);
    }
    keys = std::move(unique);
}
ModelAsset ImportScene(const std::filesystem::path& path, float unitsToMeters) {
    if (!std::isfinite(unitsToMeters) || unitsToMeters <= 0) throw std::runtime_error("Import unit scale must be positive.");
    Assimp::Importer importer;
    importer.SetPropertyInteger(AI_CONFIG_PP_LBW_MAX_WEIGHTS, 4);
    importer.SetPropertyFloat(AI_CONFIG_GLOBAL_SCALE_FACTOR_KEY, unitsToMeters);
    importer.SetPropertyBool(AI_CONFIG_IMPORT_FBX_PRESERVE_PIVOTS, false);
    // Assimp 6.0.4's default FBX root correction also multiplies UnitScaleFactor.
    // Use the axis basis without its unit factor; GlobalScale handles units once.
    importer.SetPropertyBool(AI_CONFIG_IMPORT_FBX_IGNORE_UP_DIRECTION, true);
    const aiScene* scene = importer.ReadFile(path.u8string(), 0);
    if (!scene || !scene->mRootNode) throw std::runtime_error(std::string("Assimp: ") + importer.GetErrorString());
    int32_t up=1,upSign=1,front=2,frontSign=1,right=0,rightSign=1;
    if (scene->mMetaData && scene->mMetaData->Get("UpAxis",up) && scene->mMetaData->Get("FrontAxis",front)
        && scene->mMetaData->Get("CoordAxis",right)) {
        scene->mMetaData->Get("UpAxisSign",upSign); scene->mMetaData->Get("FrontAxisSign",frontSign); scene->mMetaData->Get("CoordAxisSign",rightSign);
        if (up<0 || up>2 || front<0 || front>2 || right<0 || right>2 || up==front || up==right || front==right)
            throw std::runtime_error("Invalid FBX axis metadata.");
        aiVector3D u,f,r; u[up]=static_cast<float>(upSign); f[front]=static_cast<float>(frontSign); r[right]=static_cast<float>(rightSign);
        const aiMatrix4x4 basis(r.x,r.y,r.z,0,u.x,u.y,u.z,0,f.x,f.y,f.z,0,0,0,0,1);
        scene->mRootNode->mTransformation *= basis;
    }
    scene = importer.ApplyPostProcessing(aiProcess_Triangulate | aiProcess_JoinIdenticalVertices
        | aiProcess_GenSmoothNormals | aiProcess_ConvertToLeftHanded | aiProcess_LimitBoneWeights
        | aiProcess_ValidateDataStructure | aiProcess_GlobalScale | aiProcess_PopulateArmatureData);
    if (!scene || !scene->mRootNode) throw std::runtime_error(std::string("Assimp: ") + importer.GetErrorString());
    ModelAsset out; out.source = path;
    std::unordered_map<const aiNode*, uint32_t> nodeIds;
    std::unordered_map<std::string, std::vector<uint32_t>> byName;
    std::function<void(const aiNode*, int, const std::string&)> visit;
    visit = [&](const aiNode* n, int parent, const std::string& component) {
        const uint32_t index = static_cast<uint32_t>(out.skeleton.bones.size());
        const std::string full = parent < 0 ? "/" + component : out.skeleton.bones[parent].path + "/" + component;
        out.skeleton.bones.push_back({n->mName.C_Str(), full, parent, Decompose(Convert(n->mTransformation))});
        nodeIds[n] = index; byName[n->mName.C_Str()].push_back(index);
        std::unordered_map<std::string, unsigned> counts, ordinals;
        for (unsigned i = 0; i < n->mNumChildren; ++i) ++counts[n->mChildren[i]->mName.C_Str()];
        for (unsigned i = 0; i < n->mNumChildren; ++i) {
            const std::string name = n->mChildren[i]->mName.C_Str();
            const std::string suffix = counts[name] > 1 ? "~" + std::to_string(ordinals[name]++) : "";
            visit(n->mChildren[i], static_cast<int>(index), PathComponent(name) + suffix);
        }
    };
    visit(scene->mRootNode, -1, PathComponent(scene->mRootNode->mName.C_Str()));
    auto resolve = [&](const std::string& name) {
        auto it = byName.find(name);
        if (it == byName.end() || it->second.size() != 1) throw std::runtime_error("Missing or ambiguous animated bone name: " + name);
        return it->second.front();
    };
    for (unsigned i = 0; i < scene->mNumMaterials; ++i) {
        const auto* material = scene->mMaterials[i]; Material m;
        aiString name; material->Get(AI_MATKEY_NAME, name); m.name = name.C_Str();
        aiColor4D color;
        if (material->Get("$clr.base",0,0,color) == AI_SUCCESS || material->Get(AI_MATKEY_COLOR_DIFFUSE, color) == AI_SUCCESS)
            m.baseColor = {color.r, color.g, color.b, color.a};
        aiString texture;
        if (material->GetTexture(aiTextureType_BASE_COLOR,0,&texture) == AI_SUCCESS || material->GetTexture(aiTextureType_DIFFUSE,0,&texture) == AI_SUCCESS) {
            if (const auto* embedded = scene->GetEmbeddedTexture(texture.C_Str())) {
                if (embedded->mHeight == 0) {
                    const auto* begin = reinterpret_cast<const uint8_t*>(embedded->pcData);
                    m.textureBytes.assign(begin, begin + embedded->mWidth);
                } else {
                    m.textureWidth = embedded->mWidth; m.textureHeight = embedded->mHeight;
                    m.textureRGBA.reserve(static_cast<size_t>(m.textureWidth) * m.textureHeight * 4);
                    for (size_t k = 0; k < static_cast<size_t>(m.textureWidth) * m.textureHeight; ++k) {
                        const aiTexel& px = embedded->pcData[k];
                        m.textureRGBA.insert(m.textureRGBA.end(), {px.r,px.g,px.b,px.a});
                    }
                }
            } else {
                auto candidate = std::filesystem::u8path(texture.C_Str());
                m.texturePath = candidate.is_absolute() ? candidate : path.parent_path() / candidate;
                if (!std::filesystem::exists(m.texturePath)) {
                    const auto nearby = path.parent_path() / candidate.filename();
                    if (std::filesystem::exists(nearby)) m.texturePath = nearby;
                    else out.warnings.push_back("Texture missing; using base color: " + m.texturePath.u8string());
                }
            }
        }
        out.materials.push_back(std::move(m));
    }
    if (out.materials.empty()) out.materials.emplace_back();
    std::function<void(const aiNode*)> readMeshes;
    readMeshes = [&](const aiNode* n) {
        for (unsigned instance = 0; instance < n->mNumMeshes; ++instance) {
            const auto* source = scene->mMeshes[n->mMeshes[instance]];
            if ((source->mPrimitiveTypes & aiPrimitiveType_TRIANGLE) == 0) continue;
            if (source->mNumBones >= MaxSkinBones) throw std::runtime_error("A mesh exceeds the 255-bone skin limit. Split its skin palette before import.");
            MeshData mesh; mesh.name = source->mName.C_Str();
            mesh.material = source->mMaterialIndex < out.materials.size() ? source->mMaterialIndex : 0;
            mesh.vertices.resize(source->mNumVertices);
            for (unsigned v = 0; v < source->mNumVertices; ++v) {
                auto& dst = mesh.vertices[v]; dst.position = Convert(source->mVertices[v]);
                dst.normal = source->HasNormals() ? Convert(source->mNormals[v]) : XMFLOAT3{0,1,0};
                if (source->HasTextureCoords(0)) dst.uv = {source->mTextureCoords[0][v].x,source->mTextureCoords[0][v].y};
            }
            for (unsigned b = 0; b < source->mNumBones; ++b) {
                const auto* bone = source->mBones[b]; SkinBinding binding;
                auto found = nodeIds.find(bone->mNode);
                binding.bone = found != nodeIds.end() ? found->second : resolve(bone->mName.C_Str());
                XMStoreFloat4x4(&binding.inverseBind, Convert(bone->mOffsetMatrix));
                mesh.skin.push_back(binding);
                for (unsigned w = 0; w < bone->mNumWeights; ++w) {
                    const auto weight = bone->mWeights[w];
                    if (weight.mVertexId >= mesh.vertices.size() || !std::isfinite(weight.mWeight) || weight.mWeight < 0)
                        throw std::runtime_error("Invalid vertex skin weight.");
                    if (weight.mWeight == 0) continue;
                    auto& vertex = mesh.vertices[weight.mVertexId];
                    auto smallest = std::min_element(vertex.weights.begin(), vertex.weights.end());
                    if (weight.mWeight > *smallest) {
                        const auto slot = static_cast<size_t>(smallest - vertex.weights.begin());
                        vertex.weights[slot] = weight.mWeight; vertex.joints[slot] = b;
                    }
                }
            }
            // A mesh node can carry rigid vertices without any skin weights.
            const uint32_t rigidIndex = static_cast<uint32_t>(mesh.skin.size());
            SkinBinding rigid; rigid.bone = nodeIds.at(n); XMStoreFloat4x4(&rigid.inverseBind, XMMatrixIdentity());
            mesh.skin.push_back(rigid);
            size_t unweighted = 0;
            for (auto& vertex : mesh.vertices) {
                float sum = 0; for (float weight : vertex.weights) sum += weight;
                if (sum <= 1e-8f) { vertex.joints[0] = rigidIndex; vertex.weights[0] = 1; ++unweighted; }
                else for (auto& weight : vertex.weights) weight /= sum;
            }
            if (unweighted && source->mNumBones) out.warnings.push_back(std::to_string(unweighted) + " unweighted vertices attached to mesh node: " + mesh.name);
            for (unsigned f = 0; f < source->mNumFaces; ++f) {
                const auto& face = source->mFaces[f]; if (face.mNumIndices != 3) continue;
                mesh.indices.insert(mesh.indices.end(),face.mIndices,face.mIndices + 3);
            }
            if (!mesh.indices.empty()) out.meshes.push_back(std::move(mesh));
        }
        for (unsigned i = 0; i < n->mNumChildren; ++i) readMeshes(n->mChildren[i]);
    };
    readMeshes(scene->mRootNode);
    if (out.meshes.empty()) throw std::runtime_error("No triangle meshes in the model.");
    for (unsigned i = 0; i < scene->mNumAnimations; ++i) {
        const auto* src = scene->mAnimations[i];
        const double ticks = src->mTicksPerSecond > 0 ? src->mTicksPerSecond : 25.0;
        AnimationClip clip;
        clip.id = "imported-" + std::to_string(i); clip.name = src->mName.length ? src->mName.C_Str() : "Animation " + std::to_string(i+1);
        clip.duration = std::max(0.0,src->mDuration / ticks);
        for (unsigned c = 0; c < src->mNumChannels; ++c) {
            const auto* channel = src->mChannels[c]; BoneTrack track; track.bone = resolve(channel->mNodeName.C_Str());
            for (unsigned k = 0; k < channel->mNumPositionKeys; ++k) track.positions.push_back({channel->mPositionKeys[k].mTime / ticks,Convert(channel->mPositionKeys[k].mValue)});
            for (unsigned k = 0; k < channel->mNumScalingKeys; ++k) track.scales.push_back({channel->mScalingKeys[k].mTime / ticks,Convert(channel->mScalingKeys[k].mValue)});
            for (unsigned k = 0; k < channel->mNumRotationKeys; ++k) track.rotations.push_back({channel->mRotationKeys[k].mTime / ticks,Convert(channel->mRotationKeys[k].mValue)});
            SortKeys(track.positions); SortKeys(track.scales); SortKeys(track.rotations);
            if (!track.positions.empty()) clip.duration = std::max(clip.duration,track.positions.back().time);
            if (!track.scales.empty()) clip.duration = std::max(clip.duration,track.scales.back().time);
            if (!track.rotations.empty()) clip.duration = std::max(clip.duration,track.rotations.back().time);
            clip.tracks.push_back(std::move(track));
        }
        out.clips.push_back(std::move(clip));
    }
    if (out.clips.empty()) out.clips.push_back({"bind-pose","Bind pose",2,1,{}});
    out.objects.push_back({"imported-character",path.stem().u8string(),0});return out;
}
}

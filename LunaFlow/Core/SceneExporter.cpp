#include "Core/SceneExporter.h"
#include <assimp/Exporter.hpp>
#include <assimp/material.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <algorithm>
#include <cstring>
#include <fstream>
#include <memory>
#include <set>
#include <stdexcept>
#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>

namespace LunaFlow {
static std::vector<uint8_t> EncodePng(const Material& material) {
    using Microsoft::WRL::ComPtr;
    struct ComScope{HRESULT result=CoInitializeEx(nullptr,COINIT_MULTITHREADED);~ComScope(){if(SUCCEEDED(result))CoUninitialize();}} com;
    auto check=[](HRESULT hr){if(FAILED(hr))throw std::runtime_error("Cannot encode an embedded texture for FBX.");};
    ComPtr<IWICImagingFactory> factory;check(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)));
    ComPtr<IStream> stream;check(CreateStreamOnHGlobal(nullptr,TRUE,&stream));ComPtr<IWICBitmapEncoder> encoder;
    check(factory->CreateEncoder(GUID_ContainerFormatPng,nullptr,&encoder));check(encoder->Initialize(stream.Get(),WICBitmapEncoderNoCache));
    ComPtr<IWICBitmapFrameEncode> frame;check(encoder->CreateNewFrame(&frame,nullptr));check(frame->Initialize(nullptr));check(frame->SetSize(material.textureWidth,material.textureHeight));
    WICPixelFormatGUID format=GUID_WICPixelFormat32bppRGBA;check(frame->SetPixelFormat(&format));ComPtr<IWICBitmap> bitmap;
    check(factory->CreateBitmapFromMemory(material.textureWidth,material.textureHeight,GUID_WICPixelFormat32bppRGBA,material.textureWidth*4,static_cast<UINT>(material.textureRGBA.size()),const_cast<BYTE*>(material.textureRGBA.data()),&bitmap));
    ComPtr<IWICFormatConverter> converter;check(factory->CreateFormatConverter(&converter));check(converter->Initialize(bitmap.Get(),format,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom));
    check(frame->WriteSource(converter.Get(),nullptr));check(frame->Commit());check(encoder->Commit());
    STATSTG stat{};check(stream->Stat(&stat,STATFLAG_NONAME));HGLOBAL memory=nullptr;check(GetHGlobalFromStream(stream.Get(),&memory));
    const auto* data=static_cast<const uint8_t*>(GlobalLock(memory));if(!data)throw std::runtime_error("Cannot read encoded texture.");
    std::vector<uint8_t> bytes(data,data+static_cast<size_t>(stat.cbSize.QuadPart));GlobalUnlock(memory);return bytes;
}
static aiMatrix4x4 Convert(const XMFLOAT4X4& m) {
    return aiMatrix4x4(m._11,m._21,m._31,m._41,m._12,m._22,m._32,m._42,m._13,m._23,m._33,m._43,m._14,m._24,m._34,m._44);
}
static aiMatrix4x4 Convert(const Transform& t) {XMFLOAT4X4 m;XMStoreFloat4x4(&m,Matrix(t));return Convert(m);}
static aiVector3D Convert(XMFLOAT3 v){return {v.x,v.y,v.z};}
static void AddChild(aiNode* parent,aiNode* node) {
    auto** children=new aiNode*[parent->mNumChildren+1];for(unsigned i=0;i<parent->mNumChildren;++i)children[i]=parent->mChildren[i];
    children[parent->mNumChildren++]=node;delete[] parent->mChildren;parent->mChildren=children;node->mParent=parent;
}
void ExportFbx(const std::filesystem::path& path,const ModelAsset& model,double fps) {
    if(model.meshes.empty())throw std::runtime_error("Add a model or shape before exporting FBX.");
    if(fps<1||fps>240)throw std::runtime_error("Invalid FBX sampling rate.");
    auto scene=std::make_unique<aiScene>();scene->mRootNode=new aiNode("__LunaFlowExport__");
    scene->mMetaData=aiMetadata::Alloc(10);
    scene->mMetaData->Set(0,"UpAxis",int32_t{1});scene->mMetaData->Set(1,"UpAxisSign",int32_t{1});
    scene->mMetaData->Set(2,"FrontAxis",int32_t{2});scene->mMetaData->Set(3,"FrontAxisSign",int32_t{1});
    scene->mMetaData->Set(4,"CoordAxis",int32_t{0});scene->mMetaData->Set(5,"CoordAxisSign",int32_t{1});
    scene->mMetaData->Set(6,"UnitScaleFactor",100.0);scene->mMetaData->Set(7,"OriginalUnitScaleFactor",100.0);
    scene->mMetaData->Set(8,"CustomFrameRate",fps);scene->mMetaData->Set(9,"TimeMode",int32_t{14});
    std::vector<aiNode*> nodes;std::set<std::string> names{"__LunaFlowExport__"};
    for(size_t i=0;i<model.skeleton.bones.size();++i){
        const auto& bone=model.skeleton.bones[i];if(!names.insert(bone.name).second)throw std::runtime_error("FBX export requires unique node names: "+bone.name);
        if(bone.parent>=static_cast<int>(i)||bone.parent< -1)throw std::runtime_error("Invalid export hierarchy.");
        auto* node=new aiNode(bone.name);node->mTransformation=Convert(bone.bindLocal);
        AddChild(bone.parent<0?scene->mRootNode:nodes.at(bone.parent),node);nodes.push_back(node);
    }
    scene->mNumMaterials=static_cast<unsigned>(model.materials.size());scene->mMaterials=new aiMaterial*[scene->mNumMaterials]{};
    for(unsigned i=0;i<scene->mNumMaterials;++i){
        const auto& source=model.materials[i];auto* material=new aiMaterial;scene->mMaterials[i]=material;
        aiString name(source.name);material->AddProperty(&name,AI_MATKEY_NAME);
        aiColor4D color(source.baseColor.x,source.baseColor.y,source.baseColor.z,source.baseColor.w);material->AddProperty(&color,1,AI_MATKEY_COLOR_DIFFUSE);
        auto bytes=source.textureBytes;
        if(bytes.empty()&&!source.textureRGBA.empty())bytes=EncodePng(source);
        if(bytes.empty()&&!source.texturePath.empty()&&std::filesystem::is_regular_file(source.texturePath)){std::ifstream file(source.texturePath,std::ios::binary);bytes.assign(std::istreambuf_iterator<char>(file),{});}
        if(!bytes.empty()){
            auto* texture=new aiTexture;texture->mWidth=static_cast<unsigned>(bytes.size());texture->mHeight=0;
            texture->pcData=new aiTexel[(bytes.size()+sizeof(aiTexel)-1)/sizeof(aiTexel)]{};std::memcpy(texture->pcData,bytes.data(),bytes.size());
            const bool png=bytes.size()>4&&bytes[0]==137&&bytes[1]==80;strcpy_s(texture->achFormatHint,png?"png":"jpg");
            const std::string filename="Texture_"+std::to_string(i)+(png?".png":".jpg");texture->mFilename=aiString(filename);
            auto** textures=new aiTexture*[scene->mNumTextures+1];for(unsigned n=0;n<scene->mNumTextures;++n)textures[n]=scene->mTextures[n];textures[scene->mNumTextures]=texture;
            aiString reference("*"+std::to_string(scene->mNumTextures));delete[] scene->mTextures;scene->mTextures=textures;++scene->mNumTextures;material->AddProperty(&reference,AI_MATKEY_TEXTURE_DIFFUSE(0));
        }
    }
    scene->mNumMeshes=static_cast<unsigned>(model.meshes.size());scene->mMeshes=new aiMesh*[scene->mNumMeshes]{};
    scene->mRootNode->mNumMeshes=scene->mNumMeshes;scene->mRootNode->mMeshes=new unsigned[scene->mNumMeshes];
    for(unsigned i=0;i<scene->mNumMeshes;++i){
        const auto& source=model.meshes[i];auto* mesh=new aiMesh;scene->mMeshes[i]=mesh;scene->mRootNode->mMeshes[i]=i;
        mesh->mName=aiString("__Mesh_"+std::to_string(i)+"_"+source.name);mesh->mMaterialIndex=source.material;mesh->mPrimitiveTypes=aiPrimitiveType_TRIANGLE;
        mesh->mNumVertices=static_cast<unsigned>(source.vertices.size());mesh->mVertices=new aiVector3D[mesh->mNumVertices];mesh->mNormals=new aiVector3D[mesh->mNumVertices];mesh->mTextureCoords[0]=new aiVector3D[mesh->mNumVertices];mesh->mNumUVComponents[0]=2;
        for(unsigned v=0;v<mesh->mNumVertices;++v){mesh->mVertices[v]=Convert(source.vertices[v].position);mesh->mNormals[v]=Convert(source.vertices[v].normal);mesh->mTextureCoords[0][v]={source.vertices[v].uv.x,source.vertices[v].uv.y,0};}
        mesh->mNumFaces=static_cast<unsigned>(source.indices.size()/3);mesh->mFaces=new aiFace[mesh->mNumFaces];
        for(unsigned f=0;f<mesh->mNumFaces;++f){mesh->mFaces[f].mNumIndices=3;mesh->mFaces[f].mIndices=new unsigned[3]{source.indices[f*3],source.indices[f*3+1],source.indices[f*3+2]};}
        std::vector<std::vector<aiVertexWeight>> weights(source.skin.size());
        for(unsigned v=0;v<mesh->mNumVertices;++v)for(size_t k=0;k<4;++k)if(source.vertices[v].weights[k]>0)weights.at(source.vertices[v].joints[k]).emplace_back(v,source.vertices[v].weights[k]);
        mesh->mNumBones=static_cast<unsigned>(std::count_if(weights.begin(),weights.end(),[](const auto& w){return !w.empty();}));mesh->mBones=new aiBone*[mesh->mNumBones]{};unsigned nextBone=0;
        for(size_t b=0;b<weights.size();++b)if(!weights[b].empty()){
            auto* bone=new aiBone;mesh->mBones[nextBone++]=bone;bone->mName=aiString(model.skeleton.bones.at(source.skin[b].bone).name);bone->mOffsetMatrix=Convert(source.skin[b].inverseBind);
            bone->mNumWeights=static_cast<unsigned>(weights[b].size());bone->mWeights=new aiVertexWeight[bone->mNumWeights];std::copy(weights[b].begin(),weights[b].end(),bone->mWeights);
        }
    }
    scene->mNumAnimations=static_cast<unsigned>(model.clips.size());scene->mAnimations=new aiAnimation*[scene->mNumAnimations]{};
    for(unsigned i=0;i<scene->mNumAnimations;++i){
        const auto baked=BakeClip(model.skeleton,model.clips[i],fps);auto* animation=new aiAnimation;scene->mAnimations[i]=animation;
        animation->mName=aiString(model.clips[i].name);animation->mTicksPerSecond=fps;animation->mDuration=baked.duration*fps;
        animation->mNumChannels=static_cast<unsigned>(nodes.size());animation->mChannels=new aiNodeAnim*[animation->mNumChannels]{};
        for(unsigned b=0;b<animation->mNumChannels;++b){
            auto* channel=new aiNodeAnim;animation->mChannels[b]=channel;channel->mNodeName=nodes[b]->mName;
            channel->mNumPositionKeys=channel->mNumRotationKeys=channel->mNumScalingKeys=static_cast<unsigned>(baked.frames.size());
            channel->mPositionKeys=new aiVectorKey[channel->mNumPositionKeys];channel->mRotationKeys=new aiQuatKey[channel->mNumRotationKeys];channel->mScalingKeys=new aiVectorKey[channel->mNumScalingKeys];
            for(unsigned k=0;k<channel->mNumPositionKeys;++k){const auto& frame=baked.frames[k];const auto& t=frame.local[b];
                channel->mPositionKeys[k]={frame.time*fps,Convert(t.translation)};channel->mScalingKeys[k]={frame.time*fps,Convert(t.scale)};
                channel->mRotationKeys[k]={frame.time*fps,aiQuaternion(t.rotation.w,t.rotation.x,t.rotation.y,t.rotation.z)};
            }
        }
    }
    auto temporary=path.parent_path()/std::filesystem::u8path("."+path.filename().u8string()+".tmp");
    Assimp::Exporter exporter;
    const auto result=exporter.Export(scene.get(),"fbx",temporary.u8string(),aiProcess_ConvertToLeftHanded);
    if(result!=AI_SUCCESS){std::error_code ignored;std::filesystem::remove(temporary,ignored);throw std::runtime_error(std::string("FBX export failed: ")+exporter.GetErrorString());}
    if(!MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw std::runtime_error("Cannot replace the FBX output file.");
}
}

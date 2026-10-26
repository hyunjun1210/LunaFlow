#include "Rendering/RendererDX11.h"
#include <d3dcompiler.h>
#include <wincodec.h>
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace LunaFlow {
static void Check(HRESULT hr,const char* message) {
    if (FAILED(hr)) throw std::runtime_error(std::string(message) + " (HRESULT " + std::to_string(static_cast<unsigned>(hr)) + ")");
}
void PreviewCamera::Orbit(float dx,float dy) { yaw-=dx*.008f; pitch=std::clamp(pitch+dy*.008f,-1.5f,1.5f); }
void PreviewCamera::Zoom(float wheel) { distance=std::clamp(distance*std::exp(-wheel*.15f),sceneRadius*.05f,sceneRadius*150); }
XMMATRIX PreviewCamera::View() const {
    const XMVECTOR targetV=XMLoadFloat3(&target);
    const XMVECTOR offset=XMVectorSet(std::sin(yaw)*std::cos(pitch),std::sin(pitch),-std::cos(yaw)*std::cos(pitch),0)*distance;
    return XMMatrixLookAtLH(targetV+offset,targetV,XMVectorSet(0,1,0,0));
}
XMMATRIX PreviewCamera::Projection(float aspect) const {
    return XMMatrixPerspectiveFovLH(XMConvertToRadians(42),std::max(aspect,.01f),std::max(sceneRadius*.001f,.00001f),std::max(distance+sceneRadius*100,1.0f));
}
void PreviewCamera::Pan(float dx,float dy,float height) {
    const XMMATRIX inv=XMMatrixInverse(nullptr,View());
    const float perPixel=2*distance*std::tan(XMConvertToRadians(21))/std::max(height,1.0f);
    XMStoreFloat3(&target,XMLoadFloat3(&target)+(-inv.r[0]*dx+inv.r[1]*dy)*perPixel);
}
static ComPtr<ID3D11ShaderResourceView> MakeTexture(ID3D11Device* device,const uint8_t* pixels,UINT w,UINT h) {
    D3D11_TEXTURE2D_DESC desc{}; desc.Width=w; desc.Height=h; desc.MipLevels=1; desc.ArraySize=1;
    desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM_SRGB; desc.SampleDesc.Count=1; desc.Usage=D3D11_USAGE_IMMUTABLE; desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA initial{}; initial.pSysMem=pixels; initial.SysMemPitch=w*4;
    ComPtr<ID3D11Texture2D> texture; Check(device->CreateTexture2D(&desc,&initial,&texture),"Create texture");
    ComPtr<ID3D11ShaderResourceView> srv; Check(device->CreateShaderResourceView(texture.Get(),nullptr,&srv),"Create texture view"); return srv;
}
static ComPtr<ID3D11ShaderResourceView> LoadTexture(ID3D11Device* device,const Material& material) {
    if (!material.textureRGBA.empty()) return MakeTexture(device,material.textureRGBA.data(),material.textureWidth,material.textureHeight);
    if (material.textureBytes.empty() && material.texturePath.empty()) return {};
    ComPtr<IWICImagingFactory> factory;
    Check(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)),"Initialize WIC");
    ComPtr<IWICBitmapDecoder> decoder; ComPtr<IWICStream> stream;
    if (!material.textureBytes.empty()) {
        Check(factory->CreateStream(&stream),"Create image stream");
        Check(stream->InitializeFromMemory(const_cast<BYTE*>(material.textureBytes.data()),static_cast<DWORD>(material.textureBytes.size())),"Read embedded texture");
        Check(factory->CreateDecoderFromStream(stream.Get(),nullptr,WICDecodeMetadataCacheOnLoad,&decoder),"Decode embedded texture");
    } else Check(factory->CreateDecoderFromFilename(material.texturePath.c_str(),nullptr,GENERIC_READ,WICDecodeMetadataCacheOnLoad,&decoder),"Decode texture file");
    ComPtr<IWICBitmapFrameDecode> frame; Check(decoder->GetFrame(0,&frame),"Read image frame");
    UINT w=0,h=0; Check(frame->GetSize(&w,&h),"Read image dimensions");
    if (!w || !h || w>16384 || h>16384 || static_cast<uint64_t>(w)*h>67108864) throw std::runtime_error("Texture dimensions exceed the supported limit.");
    ComPtr<IWICFormatConverter> converter; Check(factory->CreateFormatConverter(&converter),"Create image converter");
    Check(converter->Initialize(frame.Get(),GUID_WICPixelFormat32bppRGBA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom),"Convert image to RGBA");
    std::vector<uint8_t> pixels(static_cast<size_t>(w)*h*4);
    Check(converter->CopyPixels(nullptr,w*4,static_cast<UINT>(pixels.size()),pixels.data()),"Read texture pixels");
    return MakeTexture(device,pixels.data(),w,h);
}
void RendererDX11::Initialize(ID3D11Device* d,ID3D11DeviceContext* c,const std::filesystem::path& shaders) {
    device_=d; context_=c;
    ComPtr<ID3DBlob> vertex,pixel,error;
    const auto file=shaders/L"Skinned.hlsl";
    auto compile=[&](const char* entry,const char* profile,ComPtr<ID3DBlob>& code) {
        const HRESULT hr=D3DCompileFromFile(file.c_str(),nullptr,D3D_COMPILE_STANDARD_FILE_INCLUDE,entry,profile,D3DCOMPILE_ENABLE_STRICTNESS,0,&code,&error);
        if (FAILED(hr)) throw std::runtime_error(error ? std::string(static_cast<const char*>(error->GetBufferPointer()),error->GetBufferSize()) : "Cannot load Skinned.hlsl next to the executable.");
    };
    compile("VSMain","vs_5_0",vertex); compile("PSMain","ps_5_0",pixel);
    Check(d->CreateVertexShader(vertex->GetBufferPointer(),vertex->GetBufferSize(),nullptr,&vs_),"Create vertex shader");
    Check(d->CreatePixelShader(pixel->GetBufferPointer(),pixel->GetBufferSize(),nullptr,&ps_),"Create pixel shader");
    const D3D11_INPUT_ELEMENT_DESC elements[]={
        {"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,static_cast<UINT>(offsetof(Vertex,position)),D3D11_INPUT_PER_VERTEX_DATA,0},
        {"NORMAL",0,DXGI_FORMAT_R32G32B32_FLOAT,0,static_cast<UINT>(offsetof(Vertex,normal)),D3D11_INPUT_PER_VERTEX_DATA,0},
        {"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,static_cast<UINT>(offsetof(Vertex,uv)),D3D11_INPUT_PER_VERTEX_DATA,0},
        {"BLENDINDICES",0,DXGI_FORMAT_R32G32B32A32_UINT,0,static_cast<UINT>(offsetof(Vertex,joints)),D3D11_INPUT_PER_VERTEX_DATA,0},
        {"BLENDWEIGHT",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,static_cast<UINT>(offsetof(Vertex,weights)),D3D11_INPUT_PER_VERTEX_DATA,0},
        {"COLOR",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,static_cast<UINT>(offsetof(Vertex,color)),D3D11_INPUT_PER_VERTEX_DATA,0}};
    Check(d->CreateInputLayout(elements,6,vertex->GetBufferPointer(),vertex->GetBufferSize(),&layout_),"Create input layout");
    D3D11_BUFFER_DESC cb{}; cb.ByteWidth=sizeof(Constants); cb.Usage=D3D11_USAGE_DEFAULT; cb.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
    Check(d->CreateBuffer(&cb,nullptr,&constants_),"Create skin constant buffer");
    D3D11_RASTERIZER_DESC raster{}; raster.FillMode=D3D11_FILL_SOLID; raster.CullMode=D3D11_CULL_NONE; raster.DepthClipEnable=TRUE;
    Check(d->CreateRasterizerState(&raster,&solid_),"Create solid rasterizer"); raster.FillMode=D3D11_FILL_WIREFRAME;
    Check(d->CreateRasterizerState(&raster,&wire_),"Create wire rasterizer");
    D3D11_DEPTH_STENCIL_DESC depth{}; depth.DepthEnable=TRUE; depth.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ALL; depth.DepthFunc=D3D11_COMPARISON_LESS_EQUAL;
    Check(d->CreateDepthStencilState(&depth,&depthState_),"Create depth state"); depth.DepthEnable=FALSE;
    Check(d->CreateDepthStencilState(&depth,&overlayState_),"Create skeleton overlay state");
    depth.DepthEnable=TRUE;depth.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ZERO;
    Check(d->CreateDepthStencilState(&depth,&effectDepthState_),"Create transparent effect depth state");
    D3D11_SAMPLER_DESC sample{}; sample.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sample.AddressU=sample.AddressV=sample.AddressW=D3D11_TEXTURE_ADDRESS_WRAP; sample.MaxLOD=D3D11_FLOAT32_MAX;
    Check(d->CreateSamplerState(&sample,&sampler_),"Create sampler");
    D3D11_BLEND_DESC blend{}; blend.RenderTarget[0].RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL;
    Check(d->CreateBlendState(&blend,&blend_),"Create blend state");
    auto& target=blend.RenderTarget[0];target.BlendEnable=TRUE;target.SrcBlend=D3D11_BLEND_SRC_ALPHA;target.DestBlend=D3D11_BLEND_ONE;target.BlendOp=D3D11_BLEND_OP_ADD;
    target.SrcBlendAlpha=D3D11_BLEND_ONE;target.DestBlendAlpha=D3D11_BLEND_ZERO;target.BlendOpAlpha=D3D11_BLEND_OP_ADD;
    Check(d->CreateBlendState(&blend,&additive_),"Create effect blend state");
    const uint8_t white[4]={255,255,255,255}; white_=MakeTexture(d,white,1,1);
}
void RendererDX11::SetModel(const ModelAsset& model) {
    std::vector<GpuMesh> meshes; std::vector<ComPtr<ID3D11ShaderResourceView>> textures; std::vector<std::string> warnings;
    const Pose bind=EvaluatePose(model.skeleton,nullptr,0);
    XMFLOAT3 minimum{FLT_MAX,FLT_MAX,FLT_MAX},maximum{-FLT_MAX,-FLT_MAX,-FLT_MAX};
    for (const auto& m:model.meshes) {
        if (m.vertices.empty() || m.indices.empty()) throw std::runtime_error("Cannot upload an empty mesh.");
        GpuMesh gpu; D3D11_BUFFER_DESC desc{}; desc.Usage=D3D11_USAGE_IMMUTABLE; desc.BindFlags=D3D11_BIND_VERTEX_BUFFER;
        desc.ByteWidth=static_cast<UINT>(m.vertices.size()*sizeof(Vertex)); D3D11_SUBRESOURCE_DATA data{}; data.pSysMem=m.vertices.data();
        Check(device_->CreateBuffer(&desc,&data,&gpu.vertices),"Upload vertices");
        desc.ByteWidth=static_cast<UINT>(m.indices.size()*sizeof(uint32_t)); desc.BindFlags=D3D11_BIND_INDEX_BUFFER; data.pSysMem=m.indices.data();
        Check(device_->CreateBuffer(&desc,&data,&gpu.indices),"Upload indices"); gpu.count=static_cast<UINT>(m.indices.size()); meshes.push_back(gpu);
        for (const auto& v:m.vertices) {
            XMVECTOR pos=XMVectorZero();
            for (size_t j=0;j<4;++j) if (v.weights[j]>0) {
                const auto& b=m.skin.at(v.joints[j]);
                const XMMATRIX matrix=XMLoadFloat4x4(&b.inverseBind)*XMLoadFloat4x4(&bind.global.at(b.bone));
                pos+=XMVector3TransformCoord(XMLoadFloat3(&v.position),matrix)*v.weights[j];
            }
            XMFLOAT3 p; XMStoreFloat3(&p,pos);
            minimum={std::min(minimum.x,p.x),std::min(minimum.y,p.y),std::min(minimum.z,p.z)};
            maximum={std::max(maximum.x,p.x),std::max(maximum.y,p.y),std::max(maximum.z,p.z)};
        }
    }
    for (const auto& material:model.materials) {
        try { textures.push_back(LoadTexture(device_.Get(),material)); }
        catch (const std::exception& e) { warnings.push_back(std::string("Texture fallback: ")+e.what()); textures.emplace_back(); }
    }
    if (meshes.empty()) { minimum={-1,0,-1}; maximum={1,2,1}; }
    meshes_=std::move(meshes); textures_=std::move(textures); warnings_=std::move(warnings); boundsMin_=minimum; boundsMax_=maximum;
}
void RendererDX11::UpdateBounds(const ModelAsset& model,const Pose& pose) {
    if(model.meshes.empty()){boundsMin_={-1,0,-1};boundsMax_={1,2,1};return;}
    XMVECTOR minimum=XMVectorReplicate(FLT_MAX),maximum=XMVectorReplicate(-FLT_MAX);
    for(const auto& mesh:model.meshes)for(const auto& vertex:mesh.vertices){
        XMVECTOR point=XMVectorZero();
        for(size_t i=0;i<4;++i)if(vertex.weights[i]>0){const auto& skin=mesh.skin.at(vertex.joints[i]);point+=XMVector3TransformCoord(XMLoadFloat3(&vertex.position),XMLoadFloat4x4(&skin.inverseBind)*XMLoadFloat4x4(&pose.global.at(skin.bone)))*vertex.weights[i];}
        minimum=XMVectorMin(minimum,point);maximum=XMVectorMax(maximum,point);
    }XMStoreFloat3(&boundsMin_,minimum);XMStoreFloat3(&boundsMax_,maximum);
}
void RendererDX11::FitCamera(PreviewCamera& camera) const {
    XMStoreFloat3(&camera.target,(XMLoadFloat3(&boundsMin_)+XMLoadFloat3(&boundsMax_))*.5f);
    camera.sceneRadius=std::max(XMVectorGetX(XMVector3Length(XMLoadFloat3(&boundsMax_)-XMLoadFloat3(&boundsMin_)))*.5f,.01f);
    const float aspect=height_?static_cast<float>(width_)/height_:1;
    const float halfFov=std::min(XMConvertToRadians(21),std::atan(std::tan(XMConvertToRadians(21))*aspect));
    camera.distance=camera.sceneRadius/std::sin(halfFov)*1.15f; camera.yaw=.35f; camera.pitch=.12f;
}
void RendererDX11::Resize(unsigned width,unsigned height) {
    width=std::clamp(width,1u,8192u); height=std::clamp(height,1u,8192u);
    if (width_==width && height_==height) return;
    ID3D11ShaderResourceView* empty=nullptr; context_->PSSetShaderResources(0,1,&empty); context_->OMSetRenderTargets(0,nullptr,nullptr);
    ComPtr<ID3D11Texture2D> color,depth; ComPtr<ID3D11RenderTargetView> rtv; ComPtr<ID3D11ShaderResourceView> srv; ComPtr<ID3D11DepthStencilView> dsv;
    D3D11_TEXTURE2D_DESC desc{}; desc.Width=width; desc.Height=height; desc.MipLevels=1; desc.ArraySize=1;
    desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM; desc.SampleDesc.Count=1; desc.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
    Check(device_->CreateTexture2D(&desc,nullptr,&color),"Create viewport color texture");
    Check(device_->CreateRenderTargetView(color.Get(),nullptr,&rtv),"Create viewport render target");
    Check(device_->CreateShaderResourceView(color.Get(),nullptr,&srv),"Create viewport image");
    desc.Format=DXGI_FORMAT_D24_UNORM_S8_UINT; desc.BindFlags=D3D11_BIND_DEPTH_STENCIL;
    Check(device_->CreateTexture2D(&desc,nullptr,&depth),"Create viewport depth texture");
    Check(device_->CreateDepthStencilView(depth.Get(),nullptr,&dsv),"Create viewport depth view");
    color_=color; depth_=depth; rtv_=rtv; colorSRV_=srv; dsv_=dsv; width_=width; height_=height;
}
void RendererDX11::Draw(const MeshData& mesh,const GpuMesh& gpu,const Pose& pose,Constants& cb) {
    if (mesh.skin.size()>MaxSkinBones) throw std::runtime_error("Skin palette exceeds the GPU limit.");
    for (size_t i=0;i<mesh.skin.size();++i) {
        const auto& b=mesh.skin[i];
        const XMMATRIX skin=XMLoadFloat4x4(&b.inverseBind)*XMLoadFloat4x4(&pose.global.at(b.bone));
        XMStoreFloat4x4(&cb.skin[i],skin);
        const XMVECTOR det=XMMatrixDeterminant(skin);
        XMStoreFloat4x4(&cb.normal[i],std::abs(XMVectorGetX(det))>1e-12f?XMMatrixTranspose(XMMatrixInverse(nullptr,skin)):XMMatrixIdentity());
    }
    context_->UpdateSubresource(constants_.Get(),0,nullptr,&cb,0,0);
    UINT stride=sizeof(Vertex),offset=0; ID3D11Buffer* vb=gpu.vertices.Get(); context_->IASetVertexBuffers(0,1,&vb,&stride,&offset);
    context_->IASetIndexBuffer(gpu.indices.Get(),DXGI_FORMAT_R32_UINT,0); context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context_->DrawIndexed(gpu.count,0,0);
}
void RendererDX11::DrawLines(const std::vector<Vertex>& vertices,Constants& cb,D3D11_PRIMITIVE_TOPOLOGY topology) {
    if (vertices.empty()) return;
    if (vertices.size()>lineCapacity_) {
        lineCapacity_=vertices.size()*2; lines_.Reset(); D3D11_BUFFER_DESC desc{};
        desc.ByteWidth=static_cast<UINT>(lineCapacity_*sizeof(Vertex)); desc.Usage=D3D11_USAGE_DYNAMIC; desc.BindFlags=D3D11_BIND_VERTEX_BUFFER; desc.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
        Check(device_->CreateBuffer(&desc,nullptr,&lines_),"Create debug line buffer");
    }
    D3D11_MAPPED_SUBRESOURCE map{}; Check(context_->Map(lines_.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&map),"Upload debug lines");
    std::memcpy(map.pData,vertices.data(),vertices.size()*sizeof(Vertex)); context_->Unmap(lines_.Get(),0);
    cb.options={0,1,0,0}; XMStoreFloat4x4(&cb.skin[0],XMMatrixIdentity()); XMStoreFloat4x4(&cb.normal[0],XMMatrixIdentity());
    context_->UpdateSubresource(constants_.Get(),0,nullptr,&cb,0,0);
    UINT stride=sizeof(Vertex),offset=0; ID3D11Buffer* vb=lines_.Get(); context_->IASetVertexBuffers(0,1,&vb,&stride,&offset);
    context_->IASetPrimitiveTopology(topology); context_->Draw(static_cast<UINT>(vertices.size()),0);
}
void RendererDX11::DrawEffects(const ModelAsset& model,const Pose& pose,const RenderOptions& options,Constants& cb) {
    if(!options.showEffects)return;
    context_->OMSetBlendState(additive_.Get(),nullptr,0xffffffff);context_->OMSetDepthStencilState(effectDepthState_.Get(),0);
    for(const auto& effect:model.effects){
        if(effect.bone>=pose.global.size()||options.time<effect.start||options.time>effect.end)continue;
        if(!effect.clipId.empty()&&(!options.activeClip||effect.clipId!=options.activeClip->id))continue;
        cb.color=effect.color;cb.color.x*=effect.intensity;cb.color.y*=effect.intensity;cb.color.z*=effect.intensity;cb.color.w*=.55f;
        std::vector<Vertex> vertices;
        auto add=[&](XMFLOAT3 p,float opacity=1){Vertex v;v.position=p;v.weights[0]=1;v.color.w=opacity;vertices.push_back(v);};
        auto transform=[&](XMFLOAT3 local,const Pose& sample){XMFLOAT3 world;XMStoreFloat3(&world,XMVector3TransformCoord(XMLoadFloat3(&local),Matrix(effect.local)*XMLoadFloat4x4(&sample.global[effect.bone])));return world;};
        const float time=static_cast<float>(options.time-effect.start);
        if(effect.type==EffectType::Trail&&options.activeClip){
            const double start=std::max(effect.start,options.time-.25);auto previous=EvaluatePose(model.skeleton,options.activeClip,start);
            auto a=transform({0,0,0},previous),b=transform({0,effect.size,0},previous);
            for(int i=1;i<=16;++i){auto sample=EvaluatePose(model.skeleton,options.activeClip,start+(options.time-start)*i/16);
                const auto c=transform({0,0,0},sample),d=transform({0,effect.size,0},sample);const float older=static_cast<float>(i-1)/16,newer=static_cast<float>(i)/16;
                add(a,older);add(b,older);add(d,newer);add(a,older);add(d,newer);add(c,newer);a=c;b=d;
            }
            DrawLines(vertices,cb,D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        }else if(effect.type==EffectType::Sparks){
            const auto emitter=transform({0,0,0},pose);
            for(int i=0;i<24;++i){const float age=std::fmod(time+i*.037f,.65f),angle=i*2.399963f;
                const XMFLOAT3 velocity{std::cos(angle)*effect.size,(1.5f+static_cast<float>(i%5)*.15f)*effect.size,std::sin(angle)*effect.size};
                auto at=[&](float t){return XMFLOAT3{emitter.x+velocity.x*t,emitter.y+velocity.y*t-1.8f*t*t,emitter.z+velocity.z*t};};add(at(age));add(at(std::max(age-.035f,0.0f)));
            }DrawLines(vertices,cb);
        }else if(effect.type==EffectType::Aura){
            const float radius=effect.size*(.9f+.1f*std::sin(time*5));
            for(int axis=0;axis<3;++axis)for(int i=0;i<64;++i){
                auto point=[&](float angle){const float c=std::cos(angle)*radius,s=std::sin(angle)*radius;return axis==0?XMFLOAT3{0,c,s}:axis==1?XMFLOAT3{c,0,s}:XMFLOAT3{c,s,0};};
                add(transform(point(i*XM_2PI/64),pose));add(transform(point((i+1)*XM_2PI/64),pose));
            }DrawLines(vertices,cb);
        }
    }
    context_->OMSetBlendState(blend_.Get(),nullptr,0xffffffff);
}
void RendererDX11::Render(const ModelAsset& model,const Pose& pose,const PreviewCamera& camera,const RenderOptions& options) {
    if (!width_ || !height_) return;
    ID3D11ShaderResourceView* empty=nullptr; context_->PSSetShaderResources(0,1,&empty);
    ID3D11RenderTargetView* target=rtv_.Get(); context_->OMSetRenderTargets(1,&target,dsv_.Get());
    const float background[]={.053f,.067f,.095f,1}; context_->ClearRenderTargetView(target,background); context_->ClearDepthStencilView(dsv_.Get(),D3D11_CLEAR_DEPTH|D3D11_CLEAR_STENCIL,1,0);
    D3D11_VIEWPORT viewport{0,0,static_cast<float>(width_),static_cast<float>(height_),0,1}; context_->RSSetViewports(1,&viewport);
    context_->IASetInputLayout(layout_.Get()); context_->VSSetShader(vs_.Get(),nullptr,0); context_->PSSetShader(ps_.Get(),nullptr,0);
    context_->GSSetShader(nullptr,nullptr,0); context_->HSSetShader(nullptr,nullptr,0); context_->DSSetShader(nullptr,nullptr,0);
    ID3D11Buffer* buffer=constants_.Get(); context_->VSSetConstantBuffers(0,1,&buffer); context_->PSSetConstantBuffers(0,1,&buffer);
    ID3D11SamplerState* sampler=sampler_.Get(); context_->PSSetSamplers(0,1,&sampler);
    context_->OMSetBlendState(blend_.Get(),nullptr,0xffffffff); context_->OMSetDepthStencilState(depthState_.Get(),0);
    context_->RSSetState(options.wireframe?wire_.Get():solid_.Get());
    Constants cb{}; XMStoreFloat4x4(&cb.viewProjection,camera.View()*camera.Projection(static_cast<float>(width_)/height_));
    for (size_t i=0;i<model.meshes.size();++i) {
        const auto& mesh=model.meshes[i]; const auto& m=model.materials.at(mesh.material); cb.color=m.baseColor;
        auto* tex=textures_.at(mesh.material).Get(); cb.options={tex?1.0f:0.0f,0,0,0}; if (!tex) tex=white_.Get();
        context_->PSSetShaderResources(0,1,&tex); Draw(mesh,meshes_.at(i),pose,cb);
    }
    auto line=[](std::vector<Vertex>& v,XMFLOAT3 a,XMFLOAT3 b) { Vertex x,y; x.position=a; y.position=b; x.weights[0]=y.weights[0]=1; v.push_back(x); v.push_back(y); };
    context_->RSSetState(solid_.Get());
    if (options.grid) {
        std::vector<Vertex> lines; const float step=camera.sceneRadius*.2f, extent=step*20;
        const float y=-.002f*camera.sceneRadius;
        const float centerX=std::floor(camera.target.x/step)*step,centerZ=std::floor(camera.target.z/step)*step;
        for (int i=-20;i<=20;++i) { const float f=i*step; line(lines,{centerX-extent,y,centerZ+f},{centerX+extent,y,centerZ+f}); line(lines,{centerX+f,y,centerZ-extent},{centerX+f,y,centerZ+extent}); }
        cb.color={.14f,.18f,.24f,1}; DrawLines(lines,cb);
        lines.clear(); line(lines,{-extent,y,0},{extent,y,0}); cb.color={.40f,.20f,.27f,1}; DrawLines(lines,cb);
        lines.clear(); line(lines,{0,y,-extent},{0,y,extent}); cb.color={.20f,.36f,.39f,1}; DrawLines(lines,cb);
    }
    if (options.skeleton) {
        context_->OMSetDepthStencilState(overlayState_.Get(),0); std::vector<Vertex> lines,selected;
        for (size_t i=0;i<model.skeleton.bones.size();++i) {
            const auto& m=pose.global[i]; const XMFLOAT3 b{m._41,m._42,m._43};
            const int parent=model.skeleton.bones[i].parent;
            if (parent>=0) {
                const auto& p=pose.global[parent]; line(static_cast<int>(i)==options.selectedBone?selected:lines,{p._41,p._42,p._43},b);
            }
            if (static_cast<int>(i)==options.selectedBone) {
                const float r=camera.sceneRadius*.015f;
                line(selected,{b.x-r,b.y,b.z},{b.x+r,b.y,b.z}); line(selected,{b.x,b.y-r,b.z},{b.x,b.y+r,b.z});
            }
        }
        cb.color={.34f,.92f,.76f,1}; DrawLines(lines,cb); cb.color={1,.73f,.32f,1}; DrawLines(selected,cb);
    }
    DrawEffects(model,pose,options,cb);
    context_->PSSetShaderResources(0,1,&empty);
    context_->OMSetRenderTargets(0,nullptr,nullptr);
}
void RendererDX11::SaveScreenshot(const std::filesystem::path& path) const {
    if (!color_) throw std::runtime_error("No viewport image to save.");
    D3D11_TEXTURE2D_DESC desc; color_->GetDesc(&desc); desc.Usage=D3D11_USAGE_STAGING; desc.BindFlags=0; desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> staging; Check(device_->CreateTexture2D(&desc,nullptr,&staging),"Create screenshot staging texture"); context_->CopyResource(staging.Get(),color_.Get());
    D3D11_MAPPED_SUBRESOURCE map{}; Check(context_->Map(staging.Get(),0,D3D11_MAP_READ,0,&map),"Read screenshot");
    std::vector<uint8_t> pixels(static_cast<size_t>(width_)*height_*4);
    for (unsigned y=0;y<height_;++y) std::memcpy(pixels.data()+static_cast<size_t>(y)*width_*4,static_cast<const uint8_t*>(map.pData)+static_cast<size_t>(y)*map.RowPitch,width_*4);
    context_->Unmap(staging.Get(),0);
    ComPtr<IWICImagingFactory> factory; Check(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)),"Create PNG writer");
    ComPtr<IWICStream> stream; Check(factory->CreateStream(&stream),"Create PNG stream"); Check(stream->InitializeFromFilename(path.c_str(),GENERIC_WRITE),"Open PNG output");
    ComPtr<IWICBitmapEncoder> encoder; Check(factory->CreateEncoder(GUID_ContainerFormatPng,nullptr,&encoder),"Create PNG encoder"); Check(encoder->Initialize(stream.Get(),WICBitmapEncoderNoCache),"Initialize PNG encoder");
    ComPtr<IWICBitmapFrameEncode> frame; Check(encoder->CreateNewFrame(&frame,nullptr),"Create PNG frame"); Check(frame->Initialize(nullptr),"Initialize PNG frame");
    Check(frame->SetSize(width_,height_),"Set PNG size"); WICPixelFormatGUID format=GUID_WICPixelFormat32bppRGBA; Check(frame->SetPixelFormat(&format),"Set PNG format");
    ComPtr<IWICBitmap> bitmap;
    Check(factory->CreateBitmapFromMemory(width_,height_,GUID_WICPixelFormat32bppRGBA,width_*4,static_cast<UINT>(pixels.size()),pixels.data(),&bitmap),"Create screenshot bitmap");
    ComPtr<IWICFormatConverter> converter; Check(factory->CreateFormatConverter(&converter),"Create PNG pixel converter");
    Check(converter->Initialize(bitmap.Get(),format,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom),"Convert pixels for PNG encoder");
    Check(frame->WriteSource(converter.Get(),nullptr),"Write PNG pixels"); Check(frame->Commit(),"Commit PNG frame"); Check(encoder->Commit(),"Commit PNG file");
}
}

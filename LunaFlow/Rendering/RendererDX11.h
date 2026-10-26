#pragma once
#include "Core/Scene.h"
#include <d3d11.h>
#include <wrl/client.h>

namespace LunaFlow {
using Microsoft::WRL::ComPtr;
struct PreviewCamera {
    XMFLOAT3 target{0,1,0};
    float yaw = .35f, pitch = .12f, distance = 4, sceneRadius = 1;
    void Orbit(float dx,float dy);
    void Pan(float dx,float dy,float viewportHeight);
    void Zoom(float wheel);
    XMMATRIX View() const;
    XMMATRIX Projection(float aspect) const;
};
struct RenderOptions {
    bool grid=true,skeleton=false,wireframe=false,showEffects=true;
    int selectedBone=-1;
    const AnimationClip* activeClip=nullptr;
    double time=0;
};
class RendererDX11 {
public:
    void Initialize(ID3D11Device*, ID3D11DeviceContext*, const std::filesystem::path& shaderDirectory);
    void SetModel(const ModelAsset&);
    void Resize(unsigned width,unsigned height);
    void Render(const ModelAsset&, const Pose&, const PreviewCamera&, const RenderOptions&);
    void FitCamera(PreviewCamera&) const;
    void UpdateBounds(const ModelAsset&,const Pose&);
    ID3D11ShaderResourceView* Texture() const { return colorSRV_.Get(); }
    void SaveScreenshot(const std::filesystem::path&) const;
    unsigned Width() const { return width_; }
    unsigned Height() const { return height_; }
    const std::vector<std::string>& Warnings() const { return warnings_; }
private:
    struct GpuMesh { ComPtr<ID3D11Buffer> vertices,indices; UINT count=0; };
    struct Constants {
        XMFLOAT4X4 viewProjection;
        std::array<XMFLOAT4X4,MaxSkinBones> skin,normal;
        XMFLOAT4 color;
        XMFLOAT4 options; // texture enabled, unlit, unused, unused
    };
    void Draw(const MeshData&,const GpuMesh&,const Pose&,Constants&);
    void DrawLines(const std::vector<Vertex>&,Constants&,D3D11_PRIMITIVE_TOPOLOGY topology=D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
    void DrawEffects(const ModelAsset&,const Pose&,const RenderOptions&,Constants&);
    ComPtr<ID3D11Device> device_;
    ComPtr<ID3D11DeviceContext> context_;
    ComPtr<ID3D11Texture2D> color_,depth_;
    ComPtr<ID3D11RenderTargetView> rtv_;
    ComPtr<ID3D11ShaderResourceView> colorSRV_,white_;
    ComPtr<ID3D11DepthStencilView> dsv_;
    ComPtr<ID3D11VertexShader> vs_;
    ComPtr<ID3D11PixelShader> ps_;
    ComPtr<ID3D11InputLayout> layout_;
    ComPtr<ID3D11Buffer> constants_,lines_;
    ComPtr<ID3D11RasterizerState> solid_,wire_;
    ComPtr<ID3D11DepthStencilState> depthState_,overlayState_,effectDepthState_;
    ComPtr<ID3D11SamplerState> sampler_;
    ComPtr<ID3D11BlendState> blend_;
    ComPtr<ID3D11BlendState> additive_;
    std::vector<GpuMesh> meshes_;
    std::vector<ComPtr<ID3D11ShaderResourceView>> textures_;
    std::vector<std::string> warnings_;
    XMFLOAT3 boundsMin_{},boundsMax_{};
    unsigned width_=0,height_=0;
    size_t lineCapacity_=0;
};
}

#include "App/EditorApp.h"
#include "App/UiTheme.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_impl_dx11.h"
#include "ImGuizmo.h"
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace LunaFlow;
static void Check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
static ImGuiTabBar* FindTabBar(const char* suffix) {
    auto& bars=ImGui::GetCurrentContext()->TabBars;
    for(int i=0;i<bars.GetMapSize();++i)if(auto* bar=bars.TryGetMapData(i))
        for(auto& tab:bar->Tabs)if(std::strstr(ImGui::TabBarGetTabName(bar,&tab),suffix))return bar;
    throw std::runtime_error(std::string("Missing tab bar: ")+suffix);
}
static ImGuiTabItem& FindTab(const char* suffix) {
    auto* bar=FindTabBar(suffix);
    for(auto& tab:bar->Tabs)if(std::strstr(ImGui::TabBarGetTabName(bar,&tab),suffix))return tab;
    throw std::runtime_error("Missing tab");
}
static bool TabSelected(const char* suffix){return FindTabBar(suffix)->SelectedTabId==FindTab(suffix).ID;}
int main() {
    const HRESULT com=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);int code=0;
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;D3D_FEATURE_LEVEL feature;
    bool backend=false;
    try {
        Check(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,&feature,&context)),"Create offscreen UI device");
        ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.DeltaTime=1.0f/60;
        ConfigureUiTheme();LoadUiFonts(std::filesystem::path(LUNAFLOW_SOURCE_DIR)/"Assets"/"Fonts");
        backend=ImGui_ImplDX11_Init(device.Get(),context.Get());Check(backend,"Initialize offscreen UI renderer");
        D3D11_TEXTURE2D_DESC desc{};desc.Width=1440;desc.Height=900;desc.MipLevels=desc.ArraySize=1;desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.SampleDesc.Count=1;desc.BindFlags=D3D11_BIND_RENDER_TARGET;
        ComPtr<ID3D11Texture2D> target;Check(SUCCEEDED(device->CreateTexture2D(&desc,nullptr,&target)),"Create UI target");ComPtr<ID3D11RenderTargetView> rtv;device->CreateRenderTargetView(target.Get(),nullptr,&rtv);
        {
            const auto preferences=std::filesystem::current_path()/"test-output"/"ui-preferences.json";
            const bool hadPreferences=std::filesystem::exists(preferences);const auto previousWrite=hadPreferences?std::filesystem::last_write_time(preferences):std::filesystem::file_time_type{};
            EditorApp app(nullptr,device.Get(),context.Get(),std::filesystem::path(LUNAFLOW_SOURCE_DIR)/"Rendering"/"Shaders",preferences);
            ImVec2 mouse{4,70};int frameWidth=1440,frameHeight=900;
            auto frame=[&](int width,int height,bool wheel,bool stable=true){
                frameWidth=width;frameHeight=height;
                io.DisplaySize={static_cast<float>(width),static_cast<float>(height)};io.DeltaTime=1.0f/60;
                io.AddMousePosEvent(mouse.x,mouse.y);if(wheel)io.AddMouseWheelEvent(0,-8);
                ImGui_ImplDX11_NewFrame();ImGui::NewFrame();ImGuizmo::BeginFrame();app.Frame(io.DeltaTime);ImGui::Render();
                auto* root=ImGui::FindWindowByName("LunaFlow");Check(root!=nullptr,"Main UI exists");
                Check((root->Flags&ImGuiWindowFlags_NoScrollWithMouse)!=0,"Workspace consumes no wheel scrolling");
                if(stable&&root->ScrollMax.y>.5f){std::cerr<<"Root scroll extent "<<root->ScrollMax.y<<" at "<<width<<"x"<<height<<"\n";throw std::runtime_error("Workspace exceeds the window height");}
                Check(root->Scroll.y==0,"Root workspace stays pinned while wheel is used");
                ID3D11RenderTargetView* view=rtv.Get();context->OMSetRenderTargets(1,&view,nullptr);const float clear[]={.043f,.051f,.075f,1};context->ClearRenderTargetView(view,clear);ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
            };
            auto tick=[&]{frame(frameWidth,frameHeight,false);};
            auto clickTab=[&](const char* suffix){
                auto* bar=FindTabBar(suffix);const auto& tab=FindTab(suffix);
                mouse={bar->BarRect.Min.x+tab.Offset-bar->ScrollingAnim+tab.Width*.5f,(bar->BarRect.Min.y+bar->BarRect.Max.y)*.5f};
                tick();io.AddMouseButtonEvent(0,true);tick();io.AddMouseButtonEvent(0,false);tick();tick();
                if(!TabSelected(suffix)){
                    std::cerr<<"Click "<<mouse.x<<","<<mouse.y<<" bar "<<bar->BarRect.Min.x<<","<<bar->BarRect.Min.y<<" selected "<<bar->SelectedTabId<<"\n";
                    for(auto& item:bar->Tabs)std::cerr<<ImGui::TabBarGetTabName(bar,&item)<<" offset="<<item.Offset<<" width="<<item.Width<<" id="<<item.ID<<"\n";
                    throw std::runtime_error(std::string("Clicked tab remains selected: ")+suffix+" at "+std::to_string(frameWidth)+"x"+std::to_string(frameHeight));
                }
                mouse={4,70};tick();
            };
            auto saveImage=[&](const char* filename){
                context->OMSetRenderTargets(0,nullptr,nullptr);auto stagingDesc=desc;stagingDesc.BindFlags=0;stagingDesc.Usage=D3D11_USAGE_STAGING;stagingDesc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
                ComPtr<ID3D11Texture2D> staging;Check(SUCCEEDED(device->CreateTexture2D(&stagingDesc,nullptr,&staging)),"Create UI readback");context->CopyResource(staging.Get(),target.Get());D3D11_MAPPED_SUBRESOURCE map{};
                Check(SUCCEEDED(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&map)),"Read UI image");
                auto folder=std::filesystem::current_path()/"test-output";std::filesystem::create_directories(folder);std::ofstream image(folder/filename,std::ios::binary);
                for(unsigned y=0;y<desc.Height;++y)image.write(static_cast<const char*>(map.pData)+static_cast<size_t>(y)*map.RowPitch,desc.Width*4);context->Unmap(staging.Get(),0);
            };
            for(bool korean:{true,false}){
                app.SetLanguage(korean);
                for(const auto size:{ImVec2{1440,900},ImVec2{1000,700},ImVec2{1920,1080}}){
                    for(int i=0;i<3;++i)frame(static_cast<int>(size.x),static_cast<int>(size.y),i>0,i==2);
                    clickTab("###VFX");Check(TabSelected("###Effects"),"VFX workspace opens the effects list");
                    clickTab("###Hierarchy");Check(TabSelected("###VFX"),"Library navigation leaves the workspace unchanged");
                    clickTab("###VFX");Check(TabSelected("###Effects"),"Clicking the active workspace restores its tools");
                    clickTab("###Scene");Check(TabSelected("###Hierarchy"),"Scene workspace opens the hierarchy");
                    clickTab("###Asset");clickTab("###Animation");Check(TabSelected("###Motions"),"Animation workspace opens motions");
                }
            }
            app.SetLanguage(true);for(int i=0;i<3;++i)frame(1440,900,false,i==2);
            clickTab("###Hierarchy");saveImage("ui-preview.rgba");
            clickTab("###Effects");
            ImGui::ActivateItemByID(ImHashStr("+ 스파크",0,FindTab("###Effects").ID));tick();tick();tick();
            Check(TabSelected("###VFX")&&TabSelected("###Effects"),"Adding an effect opens the VFX workspace and effects list");
            saveImage("ui-vfx-preview.rgba");
            Check(std::filesystem::exists(preferences)==hadPreferences,"Rendering does not create preferences implicitly");
            if(hadPreferences)Check(std::filesystem::last_write_time(preferences)==previousWrite,"Rendering does not rewrite preferences every frame");
        }
        std::cout<<"PASS Korean/English, 3 window sizes, fixed root workspace, workspace/library navigation, effect creation, offscreen DX11 UI\n";
    }catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<"\n";code=1;}
    if(backend)ImGui_ImplDX11_Shutdown();if(ImGui::GetCurrentContext())ImGui::DestroyContext();
    if(context){context->ClearState();context->Flush();}if(SUCCEEDED(com))CoUninitialize();return code;
}

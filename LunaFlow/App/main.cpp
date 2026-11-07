#include "App/EditorApp.h"
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include "UiFonts.h"
#include "UiTheme.h"
#include "ImGuizmo.h"
#include "App/ContinuousInput.h"
#include "App/Resource.h"
#include <shellapi.h>
#include <stdexcept>
#include <chrono>
#include <thread>
#include <algorithm>
#include <cmath>

using Microsoft::WRL::ComPtr;
static UINT resizeWidth=0,resizeHeight=0;
static bool minimized=false;
static LunaFlow::EditorApp* activeEditor=nullptr;
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND,UINT,WPARAM,LPARAM);
static LRESULT WINAPI WindowProc(HWND hwnd,UINT msg,WPARAM w,LPARAM l) {
    if (ImGui_ImplWin32_WndProcHandler(hwnd,msg,w,l)) return true;
    switch (msg) {
    case WM_CAPTURECHANGED:
        if(reinterpret_cast<HWND>(l)!=hwnd&&ImGui::GetCurrentContext()){
            auto& input=ImGui::GetIO();for(int button=0;button<5;++button)input.AddMouseButtonEvent(button,false);
        }break;
    case WM_SIZE: minimized=w==SIZE_MINIMIZED; if (!minimized) { resizeWidth=LOWORD(l); resizeHeight=HIWORD(l); } return 0;
    case WM_DPICHANGED: { const auto* r=reinterpret_cast<RECT*>(l); SetWindowPos(hwnd,nullptr,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER|SWP_NOACTIVATE); return 0; }
    case WM_GETMINMAXINFO: {
        auto* info=reinterpret_cast<MINMAXINFO*>(l);const float dpi=ImGui_ImplWin32_GetDpiScaleForHwnd(hwnd);
        MONITORINFO monitor{sizeof(monitor)};GetMonitorInfoW(MonitorFromWindow(hwnd,MONITOR_DEFAULTTONEAREST),&monitor);
        info->ptMinTrackSize={std::min(static_cast<LONG>(900*dpi),monitor.rcWork.right-monitor.rcWork.left),std::min(static_cast<LONG>(600*dpi),monitor.rcWork.bottom-monitor.rcWork.top)};return 0;
    }
    case WM_CLOSE: if(activeEditor){activeEditor->RequestClose();return 0;}break;
    case WM_SYSCOMMAND: if ((w&0xfff0)==SC_KEYMENU) return 0; break;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(hwnd,msg,w,l);
}
static void Check(HRESULT hr,const char* text) { if (FAILED(hr)) throw std::runtime_error(text); }
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR,int show) {
    const HRESULT com=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    HWND window=nullptr;
    bool win32Ready=false,dxReady=false;
    int result=0;
    try {
        ImGui_ImplWin32_EnableDpiAwareness();
        const auto smallIcon=static_cast<HICON>(LoadImageW(instance,MAKEINTRESOURCEW(IDI_LUNAFLOW),IMAGE_ICON,GetSystemMetrics(SM_CXSMICON),GetSystemMetrics(SM_CYSMICON),LR_DEFAULTCOLOR));
        WNDCLASSEXW wc{sizeof(wc),CS_CLASSDC,WindowProc,0,0,instance,LoadIconW(instance,MAKEINTRESOURCEW(IDI_LUNAFLOW)),LoadCursor(nullptr,IDC_ARROW),nullptr,nullptr,L"LunaFlowDX11",smallIcon};
        RegisterClassExW(&wc);
        const auto startupMonitor=MonitorFromPoint({0,0},MONITOR_DEFAULTTOPRIMARY);
        float dpi=ImGui_ImplWin32_GetDpiScaleForMonitor(startupMonitor);
        MONITORINFO monitor{sizeof(monitor)};GetMonitorInfoW(startupMonitor,&monitor);
        const int width=std::min(static_cast<int>(1440*dpi),static_cast<int>(monitor.rcWork.right-monitor.rcWork.left)),height=std::min(static_cast<int>(940*dpi),static_cast<int>(monitor.rcWork.bottom-monitor.rcWork.top));
        window=CreateWindowW(wc.lpszClassName,L"LunaFlow",WS_OVERLAPPEDWINDOW,monitor.rcWork.left+(monitor.rcWork.right-monitor.rcWork.left-width)/2,monitor.rcWork.top+(monitor.rcWork.bottom-monitor.rcWork.top-height)/2,width,height,nullptr,nullptr,instance,nullptr);
        if (!window) throw std::runtime_error("Cannot create the application window.");
        DXGI_SWAP_CHAIN_DESC desc{}; desc.BufferCount=2; desc.BufferDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT; desc.OutputWindow=window; desc.SampleDesc.Count=1;
        desc.Windowed=TRUE; desc.SwapEffect=DXGI_SWAP_EFFECT_FLIP_DISCARD;
        ComPtr<ID3D11Device> device; ComPtr<ID3D11DeviceContext> context; ComPtr<IDXGISwapChain> swap;
        D3D_FEATURE_LEVEL feature; const D3D_FEATURE_LEVEL levels[]={D3D_FEATURE_LEVEL_11_0};
        HRESULT hr=D3D11CreateDeviceAndSwapChain(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,levels,1,D3D11_SDK_VERSION,&desc,&swap,&device,&feature,&context);
        if (FAILED(hr)) hr=D3D11CreateDeviceAndSwapChain(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,levels,1,D3D11_SDK_VERSION,&desc,&swap,&device,&feature,&context);
        Check(hr,"DirectX 11 initialization failed.");
        ComPtr<ID3D11RenderTargetView> mainTarget;
        auto createTarget=[&] { ComPtr<ID3D11Texture2D> buffer; Check(swap->GetBuffer(0,IID_PPV_ARGS(&buffer)),"Cannot get back buffer."); Check(device->CreateRenderTargetView(buffer.Get(),nullptr,&mainTarget),"Cannot create window render target."); };
        createTarget();
        IMGUI_CHECKVERSION(); ImGui::CreateContext(); ImGuiIO& io=ImGui::GetIO(); io.ConfigFlags|=ImGuiConfigFlags_NavEnableKeyboard;
        io.IniFilename=nullptr;
        LunaFlow::ConfigureUiTheme(dpi);
        wchar_t executable[MAX_PATH]{}; GetModuleFileNameW(nullptr,executable,MAX_PATH);
        LunaFlow::LoadUiFonts(std::filesystem::path(executable).parent_path()/L"Fonts");
        win32Ready=ImGui_ImplWin32_Init(window); dxReady=ImGui_ImplDX11_Init(device.Get(),context.Get());
        if (!win32Ready || !dxReady) throw std::runtime_error("ImGui backend initialization failed.");
        auto shaders=std::filesystem::path(executable).parent_path()/L"Shaders";
        if (!std::filesystem::exists(shaders/L"Skinned.hlsl")) shaders=std::filesystem::path(LUNAFLOW_SHADER_DIR);
        LunaFlow::EditorApp app(window,device.Get(),context.Get(),shaders);
        activeEditor=&app;
        int argc=0; LPWSTR* argv=CommandLineToArgvW(GetCommandLineW(),&argc);
        for (int i=1;i<argc;++i) if (std::wstring(argv[i])==L"--model" && i+1<argc) app.LoadModel(argv[++i]);
        if (argv) LocalFree(argv);
        ShowWindow(window,show); UpdateWindow(window);
        bool quit=false,occluded=false;
        while (!quit) {
            const auto frameStart=std::chrono::steady_clock::now();
            MSG msg{};
            while (PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageW(&msg); if (msg.message==WM_QUIT) quit=true; }
            if (quit) break;
            if (minimized || (occluded && swap->Present(0,DXGI_PRESENT_TEST)==DXGI_STATUS_OCCLUDED)) { Sleep(10); continue; }
            occluded=false;
            if (resizeWidth && resizeHeight) {
                context->OMSetRenderTargets(0,nullptr,nullptr); mainTarget.Reset();
                Check(swap->ResizeBuffers(0,resizeWidth,resizeHeight,DXGI_FORMAT_UNKNOWN,0),"Window resize failed.");
                resizeWidth=resizeHeight=0; createTarget();
            }
            const float windowDpi=ImGui_ImplWin32_GetDpiScaleForHwnd(window);
            if(std::abs(windowDpi-dpi)>.001f){LunaFlow::ConfigureUiTheme(windowDpi);dpi=windowDpi;}
            ImGui_ImplDX11_NewFrame(); ImGui_ImplWin32_NewFrame(); ImGui::NewFrame(); ImGuizmo::BeginFrame();
            LunaFlow::BeginContinuousInputFrame();app.Frame(io.DeltaTime);LunaFlow::WrapContinuousMouse(window);ImGui::Render();
            if(app.WantQuit())break;
            ID3D11RenderTargetView* target=mainTarget.Get(); context->OMSetRenderTargets(1,&target,nullptr);
            const float color[]={.043f,.051f,.075f,1}; context->ClearRenderTargetView(target,color); ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
            hr=swap->Present(app.Vsync()?1:0,0); occluded=hr==DXGI_STATUS_OCCLUDED; Check(hr,"The graphics device stopped responding.");
            if(app.FrameLimit()>0){const auto frameDuration=std::chrono::duration<double>(1.0/app.FrameLimit());std::this_thread::sleep_until(frameStart+std::chrono::duration_cast<std::chrono::steady_clock::duration>(frameDuration));}
        }
        activeEditor=nullptr;context->ClearState(); context->Flush();
    } catch (const std::exception& e) { activeEditor=nullptr;MessageBoxA(window,e.what(),"LunaFlow",MB_ICONERROR|MB_OK); result=1; }
    if (dxReady) ImGui_ImplDX11_Shutdown(); if (win32Ready) ImGui_ImplWin32_Shutdown();
    if (ImGui::GetCurrentContext()) ImGui::DestroyContext();
    if (window) DestroyWindow(window); UnregisterClassW(L"LunaFlowDX11",instance);
    if (SUCCEEDED(com)) CoUninitialize();
    return result;
}

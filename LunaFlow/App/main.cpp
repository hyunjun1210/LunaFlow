#include "App/EditorApp.h"
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include <shellapi.h>
#include <stdexcept>

using Microsoft::WRL::ComPtr;
static UINT resizeWidth=0,resizeHeight=0;
static bool minimized=false;
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND,UINT,WPARAM,LPARAM);
static LRESULT WINAPI WindowProc(HWND hwnd,UINT msg,WPARAM w,LPARAM l) {
    if (ImGui_ImplWin32_WndProcHandler(hwnd,msg,w,l)) return true;
    switch (msg) {
    case WM_SIZE: minimized=w==SIZE_MINIMIZED; if (!minimized) { resizeWidth=LOWORD(l); resizeHeight=HIWORD(l); } return 0;
    case WM_DPICHANGED: { const auto* r=reinterpret_cast<RECT*>(l); SetWindowPos(hwnd,nullptr,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER|SWP_NOACTIVATE); return 0; }
    case WM_GETMINMAXINFO: { auto* info=reinterpret_cast<MINMAXINFO*>(l); info->ptMinTrackSize={1000,700}; return 0; }
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
        WNDCLASSEXW wc{sizeof(wc),CS_CLASSDC,WindowProc,0,0,instance,nullptr,LoadCursor(nullptr,IDC_ARROW),nullptr,nullptr,L"LunaFlowDX11",nullptr};
        RegisterClassExW(&wc);
        const float dpi=ImGui_ImplWin32_GetDpiScaleForMonitor(MonitorFromPoint({0,0},MONITOR_DEFAULTTOPRIMARY));
        window=CreateWindowW(wc.lpszClassName,L"LunaFlow | Team A - DirectX 11",WS_OVERLAPPEDWINDOW,70,60,static_cast<int>(1440*dpi),static_cast<int>(940*dpi),nullptr,nullptr,instance,nullptr);
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
        ImGui::StyleColorsDark(); auto& style=ImGui::GetStyle();
        style.WindowRounding=0; style.ChildRounding=10; style.FrameRounding=6; style.GrabRounding=5;
        style.WindowPadding={18,16}; style.ItemSpacing={10,9}; style.FramePadding={10,7}; style.WindowBorderSize=0;
        style.Colors[ImGuiCol_WindowBg]={.043f,.051f,.075f,1}; style.Colors[ImGuiCol_ChildBg]={.065f,.077f,.106f,1};
        style.Colors[ImGuiCol_FrameBg]={.10f,.12f,.16f,1}; style.Colors[ImGuiCol_FrameBgHovered]={.16f,.18f,.26f,1};
        style.Colors[ImGuiCol_Button]={.16f,.15f,.27f,1}; style.Colors[ImGuiCol_ButtonHovered]={.29f,.23f,.45f,1};
        style.Colors[ImGuiCol_ButtonActive]={.39f,.29f,.62f,1}; style.Colors[ImGuiCol_Header]={.22f,.18f,.35f,1};
        style.Colors[ImGuiCol_HeaderHovered]={.28f,.23f,.42f,1}; style.Colors[ImGuiCol_HeaderActive]={.34f,.27f,.50f,1};
        style.Colors[ImGuiCol_CheckMark]={.69f,.52f,1,1}; style.Colors[ImGuiCol_SliderGrab]={.60f,.44f,.91f,1};
        style.Colors[ImGuiCol_Text]={.88f,.90f,.95f,1}; style.Colors[ImGuiCol_TextDisabled]={.45f,.51f,.62f,1};
        style.Colors[ImGuiCol_Border]={.16f,.18f,.24f,1}; style.ScaleAllSizes(dpi); style.FontScaleDpi=dpi;
        wchar_t windowsPath[MAX_PATH]{}; GetWindowsDirectoryW(windowsPath,MAX_PATH);
        const auto fontPath=std::filesystem::path(windowsPath)/L"Fonts"/L"malgun.ttf";
        if (std::filesystem::exists(fontPath)) io.Fonts->AddFontFromFileTTF(fontPath.u8string().c_str(),17,nullptr,io.Fonts->GetGlyphRangesKorean());
        else io.Fonts->AddFontDefault();
        win32Ready=ImGui_ImplWin32_Init(window); dxReady=ImGui_ImplDX11_Init(device.Get(),context.Get());
        if (!win32Ready || !dxReady) throw std::runtime_error("ImGui backend initialization failed.");
        wchar_t executable[MAX_PATH]{}; GetModuleFileNameW(nullptr,executable,MAX_PATH);
        auto shaders=std::filesystem::path(executable).parent_path()/L"Shaders";
        if (!std::filesystem::exists(shaders/L"Skinned.hlsl")) shaders=std::filesystem::path(LUNAFLOW_SHADER_DIR);
        LunaFlow::EditorApp app(window,device.Get(),context.Get(),shaders);
        int argc=0; LPWSTR* argv=CommandLineToArgvW(GetCommandLineW(),&argc);
        for (int i=1;i<argc;++i) if (std::wstring(argv[i])==L"--model" && i+1<argc) app.LoadModel(argv[++i]);
        if (argv) LocalFree(argv);
        ShowWindow(window,show); UpdateWindow(window);
        bool quit=false,occluded=false;
        while (!quit) {
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
            ImGui_ImplDX11_NewFrame(); ImGui_ImplWin32_NewFrame(); ImGui::NewFrame(); app.Frame(io.DeltaTime); ImGui::Render();
            ID3D11RenderTargetView* target=mainTarget.Get(); context->OMSetRenderTargets(1,&target,nullptr);
            const float color[]={.043f,.051f,.075f,1}; context->ClearRenderTargetView(target,color); ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
            hr=swap->Present(1,0); occluded=hr==DXGI_STATUS_OCCLUDED; Check(hr,"The graphics device stopped responding.");
        }
        context->ClearState(); context->Flush();
    } catch (const std::exception& e) { MessageBoxA(window,e.what(),"LunaFlow",MB_ICONERROR|MB_OK); result=1; }
    if (dxReady) ImGui_ImplDX11_Shutdown(); if (win32Ready) ImGui_ImplWin32_Shutdown();
    if (ImGui::GetCurrentContext()) ImGui::DestroyContext();
    if (window) DestroyWindow(window); UnregisterClassW(L"LunaFlowDX11",instance);
    if (SUCCEEDED(com)) CoUninitialize();
    return result;
}

#pragma once
#include "App/ContinuousInput.h"
#include "ImGuizmo.h"
#include "Rendering/RendererDX11.h"
#include <cmath>
#include <stdexcept>

inline void TestViewportUx() {
    using namespace LunaFlow;
    const auto check=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};
    const RECT monitor{-1920,-1080,0,0};
    auto point=WrappedMousePosition({-1,-540},monitor);
    check(point.x==-1917&&point.y==-540,"Right monitor edge wraps without vertical motion, including negative coordinates");
    point=WrappedMousePosition({-1920,-1080},monitor);
    check(point.x==-3&&point.y==-3,"Both monitor edges wrap at corners");
    point=WrappedMousePosition({-900,-500},monitor);
    check(point.x==-900&&point.y==-500,"Cursor remains unchanged away from monitor edges");

    PreviewCamera camera;camera.yaw=camera.pitch=0;camera.target={0,0,0};camera.sceneRadius=1;
    camera.Fly({0,0,1},.1f);
    check(std::abs(camera.target.z-.2f)<1e-5f,"W moves camera forward");
    camera.Fly({1,0,0},.1f);camera.Fly({0,1,0},.1f);
    check(std::abs(camera.target.x-.2f)<1e-5f&&std::abs(camera.target.y-.2f)<1e-5f,"D and E move camera right and up");
    PreviewCamera smallSteps=camera,largeSteps=camera;
    for(int i=0;i<10;++i)smallSteps.Fly({1,0,1},.01f);
    largeSteps.Fly({1,0,1},.1f);
    check(std::abs(smallSteps.target.x-largeSteps.target.x)<1e-5f&&std::abs(smallSteps.target.z-largeSteps.target.z)<1e-5f,"Fly speed is independent of frame rate");
    auto fast=camera;fast.Fly({0,0,1},.1f,true);
    check(std::abs(fast.target.z-camera.target.z-.6f)<1e-5f,"Shift accelerates the fly camera");
    auto diagonal=camera;diagonal.Fly({1,0,1},.1f);
    check(std::abs(std::hypot(diagonal.target.x-camera.target.x,diagonal.target.z-camera.target.z)-.2f)<1e-5f,"Diagonal fly speed is normalized");

    auto& io=ImGui::GetIO();io.ClearEventsQueue();io.ClearInputKeys();io.ClearInputMouse();
    io.MousePos=io.MousePosPrev={1918,200};io.MouseClickedPos[0]={1900,200};
    RebaseContinuousMouse({3,200});
    check(io.MousePos.x==3&&io.MousePosPrev.x==3&&io.MousePos.x-io.MouseClickedPos[0].x==18,"Cursor wrap preserves drag distance and prevents a delta spike");
    const auto previousDisplay=io.DisplaySize;
    XMFLOAT4X4 view,projection,matrix;
    XMStoreFloat4x4(&view,XMMatrixLookAtLH(XMVectorSet(0,0,-3,1),XMVectorZero(),XMVectorSet(0,1,0,0)));
    XMStoreFloat4x4(&projection,XMMatrixPerspectiveFovLH(XMConvertToRadians(42),800.0f/600,.01f,100));
    XMStoreFloat4x4(&matrix,XMMatrixIdentity());
    const auto frame=[&](ImVec2 mouse,int buttonChange=0){
        io.DisplaySize={800,600};io.DeltaTime=1.0f/60;io.AddMousePosEvent(mouse.x,mouse.y);
        if(buttonChange)io.AddMouseButtonEvent(0,buttonChange>0);
        ImGui::NewFrame();ImGuizmo::BeginFrame();
        ImGui::SetNextWindowPos({0,0});ImGui::SetNextWindowSize({800,600});
        ImGui::Begin("ViewportUxTest",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoSavedSettings);
        ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());ImGuizmo::SetRect(0,0,800,600);ImGuizmo::SetOrthographic(false);ImGuizmo::SetGizmoSizeClipSpace(.12f);ImGuizmo::Enable(true);ImGuizmo::SetContinuousRotation(true);
        const bool changed=ImGuizmo::Manipulate(&view._11,&projection._11,ImGuizmo::ROTATE,ImGuizmo::WORLD,&matrix._11);
        ImGui::End();ImGui::Render();return changed;
    };
    // Sweep diagonally (edge-on X/Y rings project to the center lines) to find the visible ring radius.
    ImVec2 ring{-1,-1};
    for(float radius=6;radius<=280&&ring.x<0;radius+=1){const ImVec2 probe{400+radius*.7071f,300+radius*.7071f};frame(probe);frame(probe);if(ImGuizmo::GetHoveredHandleType()!=ImGuizmo::MT_NONE)ring=probe;}
    check(ring.x>=0,"Rotation ring can be picked");
    frame(ring,1);check(ImGuizmo::IsUsing(),"Rotation drag begins on ring");
    check(frame({ring.x,ring.y+20}),"Rotation ring consumes mouse motion");
    check(frame({1200,900}),"Rotation continues outside the viewport and window");
    const auto before=matrix;
    check(frame({1200,2100}),"Rotation continues over multiple full turns");
    float rotationChange=0;for(int row=0;row<3;++row)for(int column=0;column<3;++column)rotationChange+=std::abs(matrix.m[row][column]-before.m[row][column]);
    check(rotationChange>.001f,"Multiple-turn drag changes actual object rotation");
    frame({1200,2100},-1);check(!ImGuizmo::IsUsing(),"Release outside viewport ends rotation drag");
    ImGuizmo::Enable(false);ImGuizmo::SetContinuousRotation(false);io.ClearInputMouse();io.DisplaySize=previousDisplay;
}

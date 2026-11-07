#pragma once
#include "imgui.h"
#include "imgui_internal.h"
#include <windows.h>

namespace LunaFlow {
// Only controls which consume mouse deltas opt in. Text fields, sliders and
// absolute-position handles retain their normal cursor behavior.
inline bool continuousMouseRequested=false;
inline void BeginContinuousInputFrame() { continuousMouseRequested=false; }
inline void RequestContinuousMouse() { continuousMouseRequested=true; }
inline void ContinuousDragItem() {
    const auto& g=*ImGui::GetCurrentContext();
    if(ImGui::IsItemActive()&&g.ActiveId!=g.TempInputId&&g.IO.MouseDown[0]&&!g.IO.WantTextInput)RequestContinuousMouse();
}
inline bool ContinuousDragFloat(const char* label,float* value,float speed=1,float low=0,float high=0,const char* format="%.3f",ImGuiSliderFlags flags=0) {
    const bool changed=ImGui::DragFloat(label,value,speed,low,high,format,flags);ContinuousDragItem();return changed;
}
inline bool ContinuousDragFloat3(const char* label,float* value,float speed=1,float low=0,float high=0,const char* format="%.3f",ImGuiSliderFlags flags=0) {
    const bool changed=ImGui::DragFloat3(label,value,speed,low,high,format,flags);ContinuousDragItem();return changed;
}
inline bool ContinuousDragInt(const char* label,int* value,float speed=1,int low=0,int high=0,const char* format="%d",ImGuiSliderFlags flags=0) {
    const bool changed=ImGui::DragInt(label,value,speed,low,high,format,flags);ContinuousDragItem();return changed;
}
inline bool ContinuousDragFloatRange2(const char* label,float* lowValue,float* highValue,float speed=1,float low=0,float high=0,const char* format="%.3f",const char* highFormat=nullptr,ImGuiSliderFlags flags=0) {
    const bool changed=ImGui::DragFloatRange2(label,lowValue,highValue,speed,low,high,format,highFormat,flags);ContinuousDragItem();return changed;
}
inline POINT WrappedMousePosition(POINT point,const RECT& r) {
    POINT wrapped=point;constexpr LONG margin=2;
    if(point.x<=r.left+margin)wrapped.x=r.right-margin-1;
    else if(point.x>=r.right-margin-1)wrapped.x=r.left+margin+1;
    if(point.y<=r.top+margin)wrapped.y=r.bottom-margin-1;
    else if(point.y>=r.bottom-margin-1)wrapped.y=r.top+margin+1;
    return wrapped;
}
inline void RebaseContinuousMouse(ImVec2 position) {
    auto& io=ImGui::GetIO();
    const ImVec2 shift{position.x-io.MousePos.x,position.y-io.MousePos.y};
    for(auto& clicked:io.MouseClickedPos){clicked.x+=shift.x;clicked.y+=shift.y;}
    io.MousePos=io.MousePosPrev=position;
    ImGui::GetCurrentContext()->MouseLastValidPos=position;
}
inline void WrapContinuousMouse(HWND window) {
    auto& io=ImGui::GetIO();
    if(!continuousMouseRequested||!window||GetForegroundWindow()!=window||GetCapture()!=window||io.AppFocusLost)return;
    if(!io.MouseDown[0]&&!io.MouseDown[1]&&!io.MouseDown[2])return;
    POINT point{};if(!GetCursorPos(&point))return;
    MONITORINFO monitor{sizeof(monitor)};
    if(!GetMonitorInfoW(MonitorFromPoint(point,MONITOR_DEFAULTTONEAREST),&monitor))return;
    const POINT wrapped=WrappedMousePosition(point,monitor.rcMonitor);
    if(wrapped.x==point.x&&wrapped.y==point.y)return;
    if(!SetCursorPos(wrapped.x,wrapped.y))return;
    POINT client=wrapped;if(!ScreenToClient(window,&client))return;
    // Move the previous sample with the cursor, so the wrap contributes zero
    // movement on the next frame. Subsequent physical motion remains intact.
    RebaseContinuousMouse({static_cast<float>(client.x),static_cast<float>(client.y)});
}
}

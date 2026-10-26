cmake_minimum_required(VERSION 3.24)
project(LunaFlow VERSION 0.1.0 LANGUAGES C CXX)
if(NOT WIN32)
  message(FATAL_ERROR "LunaFlow requires Windows and DirectX 11.")
endif()
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
include(FetchContent)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ASSIMP_TOOLS OFF CACHE BOOL "" FORCE)
set(ASSIMP_INSTALL OFF CACHE BOOL "" FORCE)
set(ASSIMP_WARNINGS_AS_ERRORS OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ALL_IMPORTERS_BY_DEFAULT OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_FBX_IMPORTER ON CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_GLTF_IMPORTER ON CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_OBJ_IMPORTER ON CACHE BOOL "" FORCE)
set(ASSIMP_NO_EXPORT ON CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ZLIB ON CACHE BOOL "" FORCE)
FetchContent_Declare(assimp URL https://github.com/assimp/assimp/archive/refs/tags/v6.0.4.tar.gz
  URL_HASH SHA256=afa5487efdd285661afa842c85187cd8c541edad92e8d4aa85be4fca7476eccc DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_Declare(json URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
  URL_HASH SHA256=42f6e95cad6ec532fd372391373363b62a14af6d771056dbfc86160e6dfff7aa DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(assimp json)

add_library(lunaflow_core
  LunaFlow/Core/Animation.cpp LunaFlow/Core/SceneImporter.cpp LunaFlow/Core/ClipIO.cpp
  LunaFlow/Core/DemoScene.cpp LunaFlow/Core/Scene.h)
target_include_directories(lunaflow_core PUBLIC LunaFlow)
target_link_libraries(lunaflow_core PUBLIC assimp::assimp nlohmann_json::nlohmann_json)
target_compile_definitions(lunaflow_core PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)

add_library(lunaflow_render LunaFlow/Rendering/RendererDX11.cpp LunaFlow/Rendering/RendererDX11.h LunaFlow/Rendering/Shaders/Skinned.hlsl)
set_source_files_properties(LunaFlow/Rendering/Shaders/Skinned.hlsl PROPERTIES HEADER_FILE_ONLY TRUE)
target_link_libraries(lunaflow_render PUBLIC lunaflow_core d3d11 d3dcompiler dxgi windowscodecs ole32)

add_library(lunaflow_imgui imgui.cpp imgui_draw.cpp imgui_tables.cpp imgui_widgets.cpp
  backends/imgui_impl_win32.cpp backends/imgui_impl_dx11.cpp)
target_include_directories(lunaflow_imgui PUBLIC . backends)
target_link_libraries(lunaflow_imgui PUBLIC d3d11 dwmapi)

add_executable(LunaFlow WIN32 LunaFlow/App/main.cpp LunaFlow/App/EditorApp.cpp LunaFlow/App/EditorApp.h
  LunaFlow/README.ko.md LunaFlow/THIRD_PARTY_NOTICES.md LunaFlow/Assets/TeamC_MotionExample.json)
target_link_libraries(LunaFlow PRIVATE lunaflow_render lunaflow_imgui comdlg32 shell32)
set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT LunaFlow)
target_compile_definitions(LunaFlow PRIVATE LUNAFLOW_SHADER_DIR=L"${CMAKE_CURRENT_SOURCE_DIR}/LunaFlow/Rendering/Shaders")
add_custom_command(TARGET LunaFlow POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/LunaFlow/Rendering/Shaders" "$<TARGET_FILE_DIR:LunaFlow>/Shaders")
add_custom_command(TARGET LunaFlow POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:LunaFlow>/licenses"
  COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE.txt" "$<TARGET_FILE_DIR:LunaFlow>/licenses/DearImGui.txt"
  COMMAND ${CMAKE_COMMAND} -E copy "${assimp_SOURCE_DIR}/LICENSE" "$<TARGET_FILE_DIR:LunaFlow>/licenses/Assimp.txt"
  COMMAND ${CMAKE_COMMAND} -E copy "${json_SOURCE_DIR}/LICENSE.MIT" "$<TARGET_FILE_DIR:LunaFlow>/licenses/JSON.txt"
  COMMAND ${CMAKE_COMMAND} -E copy "${assimp_SOURCE_DIR}/contrib/zlib/README" "$<TARGET_FILE_DIR:LunaFlow>/licenses/zlib.txt")

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/LunaFlow" PREFIX LunaFlow FILES
  LunaFlow/App/main.cpp LunaFlow/App/EditorApp.cpp)

enable_testing()
add_executable(LunaFlowTests LunaFlow/Tests/CoreTests.cpp)
target_link_libraries(LunaFlowTests PRIVATE lunaflow_render)
target_compile_definitions(LunaFlowTests PRIVATE LUNAFLOW_SHADER_DIR=L"${CMAKE_CURRENT_SOURCE_DIR}/LunaFlow/Rendering/Shaders"
  LUNAFLOW_SOURCE_DIR=L"${CMAKE_CURRENT_SOURCE_DIR}/LunaFlow" ASSIMP_TEST_MODELS=L"${assimp_SOURCE_DIR}/test/models")
add_test(NAME LunaFlow.CoreAndDX11 COMMAND LunaFlowTests)
if(MSVC)
  foreach(t lunaflow_core lunaflow_render lunaflow_imgui LunaFlow LunaFlowTests)
    target_compile_options(${t} PRIVATE /utf-8 /W4 /permissive-)
  endforeach()
endif()

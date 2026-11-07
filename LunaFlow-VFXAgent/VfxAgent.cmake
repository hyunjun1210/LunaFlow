include_guard(GLOBAL)
set(LUNAFLOW_VFX_AGENT_DIR "${CMAKE_CURRENT_LIST_DIR}")
target_sources(lunaflow_core PRIVATE
  "${LUNAFLOW_VFX_AGENT_DIR}/VfxAgent.cpp"
  "${LUNAFLOW_VFX_AGENT_DIR}/VfxAgent.h")
target_include_directories(lunaflow_core PUBLIC "${LUNAFLOW_VFX_AGENT_DIR}")
target_sources(lunaflow_editor PRIVATE "${LUNAFLOW_VFX_AGENT_DIR}/EditorVfxAgent.cpp")
add_executable(LunaFlowVfxAgentTests "${LUNAFLOW_VFX_AGENT_DIR}/Tests/VfxAgentTests.cpp")
target_link_libraries(LunaFlowVfxAgentTests PRIVATE lunaflow_editor)
target_compile_definitions(LunaFlowVfxAgentTests PRIVATE
  LUNAFLOW_SOURCE_DIR=L"${CMAKE_CURRENT_SOURCE_DIR}/LunaFlow")
if(MSVC)
  target_compile_options(LunaFlowVfxAgentTests PRIVATE /utf-8 /W4 /permissive-)
endif()
add_test(NAME LunaFlow.VfxAgent COMMAND LunaFlowVfxAgentTests)

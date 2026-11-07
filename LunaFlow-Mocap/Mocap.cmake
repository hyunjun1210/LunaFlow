include_guard(GLOBAL)
set(LUNAFLOW_MOCAP_DIR "${CMAKE_CURRENT_LIST_DIR}")
add_library(lunaflow_mocap
  "${LUNAFLOW_MOCAP_DIR}/Mocap.cpp" "${LUNAFLOW_MOCAP_DIR}/Mocap.h"
  "${LUNAFLOW_MOCAP_DIR}/TrackerProcess.cpp" "${LUNAFLOW_MOCAP_DIR}/TrackerProcess.h")
target_include_directories(lunaflow_mocap PUBLIC "${LUNAFLOW_MOCAP_DIR}")
target_link_libraries(lunaflow_mocap PUBLIC lunaflow_core)
target_link_libraries(lunaflow_editor PUBLIC lunaflow_mocap)
target_sources(lunaflow_editor PRIVATE "${LUNAFLOW_MOCAP_DIR}/EditorMocap.cpp")
target_compile_definitions(lunaflow_editor PRIVATE LUNAFLOW_MOCAP_DIR=L"${LUNAFLOW_MOCAP_DIR}")
add_executable(LunaFlowMocapTests "${LUNAFLOW_MOCAP_DIR}/Tests/MocapTests.cpp")
target_link_libraries(LunaFlowMocapTests PRIVATE lunaflow_editor)
target_compile_definitions(LunaFlowMocapTests PRIVATE LUNAFLOW_SOURCE_DIR=L"${CMAKE_CURRENT_SOURCE_DIR}/LunaFlow"
  LUNAFLOW_MOCAP_DIR=L"${LUNAFLOW_MOCAP_DIR}")
if(MSVC)
  target_compile_options(lunaflow_mocap PRIVATE /utf-8 /W4 /permissive-)
  target_compile_options(LunaFlowMocapTests PRIVATE /utf-8 /W4 /permissive-)
endif()
find_package(Python3 QUIET COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
  add_test(NAME LunaFlow.Mocap COMMAND LunaFlowMocapTests "${Python3_EXECUTABLE}")
  add_test(NAME LunaFlow.MocapTracker COMMAND "${Python3_EXECUTABLE}" -B -m unittest discover
    -s "${LUNAFLOW_MOCAP_DIR}/Tests" -p test_tracker.py)
else()
  add_test(NAME LunaFlow.Mocap COMMAND LunaFlowMocapTests)
  message(STATUS "Python not found: mocap Python/process fixture tests unavailable until Python is installed.")
endif()
if(EXISTS "${LUNAFLOW_MOCAP_DIR}/.venv/Scripts/python.exe" AND
   EXISTS "${LUNAFLOW_MOCAP_DIR}/Models/pose_landmarker_lite.task")
  add_test(NAME LunaFlow.MocapModelSmoke COMMAND LunaFlowMocapTests
    "${LUNAFLOW_MOCAP_DIR}/.venv/Scripts/python.exe" --model-smoke)
  set_tests_properties(LunaFlow.MocapModelSmoke PROPERTIES TIMEOUT 90)
endif()

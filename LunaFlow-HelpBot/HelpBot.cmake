include_guard(GLOBAL)
set(LUNAFLOW_HELP_BOT_DIR "${CMAKE_CURRENT_LIST_DIR}")
target_sources(lunaflow_core PRIVATE
  "${LUNAFLOW_HELP_BOT_DIR}/HelpBot.cpp"
  "${LUNAFLOW_HELP_BOT_DIR}/HelpBot.h")
target_include_directories(lunaflow_core PUBLIC "${LUNAFLOW_HELP_BOT_DIR}")
target_sources(lunaflow_editor PRIVATE "${LUNAFLOW_HELP_BOT_DIR}/EditorHelpBot.cpp")
# The bundled manual answers offline and is the knowledge file uploaded to the Copilot Studio help agent.
add_custom_target(LunaFlowHelpKnowledge ALL
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${LUNAFLOW_HELP_BOT_DIR}/Knowledge" "$<TARGET_FILE_DIR:LunaFlow>/HelpBot")
add_dependencies(LunaFlow LunaFlowHelpKnowledge)
add_executable(LunaFlowHelpBotTests "${LUNAFLOW_HELP_BOT_DIR}/Tests/HelpBotTests.cpp")
target_link_libraries(LunaFlowHelpBotTests PRIVATE lunaflow_editor)
add_dependencies(LunaFlowHelpBotTests LunaFlowAssets LunaFlowHelpKnowledge)
target_compile_definitions(LunaFlowHelpBotTests PRIVATE
  LUNAFLOW_SOURCE_DIR=L"${CMAKE_CURRENT_SOURCE_DIR}/LunaFlow"
  LUNAFLOW_HELP_KNOWLEDGE=L"${LUNAFLOW_HELP_BOT_DIR}/Knowledge/LunaFlow_Help.ko.md")
if(MSVC)
  target_compile_options(LunaFlowHelpBotTests PRIVATE /utf-8 /W4 /permissive-)
endif()
add_test(NAME LunaFlow.HelpBot COMMAND LunaFlowHelpBotTests)

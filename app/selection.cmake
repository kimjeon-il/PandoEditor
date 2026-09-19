# M3.1 component targets. Included after the existing application targets.
target_sources(pandoeditor_editor PRIVATE "${PROJECT_SOURCE_DIR}/app/editorselection.cpp")
if(BUILD_TESTING)
    add_executable(selection_probe "${PROJECT_SOURCE_DIR}/tests/selection_probe.cpp")
    target_link_libraries(selection_probe PRIVATE Pandoeditor::Core Qt6::Core)
    add_test(NAME selection_state_tests COMMAND selection_probe --self-test)
    find_program(PANDOEDITOR_NODE_EXECUTABLE NAMES node)
    if(PANDOEDITOR_NODE_EXECUTABLE)
        add_test(NAME selection_web_parity
            COMMAND "${PANDOEDITOR_NODE_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/m3-selection-oracle.mjs" "$<TARGET_FILE:selection_probe>")
    else()
        message(WARNING "Node.js missing: pinned-web differential selection test is not registered")
    endif()
endif()

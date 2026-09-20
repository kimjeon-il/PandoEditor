# M3.1 component targets. Included after the existing application targets.
target_sources(pandoeditor_editor PRIVATE "${PROJECT_SOURCE_DIR}/app/editorselection.cpp" "${PROJECT_SOURCE_DIR}/app/editorpicking.cpp")
if(BUILD_TESTING)
    add_executable(selection_probe "${PROJECT_SOURCE_DIR}/tests/selection_probe.cpp")
    target_link_libraries(selection_probe PRIVATE Pandoeditor::Core Qt6::Core)
    add_test(NAME selection_state_tests COMMAND selection_probe --self-test)
    find_program(PANDOEDITOR_NODE_EXECUTABLE NAMES node)
    if(PANDOEDITOR_NODE_EXECUTABLE)
        add_test(NAME m4_geometry_web_parity COMMAND "${PANDOEDITOR_NODE_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/m4-geometry-oracle.mjs" "$<TARGET_FILE:m4_geometry_probe>")
        add_test(NAME presentation_web_parity COMMAND "${PANDOEDITOR_NODE_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/m34-presentation-oracle.mjs" "$<TARGET_FILE:presentation_parity_probe>")
        add_test(NAME selection_web_parity
            COMMAND "${PANDOEDITOR_NODE_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/m3-selection-oracle.mjs" "$<TARGET_FILE:selection_probe>")
    else()
        message(WARNING "Node.js missing: pinned-web differential selection test is not registered")
    endif()
endif()

if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test QuickControls2)
    add_executable(selection_ui_tests "${PROJECT_SOURCE_DIR}/tests/selection_ui_tests.cpp")
    target_link_libraries(selection_ui_tests PRIVATE pandoeditor_editor Qt6::QuickControls2 Qt6::Test)
    qt_add_resources(selection_ui_tests ui PREFIX "/" BASE "${PROJECT_SOURCE_DIR}/ui"
        FILES "${PROJECT_SOURCE_DIR}/ui/common/Main.qml"
              "${PROJECT_SOURCE_DIR}/ui/common/EditorPanel.qml"
        "${PROJECT_SOURCE_DIR}/ui/common/ObjectSelectionButton.qml"
        "${PROJECT_SOURCE_DIR}/ui/common/ObjectSearch.qml"
        "${PROJECT_SOURCE_DIR}/ui/common/ObjectChooser.qml"
              "${PROJECT_SOURCE_DIR}/ui/common/MapView.qml"
              "${PROJECT_SOURCE_DIR}/ui/common/WebImportDialog.qml"
              "${PROJECT_SOURCE_DIR}/ui/desktop/DesktopWorkspace.qml"
              "${PROJECT_SOURCE_DIR}/ui/desktop/CaptionButton.qml")
    qt_add_resources(selection_ui_tests sample PREFIX "/assets" BASE "${PROJECT_SOURCE_DIR}/assets"
        FILES "${PROJECT_SOURCE_DIR}/assets/sample.pando.json")
    add_test(NAME selection_ui_tests COMMAND selection_ui_tests -o -,txt)
    set_tests_properties(selection_ui_tests PROPERTIES TIMEOUT 90 ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
endif()

if(BUILD_TESTING)
    add_executable(property_tests "${PROJECT_SOURCE_DIR}/tests/property_tests.cpp")
    target_link_libraries(property_tests PRIVATE pandoeditor_editor Qt6::Test)
    target_compile_definitions(property_tests PRIVATE M32_FIXTURES="${PROJECT_SOURCE_DIR}/tests/fixtures/web-properties")
    qt_add_resources(property_tests sample PREFIX "/assets" BASE "${PROJECT_SOURCE_DIR}/assets" FILES "${PROJECT_SOURCE_DIR}/assets/sample.pando.json")
    add_test(NAME property_tests COMMAND property_tests -o -,txt)
endif()

target_sources(pandoeditor_editor PRIVATE "${PROJECT_SOURCE_DIR}/app/editorproperties.cpp")

if(BUILD_TESTING)
    add_executable(property_ui_tests "${PROJECT_SOURCE_DIR}/tests/property_ui_tests.cpp")
    target_link_libraries(property_ui_tests PRIVATE pandoeditor_editor Qt6::QuickControls2 Qt6::Test)
    file(GLOB_RECURSE M32_UI_FILES CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/ui/*.qml" "${PROJECT_SOURCE_DIR}/ui/*.js" "${PROJECT_SOURCE_DIR}/ui/*.json")
    qt_add_resources(property_ui_tests property_test_ui PREFIX "/" BASE "${PROJECT_SOURCE_DIR}/ui" FILES ${M32_UI_FILES})
    qt_add_resources(property_ui_tests sample PREFIX "/assets" BASE "${PROJECT_SOURCE_DIR}/assets" FILES "${PROJECT_SOURCE_DIR}/assets/sample.pando.json")
    add_test(NAME property_ui_tests COMMAND property_ui_tests -o -,txt)
    set_tests_properties(property_ui_tests PROPERTIES TIMEOUT 90 ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
endif()

set(M32_PROPERTY_RESOURCES
    "${PROJECT_SOURCE_DIR}/ui/common/MapDisplayControls.qml"
    "${PROJECT_SOURCE_DIR}/ui/common/ColorMath.js"
    "${PROJECT_SOURCE_DIR}/ui/common/ColorPalette.js"
    "${PROJECT_SOURCE_DIR}/ui/common/CustomColorEditor.qml"
    "${PROJECT_SOURCE_DIR}/ui/common/ObjectColorPicker.qml"
    "${PROJECT_SOURCE_DIR}/ui/common/TerritorialSelectionToolbar.qml"
    "${PROJECT_SOURCE_DIR}/ui/common/ObjectNotesPopover.qml"
    "${PROJECT_SOURCE_DIR}/ui/common/RegionValidityFields.qml"
    "${PROJECT_SOURCE_DIR}/ui/common/MultiObjectProperties.qml"
    "${PROJECT_SOURCE_DIR}/ui/common/ObjectPropertyPanel.qml")
foreach(target_name pandoeditor ui_tests web_import_ui_tests selection_ui_tests)
    if(TARGET ${target_name})
        qt_add_resources(${target_name} m32_ui PREFIX "/" BASE "${PROJECT_SOURCE_DIR}/ui" FILES ${M32_PROPERTY_RESOURCES})
    endif()
endforeach()

if(BUILD_TESTING)
    add_executable(property_probe "${PROJECT_SOURCE_DIR}/tests/property_probe.cpp")
    target_link_libraries(property_probe PRIVATE pandoeditor_editor Qt6::Qml)
    target_compile_definitions(property_probe PRIVATE M32_ROOT="${PROJECT_SOURCE_DIR}")
    if(PANDOEDITOR_NODE_EXECUTABLE)
        add_test(NAME property_web_parity COMMAND "${PANDOEDITOR_NODE_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/m32-property-oracle.mjs" "$<TARGET_FILE:property_probe>")
    endif()
endif()

target_sources(pandoeditor_editor PRIVATE "${PROJECT_SOURCE_DIR}/platform/screencolorpicker.cpp" "${PROJECT_SOURCE_DIR}/platform/screencolorpicker.h")

if(BUILD_TESTING)
    add_executable(screen_color_tests "${PROJECT_SOURCE_DIR}/tests/screen_color_tests.cpp" "${PROJECT_SOURCE_DIR}/platform/screencolorpicker.cpp" "${PROJECT_SOURCE_DIR}/platform/screencolorpicker.h")
    target_include_directories(screen_color_tests PRIVATE "${PROJECT_SOURCE_DIR}/platform")
    target_link_libraries(screen_color_tests PRIVATE Qt6::Gui Qt6::Test)
    add_test(NAME screen_color_tests COMMAND screen_color_tests -o -,txt)
    set_tests_properties(screen_color_tests PROPERTIES TIMEOUT 30 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()

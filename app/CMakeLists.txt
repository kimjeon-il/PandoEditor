find_package(Qt6 REQUIRED COMPONENTS Concurrent)
qt_add_executable(pandoeditor main.cpp)

add_library(pandoeditor_editor STATIC
    webimport.cpp webmigration.cpp editorwebimport.cpp
    editorcontroller.cpp editorcommands.cpp editorjobs.cpp editorfields.cpp commandjobrunner.cpp commandjobrunner.h editorcontroller.h platformstorage.cpp platformstorage.h projectcodec.cpp
    windowsframe.cpp windowsframe.h
    ../renderer/mapprojection.cpp
)
target_include_directories(pandoeditor_editor PUBLIC . ../renderer)
target_link_libraries(pandoeditor_editor PUBLIC Pandoeditor::Core Qt6::Quick Qt6::Concurrent)
if(WIN32)
    target_link_libraries(pandoeditor_editor PRIVATE dwmapi user32)
endif()
target_link_libraries(pandoeditor PRIVATE pandoeditor_editor Qt6::QuickControls2)

qt_add_resources(pandoeditor ui
    PREFIX "/"
    BASE "${PROJECT_SOURCE_DIR}/ui"
    FILES
        "${PROJECT_SOURCE_DIR}/ui/common/Main.qml"
        "${PROJECT_SOURCE_DIR}/ui/common/WebImportDialog.qml"
        "${PROJECT_SOURCE_DIR}/ui/desktop/CaptionButton.qml"
        "${PROJECT_SOURCE_DIR}/ui/desktop/DesktopWorkspace.qml"
        "${PROJECT_SOURCE_DIR}/ui/common/MapView.qml"
        "${PROJECT_SOURCE_DIR}/ui/common/EditorPanel.qml"
)

qt_add_resources(pandoeditor sample PREFIX "/assets" BASE "${PROJECT_SOURCE_DIR}/assets"
    FILES "${PROJECT_SOURCE_DIR}/assets/sample.pando.json")

if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    foreach(test_name web_import_tests web_import_editor_tests selection_editor_tests)
        add_executable(${test_name} ../tests/${test_name}.cpp)
        target_link_libraries(${test_name} PRIVATE pandoeditor_editor Qt6::Test)
        target_compile_definitions(${test_name} PRIVATE WEB_IMPORT_FIXTURES="${PROJECT_SOURCE_DIR}/tests/fixtures/web-import")
        qt_add_resources(${test_name} sample PREFIX "/assets" BASE "${PROJECT_SOURCE_DIR}/assets"
            FILES "${PROJECT_SOURCE_DIR}/assets/sample.pando.json")
        add_test(NAME ${test_name} COMMAND ${test_name} -o -,txt)
    endforeach()
    add_executable(migration_tests ../tests/migration_tests.cpp)
    target_link_libraries(migration_tests PRIVATE pandoeditor_editor Qt6::Test)
    qt_add_resources(migration_tests sample PREFIX "/assets" BASE "${PROJECT_SOURCE_DIR}/assets"
        FILES "${PROJECT_SOURCE_DIR}/assets/sample.pando.json")
    add_test(NAME migration_tests COMMAND migration_tests -o -,txt)
    add_executable(storage_tests ../tests/storage_tests.cpp)
    target_link_libraries(storage_tests PRIVATE pandoeditor_editor Qt6::Test)
    qt_add_resources(storage_tests sample PREFIX "/assets" BASE "${PROJECT_SOURCE_DIR}/assets"
        FILES "${PROJECT_SOURCE_DIR}/assets/sample.pando.json")
    add_test(NAME storage_tests COMMAND storage_tests -o -,txt)
    add_executable(editor_tests ../tests/editor_tests.cpp)
    target_link_libraries(editor_tests PRIVATE pandoeditor_editor)
    qt_add_resources(editor_tests sample PREFIX "/assets" BASE "${PROJECT_SOURCE_DIR}/assets"
        FILES "${PROJECT_SOURCE_DIR}/assets/sample.pando.json")
    add_test(NAME editor_tests COMMAND editor_tests)
    add_executable(ui_tests ../tests/ui_tests.cpp)
    target_link_libraries(ui_tests PRIVATE pandoeditor_editor Qt6::QuickControls2 Qt6::Test)
    qt_add_resources(ui_tests ui PREFIX "/" BASE "${PROJECT_SOURCE_DIR}/ui"
        FILES "${PROJECT_SOURCE_DIR}/ui/common/Main.qml"
        "${PROJECT_SOURCE_DIR}/ui/common/WebImportDialog.qml"
              "${PROJECT_SOURCE_DIR}/ui/desktop/CaptionButton.qml"
              "${PROJECT_SOURCE_DIR}/ui/common/MapView.qml"
              "${PROJECT_SOURCE_DIR}/ui/common/EditorPanel.qml"
              "${PROJECT_SOURCE_DIR}/ui/desktop/DesktopWorkspace.qml")
    qt_add_resources(ui_tests sample PREFIX "/assets" BASE "${PROJECT_SOURCE_DIR}/assets"
        FILES "${PROJECT_SOURCE_DIR}/assets/sample.pando.json")
    add_test(NAME ui_tests COMMAND ui_tests -o -,txt)
    set_tests_properties(ui_tests PROPERTIES TIMEOUT 60 ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
endif()

set_target_properties(pandoeditor PROPERTIES
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE TRUE
)

if(ANDROID)
    include("${PROJECT_SOURCE_DIR}/platform/android/packaging.cmake")
endif()

if(BUILD_TESTING)
    add_executable(command_editor_tests ../tests/command_editor_tests.cpp)
    target_link_libraries(command_editor_tests PRIVATE pandoeditor_editor Qt6::Test)
    qt_add_resources(command_editor_tests sample PREFIX "/assets" BASE "${PROJECT_SOURCE_DIR}/assets"
        FILES "${PROJECT_SOURCE_DIR}/assets/sample.pando.json")
    add_test(NAME command_editor_tests COMMAND command_editor_tests -o -,txt)
endif()

if(BUILD_TESTING)
    add_executable(job_editor_tests ../tests/job_editor_tests.cpp)
    target_link_libraries(job_editor_tests PRIVATE pandoeditor_editor Qt6::Test)
    qt_add_resources(job_editor_tests sample PREFIX "/assets" BASE "${PROJECT_SOURCE_DIR}/assets"
        FILES "${PROJECT_SOURCE_DIR}/assets/sample.pando.json")
    add_test(NAME job_editor_tests COMMAND job_editor_tests -o -,txt)
endif()

if(BUILD_TESTING)
    add_executable(job_runner_tests ../tests/job_runner_tests.cpp)
    target_link_libraries(job_runner_tests PRIVATE pandoeditor_editor Qt6::Test)
    add_test(NAME job_runner_tests COMMAND job_runner_tests -o -,txt)
    set_tests_properties(job_runner_tests job_editor_tests PROPERTIES TIMEOUT 30)
endif()

if(BUILD_TESTING)
    add_executable(web_import_ui_tests ../tests/web_import_ui_tests.cpp)
    target_link_libraries(web_import_ui_tests PRIVATE pandoeditor_editor Qt6::QuickControls2 Qt6::Test)
    target_compile_definitions(web_import_ui_tests PRIVATE WEB_IMPORT_FIXTURES="${PROJECT_SOURCE_DIR}/tests/fixtures/web-import")
    qt_add_resources(web_import_ui_tests ui PREFIX "/" BASE "${PROJECT_SOURCE_DIR}/ui"
        FILES "${PROJECT_SOURCE_DIR}/ui/common/Main.qml"
        "${PROJECT_SOURCE_DIR}/ui/common/WebImportDialog.qml"
              "${PROJECT_SOURCE_DIR}/ui/desktop/CaptionButton.qml"
              "${PROJECT_SOURCE_DIR}/ui/common/MapView.qml"
              "${PROJECT_SOURCE_DIR}/ui/common/EditorPanel.qml"
              "${PROJECT_SOURCE_DIR}/ui/desktop/DesktopWorkspace.qml")
    qt_add_resources(web_import_ui_tests sample PREFIX "/assets" BASE "${PROJECT_SOURCE_DIR}/assets"
        FILES "${PROJECT_SOURCE_DIR}/assets/sample.pando.json")
    add_test(NAME web_import_ui_tests COMMAND web_import_ui_tests -o -,txt)
    set_tests_properties(web_import_ui_tests PROPERTIES TIMEOUT 45 ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
endif()

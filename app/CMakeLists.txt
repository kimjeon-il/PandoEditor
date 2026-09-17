qt_add_executable(pandoeditor main.cpp)

add_library(pandoeditor_editor STATIC
    editorcontroller.cpp editorcontroller.h platformstorage.cpp platformstorage.h projectcodec.cpp
    ../renderer/mapprojection.cpp
)
target_include_directories(pandoeditor_editor PUBLIC . ../renderer)
target_link_libraries(pandoeditor_editor PUBLIC Pandoeditor::Core Qt6::Quick)
target_link_libraries(pandoeditor PRIVATE pandoeditor_editor Qt6::QuickControls2)

qt_add_resources(pandoeditor ui
    PREFIX "/"
    BASE "${PROJECT_SOURCE_DIR}/ui"
    FILES
        "${PROJECT_SOURCE_DIR}/ui/common/Main.qml"
        "${PROJECT_SOURCE_DIR}/ui/desktop/DesktopWorkspace.qml"
        "${PROJECT_SOURCE_DIR}/ui/common/MapView.qml"
        "${PROJECT_SOURCE_DIR}/ui/common/EditorPanel.qml"
)

qt_add_resources(pandoeditor sample PREFIX "/assets" BASE "${PROJECT_SOURCE_DIR}/assets"
    FILES "${PROJECT_SOURCE_DIR}/assets/sample.pando.json")

if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)
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

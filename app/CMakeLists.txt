find_package(Qt6 REQUIRED COMPONENTS Concurrent)
find_package(Qt6 REQUIRED COMPONENTS Svg)
find_package(ZLIB REQUIRED)
qt_add_executable(pandoeditor main.cpp)

add_library(pandoeditor_editor STATIC
    referencewarp.cpp referencewarp.h
    referencetracing.cpp referencetracing.h
    referenceimagelibrary.cpp referenceimagelibrary.h
    defaultflagresolver.cpp defaultflagresolver.h
    hydrodataprovider.cpp hydrodataprovider.h
    hydromanifest.cpp hydromanifest.h
    hydroassetreader.cpp hydroassetreader.h
    hydrometadata.cpp hydrometadata.h
    hydroshardreader.cpp hydroshardreader.h
    geometrycalculator.cpp geometrycalculator.h
    territorialgeometry.cpp territorialgeometry.h
    editorpresentation.cpp
    webimport.cpp webmigration.cpp retainedreferencerewriter.cpp editorwebimport.cpp editorstructure.cpp
    editorgeometry.cpp
    editorcontent.cpp
    editorcontroller.cpp editorcommands.cpp editorjobs.cpp editorfields.cpp commandjobrunner.cpp commandjobrunner.h editorcontroller.h platformstorage.cpp platformstorage.h projectcodec.cpp
    windowsframe.cpp windowsframe.h
    ../renderer/mapprojection.cpp
    ../renderer/maprenderitem.cpp ../renderer/maprenderitem.h
    ../renderer/referenceimageitem.cpp ../renderer/referenceimageitem.h
)
target_include_directories(pandoeditor_editor PUBLIC . ../renderer)
target_link_libraries(pandoeditor_editor PUBLIC Pandoeditor::Core Qt6::Quick Qt6::Concurrent Qt6::Svg ZLIB::ZLIB)
qt_add_resources(pandoeditor_editor m4_geometry
    PREFIX "/geometry" BASE "${PROJECT_SOURCE_DIR}/assets/geometry"
    FILES "${PROJECT_SOURCE_DIR}/assets/geometry/polygon-clipping-0.15.7.js"
          "${PROJECT_SOURCE_DIR}/assets/geometry/LICENSE.polygon-clipping.txt")
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
        "${PROJECT_SOURCE_DIR}/ui/common/ObjectSelectionButton.qml"
        "${PROJECT_SOURCE_DIR}/ui/common/ObjectSearch.qml"
        "${PROJECT_SOURCE_DIR}/ui/common/ObjectChooser.qml"
)

qt_add_resources(pandoeditor sample PREFIX "/assets" BASE "${PROJECT_SOURCE_DIR}/assets"
    FILES "${PROJECT_SOURCE_DIR}/assets/sample.pando.json")
file(GLOB_RECURSE DEFAULT_FLAG_FILES CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/assets/defaults/flags/*")
qt_add_resources(pandoeditor default_flags PREFIX "/defaults/flags" BASE "${PROJECT_SOURCE_DIR}/assets/defaults/flags" FILES ${DEFAULT_FLAG_FILES})

if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    add_executable(hydro_manifest_tests ../tests/hydro_manifest_tests.cpp)
    target_link_libraries(hydro_manifest_tests PRIVATE pandoeditor_editor Qt6::Test)
    target_compile_definitions(hydro_manifest_tests PRIVATE WEB_HYDRO_FIXTURE="${PROJECT_SOURCE_DIR}/tests/fixtures/web-hydro")
    add_test(NAME hydro_manifest_tests COMMAND hydro_manifest_tests -o -,txt)
    add_executable(hydro_probe ../tests/hydro_probe.cpp)
    target_link_libraries(hydro_probe PRIVATE pandoeditor_editor)
    add_executable(geometry_calculator_tests ../tests/geometry_calculator_tests.cpp)
    target_link_libraries(geometry_calculator_tests PRIVATE pandoeditor_editor Qt6::Test)
    add_test(NAME geometry_calculator_tests COMMAND geometry_calculator_tests -o -,txt)
    add_executable(map_render_tests ../tests/map_render_tests.cpp)
    target_link_libraries(map_render_tests PRIVATE pandoeditor_editor Qt6::Test)
    add_test(NAME map_render_tests COMMAND map_render_tests -o -,txt)
    set_tests_properties(map_render_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
    add_executable(reference_image_tests ../tests/reference_image_tests.cpp)
    target_link_libraries(reference_image_tests PRIVATE pandoeditor_editor Qt6::Test)
    add_test(NAME reference_image_tests COMMAND reference_image_tests -o -,txt)
    add_executable(territorial_geometry_tests ../tests/territorial_geometry_tests.cpp)
    add_executable(m4_geometry_probe ../tests/m4_geometry_probe.cpp)
    target_link_libraries(m4_geometry_probe PRIVATE pandoeditor_editor)
    target_link_libraries(territorial_geometry_tests PRIVATE pandoeditor_editor Qt6::Test)
    qt_add_resources(territorial_geometry_tests sample PREFIX "/assets" BASE "${PROJECT_SOURCE_DIR}/assets"
        FILES "${PROJECT_SOURCE_DIR}/assets/sample.pando.json")
    add_test(NAME territorial_geometry_tests COMMAND territorial_geometry_tests -o -,txt)
    set_tests_properties(territorial_geometry_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
    add_executable(retained_reference_tests ../tests/retained_reference_tests.cpp)
    target_link_libraries(retained_reference_tests PRIVATE pandoeditor_editor Qt6::Test)
    add_test(NAME retained_reference_tests COMMAND retained_reference_tests -o -,txt)
    foreach(test_name web_import_tests web_import_editor_tests selection_editor_tests presentation_editor_tests)
        add_executable(${test_name} ../tests/${test_name}.cpp)
        target_link_libraries(${test_name} PRIVATE pandoeditor_editor Qt6::Test)
        target_compile_definitions(${test_name} PRIVATE WEB_IMPORT_FIXTURES="${PROJECT_SOURCE_DIR}/tests/fixtures/web-import")
        qt_add_resources(${test_name} sample PREFIX "/assets" BASE "${PROJECT_SOURCE_DIR}/assets"
            FILES "${PROJECT_SOURCE_DIR}/assets/sample.pando.json")
        add_test(NAME ${test_name} COMMAND ${test_name} -o -,txt)
    endforeach()
    target_compile_definitions(presentation_editor_tests PRIVATE WEB_HYDRO_FIXTURE="${PROJECT_SOURCE_DIR}/tests/fixtures/web-hydro")
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
    set_tests_properties(selection_editor_tests editor_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
    add_executable(ui_tests ../tests/ui_tests.cpp)
    target_link_libraries(ui_tests PRIVATE pandoeditor_editor Qt6::QuickControls2 Qt6::Test)
    qt_add_resources(ui_tests ui PREFIX "/" BASE "${PROJECT_SOURCE_DIR}/ui"
        FILES "${PROJECT_SOURCE_DIR}/ui/common/Main.qml"
        "${PROJECT_SOURCE_DIR}/ui/common/WebImportDialog.qml"
              "${PROJECT_SOURCE_DIR}/ui/desktop/CaptionButton.qml"
              "${PROJECT_SOURCE_DIR}/ui/common/MapView.qml"
              "${PROJECT_SOURCE_DIR}/ui/common/EditorPanel.qml"
        "${PROJECT_SOURCE_DIR}/ui/common/ObjectSelectionButton.qml"
        "${PROJECT_SOURCE_DIR}/ui/common/ObjectSearch.qml"
        "${PROJECT_SOURCE_DIR}/ui/common/ObjectChooser.qml"
              "${PROJECT_SOURCE_DIR}/ui/desktop/DesktopWorkspace.qml")
    qt_add_resources(ui_tests sample PREFIX "/assets" BASE "${PROJECT_SOURCE_DIR}/assets"
        FILES "${PROJECT_SOURCE_DIR}/assets/sample.pando.json")
    add_test(NAME ui_tests COMMAND ui_tests -o -,txt)
    set_tests_properties(ui_tests PROPERTIES TIMEOUT 120 ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
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
        "${PROJECT_SOURCE_DIR}/ui/common/ObjectSelectionButton.qml"
        "${PROJECT_SOURCE_DIR}/ui/common/ObjectSearch.qml"
        "${PROJECT_SOURCE_DIR}/ui/common/ObjectChooser.qml"
              "${PROJECT_SOURCE_DIR}/ui/desktop/DesktopWorkspace.qml")
    qt_add_resources(web_import_ui_tests sample PREFIX "/assets" BASE "${PROJECT_SOURCE_DIR}/assets"
        FILES "${PROJECT_SOURCE_DIR}/assets/sample.pando.json")
    add_test(NAME web_import_ui_tests COMMAND web_import_ui_tests -o -,txt)
    set_tests_properties(web_import_ui_tests PROPERTIES TIMEOUT 45 ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
endif()

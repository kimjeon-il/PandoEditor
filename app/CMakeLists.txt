find_package(Qt6 REQUIRED COMPONENTS Concurrent)
find_package(Qt6 REQUIRED COMPONENTS Svg)
find_package(Qt6 REQUIRED COMPONENTS Sql)
find_package(Qt6 6.5 REQUIRED COMPONENTS ShaderTools)
find_package(ZLIB REQUIRED)
qt_add_executable(pandoeditor main.cpp)

add_library(pandoeditor_editor STATIC
    referencewarp.cpp referencewarp.h
    referencetracing.cpp referencetracing.h
    referenceimagelibrary.cpp referenceimagelibrary.h
    defaultflagresolver.cpp defaultflagresolver.h
    hydrodataprovider.cpp hydrodataprovider.h
    historicallibraryloader.cpp historicallibraryloader.h
    historicaltransaction.cpp historicaltransaction.h
    gisgeojson.cpp gisgeojson.h
    giszip.cpp giszip.h
    gisgeopackage.cpp gisgeopackage.h
    gisgeopackagewriter.cpp
    projectgeopackage.cpp projectgeopackage.h
    autosavecoordinator.cpp autosavecoordinator.h
    projectpreviewcache.cpp projectpreviewcache.h
    countrylabelanchors.cpp countrylabelanchors.h
    mapscenebuilder.cpp mapscenebuilder.h
    mapscenebridge.cpp mapscenebridge.h
    builtinworldpolicy.cpp builtinworldpolicy.h
    worlddataset.cpp worlddataset.h
    worlddatasetloader.cpp worlddatasetloader.h
    terrainimageprovider.cpp terrainimageprovider.h
    worldbootstrap.cpp
    webprojectgeopackage.cpp webprojectgeopackage.h
    giscontentimport.cpp giscontentimport.h
    gisterritorial.cpp gisterritorial.h
    gisdocumentexport.cpp gisdocumentexport.h
    hydromanifest.cpp hydromanifest.h
    hydroassetreader.cpp hydroassetreader.h
    hydrometadata.cpp hydrometadata.h
    hydroshardreader.cpp hydroshardreader.h
    hydroloadscheduler.cpp hydroloadscheduler.h
    hydroruntimeprovider.cpp hydroruntimeprovider.h
    hydroruntimecache.cpp hydroruntimecache.h
    geometrycalculator.cpp geometrycalculator.h
    territorialgeometry.cpp territorialgeometry.h
    editorpresentation.cpp
    webimport.cpp webmigration.cpp retainedreferencerewriter.cpp editorwebimport.cpp editorstructure.cpp
    editorgeometry.cpp
    editorhistorical.cpp
    editorprojectgeopackage.cpp
    editorgis.cpp
    editorgisexport.cpp
    editorcontent.cpp
    editorcontroller.cpp editorcommands.cpp editorjobs.cpp editorfields.cpp commandjobrunner.cpp commandjobrunner.h editorcontroller.h platformstorage.cpp platformstorage.h projectcodec.cpp
    windowsframe.cpp windowsframe.h
    ../renderer/mapprojection.cpp
    ../renderer/mapviewstate.cpp
    ../renderer/projectionengine.cpp
    ../renderer/renderpacket.cpp
    ../renderer/canonicalpacket.cpp ../renderer/canonicalpacket.h
    ../renderer/countrymesh.cpp ../renderer/countrymesh.h
    ../renderer/terrainprovider.cpp ../renderer/terrainprovider.h
    ../renderer/renderlod.cpp ../renderer/renderlod.h
    ../renderer/renderquality.cpp ../renderer/renderquality.h
    ../renderer/countryculling.cpp ../renderer/countryculling.h
    ../renderer/uploadscheduler.cpp ../renderer/uploadscheduler.h
    ../renderer/renderscene.cpp
    ../renderer/scenepatch.cpp ../renderer/scenepatch.h
    ../renderer/geometrypacketcache.cpp
    ../renderer/maprenderitem.cpp ../renderer/maprenderitem.h
    ../renderer/gpumapitem.cpp ../renderer/gpumapitem.h
    ../renderer/scenegraph/mapmaterial.cpp ../renderer/scenegraph/mapmaterial.h
    ../renderer/scenegraph/mapscenenode.cpp ../renderer/scenegraph/mapscenenode.h
    ../renderer/referenceimageitem.cpp ../renderer/referenceimageitem.h
)
target_include_directories(pandoeditor_editor PUBLIC . ../renderer PRIVATE ../third_party/earcut)
target_link_libraries(pandoeditor_editor PUBLIC Pandoeditor::Core Qt6::Quick Qt6::Concurrent Qt6::Svg Qt6::Sql ZLIB::ZLIB)
qt_add_shaders(pandoeditor_editor m73_map_shaders
    PREFIX "/m73" BASE "${PROJECT_SOURCE_DIR}/renderer"
    FILES
        "${PROJECT_SOURCE_DIR}/renderer/shaders/fill.vert"
        "${PROJECT_SOURCE_DIR}/renderer/shaders/fill.frag"
        "${PROJECT_SOURCE_DIR}/renderer/shaders/stroke.vert"
        "${PROJECT_SOURCE_DIR}/renderer/shaders/stroke.frag"
        "${PROJECT_SOURCE_DIR}/renderer/shaders/point.vert"
        "${PROJECT_SOURCE_DIR}/renderer/shaders/point.frag")
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
        "${PROJECT_SOURCE_DIR}/ui/common/HistoricalLibraryPanel.qml"
        "${PROJECT_SOURCE_DIR}/ui/common/GisImportPanel.qml"
        "${PROJECT_SOURCE_DIR}/ui/common/GisExportPanel.qml"
        "${PROJECT_SOURCE_DIR}/ui/common/HistoricalEntityPreview.qml"
        "${PROJECT_SOURCE_DIR}/ui/common/HistoricalOwnershipSetup.qml"
        "${PROJECT_SOURCE_DIR}/ui/desktop/CaptionButton.qml"
        "${PROJECT_SOURCE_DIR}/ui/desktop/DesktopWorkspace.qml"
        "${PROJECT_SOURCE_DIR}/ui/common/MapView.qml"
        "${PROJECT_SOURCE_DIR}/ui/common/EditorPanel.qml"
        "${PROJECT_SOURCE_DIR}/ui/common/ObjectSelectionButton.qml"
        "${PROJECT_SOURCE_DIR}/ui/common/ObjectSearch.qml"
        "${PROJECT_SOURCE_DIR}/ui/common/ObjectChooser.qml"
)
qt_add_resources(pandoeditor world_metadata
    PREFIX "/world" BASE "${PROJECT_SOURCE_DIR}/assets/world"
    FILES "${PROJECT_SOURCE_DIR}/assets/world/country-label-anchors-v0.10.1.json")
qt_add_resources(pandoeditor historical_pilot
    PREFIX "/historical" BASE "${PROJECT_SOURCE_DIR}/assets/historical"
    FILES "${PROJECT_SOURCE_DIR}/assets/historical/historical-library-pilot.json")

qt_add_resources(pandoeditor sample PREFIX "/assets" BASE "${PROJECT_SOURCE_DIR}/assets"
    FILES "${PROJECT_SOURCE_DIR}/assets/sample.pando.json")
qt_add_resources(pandoeditor world_data PREFIX "/world" BASE "${PROJECT_SOURCE_DIR}/assets/world"
    FILES
        "${PROJECT_SOURCE_DIR}/assets/world/manifest.json"
        "${PROJECT_SOURCE_DIR}/assets/world/countries-preview-v0.33.0.geojson.gz"
        "${PROJECT_SOURCE_DIR}/assets/world/world-mesh-preview-v0.33.0.bin.gz"
        "${PROJECT_SOURCE_DIR}/assets/world/countries-canonical-v0.33.0.pcg.gz"
        "${PROJECT_SOURCE_DIR}/assets/world/world-mesh-v0.12.6.bin.gz"
        "${PROJECT_SOURCE_DIR}/assets/world/terrain/v0.12.6/manifest.json"
        "${PROJECT_SOURCE_DIR}/assets/world/hydro/v0.13.1/manifest.json")
file(GLOB_RECURSE DEFAULT_FLAG_FILES CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/assets/defaults/flags/*")
qt_add_resources(pandoeditor default_flags PREFIX "/defaults/flags" BASE "${PROJECT_SOURCE_DIR}/assets/defaults/flags" FILES ${DEFAULT_FLAG_FILES})

if(BUILD_TESTING)
    add_executable(gis_geojson_tests ../tests/gis_geojson_tests.cpp)
    target_link_libraries(gis_geojson_tests PRIVATE pandoeditor_editor)
    add_test(NAME gis_geojson_tests COMMAND gis_geojson_tests)
    add_executable(gis_zip_tests ../tests/gis_zip_tests.cpp)
    target_link_libraries(gis_zip_tests PRIVATE pandoeditor_editor)
    target_compile_definitions(gis_zip_tests PRIVATE
        WEB_GIS_ZIP_FIXTURE="${PROJECT_SOURCE_DIR}/tests/fixtures/web-m6")
    add_test(NAME gis_zip_tests COMMAND gis_zip_tests)
    add_executable(gis_geopackage_tests ../tests/gis_geopackage_tests.cpp)
    target_link_libraries(gis_geopackage_tests PRIVATE pandoeditor_editor Qt6::Sql)
    target_compile_definitions(gis_geopackage_tests PRIVATE
        WEB_GPKG_FIXTURE="${PROJECT_SOURCE_DIR}/tests/fixtures/web-m6")
    add_test(NAME gis_geopackage_tests COMMAND gis_geopackage_tests)
    add_executable(gis_territorial_import_tests ../tests/gis_territorial_import_tests.cpp)
    target_link_libraries(gis_territorial_import_tests PRIVATE pandoeditor_editor)
    target_compile_definitions(gis_territorial_import_tests PRIVATE
        WEB_GIS_FIXTURE="${PROJECT_SOURCE_DIR}/tests/fixtures/web-m6")
    add_test(NAME gis_territorial_import_tests COMMAND gis_territorial_import_tests)
    add_executable(gis_content_import_tests ../tests/gis_content_import_tests.cpp)
    target_link_libraries(gis_content_import_tests PRIVATE pandoeditor_editor)
    target_compile_definitions(gis_content_import_tests PRIVATE
        WEB_GIS_FIXTURE="${PROJECT_SOURCE_DIR}/tests/fixtures/web-m6")
    add_test(NAME gis_content_import_tests COMMAND gis_content_import_tests)
    add_executable(gis_import_controller_tests ../tests/gis_import_controller_tests.cpp)
    target_link_libraries(gis_import_controller_tests PRIVATE pandoeditor_editor Qt6::Test)
    qt_add_resources(gis_import_controller_tests sample PREFIX "/assets" BASE "${PROJECT_SOURCE_DIR}/assets"
        FILES "${PROJECT_SOURCE_DIR}/assets/sample.pando.json")
    add_test(NAME gis_import_controller_tests COMMAND gis_import_controller_tests -o -,txt)
    set_tests_properties(gis_import_controller_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
    add_executable(gis_failure_matrix_tests ../tests/gis_failure_matrix_tests.cpp)
    target_link_libraries(gis_failure_matrix_tests PRIVATE pandoeditor_editor Qt6::Test Qt6::Sql)
    target_compile_definitions(gis_failure_matrix_tests PRIVATE
        WEB_GIS_FIXTURE="${PROJECT_SOURCE_DIR}/tests/fixtures/web-m6")
    qt_add_resources(gis_failure_matrix_tests sample PREFIX "/assets" BASE "${PROJECT_SOURCE_DIR}/assets"
        FILES "${PROJECT_SOURCE_DIR}/assets/sample.pando.json")
    add_test(NAME gis_failure_matrix_tests COMMAND gis_failure_matrix_tests -o -,txt)
    set_tests_properties(gis_failure_matrix_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_Interpreter_FOUND)
        add_test(NAME m6_gpkg_web_fixture
            COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/m6-gpkg-fixture-oracle.py")
    endif()
    add_executable(gis_document_export_tests ../tests/gis_document_export_tests.cpp)
    target_link_libraries(gis_document_export_tests PRIVATE pandoeditor_editor)
    add_test(NAME gis_document_export_tests COMMAND gis_document_export_tests)
    add_executable(gis_export_tests ../tests/gis_export_tests.cpp)
    target_link_libraries(gis_export_tests PRIVATE pandoeditor_editor Qt6::Sql)
    add_test(NAME gis_export_tests COMMAND gis_export_tests)
    add_executable(project_geopackage_tests ../tests/project_geopackage_tests.cpp)
    target_link_libraries(project_geopackage_tests PRIVATE pandoeditor_editor Qt6::Sql)
    target_compile_definitions(project_geopackage_tests PRIVATE
        WEB_GPKG_FIXTURE="${PROJECT_SOURCE_DIR}/tests/fixtures/web-m6")
    add_test(NAME project_geopackage_tests COMMAND project_geopackage_tests)
    if(Python3_Interpreter_FOUND)
        add_test(NAME m6_project_gpkg_sqlite_oracle
            COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/m6-project-gpkg-oracle.py"
                "$<TARGET_FILE:project_geopackage_tests>")
    endif()
    add_executable(project_geopackage_controller_tests ../tests/project_geopackage_controller_tests.cpp)
    target_link_libraries(project_geopackage_controller_tests PRIVATE pandoeditor_editor Qt6::Test)
    target_compile_definitions(project_geopackage_controller_tests PRIVATE
        WEB_GPKG_FIXTURE="${PROJECT_SOURCE_DIR}/tests/fixtures/web-m6")
    qt_add_resources(project_geopackage_controller_tests sample PREFIX "/assets" BASE "${PROJECT_SOURCE_DIR}/assets"
        FILES "${PROJECT_SOURCE_DIR}/assets/sample.pando.json")
    add_test(NAME project_geopackage_controller_tests COMMAND project_geopackage_controller_tests -o -,txt)
    set_tests_properties(project_geopackage_controller_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
    add_executable(gis_export_controller_tests ../tests/gis_export_controller_tests.cpp)
    target_link_libraries(gis_export_controller_tests PRIVATE pandoeditor_editor Qt6::Test)
    qt_add_resources(gis_export_controller_tests sample PREFIX "/assets" BASE "${PROJECT_SOURCE_DIR}/assets"
        FILES "${PROJECT_SOURCE_DIR}/assets/sample.pando.json")
    add_test(NAME gis_export_controller_tests COMMAND gis_export_controller_tests -o -,txt)
    set_tests_properties(gis_export_controller_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
    if(Python3_Interpreter_FOUND)
        add_test(NAME m6_gis_export_sqlite_oracle
            COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/m6-gis-export-oracle.py"
                "$<TARGET_FILE:gis_export_tests>")
    endif()
    add_executable(historical_library_loader_tests ../tests/historical_library_loader_tests.cpp)
    target_link_libraries(historical_library_loader_tests PRIVATE pandoeditor_editor)
    target_compile_definitions(historical_library_loader_tests PRIVATE
        M6_HISTORICAL_PILOT_SAMPLE="${PROJECT_SOURCE_DIR}/tests/fixtures/web-m6/pilot-sample.json")
    add_test(NAME historical_library_loader_tests COMMAND historical_library_loader_tests)
    add_executable(historical_transaction_tests ../tests/historical_transaction_tests.cpp)
    target_link_libraries(historical_transaction_tests PRIVATE pandoeditor_editor Qt6::Test)
    add_test(NAME historical_transaction_tests COMMAND historical_transaction_tests -o -,txt)
    set_tests_properties(historical_transaction_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
    add_executable(historical_editor_tests ../tests/historical_editor_tests.cpp)
    target_link_libraries(historical_editor_tests PRIVATE pandoeditor_editor Qt6::Test)
    qt_add_resources(historical_editor_tests sample PREFIX "/assets" BASE "${PROJECT_SOURCE_DIR}/assets"
        FILES "${PROJECT_SOURCE_DIR}/assets/sample.pando.json")
    qt_add_resources(historical_editor_tests historical_pilot PREFIX "/historical"
        BASE "${PROJECT_SOURCE_DIR}/assets/historical"
        FILES "${PROJECT_SOURCE_DIR}/assets/historical/historical-library-pilot.json")
    add_test(NAME historical_editor_tests COMMAND historical_editor_tests -o -,txt)
    set_tests_properties(historical_editor_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
    find_package(Qt6 REQUIRED COMPONENTS Test)
    add_executable(hydro_manifest_tests ../tests/hydro_manifest_tests.cpp)
    target_link_libraries(hydro_manifest_tests PRIVATE pandoeditor_editor Qt6::Test)
    target_compile_definitions(hydro_manifest_tests PRIVATE WEB_HYDRO_FIXTURE="${PROJECT_SOURCE_DIR}/tests/fixtures/web-hydro")
    add_test(NAME hydro_manifest_tests COMMAND hydro_manifest_tests -o -,txt)
    add_executable(hydro_probe ../tests/hydro_probe.cpp)
    target_link_libraries(hydro_probe PRIVATE pandoeditor_editor)
    add_executable(hydro_runtime_tests ../tests/hydro_runtime_tests.cpp)
    target_link_libraries(hydro_runtime_tests PRIVATE pandoeditor_editor Qt6::Test)
    target_compile_definitions(hydro_runtime_tests PRIVATE WEB_HYDRO_FIXTURE="${PROJECT_SOURCE_DIR}/tests/fixtures/web-hydro")
    add_test(NAME hydro_runtime_tests COMMAND hydro_runtime_tests -o -,txt)
    add_executable(hydro_full_dataset_tests ../tests/hydro_full_dataset_tests.cpp)
    target_link_libraries(hydro_full_dataset_tests PRIVATE pandoeditor_editor Qt6::Test)
    add_test(NAME hydro_full_dataset_tests COMMAND hydro_full_dataset_tests -o -,txt)
    add_executable(hydro_cache_tests ../tests/hydro_cache_tests.cpp)
    target_link_libraries(hydro_cache_tests PRIVATE pandoeditor_editor)
    add_test(NAME hydro_cache_tests COMMAND hydro_cache_tests)
    add_executable(geometry_calculator_tests ../tests/geometry_calculator_tests.cpp)
    target_link_libraries(geometry_calculator_tests PRIVATE pandoeditor_editor Qt6::Test)
    add_test(NAME geometry_calculator_tests COMMAND geometry_calculator_tests -o -,txt)
    add_executable(map_render_tests ../tests/map_render_tests.cpp)
    target_link_libraries(map_render_tests PRIVATE pandoeditor_editor Qt6::Test)
    add_test(NAME map_render_tests COMMAND map_render_tests -o -,txt)
    set_tests_properties(map_render_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
    add_executable(world_corpus_tests ../tests/world_corpus_tests.cpp)
    add_executable(builtin_world_policy_tests ../tests/builtin_world_policy_tests.cpp)
    target_link_libraries(builtin_world_policy_tests PRIVATE pandoeditor_editor Qt6::Test)
    target_compile_definitions(builtin_world_policy_tests PRIVATE
        PANDOEDITOR_WORLD_ASSET_DIR="${PROJECT_SOURCE_DIR}/assets/world")
    add_test(NAME builtin_world_policy_tests COMMAND builtin_world_policy_tests -o -,txt)
    add_executable(autosave_tests ../tests/autosave_tests.cpp)
    target_link_libraries(autosave_tests PRIVATE pandoeditor_editor Qt6::Test)
    qt_add_resources(autosave_tests sample PREFIX "/assets" BASE "${PROJECT_SOURCE_DIR}/assets"
        FILES "${PROJECT_SOURCE_DIR}/assets/sample.pando.json")
    add_test(NAME autosave_tests COMMAND autosave_tests -o -,txt)
    add_executable(project_preview_cache_tests ../tests/project_preview_cache_tests.cpp)
    target_link_libraries(project_preview_cache_tests PRIVATE pandoeditor_editor Qt6::Test)
    qt_add_resources(project_preview_cache_tests sample PREFIX "/assets" BASE "${PROJECT_SOURCE_DIR}/assets"
        FILES "${PROJECT_SOURCE_DIR}/assets/sample.pando.json")
    add_test(NAME project_preview_cache_tests COMMAND project_preview_cache_tests -o -,txt)
    add_executable(country_label_anchor_tests ../tests/country_label_anchor_tests.cpp)
    target_link_libraries(country_label_anchor_tests PRIVATE pandoeditor_editor Qt6::Test)
    qt_add_resources(country_label_anchor_tests world_metadata
        PREFIX "/world" BASE "${PROJECT_SOURCE_DIR}/assets/world"
        FILES "${PROJECT_SOURCE_DIR}/assets/world/country-label-anchors-v0.10.1.json")
    add_test(NAME country_label_anchor_tests COMMAND country_label_anchor_tests -o -,txt)
    add_executable(m72_view_state_tests ../tests/m72_view_state_tests.cpp)
    target_link_libraries(m72_view_state_tests PRIVATE pandoeditor_editor)
    add_test(NAME m72_view_state_tests COMMAND m72_view_state_tests)
    add_executable(m72_projection_tests ../tests/m72_projection_tests.cpp)
    target_link_libraries(m72_projection_tests PRIVATE pandoeditor_editor)
    add_test(NAME m72_projection_tests COMMAND m72_projection_tests)
    add_executable(m72_view_pick_tests ../tests/m72_view_pick_tests.cpp)
    target_link_libraries(m72_view_pick_tests PRIVATE pandoeditor_editor)
    add_test(NAME m72_view_pick_tests COMMAND m72_view_pick_tests)
    add_executable(m72_render_packet_tests ../tests/m72_render_packet_tests.cpp)
    target_link_libraries(m72_render_packet_tests PRIVATE pandoeditor_editor)
    add_test(NAME m72_render_packet_tests COMMAND m72_render_packet_tests)
    add_executable(m72_packet_corpus_probe ../tests/m72_packet_corpus_probe.cpp)
    target_link_libraries(m72_packet_corpus_probe PRIVATE pandoeditor_editor)
    add_executable(m72_render_scene_tests ../tests/m72_render_scene_tests.cpp)
    target_link_libraries(m72_render_scene_tests PRIVATE pandoeditor_editor)
    add_test(NAME m72_render_scene_tests COMMAND m72_render_scene_tests)
    add_executable(m72_packet_cache_tests ../tests/m72_packet_cache_tests.cpp)
    target_link_libraries(m72_packet_cache_tests PRIVATE pandoeditor_editor)
    add_test(NAME m72_packet_cache_tests COMMAND m72_packet_cache_tests)
    add_executable(m72_scene_builder_tests ../tests/m72_scene_builder_tests.cpp)
    target_link_libraries(m72_scene_builder_tests PRIVATE pandoeditor_editor)
    add_test(NAME m72_scene_builder_tests COMMAND m72_scene_builder_tests)
    add_executable(m72_scene_editor_tests ../tests/m72_scene_editor_tests.cpp)
    target_link_libraries(m72_scene_editor_tests PRIVATE pandoeditor_editor)
    target_include_directories(m72_scene_editor_tests PRIVATE ../tests)
    target_compile_definitions(m72_scene_editor_tests PRIVATE
        M71_WORLD_FIXTURE="${PROJECT_SOURCE_DIR}/tests/fixtures/world-rendering")
    add_test(NAME m72_scene_editor_tests COMMAND m72_scene_editor_tests)
    add_executable(m72_scene_bridge_tests ../tests/m72_scene_bridge_tests.cpp)
    target_link_libraries(m72_scene_bridge_tests PRIVATE pandoeditor_editor Qt6::Core)
    add_test(NAME m72_scene_bridge_tests COMMAND m72_scene_bridge_tests)
    add_executable(m72_cpu_scene_adapter_tests ../tests/m72_cpu_scene_adapter_tests.cpp)
    target_link_libraries(m72_cpu_scene_adapter_tests PRIVATE pandoeditor_editor Qt6::Gui)
    add_test(NAME m72_cpu_scene_adapter_tests COMMAND m72_cpu_scene_adapter_tests)
    set_tests_properties(m72_cpu_scene_adapter_tests PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
    add_executable(m72_mapprojection_lifetime_tests ../tests/m72_mapprojection_lifetime_tests.cpp)
    target_link_libraries(m72_mapprojection_lifetime_tests PRIVATE pandoeditor_editor)
    add_test(NAME m72_mapprojection_lifetime_tests COMMAND m72_mapprojection_lifetime_tests)
    find_program(PANDOEDITOR_M72_NODE_EXECUTABLE NAMES node)
    if(PANDOEDITOR_M72_NODE_EXECUTABLE)
        add_test(NAME m72_packet_corpus
            COMMAND "${CMAKE_COMMAND}" -E env
                "M72_PACKET_PROBE=$<TARGET_FILE:m72_packet_corpus_probe>"
                "${PANDOEDITOR_M72_NODE_EXECUTABLE}" --test
                "${PROJECT_SOURCE_DIR}/tools/m72-packet-corpus.test.mjs")
        add_test(NAME m72_projection_web_oracle
            COMMAND "${CMAKE_COMMAND}" -E env
                "M72_PROJECTION_PROBE=$<TARGET_FILE:m72_projection_tests>"
                "${PANDOEDITOR_M72_NODE_EXECUTABLE}" --test
                "${PROJECT_SOURCE_DIR}/tools/m72-projection-oracle.test.mjs")
    endif()
    target_link_libraries(world_corpus_tests PRIVATE pandoeditor_editor Qt6::Test)
    target_compile_definitions(world_corpus_tests PRIVATE M71_WORLD_FIXTURE="${PROJECT_SOURCE_DIR}/tests/fixtures/world-rendering")
    add_test(NAME world_corpus_tests COMMAND world_corpus_tests -o -,txt)
    set_tests_properties(world_corpus_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
    add_executable(world_projection_diagnostics ../tests/world_projection_diagnostics.cpp)
    target_link_libraries(world_projection_diagnostics PRIVATE pandoeditor_editor Qt6::Test)
    target_compile_definitions(world_projection_diagnostics PRIVATE M71_WORLD_FIXTURE="${PROJECT_SOURCE_DIR}/tests/fixtures/world-rendering")
    add_test(NAME world_projection_diagnostics COMMAND world_projection_diagnostics -o -,txt)
    set_tests_properties(world_projection_diagnostics PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
    add_executable(world_render_baseline ../tests/world_render_baseline.cpp)
    target_link_libraries(world_render_baseline PRIVATE pandoeditor_editor)
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
        "${PROJECT_SOURCE_DIR}/ui/common/HistoricalLibraryPanel.qml"
        "${PROJECT_SOURCE_DIR}/ui/common/GisImportPanel.qml"
        "${PROJECT_SOURCE_DIR}/ui/common/GisExportPanel.qml"
        "${PROJECT_SOURCE_DIR}/ui/common/HistoricalEntityPreview.qml"
        "${PROJECT_SOURCE_DIR}/ui/common/HistoricalOwnershipSetup.qml"
              "${PROJECT_SOURCE_DIR}/ui/desktop/CaptionButton.qml"
              "${PROJECT_SOURCE_DIR}/ui/common/MapView.qml"
              "${PROJECT_SOURCE_DIR}/ui/common/EditorPanel.qml"
        "${PROJECT_SOURCE_DIR}/ui/common/ObjectSelectionButton.qml"
        "${PROJECT_SOURCE_DIR}/ui/common/ObjectSearch.qml"
        "${PROJECT_SOURCE_DIR}/ui/common/ObjectChooser.qml"
              "${PROJECT_SOURCE_DIR}/ui/desktop/DesktopWorkspace.qml")
    qt_add_resources(ui_tests sample PREFIX "/assets" BASE "${PROJECT_SOURCE_DIR}/assets"
        FILES "${PROJECT_SOURCE_DIR}/assets/sample.pando.json")
    # Exercise the production default flag resolver and image loading without
    # leaving other sample-country flags unresolved in the UI test engine.
    foreach(test_name presentation_editor_tests ui_tests)
        qt_add_resources(${test_name} test_default_flag PREFIX "/defaults/flags"
            BASE "${PROJECT_SOURCE_DIR}/assets/defaults/flags"
            FILES ${DEFAULT_FLAG_FILES})
    endforeach()
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
        "${PROJECT_SOURCE_DIR}/ui/common/HistoricalLibraryPanel.qml"
        "${PROJECT_SOURCE_DIR}/ui/common/GisImportPanel.qml"
        "${PROJECT_SOURCE_DIR}/ui/common/GisExportPanel.qml"
        "${PROJECT_SOURCE_DIR}/ui/common/HistoricalEntityPreview.qml"
        "${PROJECT_SOURCE_DIR}/ui/common/HistoricalOwnershipSetup.qml"
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

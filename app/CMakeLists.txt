find_package(Qt6 REQUIRED COMPONENTS Concurrent)
find_package(Qt6 REQUIRED COMPONENTS Svg)
find_package(Qt6 REQUIRED COMPONENTS Sql)
find_package(Qt6 REQUIRED COMPONENTS Network)
if(ANDROID)
    find_package(Qt6ShaderToolsTools 6.5 REQUIRED
        PATHS "${QT_HOST_PATH_CMAKE_DIR}/Qt6ShaderToolsTools"
        NO_DEFAULT_PATH
        NO_CMAKE_FIND_ROOT_PATH
    )
else()
    find_package(Qt6 6.5 REQUIRED COMPONENTS ShaderTools)
endif()
find_package(ZLIB REQUIRED)
if(BUILD_TESTING)
    add_executable(place_label_collision_tests ../tests/place_label_collision_tests.cpp)
    target_link_libraries(place_label_collision_tests PRIVATE Pandoeditor::MapEngine)
    add_test(NAME place_label_collision_tests COMMAND place_label_collision_tests)
    add_executable(place_parity_probe ../tests/place_parity_probe.cpp
        placeruntimestore.cpp placeruntimestore.h placeruntimeprovider.cpp placeruntimeprovider.h)
    target_include_directories(place_parity_probe PRIVATE .)
    target_link_libraries(place_parity_probe PRIVATE Pandoeditor::MapEngine Pandoeditor::Core Qt6::Gui Qt6::Concurrent)
endif()
qt_add_executable(pandoeditor main.cpp)

add_library(pandoeditor_editor STATIC
    referencewarp.cpp referencewarp.h
    referencetracing.cpp referencetracing.h
    referenceimagelibrary.cpp referenceimagelibrary.h referenceimagemodel.h
    defaultflagresolver.cpp defaultflagresolver.h
    hydrodataprovider.cpp hydrodataprovider.h
    historicallibraryloader.cpp historicallibraryloader.h
    historicaltransaction.cpp historicaltransaction.h
    territoriallibrarycatalog.cpp territoriallibrarycatalog.h
    territorialcatalogadapter.cpp territorialcatalogadapter.h
    gisgeojson.cpp gisgeojson.h
    giszip.cpp giszip.h
    gisgeopackage.cpp gisgeopackage.h
    gisgeopackagewriter.cpp
    projectgeopackage.cpp projectgeopackage.h
    autosavecoordinator.cpp autosavecoordinator.h
    projectpreviewcache.cpp projectpreviewcache.h
    countrylabelanchors.cpp countrylabelanchors.h
    labelplacementmodel.h
    physicaldatastore.cpp physicaldatastore.h
    mapscenebridge.cpp mapscenebridge.h
    builtinworldpolicy.cpp builtinworldpolicy.h
    worlddataset.cpp worlddataset.h
    worlddatasetloader.cpp worlddatasetloader.h
    terrainimageprovider.cpp terrainimageprovider.h
    placeruntimestore.cpp placeruntimestore.h
    placeruntimeprovider.cpp placeruntimeprovider.h
    editorplace.cpp
    resourcecachecoordinator.cpp resourcecachecoordinator.h
    worldresourcecache.cpp worldresourcecache.h
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
    geometrycalculator.cpp geometrycalculator.h geometryruntime_p.h
    riverpartitioncalculator.cpp riverpartitioncalculator.h
    riverareacalculator.cpp riverareacalculator.h
    territorialgeometry.cpp territorialgeometry.h
    territoryselectionruntime.cpp territoryselectionruntime.h
    territorycutadapter.cpp territorycutadapter.h
    territorialpreviewruntime.cpp territorialpreviewruntime.h
    editorpresentation.cpp
    webimport.cpp retainedreferencerewriter.cpp editorwebimport.cpp editorstructure.cpp
    editorgeometry.cpp editorterritoryselection.cpp editorriverselection.cpp
    editorhistorical.cpp
    editorprojectgeopackage.cpp
    editorgis.cpp
    editorgisexport.cpp
    editorcontent.cpp
    editorcontroller.cpp editorview.cpp editorcommands.cpp editorjobs.cpp editorfields.cpp commandjobrunner.cpp commandjobrunner.h editorcontroller.h platformstorage.cpp platformstorage.h projectcodec.cpp
    windowsframe.cpp windowsframe.h
    ../renderer/mapprojection.cpp
    ../renderer/canonicalpacket.cpp ../renderer/canonicalpacket.h
    ../renderer/countrymesh.cpp ../renderer/countrymesh.h
    ../renderer/terrainprovider.cpp ../renderer/terrainprovider.h
    ../renderer/terrainrendercontract.cpp ../renderer/terrainrendercontract.h
    ../renderer/terrainlandmaskitem.cpp ../renderer/terrainlandmaskitem.h
    ../renderer/terrainrenderowner.cpp ../renderer/terrainrenderowner.h
    ../renderer/terrainlayeritem.cpp ../renderer/terrainlayeritem.h
    ../renderer/scenegraph/terrainmaterial.cpp ../renderer/scenegraph/terrainmaterial.h
    ../renderer/uploadscheduler.cpp ../renderer/uploadscheduler.h
    ../renderer/maprenderitem.cpp ../renderer/maprenderitem.h
    ../renderer/geographicimagemesh.cpp ../renderer/geographicimagemesh.h
    ../renderer/geographicimageitem.cpp ../renderer/geographicimageitem.h
    ../renderer/gpumapitem.cpp ../renderer/gpumapitem.h
    ../renderer/scenegraph/mapmaterial.cpp ../renderer/scenegraph/mapmaterial.h
    ../renderer/scenegraph/mapscenenode.cpp ../renderer/scenegraph/mapscenenode.h
    ../renderer/referenceimageitem.cpp ../renderer/referenceimageitem.h
)
target_include_directories(pandoeditor_editor PUBLIC . ../renderer)
target_link_libraries(pandoeditor_editor PUBLIC Pandoeditor::Core Pandoeditor::MapEngine Qt6::Quick Qt6::Concurrent Qt6::Svg Qt6::Sql Qt6::Network ZLIB::ZLIB)
target_link_libraries(pandoeditor_editor PRIVATE Qt6::GuiPrivate)
qt_add_shaders(pandoeditor_editor m73_map_shaders
    PREFIX "/m73" BASE "${PROJECT_SOURCE_DIR}/renderer"
    FILES
        "${PROJECT_SOURCE_DIR}/renderer/shaders/fill.vert"
        "${PROJECT_SOURCE_DIR}/renderer/shaders/fill.frag"
        "${PROJECT_SOURCE_DIR}/renderer/shaders/stroke.vert"
        "${PROJECT_SOURCE_DIR}/renderer/shaders/stroke.frag"
        "${PROJECT_SOURCE_DIR}/renderer/shaders/point.vert"
        "${PROJECT_SOURCE_DIR}/renderer/shaders/point.frag"
        "${PROJECT_SOURCE_DIR}/renderer/shaders/terrain.vert"
        "${PROJECT_SOURCE_DIR}/renderer/shaders/terrain.frag")
qt_add_resources(pandoeditor_editor terrain_dem_metadata
    PREFIX "/world" BASE "${PROJECT_SOURCE_DIR}/assets/world"
    FILES "${PROJECT_SOURCE_DIR}/assets/world/terrain/v0.13.3/manifest.json"
          "${PROJECT_SOURCE_DIR}/assets/world/physical-inventory-terrain-dem-c3c18d1.json"
          "${PROJECT_SOURCE_DIR}/assets/world/terrain-dem-provenance-c3c18d1.json")
qt_add_resources(pandoeditor_editor place_runtime PREFIX "/place" BASE "${PROJECT_SOURCE_DIR}/assets/place"
    FILES "${PROJECT_SOURCE_DIR}/assets/place/manifest.json")
qt_add_resources(pandoeditor_editor m4_geometry
    PREFIX "/geometry" BASE "${PROJECT_SOURCE_DIR}/assets/geometry"
    FILES "${PROJECT_SOURCE_DIR}/assets/geometry/polygon-clipping-0.15.7.js"
          "${PROJECT_SOURCE_DIR}/assets/geometry/LICENSE.polygon-clipping.txt")
qt_add_resources(pandoeditor_editor m972_river
    PREFIX "/river" BASE "${PROJECT_SOURCE_DIR}/assets/geometry/river"
    FILES
        "${PROJECT_SOURCE_DIR}/assets/geometry/river/original/river-territory-partition.js"
        "${PROJECT_SOURCE_DIR}/assets/geometry/river/original/planar-graph-faces.js"
        "${PROJECT_SOURCE_DIR}/assets/geometry/river/adapted/river-territory-partition.js"
        "${PROJECT_SOURCE_DIR}/assets/geometry/river/adapted/planar-graph-faces.js"
        "${PROJECT_SOURCE_DIR}/assets/geometry/river/platform.js"
        "${PROJECT_SOURCE_DIR}/assets/geometry/river/bridge.js"
        "${PROJECT_SOURCE_DIR}/assets/geometry/river/presentation.js"
        "${PROJECT_SOURCE_DIR}/assets/geometry/river/workspace-observation.js"
        "${PROJECT_SOURCE_DIR}/assets/geometry/river/original/polygon-geometry.js"
        "${PROJECT_SOURCE_DIR}/assets/geometry/river/provenance.json"
        "${PROJECT_SOURCE_DIR}/assets/geometry/river/original/d3.min.js"
        "${PROJECT_SOURCE_DIR}/assets/geometry/river/adapted/d3.min.js"
        "${PROJECT_SOURCE_DIR}/assets/geometry/river/LICENSE.d3.txt"
        "${PROJECT_SOURCE_DIR}/assets/geometry/river/d3-provenance.json")
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
file(GLOB TERRITORIAL_CATALOG_FILES CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/assets/territorial-library-v2/*.gz")
qt_add_resources(pandoeditor_editor territorial_catalog PREFIX "/territorial-library-v2"
    BASE "${PROJECT_SOURCE_DIR}/assets/territorial-library-v2"
    FILES "${PROJECT_SOURCE_DIR}/assets/territorial-library-v2/index.json" ${TERRITORIAL_CATALOG_FILES})

qt_add_resources(pandoeditor sample PREFIX "/assets" BASE "${PROJECT_SOURCE_DIR}/assets"
    FILES "${PROJECT_SOURCE_DIR}/assets/sample.pando.json")
# Embed large map bytes directly into a resource object instead of compiling
# a 100+ MB C++ byte array, which exhausts MinGW's compiler heap in Debug.
qt_add_resources(pandoeditor world_data BIG_RESOURCES PREFIX "/world" BASE "${PROJECT_SOURCE_DIR}/assets/world"
    FILES
        "${PROJECT_SOURCE_DIR}/assets/world/manifest.json"
        "${PROJECT_SOURCE_DIR}/assets/world/countries-preview-v0.33.0.geojson.gz"
        "${PROJECT_SOURCE_DIR}/assets/world/world-mesh-preview-v0.33.0.bin.gz"
        "${PROJECT_SOURCE_DIR}/assets/world/countries-canonical-v0.33.0.pcg.gz"
        "${PROJECT_SOURCE_DIR}/assets/world/world-mesh-v0.12.6.bin.gz"
        "${PROJECT_SOURCE_DIR}/assets/world/terrain/v0.12.6/manifest.json"
        "${PROJECT_SOURCE_DIR}/assets/world/hydro/v0.13.1/manifest.json"
        "${PROJECT_SOURCE_DIR}/assets/world/physical-inventory-c0bd31d1.json")

file(GLOB_RECURSE DEFAULT_FLAG_FILES CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/assets/defaults/flags/*")
qt_add_resources(pandoeditor default_flags PREFIX "/defaults/flags" BASE "${PROJECT_SOURCE_DIR}/assets/defaults/flags" FILES ${DEFAULT_FLAG_FILES})

if(BUILD_TESTING)
    add_executable(interaction_style_policy_tests ../tests/interaction_style_policy_tests.cpp)
    target_include_directories(interaction_style_policy_tests PRIVATE "${PROJECT_SOURCE_DIR}/engine/include")
    add_test(NAME interaction_style_policy_tests COMMAND interaction_style_policy_tests)
    add_executable(map_builder_stroke_policy_tests ../tests/map_builder_stroke_policy_tests.cpp)
    target_include_directories(map_builder_stroke_policy_tests PRIVATE "${PROJECT_SOURCE_DIR}/tests")
    target_link_libraries(map_builder_stroke_policy_tests PRIVATE pandoeditor_editor)
    add_test(NAME map_builder_stroke_policy_tests COMMAND map_builder_stroke_policy_tests)
    add_executable(map_stroke_handoff_tests ../tests/map_stroke_handoff_tests.cpp)
    add_executable(stroke_pixel_tests ../tests/stroke_pixel_tests.cpp)
    target_link_libraries(stroke_pixel_tests PRIVATE pandoeditor_editor Qt6::Test Qt6::Quick)
    option(PANDOEDITOR_REGISTER_STROKE_GPU_DISPLAY "Register actual RHI stroke pixel execution" OFF)
    if(PANDOEDITOR_REGISTER_STROKE_GPU_DISPLAY)
        add_test(NAME stroke_pixel_tests COMMAND stroke_pixel_tests -o -,txt)
        set_tests_properties(stroke_pixel_tests PROPERTIES LABELS "hardware;stroke-display" TIMEOUT 180)
    endif()
    add_executable(stroke_topology_probe ../tests/stroke_topology_probe.cpp)
    target_link_libraries(stroke_topology_probe PRIVATE Qt6::Core)
    target_link_libraries(map_stroke_handoff_tests PRIVATE pandoeditor_editor Qt6::Test)
    add_test(NAME map_stroke_handoff_tests COMMAND map_stroke_handoff_tests -o -,txt)
    set_tests_properties(map_stroke_handoff_tests PROPERTIES TIMEOUT 60 ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
    add_executable(geometry_presentation_handoff_tests ../tests/geometry_presentation_handoff_tests.cpp)
    target_link_libraries(geometry_presentation_handoff_tests PRIVATE pandoeditor_editor Qt6::Test)
    qt_add_resources(geometry_presentation_handoff_tests sample PREFIX "/assets" BASE "${PROJECT_SOURCE_DIR}/assets"
        FILES "${PROJECT_SOURCE_DIR}/assets/sample.pando.json")
    add_test(NAME geometry_presentation_handoff_tests COMMAND geometry_presentation_handoff_tests -o -,txt)
    set_tests_properties(geometry_presentation_handoff_tests PROPERTIES TIMEOUT 60 ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
    add_executable(historical_snapshot_timing_probe ../tests/historical_snapshot_timing_probe.cpp)
    target_link_libraries(historical_snapshot_timing_probe PRIVATE pandoeditor_editor)
    add_executable(native_editing_performance_probe ../tests/native_editing_performance_probe.cpp)
    if(WIN32)
    add_executable(geometry_presentation_device_tests ../tests/geometry_presentation_device_tests.cpp)
    target_link_libraries(geometry_presentation_device_tests PRIVATE pandoeditor_editor Qt6::Test)
    qt_add_resources(geometry_presentation_device_tests sample PREFIX "/assets" BASE "${PROJECT_SOURCE_DIR}/assets"
        FILES "${PROJECT_SOURCE_DIR}/assets/sample.pando.json")
    add_test(NAME geometry_presentation_device_tests COMMAND geometry_presentation_device_tests -o -,txt)
    set_tests_properties(geometry_presentation_device_tests PROPERTIES TIMEOUT 60 LABELS "hardware;presentation"
        ENVIRONMENT "QT_QPA_PLATFORM=windows;QSG_RHI_BACKEND=d3d11")
    endif()
    target_include_directories(native_editing_performance_probe PRIVATE "${PROJECT_SOURCE_DIR}/tests")
    target_link_libraries(native_editing_performance_probe PRIVATE pandoeditor_editor)
    qt_add_resources(native_editing_performance_probe sample PREFIX "/assets" BASE "${PROJECT_SOURCE_DIR}/assets"
        FILES "${PROJECT_SOURCE_DIR}/assets/sample.pando.json")
    add_executable(native_performance_structure_tests ../tests/native_performance_structure_tests.cpp)
    target_link_libraries(native_performance_structure_tests PRIVATE pandoeditor_editor Qt6::Test)
    qt_add_resources(native_performance_structure_tests sample PREFIX "/assets" BASE "${PROJECT_SOURCE_DIR}/assets"
        FILES "${PROJECT_SOURCE_DIR}/assets/sample.pando.json")
    add_test(NAME native_performance_structure_tests COMMAND native_performance_structure_tests -o -,txt)
    set_tests_properties(native_performance_structure_tests PROPERTIES TIMEOUT 60 ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
    add_executable(place_runtime_store_tests ../tests/place_runtime_store_tests.cpp)
    add_executable(territorial_info_controller_tests ../tests/territorial_info_controller_tests.cpp)
    target_link_libraries(territorial_info_controller_tests PRIVATE pandoeditor_editor Qt6::Test)
    qt_add_resources(territorial_info_controller_tests sample PREFIX "/assets" BASE "${PROJECT_SOURCE_DIR}/assets"
        FILES "${PROJECT_SOURCE_DIR}/assets/sample.pando.json")
    add_test(NAME territorial_info_controller_tests COMMAND territorial_info_controller_tests)
    set_tests_properties(territorial_info_controller_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
    add_executable(place_controller_tests ../tests/place_controller_tests.cpp)
    target_link_libraries(place_controller_tests PRIVATE pandoeditor_editor Qt6::Test)
    qt_add_resources(place_controller_tests sample PREFIX "/assets" BASE "${PROJECT_SOURCE_DIR}/assets"
        FILES "${PROJECT_SOURCE_DIR}/assets/sample.pando.json")
    add_test(NAME place_controller_tests COMMAND place_controller_tests)
    set_tests_properties(place_controller_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
    add_executable(territorial_catalog_controller_tests ../tests/territorial_catalog_controller_tests.cpp)
    target_link_libraries(territorial_catalog_controller_tests PRIVATE pandoeditor_editor Qt6::Test)
    qt_add_resources(territorial_catalog_controller_tests sample PREFIX "/assets" BASE "${PROJECT_SOURCE_DIR}/assets"
        FILES "${PROJECT_SOURCE_DIR}/assets/sample.pando.json")
    add_test(NAME territorial_catalog_controller_tests COMMAND territorial_catalog_controller_tests)
    set_tests_properties(territorial_catalog_controller_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
    target_link_libraries(place_runtime_store_tests PRIVATE pandoeditor_editor Qt6::Test)
    target_compile_definitions(place_runtime_store_tests PRIVATE PLACE_RUNTIME_FIXTURE="${PROJECT_SOURCE_DIR}/tests/fixtures/web-place-runtime-source")
    add_test(NAME place_runtime_store_tests COMMAND place_runtime_store_tests)
    add_executable(place_runtime_provider_tests ../tests/place_runtime_provider_tests.cpp)
    target_link_libraries(place_runtime_provider_tests PRIVATE pandoeditor_editor Qt6::Test)
    target_compile_definitions(place_runtime_provider_tests PRIVATE PLACE_RUNTIME_FIXTURE="${PROJECT_SOURCE_DIR}/tests/fixtures/web-place-runtime-source")
    add_test(NAME place_runtime_provider_tests COMMAND place_runtime_provider_tests)
    add_executable(terrain_render_owner_tests ../tests/terrain_render_owner_tests.cpp)
    target_link_libraries(terrain_render_owner_tests PRIVATE pandoeditor_editor Qt6::Test)
    add_test(NAME terrain_render_owner_tests COMMAND terrain_render_owner_tests)
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
        add_test(NAME terrain_dem_acquisition_tests
            COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/test_acquire_terrain_dem.py")
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
    add_executable(place_provenance_engine_tests ../tests/place_provenance_engine_tests.cpp)
    target_link_libraries(place_provenance_engine_tests PRIVATE pandoeditor_editor Qt6::Sql)
    add_test(NAME place_provenance_engine_tests COMMAND place_provenance_engine_tests)
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
    add_executable(label_placement_model_tests ../tests/label_placement_model_tests.cpp labelplacementmodel.h)
    target_include_directories(label_placement_model_tests PRIVATE .)
    target_link_libraries(label_placement_model_tests PRIVATE Qt6::Core Qt6::Gui Qt6::Test)
    add_test(NAME label_placement_model_tests COMMAND label_placement_model_tests -o -,txt)
    set_tests_properties(label_placement_model_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
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
    add_executable(hydro_river_source_tests ../tests/hydro_river_source_tests.cpp)
    target_link_libraries(hydro_river_source_tests PRIVATE pandoeditor_editor Qt6::Test)
    target_compile_definitions(hydro_river_source_tests PRIVATE WEB_HYDRO_FIXTURE="${PROJECT_SOURCE_DIR}/tests/fixtures/web-hydro")
    add_test(NAME hydro_river_source_tests COMMAND hydro_river_source_tests -o -,txt)
    add_executable(river_partition_tests ../tests/river_partition_tests.cpp)
    target_link_libraries(river_partition_tests PRIVATE pandoeditor_editor Qt6::Test)
    add_test(NAME river_partition_tests COMMAND river_partition_tests -o -,txt)
    target_compile_definitions(river_partition_tests PRIVATE
        M972_RIVER_FIXTURE="${PROJECT_SOURCE_DIR}/tests/fixtures/web-m972-river"
        M972_RIVER_ASSETS="${PROJECT_SOURCE_DIR}/assets/geometry/river")
    add_executable(m972_river_probe ../tests/m972_river_probe.cpp)
    target_link_libraries(m972_river_probe PRIVATE pandoeditor_editor)
    find_program(M972_NODE_EXECUTABLE NAMES node)
    if(M972_NODE_EXECUTABLE)
        add_test(NAME m972_river_oracle_contract COMMAND "${M972_NODE_EXECUTABLE}" --test
            "${PROJECT_SOURCE_DIR}/tools/m97/river-kernel.test.mjs")
        set_tests_properties(m972_river_oracle_contract PROPERTIES
            ENVIRONMENT "PANDOEDITOR_PYTHON_EXECUTABLE=${Python3_EXECUTABLE}")
        add_test(NAME m972_river_synthetic_browser_baseline COMMAND "${M972_NODE_EXECUTABLE}"
            "${PROJECT_SOURCE_DIR}/tools/m97/river/differential.mjs" --native "$<TARGET_FILE:m972_river_probe>" --synthetic)
        add_test(NAME m972_river_full_source_browser_baseline COMMAND "${M972_NODE_EXECUTABLE}" --max-old-space-size=256
            "${PROJECT_SOURCE_DIR}/tools/m97/river/differential.mjs" --native "$<TARGET_FILE:m972_river_probe>" --full)
        set_tests_properties(m972_river_full_source_browser_baseline PROPERTIES TIMEOUT 240 LABELS "full-data;river-parity")
    endif()

    add_executable(hydro_full_dataset_tests ../tests/hydro_full_dataset_tests.cpp)
    target_link_libraries(hydro_full_dataset_tests PRIVATE pandoeditor_editor Qt6::Test)
    add_test(NAME hydro_full_dataset_tests COMMAND hydro_full_dataset_tests -o -,txt)
    add_executable(terrain_resource_cache_tests ../tests/terrain_resource_cache_tests.cpp)
    target_link_libraries(terrain_resource_cache_tests PRIVATE pandoeditor_editor Qt6::Test)
    add_test(NAME terrain_resource_cache_tests COMMAND terrain_resource_cache_tests)
    add_executable(terrain_dem_provider_tests ../tests/terrain_dem_provider_tests.cpp
        ../renderer/terrainprovider.cpp)
    target_include_directories(terrain_dem_provider_tests PRIVATE ../renderer)
    target_link_libraries(terrain_dem_provider_tests PRIVATE Pandoeditor::MapEngine Qt6::Gui)
    add_test(NAME terrain_dem_provider_tests COMMAND terrain_dem_provider_tests
        "${PROJECT_SOURCE_DIR}/assets/world/terrain/v0.13.3/manifest.json")
    add_executable(terrain_lod_demand_tests ../tests/terrain_lod_demand_tests.cpp ../renderer/terrainprovider.cpp)
    target_include_directories(terrain_lod_demand_tests PRIVATE ../renderer)
    target_link_libraries(terrain_lod_demand_tests PRIVATE Pandoeditor::MapEngine Qt6::Gui)
    add_test(NAME terrain_lod_demand_tests COMMAND terrain_lod_demand_tests
        "${PROJECT_SOURCE_DIR}/assets/world/terrain/v0.13.3/manifest.json"
        "${PROJECT_SOURCE_DIR}/assets/world/terrain/v0.12.6/manifest.json")
    add_executable(terrain_display_state_tests ../tests/terrain_display_state_tests.cpp)
    add_executable(terrain_grid_mesh_tests ../tests/terrain_grid_mesh_tests.cpp)
    add_test(NAME terrain_grid_mesh_tests COMMAND terrain_grid_mesh_tests)
    target_include_directories(terrain_display_state_tests PRIVATE .)
    target_link_libraries(terrain_display_state_tests PRIVATE Qt6::Core)
    add_test(NAME terrain_display_state_tests COMMAND terrain_display_state_tests)
    add_executable(terrain_render_protection_tests ../tests/terrain_render_protection_tests.cpp
        ../renderer/terrainprovider.cpp)
    target_include_directories(terrain_render_protection_tests PRIVATE ../renderer)
    target_link_libraries(terrain_render_protection_tests PRIVATE Pandoeditor::MapEngine Qt6::Gui)
    add_test(NAME terrain_render_protection_tests COMMAND terrain_render_protection_tests
        "${PROJECT_SOURCE_DIR}/assets/world/terrain/v0.12.6/manifest.json")
    # Standalone latest-Web catalog preparation; editor/UI adoption is P8.
    find_package(ZLIB REQUIRED)
    add_executable(territorial_library_catalog_tests ../tests/territorial_library_catalog_tests.cpp
        territoriallibrarycatalog.cpp)
    target_include_directories(territorial_library_catalog_tests PRIVATE .)
    target_link_libraries(territorial_library_catalog_tests PRIVATE Pandoeditor::Core Qt6::Core ZLIB::ZLIB)
    add_test(NAME territorial_library_catalog_tests COMMAND territorial_library_catalog_tests
        "${PROJECT_SOURCE_DIR}/assets/territorial-library-v2")
    add_executable(terrain_render_contract_tests ../tests/terrain_render_contract_tests.cpp
        ../renderer/terrainrendercontract.cpp)
    target_link_libraries(terrain_render_contract_tests PRIVATE Qt6::Test Qt6::Gui)
    add_test(NAME terrain_render_contract_tests COMMAND terrain_render_contract_tests -o -,txt)
    add_executable(terrain_gpu_display_tests ../tests/terrain_gpu_display_tests.cpp)
    target_link_libraries(terrain_gpu_display_tests PRIVATE pandoeditor_editor Qt6::Test Qt6::Quick)
    add_executable(terrain_official_pixel_tests ../tests/terrain_official_pixel_tests.cpp)
    target_link_libraries(terrain_official_pixel_tests PRIVATE pandoeditor_editor Qt6::Test Qt6::Quick)
    qt_add_resources(terrain_official_pixel_tests terrain_official_fixture PREFIX "/tests"
        BASE "${PROJECT_SOURCE_DIR}/tests" FILES "${PROJECT_SOURCE_DIR}/tests/terrain_gpu_display_fixture.qml")
    qt_add_resources(terrain_gpu_display_tests terrain_gpu_fixture PREFIX "/tests"
        BASE "${PROJECT_SOURCE_DIR}/tests" FILES "${PROJECT_SOURCE_DIR}/tests/terrain_gpu_display_fixture.qml")
    option(PANDOEDITOR_REGISTER_TERRAIN_GPU_DISPLAY "Register actual RHI terrain display execution" OFF)
    if(PANDOEDITOR_REGISTER_TERRAIN_GPU_DISPLAY)
        add_test(NAME terrain_official_pixel_tests COMMAND terrain_official_pixel_tests -o -,txt)
        set_tests_properties(terrain_official_pixel_tests PROPERTIES LABELS "hardware;terrain-display;official-data" TIMEOUT 180)
        add_test(NAME terrain_gpu_display_tests COMMAND terrain_gpu_display_tests
            "${PROJECT_SOURCE_DIR}/assets/world/terrain/v0.13.3/manifest.json" -o -,txt)
        set_tests_properties(terrain_gpu_display_tests PROPERTIES
            ENVIRONMENT "PANDOEDITOR_TERRAIN_GPU_DISPLAY=1" LABELS "hardware;terrain-display" TIMEOUT 180)
    endif()
    add_executable(resource_cache_diagnostics_tests ../tests/resource_cache_diagnostics_tests.cpp)
    target_link_libraries(resource_cache_diagnostics_tests PRIVATE pandoeditor_editor Qt6::Test)
    add_test(NAME resource_cache_diagnostics_tests COMMAND resource_cache_diagnostics_tests)
    add_executable(world_resource_cache_tests ../tests/world_resource_cache_tests.cpp)
    target_link_libraries(world_resource_cache_tests PRIVATE pandoeditor_editor)
    add_test(NAME world_resource_cache_tests COMMAND world_resource_cache_tests)
    add_executable(hydro_scheduler_resource_tests ../tests/hydro_scheduler_resource_tests.cpp)
    target_link_libraries(hydro_scheduler_resource_tests PRIVATE pandoeditor_editor Qt6::Test)
    add_test(NAME hydro_scheduler_resource_tests COMMAND hydro_scheduler_resource_tests)
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
    target_compile_definitions(autosave_tests PRIVATE
        PANDOEDITOR_WORLD_ASSET_DIR="${PROJECT_SOURCE_DIR}/assets/world")
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
    add_executable(physical_data_store_tests ../tests/physical_data_store_tests.cpp)
    target_link_libraries(physical_data_store_tests PRIVATE pandoeditor_editor Qt6::Test Qt6::Network)
    target_compile_definitions(physical_data_store_tests PRIVATE
        PHYSICAL_INVENTORY="${PROJECT_SOURCE_DIR}/assets/world/physical-inventory-c0bd31d1.json")
    add_test(NAME physical_data_store_tests COMMAND physical_data_store_tests -o -,txt)
    add_executable(view_navigation_tests ../tests/view_navigation_tests.cpp)
    target_link_libraries(view_navigation_tests PRIVATE pandoeditor_editor Qt6::Test)
    qt_add_resources(view_navigation_tests world_metadata
        PREFIX "/world" BASE "${PROJECT_SOURCE_DIR}/assets/world"
        FILES "${PROJECT_SOURCE_DIR}/assets/world/country-label-anchors-v0.10.1.json")
    qt_add_resources(view_navigation_tests sample PREFIX "/assets" BASE "${PROJECT_SOURCE_DIR}/assets"
        FILES "${PROJECT_SOURCE_DIR}/assets/sample.pando.json")
    add_test(NAME view_navigation_tests COMMAND view_navigation_tests -o -,txt)
    set_tests_properties(view_navigation_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
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
        add_test(NAME terrain_dem_source_contract COMMAND "${PANDOEDITOR_M72_NODE_EXECUTABLE}"
            --test "${PROJECT_SOURCE_DIR}/tools/terrain-dem-contract.test.mjs")
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
    target_sources(reference_image_tests PRIVATE referenceimagelibrary.cpp referencewarp.cpp referencetracing.cpp referenceimagemodel.h)
    target_include_directories(reference_image_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(reference_image_tests PRIVATE Qt6::Gui Qt6::Qml Qt6::Test)
    add_test(NAME reference_image_tests COMMAND reference_image_tests -o -,txt)
    add_executable(territory_selection_tests ../tests/territory_selection_tests.cpp)
    target_link_libraries(territory_selection_tests PRIVATE pandoeditor_editor Qt6::Test)
    add_test(NAME territory_selection_tests COMMAND territory_selection_tests -o -,txt)
    set_tests_properties(territory_selection_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
    add_executable(m972_selection_probe ../tests/m972_selection_probe.cpp)
    target_link_libraries(m972_selection_probe PRIVATE pandoeditor_editor)
    qt_add_resources(m972_selection_probe sample PREFIX "/assets" BASE "${PROJECT_SOURCE_DIR}/assets"
        FILES "${PROJECT_SOURCE_DIR}/assets/sample.pando.json")
    find_program(PANDOEDITOR_M972_NODE_EXECUTABLE NAMES node)
    if(PANDOEDITOR_M972_NODE_EXECUTABLE)
        add_test(NAME m972_controller_web_differential
            COMMAND "${CMAKE_COMMAND}" -E env
                "PANDO_M972_SELECTION_PROBE=$<TARGET_FILE:m972_selection_probe>"
                "${PANDOEDITOR_M972_NODE_EXECUTABLE}" --test
                "${PROJECT_SOURCE_DIR}/tools/m97/native-selection.test.mjs")
        set_tests_properties(m972_controller_web_differential PROPERTIES
            TIMEOUT 180 ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
    endif()
    add_executable(territorial_geometry_preview_tests ../tests/territorial_geometry_preview_tests.cpp)
    target_link_libraries(territorial_geometry_preview_tests PRIVATE pandoeditor_editor Qt6::Test)
    add_test(NAME territorial_geometry_preview_tests COMMAND territorial_geometry_preview_tests -o -,txt)
    set_tests_properties(territorial_geometry_preview_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
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
    add_executable(timeline_project_tests ../tests/timeline_project_tests.cpp)
    target_link_libraries(timeline_project_tests PRIVATE pandoeditor_editor)
    target_compile_definitions(timeline_project_tests PRIVATE
        PANDOEDITOR_TIMELINE_PROJECT_FIXTURES="${PROJECT_SOURCE_DIR}/tests/fixtures/timeline-project-v10.json")
    add_test(NAME timeline_project_tests COMMAND timeline_project_tests)
    if(PANDOEDITOR_M72_NODE_EXECUTABLE)
        add_test(NAME web_v10_exchange_source_contract
            COMMAND "${PANDOEDITOR_M72_NODE_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/web-v10-exchange-oracle.mjs" --verify-only)
        add_test(NAME web_v10_exchange_oracle_unit
            COMMAND "${PANDOEDITOR_M72_NODE_EXECUTABLE}" --test "${PROJECT_SOURCE_DIR}/tests/fixtures/web-v10-exchange/oracle.test.mjs")
        add_test(NAME web_v10_production_codec_cross_exchange
            COMMAND "${PANDOEDITOR_M72_NODE_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/web-v10-exchange-oracle.mjs"
                --native "$<TARGET_FILE:timeline_project_tests>" "${PROJECT_BINARY_DIR}/test-results/web-v10-exchange")
        set_tests_properties(web_v10_production_codec_cross_exchange PROPERTIES TIMEOUT 180)
    endif()
    add_executable(timeline_editor_persistence_tests ../tests/timeline_editor_persistence_tests.cpp)
    target_link_libraries(timeline_editor_persistence_tests PRIVATE pandoeditor_editor Qt6::Test)
    target_compile_definitions(timeline_editor_persistence_tests PRIVATE
        PANDOEDITOR_EXCHANGE_FIXTURES="${PROJECT_SOURCE_DIR}/tests/fixtures/web-v10-exchange")
    qt_add_resources(timeline_editor_persistence_tests sample PREFIX "/assets" BASE "${PROJECT_SOURCE_DIR}/assets"
        FILES "${PROJECT_SOURCE_DIR}/assets/sample.pando.json")
    qt_add_resources(timeline_editor_persistence_tests flag_fixtures PREFIX "/defaults/flags"
        BASE "${PROJECT_SOURCE_DIR}/assets/defaults/flags"
        FILES "${PROJECT_SOURCE_DIR}/assets/defaults/flags/country-flags.js"
              "${PROJECT_SOURCE_DIR}/assets/defaults/flags/native/gu.svg")
    add_test(NAME timeline_editor_persistence_tests COMMAND timeline_editor_persistence_tests -o -,txt)
    add_executable(editor_tests ../tests/editor_tests.cpp)
    target_link_libraries(editor_tests PRIVATE pandoeditor_editor)
    qt_add_resources(editor_tests sample PREFIX "/assets" BASE "${PROJECT_SOURCE_DIR}/assets"
        FILES "${PROJECT_SOURCE_DIR}/assets/sample.pando.json")
    add_test(NAME editor_tests COMMAND editor_tests)
    set_tests_properties(selection_editor_tests editor_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
    add_executable(ui_tests ../tests/ui_tests.cpp)
    add_executable(terrain_controller_display_tests ../tests/terrain_controller_display_tests.cpp)
    target_link_libraries(terrain_controller_display_tests PRIVATE pandoeditor_editor Qt6::QuickControls2 Qt6::Test)
    target_compile_definitions(terrain_controller_display_tests PRIVATE PANDOEDITOR_WORLD_ASSET_DIR="${PROJECT_SOURCE_DIR}/assets/world")
    target_link_libraries(ui_tests PRIVATE pandoeditor_editor Qt6::QuickControls2 Qt6::Test)
    target_compile_definitions(ui_tests PRIVATE PANDOEDITOR_WORLD_ASSET_DIR="${PROJECT_SOURCE_DIR}/assets/world")
    foreach(native_ui_target ui_tests terrain_controller_display_tests)
    qt_add_resources(${native_ui_target} ui PREFIX "/" BASE "${PROJECT_SOURCE_DIR}/ui"
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
    qt_add_resources(${native_ui_target} sample PREFIX "/assets" BASE "${PROJECT_SOURCE_DIR}/assets"
        FILES "${PROJECT_SOURCE_DIR}/assets/sample.pando.json")
    endforeach()
    # Exercise the production default flag resolver and image loading without
    # leaving other sample-country flags unresolved in the UI test engine.
    foreach(test_name presentation_editor_tests ui_tests terrain_controller_display_tests territorial_catalog_controller_tests)
        qt_add_resources(${test_name} test_default_flag PREFIX "/defaults/flags"
            BASE "${PROJECT_SOURCE_DIR}/assets/defaults/flags"
            FILES ${DEFAULT_FLAG_FILES})
    endforeach()
    add_test(NAME ui_tests COMMAND ui_tests -o -,txt)
    if(WIN32)
        file(MAKE_DIRECTORY "${PROJECT_BINARY_DIR}/test-results")
        add_test(NAME native_geometry_pointer_route COMMAND ui_tests nativeGeometryPointerRoutePreservesProject
            -o "${PROJECT_BINARY_DIR}/test-results/native-geometry-pointer-route.qt.txt,txt")
        set_tests_properties(native_geometry_pointer_route PROPERTIES TIMEOUT 180 ENVIRONMENT "QT_QPA_PLATFORM=windows;QT_QUICK_BACKEND=;QSG_RHI_BACKEND=d3d11")
    endif()
    if(WIN32)
        add_test(NAME terrain_controller_display_tests COMMAND terrain_controller_display_tests -o -,txt)
        set_tests_properties(terrain_controller_display_tests PROPERTIES TIMEOUT 180
            ENVIRONMENT "QT_QPA_PLATFORM=windows;QSG_RHI_BACKEND=d3d11")
    endif()
    # Aggregate software-rendered UI suite budget; individual waits and performance gates are unchanged.
    # The expanded suite exceeded 120 s on CI after 31 passing cases.
    set_tests_properties(ui_tests PROPERTIES TIMEOUT 180 ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
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

if(BUILD_TESTING)
    add_executable(river_selection_controller_tests ../tests/river_selection_controller_tests.cpp)
    target_link_libraries(river_selection_controller_tests PRIVATE pandoeditor_editor Qt6::Test)
    target_compile_definitions(river_selection_controller_tests PRIVATE WEB_HYDRO_FIXTURE="${PROJECT_SOURCE_DIR}/tests/fixtures/web-hydro")
    qt_add_resources(river_selection_controller_tests sample PREFIX "/assets" BASE "${PROJECT_SOURCE_DIR}/assets" FILES "${PROJECT_SOURCE_DIR}/assets/sample.pando.json")
    qt_add_resources(river_selection_controller_tests physical_metadata PREFIX "/world" BASE "${PROJECT_SOURCE_DIR}/assets/world"
        FILES "${PROJECT_SOURCE_DIR}/assets/world/physical-inventory-c0bd31d1.json"
              "${PROJECT_SOURCE_DIR}/assets/world/hydro/v0.13.1/manifest.json"
              "${PROJECT_SOURCE_DIR}/assets/world/terrain/v0.12.6/manifest.json")
    add_test(NAME river_selection_controller_tests COMMAND river_selection_controller_tests -o -,txt)
    set_tests_properties(river_selection_controller_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
endif()

if(BUILD_TESTING AND EXISTS "${PROJECT_SOURCE_DIR}/tests/river_selection_ui_tests.cpp")
    add_executable(river_selection_ui_tests ../tests/river_selection_ui_tests.cpp)
    target_link_libraries(river_selection_ui_tests PRIVATE pandoeditor_editor Qt6::QuickControls2 Qt6::Test)
    target_compile_definitions(river_selection_ui_tests PRIVATE PANDOEDITOR_WORLD_ASSET_DIR="${PROJECT_SOURCE_DIR}/assets/world" WEB_HYDRO_FIXTURE="${PROJECT_SOURCE_DIR}/tests/fixtures/web-hydro")
    qt_add_resources(river_selection_ui_tests ui PREFIX "/" BASE "${PROJECT_SOURCE_DIR}/ui"
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
    qt_add_resources(river_selection_ui_tests sample PREFIX "/assets" BASE "${PROJECT_SOURCE_DIR}/assets"
        FILES "${PROJECT_SOURCE_DIR}/assets/sample.pando.json")
    add_test(NAME river_selection_ui_tests COMMAND river_selection_ui_tests -o -,txt)
    set_tests_properties(river_selection_ui_tests PROPERTIES TIMEOUT 120 ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
endif()

if(BUILD_TESTING AND EXISTS "${PROJECT_SOURCE_DIR}/tests/m972_river_controller_probe.cpp")
    add_executable(m972_river_controller_probe ../tests/m972_river_controller_probe.cpp)
    target_link_libraries(m972_river_controller_probe PRIVATE pandoeditor_editor)
    qt_add_resources(m972_river_controller_probe sample PREFIX "/assets" BASE "${PROJECT_SOURCE_DIR}/assets" FILES "${PROJECT_SOURCE_DIR}/assets/sample.pando.json")
    qt_add_resources(m972_river_controller_probe metric_ui PREFIX "/metric" BASE "${PROJECT_SOURCE_DIR}/ui" FILES "${PROJECT_SOURCE_DIR}/ui/common/MapView.qml")
    add_test(NAME m972_metric_contract_tests COMMAND m972_river_controller_probe --metric-contract-self-test)
    set_tests_properties(m972_metric_contract_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
endif()

if(BUILD_TESTING)
    add_executable(river_area_tests ../tests/river_area_tests.cpp)
    target_link_libraries(river_area_tests PRIVATE pandoeditor_editor Qt6::Test)
    qt_add_resources(river_area_tests metric_ui PREFIX "/metric" BASE "${PROJECT_SOURCE_DIR}/ui" FILES "${PROJECT_SOURCE_DIR}/ui/common/MapView.qml")
    add_test(NAME river_area_tests COMMAND river_area_tests -o -,txt)
    set_tests_properties(river_area_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
endif()

if(BUILD_TESTING)
    add_executable(split_selection_controller_tests ../tests/split_selection_controller_tests.cpp)
    target_link_libraries(split_selection_controller_tests PRIVATE pandoeditor_editor Qt6::Test)
    qt_add_resources(split_selection_controller_tests sample PREFIX "/assets" BASE "${PROJECT_SOURCE_DIR}/assets" FILES "${PROJECT_SOURCE_DIR}/assets/sample.pando.json")
    add_test(NAME split_selection_controller_tests COMMAND split_selection_controller_tests -o -,txt)
    set_tests_properties(split_selection_controller_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
endif()

# M9.7.3 uses the pinned web cut worker through a bounded Qt adapter.
target_sources(pandoeditor_editor PRIVATE cutgeometrycalculator.cpp cutgeometrycalculator.h)
qt_add_resources(M973_CUT_RESOURCES "${PROJECT_SOURCE_DIR}/resources/m973_cut.qrc")
target_sources(pandoeditor_editor PRIVATE ${M973_CUT_RESOURCES})
if(BUILD_TESTING)
    add_executable(territory_cut_adapter_tests ../tests/territory_cut_adapter_tests.cpp
        territorycutadapter.cpp commandjobrunner.cpp commandjobrunner.h)
    target_include_directories(territory_cut_adapter_tests PRIVATE .)
    target_link_libraries(territory_cut_adapter_tests PRIVATE Pandoeditor::MapEngine Qt6::Test Qt6::Concurrent)
    add_test(NAME territory_cut_adapter_tests COMMAND territory_cut_adapter_tests -o -,txt)
    set_tests_properties(territory_cut_adapter_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
    add_executable(cut_geometry_calculator_tests ../tests/cut_geometry_calculator_tests.cpp)
    target_link_libraries(cut_geometry_calculator_tests PRIVATE pandoeditor_editor Qt6::Test)
    target_compile_definitions(cut_geometry_calculator_tests PRIVATE M973_CUT_FIXTURE="${PROJECT_SOURCE_DIR}/tests/fixtures/web-m97/cut-corpus.json")
    add_test(NAME cut_geometry_calculator_tests COMMAND cut_geometry_calculator_tests -o -,txt)
    set_tests_properties(cut_geometry_calculator_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
endif()
if(BUILD_TESTING)
    add_executable(split_geometry_lifecycle_tests ../tests/split_geometry_lifecycle_tests.cpp)
    target_link_libraries(split_geometry_lifecycle_tests PRIVATE pandoeditor_editor Qt6::Test ZLIB::ZLIB)
    add_test(NAME split_geometry_lifecycle_tests COMMAND split_geometry_lifecycle_tests -o -,txt)
    set_tests_properties(split_geometry_lifecycle_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
endif()
if(BUILD_TESTING)
    add_executable(m973_cut_kernel_probe ../tools/m97/cut-kernel-probe.cpp)
    target_link_libraries(m973_cut_kernel_probe PRIVATE pandoeditor_editor)
endif()
if(BUILD_TESTING)
    add_executable(m973_split_controller_probe ../tests/m973_split_controller_probe.cpp)
    target_link_libraries(m973_split_controller_probe PRIVATE pandoeditor_editor)
    qt_add_resources(m973_split_controller_probe sample PREFIX "/assets" BASE "${PROJECT_SOURCE_DIR}/assets" FILES "${PROJECT_SOURCE_DIR}/assets/sample.pando.json")
endif()

# Explicitly approved geographic seam correction, isolated from historical pins.
target_sources(pandoeditor_editor PRIVATE splitgeometrynormalizer.cpp splitgeometrynormalizer.h)
qt_add_resources(M973_SPLIT_NORMALIZER_RESOURCES "${PROJECT_SOURCE_DIR}/resources/m973_split_normalizer.qrc")
target_sources(pandoeditor_editor PRIVATE ${M973_SPLIT_NORMALIZER_RESOURCES})
if(BUILD_TESTING)
    add_executable(split_geometry_normalizer_tests ../tests/split_geometry_normalizer_tests.cpp)
    target_link_libraries(split_geometry_normalizer_tests PRIVATE pandoeditor_editor Qt6::Test)
    target_compile_definitions(split_geometry_normalizer_tests PRIVATE M973_SPLIT_NORMALIZER_FIXTURE="${PROJECT_SOURCE_DIR}/assets/geometry/split-corrections/normalizer-cases.json")
    add_test(NAME split_geometry_normalizer_tests COMMAND split_geometry_normalizer_tests -o -,txt)
    set_tests_properties(split_geometry_normalizer_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
endif()
if(BUILD_TESTING)
    find_program(M973_NODE_EXECUTABLE node)
    if(M973_NODE_EXECUTABLE)
        add_test(NAME m973_split_controller_contract
            COMMAND "${CMAKE_COMMAND}" -E env "PANDO_M973_SPLIT_PROBE=$<TARGET_FILE:m973_split_controller_probe>"
                "${M973_NODE_EXECUTABLE}" --test "${PROJECT_SOURCE_DIR}/tools/m97/split-differential-native.test.mjs")
        set_tests_properties(m973_split_controller_contract PROPERTIES TIMEOUT 180
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
    endif()
endif()

if(BUILD_TESTING)
    add_executable(m974_boundary_session_tests ../tests/m974_boundary_session_tests.cpp)
    target_link_libraries(m974_boundary_session_tests PRIVATE pandoeditor_editor Qt6::Test)
    add_test(NAME m974_boundary_session_tests COMMAND m974_boundary_session_tests -o -,txt)
    set_tests_properties(m974_boundary_session_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
endif()
if(BUILD_TESTING)
    add_executable(m974_snap_workflow_tests ../tests/m974_snap_workflow_tests.cpp)
    target_link_libraries(m974_snap_workflow_tests PRIVATE pandoeditor_editor Qt6::Test)
    add_test(NAME m974_snap_workflow_tests COMMAND m974_snap_workflow_tests -o -,txt)
    set_tests_properties(m974_snap_workflow_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
endif()
if(BUILD_TESTING)
    add_executable(edit_screen_controller_tests ../tests/edit_screen_controller_tests.cpp)
    target_link_libraries(edit_screen_controller_tests PRIVATE pandoeditor_editor Qt6::Test)
    target_compile_options(edit_screen_controller_tests PRIVATE
        "$<$<CXX_COMPILER_ID:MSVC>:/fp:strict>" "$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-ffp-contract=off>")
    qt_add_resources(edit_screen_controller_tests sample PREFIX "/assets" BASE "${PROJECT_SOURCE_DIR}/assets" FILES "${PROJECT_SOURCE_DIR}/assets/sample.pando.json")
    add_test(NAME edit_screen_controller_tests COMMAND edit_screen_controller_tests -o -,txt)
    set_tests_properties(edit_screen_controller_tests PROPERTIES TIMEOUT 180 ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
endif()
if(BUILD_TESTING)
    qt_add_resources(m974_boundary_session_tests sample PREFIX "/assets" BASE "${PROJECT_SOURCE_DIR}/assets" FILES "${PROJECT_SOURCE_DIR}/assets/sample.pando.json")
    qt_add_resources(m974_snap_workflow_tests sample PREFIX "/assets" BASE "${PROJECT_SOURCE_DIR}/assets" FILES "${PROJECT_SOURCE_DIR}/assets/sample.pando.json")
endif()
if(BUILD_TESTING)
    add_executable(m974_boundary_hierarchy_tests ../tests/m974_boundary_hierarchy_tests.cpp)
    target_link_libraries(m974_boundary_hierarchy_tests PRIVATE pandoeditor_editor Qt6::Test)
    add_test(NAME m974_boundary_hierarchy_tests COMMAND m974_boundary_hierarchy_tests -o -,txt)
    set_tests_properties(m974_boundary_hierarchy_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
endif()

target_sources(pandoeditor_editor PRIVATE
    geometrysnapprovider.cpp geometrysnapprovider.h
    sharedboundarycontroller.cpp)
if(BUILD_TESTING)
    add_executable(m974_snap_provider_tests ../tests/m974_snap_provider_tests.cpp)
    target_link_libraries(m974_snap_provider_tests PRIVATE pandoeditor_editor Qt6::Test)
    add_test(NAME m974_snap_provider_tests COMMAND m974_snap_provider_tests -o -,txt)
endif()
target_sources(pandoeditor_editor PRIVATE editorsnapping.cpp)
if(BUILD_TESTING)
    add_executable(m974_snap_probe ../tests/m974_snap_probe.cpp)
    target_link_libraries(m974_snap_probe PRIVATE pandoeditor_editor)
endif()
if(BUILD_TESTING)
    add_executable(m974_snap_ui_tests ../tests/m974_snap_ui_tests.cpp)
    target_link_libraries(m974_snap_ui_tests PRIVATE pandoeditor_editor Qt6::QuickControls2 Qt6::Test)
    file(GLOB_RECURSE M974_UI_FILES CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/ui/*.qml" "${PROJECT_SOURCE_DIR}/ui/*.js" "${PROJECT_SOURCE_DIR}/ui/fonts/*.otf")
    qt_add_resources(m974_snap_ui_tests m974_ui PREFIX "/" BASE "${PROJECT_SOURCE_DIR}/ui" FILES ${M974_UI_FILES})
    qt_add_resources(m974_snap_ui_tests sample PREFIX "/assets" BASE "${PROJECT_SOURCE_DIR}/assets" FILES "${PROJECT_SOURCE_DIR}/assets/sample.pando.json")
    add_test(NAME m974_snap_ui_tests COMMAND m974_snap_ui_tests -o -,txt)
    set_tests_properties(m974_snap_ui_tests PROPERTIES TIMEOUT 90 ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
endif()
if(BUILD_TESTING)
    add_executable(edit_screen_ui_tests ../tests/edit_screen_ui_tests.cpp)
    target_link_libraries(edit_screen_ui_tests PRIVATE pandoeditor_editor Qt6::QuickControls2 Qt6::Test)
    target_compile_options(edit_screen_ui_tests PRIVATE
        "$<$<CXX_COMPILER_ID:MSVC>:/fp:strict>" "$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-ffp-contract=off>")
    qt_add_resources(edit_screen_ui_tests m976_ui PREFIX "/" BASE "${PROJECT_SOURCE_DIR}/ui" FILES ${M974_UI_FILES})
    qt_add_resources(edit_screen_ui_tests sample PREFIX "/assets" BASE "${PROJECT_SOURCE_DIR}/assets" FILES "${PROJECT_SOURCE_DIR}/assets/sample.pando.json")
    add_test(NAME edit_screen_ui_tests COMMAND edit_screen_ui_tests -o -,txt)
    set_tests_properties(edit_screen_ui_tests PROPERTIES TIMEOUT 180 ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
endif()
if(BUILD_TESTING)
    add_executable(edit_display_ui_tests ../tests/edit_display_ui_tests.cpp)
    target_link_libraries(edit_display_ui_tests PRIVATE pandoeditor_editor Qt6::QuickControls2 Qt6::Test)
    target_compile_options(edit_display_ui_tests PRIVATE
        "$<$<CXX_COMPILER_ID:MSVC>:/fp:strict>" "$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-ffp-contract=off>")
    qt_add_resources(edit_display_ui_tests m976_display_ui PREFIX "/" BASE "${PROJECT_SOURCE_DIR}/ui" FILES ${M974_UI_FILES})
    qt_add_resources(edit_display_ui_tests sample PREFIX "/assets" BASE "${PROJECT_SOURCE_DIR}/assets" FILES "${PROJECT_SOURCE_DIR}/assets/sample.pando.json")
    add_test(NAME edit_display_ui_tests COMMAND edit_display_ui_tests -o -,txt)
    set_tests_properties(edit_display_ui_tests PROPERTIES TIMEOUT 180 ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
endif()
if(BUILD_TESTING)
    add_executable(m974_boundary_probe ../tests/m974_boundary_probe.cpp)
    target_link_libraries(m974_boundary_probe PRIVATE pandoeditor_editor)
    qt_add_resources(m974_boundary_probe sample PREFIX "/assets" BASE "${PROJECT_SOURCE_DIR}/assets" FILES "${PROJECT_SOURCE_DIR}/assets/sample.pando.json")
endif()
if(BUILD_TESTING)
    add_executable(m974_snap_provider_probe ../tests/m974_snap_provider_probe.cpp)
    target_link_libraries(m974_snap_provider_probe PRIVATE pandoeditor_editor)
    add_test(NAME m974_snap_provider_probe_contract COMMAND m974_snap_provider_probe --self-test)
endif()
if(BUILD_TESTING)
    add_executable(m974_snap_benchmark ../tests/m974_snap_benchmark.cpp)
    target_link_libraries(m974_snap_benchmark PRIVATE pandoeditor_editor)
endif()
if(BUILD_TESTING)
    add_executable(m974_boundary_ui_tests ../tests/m974_boundary_ui_tests.cpp)
    target_link_libraries(m974_boundary_ui_tests PRIVATE pandoeditor_editor Qt6::QuickControls2 Qt6::Test)
    qt_add_resources(m974_boundary_ui_tests m974_ui PREFIX "/" BASE "${PROJECT_SOURCE_DIR}/ui" FILES ${M974_UI_FILES})
    qt_add_resources(m974_boundary_ui_tests sample PREFIX "/assets" BASE "${PROJECT_SOURCE_DIR}/assets" FILES "${PROJECT_SOURCE_DIR}/assets/sample.pando.json")
    add_test(NAME m974_boundary_ui_tests COMMAND m974_boundary_ui_tests -o -,txt)
    set_tests_properties(m974_boundary_ui_tests PROPERTIES TIMEOUT 180 ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
endif()
if(BUILD_TESTING)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_test(NAME m974_regression_gate_tests COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tests/m974_regression_gate_tests.py")
endif()

if(BUILD_TESTING)
    add_test(NAME m974_boundary_delivery_tests
        COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tests/m974_boundary_delivery_tests.py"
            --probe "$<TARGET_FILE:m974_boundary_probe>"
            --cases "${PROJECT_SOURCE_DIR}/tests/fixtures/web-m974/boundary-cases.json")
    set_tests_properties(m974_boundary_delivery_tests PROPERTIES TIMEOUT 180
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
endif()

if(BUILD_TESTING)
    find_program(M974_NODE_EXECUTABLE node REQUIRED)
    add_test(NAME m974_native_boundary_contract COMMAND "${M974_NODE_EXECUTABLE}" --test "${PROJECT_SOURCE_DIR}/tools/m974-native-boundary.test.mjs")
    set_tests_properties(m974_native_boundary_contract PROPERTIES TIMEOUT 120)
endif()

if(BUILD_TESTING)
    add_test(NAME m974_native_snap_contract COMMAND "${M974_NODE_EXECUTABLE}" --test "${PROJECT_SOURCE_DIR}/tools/m974-native-snap.test.mjs")
    set_tests_properties(m974_native_snap_contract PROPERTIES TIMEOUT 120)
endif()

if(BUILD_TESTING)
    add_executable(m974_boundary_supplemental_tests ../tests/m974_boundary_supplemental_tests.cpp)
    target_link_libraries(m974_boundary_supplemental_tests PRIVATE pandoeditor_editor Qt6::Test ZLIB::ZLIB)
    add_test(NAME m974_boundary_supplemental_tests COMMAND m974_boundary_supplemental_tests -o -,txt)
    set_tests_properties(m974_boundary_supplemental_tests PROPERTIES TIMEOUT 180
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
endif()

if(BUILD_TESTING)
    add_executable(m974_snap_order_tests ../tests/m974_snap_order_tests.cpp)
    target_link_libraries(m974_snap_order_tests PRIVATE pandoeditor_editor Qt6::Test)
    qt_add_resources(m974_snap_order_tests sample PREFIX "/assets" BASE "${PROJECT_SOURCE_DIR}/assets" FILES "${PROJECT_SOURCE_DIR}/assets/sample.pando.json")
    add_test(NAME m974_snap_order_tests COMMAND m974_snap_order_tests -o -,txt)
    set_tests_properties(m974_snap_order_tests PROPERTIES TIMEOUT 180
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
endif()

if(BUILD_TESTING)
    add_test(NAME m974_native_supplemental_contract COMMAND "${M974_NODE_EXECUTABLE}" --test "${PROJECT_SOURCE_DIR}/tools/m974-native-supplemental.test.mjs")
    set_tests_properties(m974_native_supplemental_contract PROPERTIES TIMEOUT 120)
endif()

if(BUILD_TESTING)
    add_test(NAME m974_source_order_contract COMMAND "${M974_NODE_EXECUTABLE}" --test
        "${PROJECT_SOURCE_DIR}/tools/m974-native-source-order.test.mjs"
        "${PROJECT_SOURCE_DIR}/tools/m974-snap-boundary/source-order-v2.test.mjs")
    set_tests_properties(m974_source_order_contract PROPERTIES TIMEOUT 120)
    add_test(NAME m974_source_history_contract COMMAND "${M974_NODE_EXECUTABLE}" --test
        "${PROJECT_SOURCE_DIR}/tools/m974-native-source-history.test.mjs"
        "${PROJECT_SOURCE_DIR}/tools/m974-snap-boundary/source-history-runtime.test.mjs"
        "${PROJECT_SOURCE_DIR}/tools/m974-snap-boundary/source-history-selection.test.mjs"
        "${PROJECT_SOURCE_DIR}/tools/m974-snap-boundary/source-history-sources.test.mjs"
        "${PROJECT_SOURCE_DIR}/tools/m974-snap-boundary/source-history-suite.test.mjs")
    set_tests_properties(m974_source_history_contract PROPERTIES TIMEOUT 120)
endif()

if(BUILD_TESTING)
    add_executable(m975_model_exchange_probe ../tests/m975_model_exchange_probe.cpp)
    target_link_libraries(m975_model_exchange_probe PRIVATE pandoeditor_editor)
    target_compile_definitions(m975_model_exchange_probe PRIVATE
        M975_EXCHANGE_FIXTURES="${PROJECT_SOURCE_DIR}/tests/fixtures/web-v10-exchange")
    qt_add_resources(m975_model_exchange_probe sample PREFIX "/assets" BASE "${PROJECT_SOURCE_DIR}/assets"
        FILES "${PROJECT_SOURCE_DIR}/assets/sample.pando.json")
    add_test(NAME m975_model_exchange_probe_contract COMMAND m975_model_exchange_probe --self-test)
    set_tests_properties(m975_model_exchange_probe_contract PROPERTIES TIMEOUT 180
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
endif()

if(BUILD_TESTING)
    add_executable(m975_geometry_provenance_codec_tests ../tests/m975_geometry_provenance_codec_tests.cpp)
    target_link_libraries(m975_geometry_provenance_codec_tests PRIVATE pandoeditor_editor Qt6::Test)
    target_compile_definitions(m975_geometry_provenance_codec_tests PRIVATE
        M975_OWNERSHIP_FIXTURES="${PROJECT_SOURCE_DIR}/tests/fixtures/web-m975-model-exchange")
    add_test(NAME m975_geometry_provenance_codec_tests COMMAND m975_geometry_provenance_codec_tests -o -,txt)
    set_tests_properties(m975_geometry_provenance_codec_tests PROPERTIES TIMEOUT 180
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
endif()

# Independent pending-input observation route; existing parity gates stay intact.
if(BUILD_TESTING)
    add_executable(m977_pending_lifecycle_probe ../tests/m977_pending_lifecycle_probe.cpp)
    target_link_libraries(m977_pending_lifecycle_probe PRIVATE pandoeditor_editor)
    qt_add_resources(m977_pending_lifecycle_probe sample PREFIX "/assets" BASE "${PROJECT_SOURCE_DIR}/assets"
        FILES "${PROJECT_SOURCE_DIR}/assets/sample.pando.json")
    add_test(NAME m977_pending_lifecycle_contract COMMAND "${M974_NODE_EXECUTABLE}" --test
        "${PROJECT_SOURCE_DIR}/tools/m977-pending-lifecycle/runtime.test.mjs"
        "${PROJECT_SOURCE_DIR}/tools/m977-pending-lifecycle/contract.test.mjs"
        "${PROJECT_SOURCE_DIR}/tools/m977-pending-lifecycle/native.test.mjs"
        "${PROJECT_SOURCE_DIR}/tools/m977-pending-lifecycle/suite.test.mjs"
        "${PROJECT_SOURCE_DIR}/tests/m977-pending-lifecycle-integration.test.mjs")
    set_tests_properties(m977_pending_lifecycle_contract PROPERTIES TIMEOUT 180
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;M977_PENDING_LIFECYCLE_PROBE=$<TARGET_FILE:m977_pending_lifecycle_probe>")

    add_executable(m977_pending_input_probe ../tests/m977_pending_input_probe.cpp)
    target_link_libraries(m977_pending_input_probe PRIVATE pandoeditor_editor)
    qt_add_resources(m977_pending_input_probe sample PREFIX "/assets" BASE "${PROJECT_SOURCE_DIR}/assets"
        FILES "${PROJECT_SOURCE_DIR}/assets/sample.pando.json")
    add_test(NAME m977_pending_input_contract COMMAND "${M974_NODE_EXECUTABLE}" --test
        "${PROJECT_SOURCE_DIR}/tools/m977-pending-input/sources.test.mjs"
        "${PROJECT_SOURCE_DIR}/tools/m977-pending-input/runtime.test.mjs"
        "${PROJECT_SOURCE_DIR}/tools/m977-pending-input/suite.test.mjs"
        "${PROJECT_SOURCE_DIR}/tools/m977-pending-input/browser-runner.test.mjs"
        "${PROJECT_SOURCE_DIR}/tools/m977-pending-input/native.test.mjs"
        "${PROJECT_SOURCE_DIR}/tools/m977-pending-input/compare.test.mjs")
    set_tests_properties(m977_pending_input_contract PROPERTIES TIMEOUT 180
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;PANDO_M977_NATIVE_PROBE=$<TARGET_FILE:m977_pending_input_probe>")
endif()

# Observation-only boundary timing route; known divergences remain explicit.
if(BUILD_TESTING)
    add_executable(m977_boundary_timing_probe ../tools/m977-boundary-adoption/native-probe.cpp)
    target_include_directories(m977_boundary_timing_probe PRIVATE "${PROJECT_SOURCE_DIR}/tests")
    target_link_libraries(m977_boundary_timing_probe PRIVATE pandoeditor_editor)
    qt_add_resources(m977_boundary_timing_probe sample PREFIX "/assets" BASE "${PROJECT_SOURCE_DIR}/assets"
        FILES "${PROJECT_SOURCE_DIR}/assets/sample.pando.json")
    add_test(NAME m977_boundary_timing_contract COMMAND "${M974_NODE_EXECUTABLE}" --test
        "${PROJECT_SOURCE_DIR}/tools/m977-boundary-adoption/runtime.test.mjs"
        "${PROJECT_SOURCE_DIR}/tools/m977-boundary-adoption/suite.test.mjs"
        "${PROJECT_SOURCE_DIR}/tools/m977-boundary-adoption/browser-runner.test.mjs"
        "${PROJECT_SOURCE_DIR}/tools/m977-boundary-adoption/native.test.mjs"
        "${PROJECT_SOURCE_DIR}/tools/m977-boundary-integration.test.mjs")
    set_tests_properties(m977_boundary_timing_contract PROPERTIES TIMEOUT 180
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;M977_BOUNDARY_PROBE=$<TARGET_FILE:m977_boundary_timing_probe>")
endif()

# Two matched public session-replacement cases. Recorded legacy limits remain explicit.
if(BUILD_TESTING)
    add_executable(m977_boundary_session_replacement_probe ../tests/m977_boundary_session_replacement_probe.cpp)
    target_link_libraries(m977_boundary_session_replacement_probe PRIVATE pandoeditor_editor)
    qt_add_resources(m977_boundary_session_replacement_probe sample PREFIX "/assets" BASE "${PROJECT_SOURCE_DIR}/assets"
        FILES "${PROJECT_SOURCE_DIR}/assets/sample.pando.json")
    add_test(NAME m977_boundary_session_replacement_contract COMMAND "${M974_NODE_EXECUTABLE}" --test
        "${PROJECT_SOURCE_DIR}/tools/m977-boundary-session-replacement/runtime.test.mjs"
        "${PROJECT_SOURCE_DIR}/tools/m977-boundary-session-replacement/suite.test.mjs"
        "${PROJECT_SOURCE_DIR}/tools/m977-boundary-session-replacement/browser-runner.test.mjs"
        "${PROJECT_SOURCE_DIR}/tools/m977-boundary-session-replacement/native.test.mjs"
        "${PROJECT_SOURCE_DIR}/tools/m977-boundary-session-replacement/gate.test.mjs"
        "${PROJECT_SOURCE_DIR}/tools/m977-boundary-session-integration.test.mjs")
    set_tests_properties(m977_boundary_session_replacement_contract PROPERTIES TIMEOUT 180
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;M977_SESSION_REPLACEMENT_PROBE=$<TARGET_FILE:m977_boundary_session_replacement_probe>")
endif()

set(REFERENCE_WEB_FILES reference-web/runtime.js)
qt_add_resources(pandoeditor_editor reference_web PREFIX "/reference-web" BASE "${CMAKE_CURRENT_SOURCE_DIR}/reference-web" FILES ${REFERENCE_WEB_FILES})
if(BUILD_TESTING)
    qt_add_resources(reference_image_tests reference_web_test PREFIX "/reference-web" BASE "${CMAKE_CURRENT_SOURCE_DIR}/reference-web" FILES ${REFERENCE_WEB_FILES})
endif()

if(BUILD_TESTING)
    qt_add_resources(edit_display_ui_tests reference_ui_flags PREFIX "/defaults/flags" BASE "${PROJECT_SOURCE_DIR}/assets/defaults/flags" FILES ${DEFAULT_FLAG_FILES})
endif()

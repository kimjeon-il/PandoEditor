configure_file(src/version.cpp.in generated/version.cpp @ONLY)
find_package(ZLIB REQUIRED)

add_library(pandoeditor_core STATIC
    src/geobounds.cpp
    src/spatialindex.cpp
    src/hydroformat.cpp
    src/hydroviewport.cpp
    src/maprenderorder.cpp
    src/project.cpp
    src/presentation.cpp
    src/presentationcommands.cpp
    src/document.cpp
    src/temporal.cpp
    src/timeline-records.cpp
    src/historicallibrary.cpp
    src/historicalinstantiation.cpp
    src/gisexchange.cpp
    src/geopackagegeometry.cpp
    src/giszip.cpp
    src/content.cpp
    src/objectproperties.cpp
    src/geometrypredicates.cpp
    src/territorialmutation.cpp
    src/commands.cpp
    src/jobs.cpp
    src/snapshot.cpp
    "${CMAKE_CURRENT_BINARY_DIR}/generated/version.cpp"
)
add_library(Pandoeditor::Core ALIAS pandoeditor_core)

target_include_directories(pandoeditor_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
target_compile_features(pandoeditor_core PUBLIC cxx_std_17)
target_link_libraries(pandoeditor_core PUBLIC ZLIB::ZLIB)
set_target_properties(pandoeditor_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(BUILD_TESTING)
    add_executable(m72_geobounds_tests ../tests/m72_geobounds_tests.cpp)
    target_link_libraries(m72_geobounds_tests PRIVATE Pandoeditor::Core)
    add_test(NAME m72_geobounds_tests COMMAND m72_geobounds_tests)
    add_executable(m72_spatial_index_tests ../tests/m72_spatial_index_tests.cpp)
    target_link_libraries(m72_spatial_index_tests PRIVATE Pandoeditor::Core)
    add_test(NAME m72_spatial_index_tests COMMAND m72_spatial_index_tests)
    add_executable(m72_geobounds_probe ../tests/m72_geobounds_probe.cpp)
    target_link_libraries(m72_geobounds_probe PRIVATE Pandoeditor::Core)
    add_executable(temporal_tests ../tests/temporal_tests.cpp)
    target_link_libraries(temporal_tests PRIVATE Pandoeditor::Core)
    add_test(NAME temporal_tests COMMAND temporal_tests)
    add_executable(historical_library_tests ../tests/historical_library_tests.cpp)
    target_link_libraries(historical_library_tests PRIVATE Pandoeditor::Core)
    add_test(NAME historical_library_tests COMMAND historical_library_tests)
    add_executable(historical_instantiation_tests ../tests/historical_instantiation_tests.cpp)
    target_link_libraries(historical_instantiation_tests PRIVATE Pandoeditor::Core)
    add_test(NAME historical_instantiation_tests COMMAND historical_instantiation_tests)
    add_executable(gis_exchange_tests ../tests/gis_exchange_tests.cpp)
    target_link_libraries(gis_exchange_tests PRIVATE Pandoeditor::Core)
    add_test(NAME gis_exchange_tests COMMAND gis_exchange_tests)
    add_executable(gis_generic_import_tests ../tests/gis_generic_import_tests.cpp)
    target_link_libraries(gis_generic_import_tests PRIVATE Pandoeditor::Core)
    add_test(NAME gis_generic_import_tests COMMAND gis_generic_import_tests)
    add_executable(gis_allocation_tests ../tests/gis_allocation_tests.cpp)
    target_link_libraries(gis_allocation_tests PRIVATE Pandoeditor::Core)
    add_test(NAME gis_allocation_tests COMMAND gis_allocation_tests)
    add_executable(gis_territorial_core_tests ../tests/gis_territorial_core_tests.cpp)
    target_link_libraries(gis_territorial_core_tests PRIVATE Pandoeditor::Core)
    add_test(NAME gis_territorial_core_tests COMMAND gis_territorial_core_tests)
    add_executable(gis_distribution_import_tests ../tests/gis_distribution_import_tests.cpp)
    target_link_libraries(gis_distribution_import_tests PRIVATE Pandoeditor::Core)
    add_test(NAME gis_distribution_import_tests COMMAND gis_distribution_import_tests)
    add_executable(geopackage_geometry_tests ../tests/geopackage_geometry_tests.cpp)
    target_link_libraries(geopackage_geometry_tests PRIVATE Pandoeditor::Core)
    add_test(NAME geopackage_geometry_tests COMMAND geopackage_geometry_tests)
    add_executable(gis_zip_format_tests ../tests/gis_zip_format_tests.cpp)
    target_link_libraries(gis_zip_format_tests PRIVATE Pandoeditor::Core)
    target_compile_definitions(gis_zip_format_tests PRIVATE
        WEB_GIS_ZIP_FIXTURE="${PROJECT_SOURCE_DIR}/tests/fixtures/web-m6/web-gis-geojson.zip")
    add_test(NAME gis_zip_format_tests COMMAND gis_zip_format_tests)
    add_executable(gis_zip_export_tests ../tests/gis_zip_export_tests.cpp)
    target_link_libraries(gis_zip_export_tests PRIVATE Pandoeditor::Core)
    add_test(NAME gis_zip_export_tests COMMAND gis_zip_export_tests)
    add_executable(gis_exchange_probe ../tests/gis_exchange_probe.cpp)
    target_link_libraries(gis_exchange_probe PRIVATE Pandoeditor::Core)
    add_executable(historical_library_probe ../tests/historical_library_probe.cpp)
    target_link_libraries(historical_library_probe PRIVATE Pandoeditor::Core)
    add_executable(temporal_probe ../tests/temporal_probe.cpp)
    target_link_libraries(temporal_probe PRIVATE Pandoeditor::Core)
    find_program(PANDOEDITOR_M6_NODE_EXECUTABLE NAMES node)
    if(PANDOEDITOR_M6_NODE_EXECUTABLE)
        set(PANDOEDITOR_TIMELINE_FIXTURES "${CMAKE_CURRENT_BINARY_DIR}/generated/timeline-records-fixtures.h")
        add_custom_command(OUTPUT "${PANDOEDITOR_TIMELINE_FIXTURES}"
            COMMAND "${PANDOEDITOR_M6_NODE_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/tools/generate-timeline-records-tests.mjs" "${PANDOEDITOR_TIMELINE_FIXTURES}"
            DEPENDS "${PROJECT_SOURCE_DIR}/tools/generate-timeline-records-tests.mjs"
                "${PROJECT_SOURCE_DIR}/tests/fixtures/timeline-records-cases.mjs"
                "${PROJECT_SOURCE_DIR}/tests/fixtures/timeline-records.json"
            VERBATIM)
        add_executable(timeline_records_tests ../tests/timeline_records_tests.cpp "${PANDOEDITOR_TIMELINE_FIXTURES}")
        target_include_directories(timeline_records_tests PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
        target_link_libraries(timeline_records_tests PRIVATE Pandoeditor::Core)
        add_test(NAME timeline_records_tests COMMAND timeline_records_tests)
        add_test(NAME m72_geobounds_corpus
            COMMAND "${CMAKE_COMMAND}" -E env
                "M72_BOUNDS_PROBE=$<TARGET_FILE:m72_geobounds_probe>"
                "${PANDOEDITOR_M6_NODE_EXECUTABLE}" --test
                "${PROJECT_SOURCE_DIR}/tools/m72-geobounds-corpus.test.mjs")
        add_test(NAME m6_web_gis_zip_fixture
            COMMAND "${PANDOEDITOR_M6_NODE_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/tools/m6-gis-zip-fixture.mjs" --verify)
        add_test(NAME m6_gis_exchange_web_parity
            COMMAND "${PANDOEDITOR_M6_NODE_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/tools/m6-gis-oracle.mjs" "$<TARGET_FILE:gis_exchange_probe>")
        add_test(NAME m6_temporal_web_parity
            COMMAND "${PANDOEDITOR_M6_NODE_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/tools/m6-temporal-oracle.mjs" "$<TARGET_FILE:temporal_probe>")
        add_test(NAME m6_historical_catalog_web_parity
            COMMAND "${PANDOEDITOR_M6_NODE_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/tools/m6-historical-oracle.mjs" "$<TARGET_FILE:historical_library_probe>")
    endif()
    find_package(ZLIB REQUIRED)
    add_executable(hydro_format_tests ../tests/hydro_format_tests.cpp)
    target_link_libraries(hydro_format_tests PRIVATE Pandoeditor::Core ZLIB::ZLIB)
    target_compile_definitions(hydro_format_tests PRIVATE WEB_HYDRO_FIXTURE="${PROJECT_SOURCE_DIR}/tests/fixtures/web-hydro")
    add_test(NAME hydro_format_tests COMMAND hydro_format_tests)
    add_executable(hydro_allocation_tests ../tests/hydro_allocation_tests.cpp)
    target_link_libraries(hydro_allocation_tests PRIVATE Pandoeditor::Core ZLIB::ZLIB)
    target_compile_definitions(hydro_allocation_tests PRIVATE WEB_HYDRO_FIXTURE="${PROJECT_SOURCE_DIR}/tests/fixtures/web-hydro")
    add_test(NAME hydro_allocation_tests COMMAND hydro_allocation_tests)
    add_executable(hydro_viewport_probe ../tests/hydro_viewport_probe.cpp)
    target_link_libraries(hydro_viewport_probe PRIVATE Pandoeditor::Core)
    add_executable(map_render_order_probe ../tests/map_render_order_probe.cpp)
    target_link_libraries(map_render_order_probe PRIVATE Pandoeditor::Core)
    add_executable(presentation_tests ../tests/presentation_tests.cpp)
    target_link_libraries(presentation_tests PRIVATE Pandoeditor::Core)
    add_test(NAME presentation_tests COMMAND presentation_tests)
    find_package(Qt6 REQUIRED COMPONENTS Core)
    add_executable(content_probe ../tests/content_probe.cpp)
    target_link_libraries(content_probe PRIVATE Pandoeditor::Core Qt6::Core)
    add_executable(presentation_parity_probe ../tests/presentation_parity_probe.cpp)
    target_link_libraries(presentation_parity_probe PRIVATE Pandoeditor::Core Qt6::Core)
    add_executable(command_allocation_tests ../tests/command_allocation_tests.cpp)
    target_link_libraries(command_allocation_tests PRIVATE Pandoeditor::Core)
    add_test(NAME command_allocation_tests COMMAND command_allocation_tests)
    add_executable(command_tests ../tests/command_tests.cpp)
    target_link_libraries(command_tests PRIVATE Pandoeditor::Core)
    add_test(NAME command_tests COMMAND command_tests)
    add_executable(core_tests ../tests/core_tests.cpp)
    target_link_libraries(core_tests PRIVATE Pandoeditor::Core)
    add_test(NAME core_tests COMMAND core_tests)
    add_executable(model_tests ../tests/model_tests.cpp)
    target_link_libraries(model_tests PRIVATE Pandoeditor::Core)
    add_test(NAME model_tests COMMAND model_tests)
    add_executable(structure_probe ../tests/structure_probe.cpp)
    target_link_libraries(structure_probe PRIVATE Pandoeditor::Core Qt6::Core)
    if(PANDOEDITOR_M6_NODE_EXECUTABLE)
        add_test(NAME m33_structure_web_parity
            COMMAND "${PANDOEDITOR_M6_NODE_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/tools/m33-structure-oracle.mjs" "$<TARGET_FILE:structure_probe>")
    endif()
    add_executable(geometry_predicate_tests ../tests/geometry_predicate_tests.cpp)
    target_link_libraries(geometry_predicate_tests PRIVATE Pandoeditor::Core)
    add_test(NAME geometry_predicate_tests COMMAND geometry_predicate_tests)
    add_executable(territorial_structure_tests ../tests/territorial_structure_tests.cpp)
    target_link_libraries(territorial_structure_tests PRIVATE Pandoeditor::Core)
    add_test(NAME territorial_structure_tests COMMAND territorial_structure_tests)
endif()

if(BUILD_TESTING)
    find_package(Threads REQUIRED)
    add_executable(job_tests ../tests/job_tests.cpp)
    target_link_libraries(job_tests PRIVATE Pandoeditor::Core Threads::Threads)
    add_test(NAME job_tests COMMAND job_tests)
endif()

add_library(pandoeditor_map_engine STATIC
    src/resourcecachepolicy.cpp
    src/engine.cpp
    src/mapviewstate.cpp
    src/mapcamera.cpp
    src/mappicker.cpp
    src/viewportresourcescheduler.cpp
    src/builtinhydrochannel.cpp
    src/labelengine.cpp
    src/projectionengine.cpp
    src/renderlod.cpp
    src/renderquality.cpp
    src/countryculling.cpp
    src/renderpacket.cpp
    src/renderscene.cpp
    src/scenepatch.cpp
    src/geometrypacketcache.cpp
    src/mapscenebuilder.cpp
)
add_library(Pandoeditor::MapEngine ALIAS pandoeditor_map_engine)

target_include_directories(pandoeditor_map_engine PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
target_include_directories(pandoeditor_map_engine PRIVATE
    "${PROJECT_SOURCE_DIR}/third_party/earcut"
)
target_compile_features(pandoeditor_map_engine PUBLIC cxx_std_17)
target_link_libraries(pandoeditor_map_engine PUBLIC Pandoeditor::Core)
set_target_properties(pandoeditor_map_engine PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(BUILD_TESTING)
    add_executable(resource_cache_policy_tests ../tests/resource_cache_policy_tests.cpp)
    target_link_libraries(resource_cache_policy_tests PRIVATE Pandoeditor::MapEngine)
    add_test(NAME resource_cache_policy_tests COMMAND resource_cache_policy_tests)
    add_executable(map_engine_boundary_tests ../tests/map_engine_boundary_tests.cpp)
    target_link_libraries(map_engine_boundary_tests PRIVATE Pandoeditor::MapEngine)
    add_test(NAME map_engine_boundary_tests COMMAND map_engine_boundary_tests)
    add_executable(map_camera_tests ../tests/map_camera_tests.cpp)
    target_link_libraries(map_camera_tests PRIVATE Pandoeditor::MapEngine)
    add_test(NAME map_camera_tests COMMAND map_camera_tests)
    add_executable(map_picker_tests ../tests/map_picker_tests.cpp)
    target_link_libraries(map_picker_tests PRIVATE Pandoeditor::MapEngine)
    add_test(NAME map_picker_tests COMMAND map_picker_tests)
    add_executable(viewport_resource_scheduler_tests ../tests/viewport_resource_scheduler_tests.cpp)
    target_link_libraries(viewport_resource_scheduler_tests PRIVATE Pandoeditor::MapEngine)
    add_test(NAME viewport_resource_scheduler_tests COMMAND viewport_resource_scheduler_tests)
    add_executable(builtin_hydro_channel_tests ../tests/builtin_hydro_channel_tests.cpp)
    target_link_libraries(builtin_hydro_channel_tests PRIVATE Pandoeditor::MapEngine)
    add_test(NAME builtin_hydro_channel_tests COMMAND builtin_hydro_channel_tests)
    add_executable(label_engine_tests ../tests/label_engine_tests.cpp)
    target_link_libraries(label_engine_tests PRIVATE Pandoeditor::MapEngine)
    add_test(NAME label_engine_tests COMMAND label_engine_tests)
endif()

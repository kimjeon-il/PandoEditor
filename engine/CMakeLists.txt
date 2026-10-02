add_library(pandoeditor_map_engine STATIC
    src/engine.cpp
    src/mapviewstate.cpp
    src/projectionengine.cpp
    src/renderlod.cpp
    src/renderquality.cpp
    src/renderpacket.cpp
    src/renderscene.cpp
    src/scenepatch.cpp
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

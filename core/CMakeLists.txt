configure_file(src/version.cpp.in generated/version.cpp @ONLY)

add_library(pandoeditor_core STATIC
    src/project.cpp
    "${CMAKE_CURRENT_BINARY_DIR}/generated/version.cpp"
)
add_library(Pandoeditor::Core ALIAS pandoeditor_core)

target_include_directories(pandoeditor_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
target_compile_features(pandoeditor_core PUBLIC cxx_std_17)
set_target_properties(pandoeditor_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(BUILD_TESTING)
    add_executable(core_tests ../tests/core_tests.cpp)
    target_link_libraries(core_tests PRIVATE Pandoeditor::Core)
    add_test(NAME core_tests COMMAND core_tests)
endif()

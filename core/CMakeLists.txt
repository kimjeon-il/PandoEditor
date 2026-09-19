configure_file(src/version.cpp.in generated/version.cpp @ONLY)

add_library(pandoeditor_core STATIC
    src/project.cpp
    src/document.cpp
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
set_target_properties(pandoeditor_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(BUILD_TESTING)
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
endif()

if(BUILD_TESTING)
    find_package(Threads REQUIRED)
    add_executable(job_tests ../tests/job_tests.cpp)
    target_link_libraries(job_tests PRIVATE Pandoeditor::Core Threads::Threads)
    add_test(NAME job_tests COMMAND job_tests)
endif()

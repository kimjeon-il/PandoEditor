cmake_minimum_required(VERSION 3.21)

project(Pandoeditor VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(PANDOEDITOR_BUILD_APP "Build the Qt Quick application" ON)
include(CTest)

if(ANDROID AND BUILD_TESTING)
    message(STATUS "Desktop test executables are disabled for Android cross-builds")
    set(BUILD_TESTING OFF CACHE BOOL "Build tests" FORCE)
endif()

add_subdirectory(core)
add_subdirectory(engine)

if(PANDOEDITOR_BUILD_APP)
    find_package(Qt6 6.5 REQUIRED COMPONENTS Quick QuickControls2)
    qt_standard_project_setup(REQUIRES 6.5)
    add_subdirectory(app)
    include(app/selection.cmake)
endif()

# The editing oracle is independent of the native renderer and runs offline.
# Dedicated CI supplies Node 24 and fails separately if this contract cannot run.
if(BUILD_TESTING)
    find_program(PANDOEDITOR_M97_NODE_EXECUTABLE NAMES node)
    if(PANDOEDITOR_M97_NODE_EXECUTABLE)
        add_test(NAME m971_editing_oracle_contract
            COMMAND "${PANDOEDITOR_M97_NODE_EXECUTABLE}" --test
                "${PROJECT_SOURCE_DIR}/tools/m97/contract.test.mjs"
                "${PROJECT_SOURCE_DIR}/tools/m97/calculations.test.mjs"
                "${PROJECT_SOURCE_DIR}/tools/m97/corpus.test.mjs"
                "${PROJECT_SOURCE_DIR}/tools/m97/cut-corpus.test.mjs"
                "${PROJECT_SOURCE_DIR}/tools/m97/native-baseline.test.mjs"
                "${PROJECT_SOURCE_DIR}/tools/m97/web-lifecycle.test.mjs"
                "${PROJECT_SOURCE_DIR}/tools/m97/web-selection.test.mjs"
                "${PROJECT_SOURCE_DIR}/tools/m97/registration.test.mjs")
        set_tests_properties(m971_editing_oracle_contract PROPERTIES TIMEOUT 60)
    endif()
endif()

# Native validation programs use assertions with required side effects. Keep
# those checks active in Release as well as Debug; the application build keeps
# its normal production flags.
if(BUILD_TESTING)
    foreach(validation_directory . core engine app)
        if(validation_directory STREQUAL "app" AND NOT PANDOEDITOR_BUILD_APP)
            continue()
        endif()
        get_property(validation_targets DIRECTORY "${PROJECT_SOURCE_DIR}/${validation_directory}" PROPERTY BUILDSYSTEM_TARGETS)
        foreach(validation_target IN LISTS validation_targets)
            get_target_property(validation_type ${validation_target} TYPE)
            if(validation_type STREQUAL "EXECUTABLE" AND NOT validation_target STREQUAL "pandoeditor")
                if(MSVC)
                    target_compile_options(${validation_target} PRIVATE /UNDEBUG)
                else()
                    target_compile_options(${validation_target} PRIVATE -UNDEBUG)
                endif()
            endif()
        endforeach()
    endforeach()
endif()

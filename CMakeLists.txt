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

if(PANDOEDITOR_BUILD_APP)
    find_package(Qt6 6.5 REQUIRED COMPONENTS Quick QuickControls2)
    qt_standard_project_setup(REQUIRES 6.5)
    add_subdirectory(app)
    include(app/selection.cmake)
endif()

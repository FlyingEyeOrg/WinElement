cmake_minimum_required(VERSION 3.21)

if(POLICY CMP0091)
    cmake_policy(SET CMP0091 NEW)
endif()

project(WinElement VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(cmake/winelement_target.cmake)

find_package(yoga CONFIG REQUIRED)

include(CTest)

option(WINELEMENT_BUILD_SAMPLES "Build WinElement sample applications" OFF)
option(WINELEMENT_WARNINGS_AS_ERRORS "Treat WinElement compiler warnings as errors" OFF)
option(WINELEMENT_BUILD_TESTS "Build WinElement tests" ${BUILD_TESTING})

add_subdirectory(src/core)
add_subdirectory(src/layout)
add_subdirectory(src/rendering)
add_subdirectory(src/animation)
add_subdirectory(src/style)
add_subdirectory(src/elements)
add_subdirectory(src/controls)
add_subdirectory(src/platform)

if(WINELEMENT_BUILD_SAMPLES)
    find_package(fmt CONFIG REQUIRED)
    add_subdirectory(samples/render_pipeline_demo)
    add_subdirectory(samples/render_pipeline_perf)
    add_subdirectory(samples/controls_showcase)
    add_subdirectory(samples/dialog_spacing_probe)
    add_subdirectory(samples/message_box_spacing_probe)
endif()

if(WINELEMENT_BUILD_TESTS)
    find_package(fmt CONFIG REQUIRED)
    enable_testing()
    find_package(GTest CONFIG REQUIRED)
    add_subdirectory(tests)
endif()

add_library(winelement INTERFACE)
add_library(WinElement::winelement ALIAS winelement)

target_link_libraries(winelement
    INTERFACE
        WinElement::core
        WinElement::layout
        WinElement::rendering
        WinElement::animation
        WinElement::style
        WinElement::elements
        WinElement::controls
        WinElement::platform
)

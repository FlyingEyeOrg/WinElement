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

option(WINELEMENT_WARNINGS_AS_ERRORS "Treat WinElement compiler warnings as errors" OFF)

add_subdirectory(src/core)
add_subdirectory(src/layout)
add_subdirectory(src/rendering)
add_subdirectory(src/platform)

add_library(winelement INTERFACE)
add_library(WinElement::winelement ALIAS winelement)

target_link_libraries(winelement
    INTERFACE
        WinElement::core
        WinElement::layout
        WinElement::rendering
        WinElement::platform
)

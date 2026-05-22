cmake_minimum_required(VERSION 3.21)

if(POLICY CMP0091)
    cmake_policy(SET CMP0091 NEW)
endif()

project(WinElement VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CMakePackageConfigHelpers)
include(GNUInstallDirs)
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

set_target_properties(winelement_core PROPERTIES EXPORT_NAME core)
set_target_properties(winelement_layout PROPERTIES EXPORT_NAME layout)
set_target_properties(winelement_rendering PROPERTIES EXPORT_NAME rendering)
set_target_properties(winelement_animation PROPERTIES EXPORT_NAME animation)
set_target_properties(winelement_style PROPERTIES EXPORT_NAME style)
set_target_properties(winelement_elements PROPERTIES EXPORT_NAME elements)
set_target_properties(winelement_controls PROPERTIES EXPORT_NAME controls)
set_target_properties(winelement_platform PROPERTIES EXPORT_NAME platform)
set_target_properties(winelement PROPERTIES EXPORT_NAME winelement)

install(
    TARGETS
        winelement_core
        winelement_layout
        winelement_rendering
        winelement_animation
        winelement_style
        winelement_elements
        winelement_controls
        winelement_platform
        winelement
    EXPORT WinElementTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

configure_package_config_file(
    cmake/WinElementConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/WinElementConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/WinElement
)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/WinElementConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(
    FILES
        ${CMAKE_CURRENT_BINARY_DIR}/WinElementConfig.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/WinElementConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/WinElement
)

install(
    EXPORT WinElementTargets
    NAMESPACE WinElement::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/WinElement
)
